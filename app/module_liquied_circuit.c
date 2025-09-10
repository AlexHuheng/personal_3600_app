#include "module_liquied_circuit.h"

static pthread_mutex_t perf_mutex;
static pthread_mutex_t clot_data_mutex;
static pthread_mutex_t press_data_mutex;
static pthread_mutex_t clr_change_mutex;//两种清洗液同时更换和洗针时更换都会导致冲突溢流
static pthread_cond_t sem_clot_data;
static sem_t sem_clot_check;
static sem_t sem_clearer_handle;

static FILE *record_file_fd = NULL;

static struct list_head press_record_list;

static slip_liquid_circuit_t liquid_circuit_monitor = {0};
static int pump_cur_steps = LIQ_PERF_FULL_STEPS;//防止泵在程序启动时直接补液下走
static clean_liquid_sta_t g_cls[CLAER_TYPE_MAX] = {{-1}, {-1}, {-1}};
static int maintance_flag = 0;//该标志置1后清洗液A不进行泵内容量监控
static clot_check_t clot_cotl = {0};
static clot_para_t clot_data = {0};//凝块检测数据
static int bubble_check_flag = 1;
static int special_clr_wait_maintence_done_flag = 1;//特殊清洗特上电后等待自检或维护完成
static liquid_pump_ctl_t pump_para_for_check = {DIAPHRAGM_PUMP_Q1, 0, 1}; //用于隔膜泵Q1为样本针外壁清洗时使用
static liquid_pump_ctl_t liquid_pump_ctl[4] = {{DIAPHRAGM_PUMP_Q1, 0}, {DIAPHRAGM_PUMP_Q2, 0},
                                               {DIAPHRAGM_PUMP_Q3, 0}, {DIAPHRAGM_PUMP_Q4, 0}};

static void sleep_in_check(int time)
{
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    usleep(time*1000);
    FAULT_CHECK_END();
}

void r2_clean_mutex_lock(int lock)
{
    if (lock) pthread_mutex_lock(&clr_change_mutex);
    else pthread_mutex_unlock(&clr_change_mutex);
}

/* 触发普通清洗气泡后的事件获取 */
int normal_clearer_bubble_stage_notify(void)
{
    return liquid_circuit_monitor.bubble_n_status;
}

void pump_cur_steps_set(int data)
{
    LOG("set 5ml pump steps = %d.\n", data);
    pump_cur_steps = data;
}

void set_cur_sampler_ordno_for_clot_check(uint32_t ordno, int cycle_flag, needle_s_cmd_t cmd)
{
    LOG("get order number is %d.\n",ordno);

    if (cycle_flag) {
        clot_cotl.orderno[0] = ordno;
        clot_cotl.cmd[0] = cmd;
    } else {
        if (clot_cotl.check_flag != 0) {
            if (clot_cotl.orderno[1] != 0) {
                LOG("clot check stash group is full !!!\n");
                return;
            }
            clot_cotl.orderno[1] = clot_cotl.orderno[0];
            clot_cotl.cmd[1] = clot_cotl.cmd[0];
        }
        clot_cotl.orderno[0] = 0;
        clot_cotl.cmd[0] = 0;
    }
}

//特殊清洗时，设置标志不进行打液。
void pump_5ml_inuse_manage(int value)
{
    if (value) {
        pthread_mutex_lock(&perf_mutex);
    } else {
        pthread_mutex_unlock(&perf_mutex);
    }
}

/**
 * @brief: 5ml泵吸清洗液步长转换。
 * @param: ul吸液体积，微升。
 */
static int liq_ul_to_steps(float ul, int mode)
{
    int steps = 0;

    if (mode) {
        steps = (int)(6.11 * ul + 103.7);
        LOG("liquid_circuit: 5mlpump perf vol is %f,steps = %d.\n", ul, steps);
    } else {
        steps = -((int)(6.41 * ul + 15.86));
        LOG("liquid_circuit: 5mlpump release vol is %f,steps = %d.\n", ul, steps);
    }// 吸液与泵液的kb值有误差分开计算。

    if (steps > LIQ_PERF_FULL_STEPS) {
        steps = LIQ_PERF_FULL_STEPS;
    }

    if ((steps + pump_cur_steps) > LIQ_PERF_FULL_STEPS) {
        LOG("liquid_circuit: ahead steps get invalid extened steps = %d,cur steps = %d\n", steps, pump_cur_steps);
        steps = LIQ_PERF_FULL_STEPS - pump_cur_steps;
        pump_cur_steps = LIQ_PERF_FULL_STEPS;
    } else if ((steps + pump_cur_steps) < 0) {
        LOG("liquid_circuit: back steps get invalid extened steps = %d,cur steps = %d\n", steps, pump_cur_steps);
        steps = -pump_cur_steps;
        pump_cur_steps = 0;
    } else {
        LOG("liquid_circuit:current pos is %d\n", pump_cur_steps);
        pump_cur_steps += steps;
    }

    LOG("liquid_circuit: ready to move steps is: %d.\n", steps);
    return steps;
}

/**
 * @brief: 样本、试剂泵吸液电机控制。
 * @param: needle_type针类。
 * @param: amount_ul 吸液体积 微升。
 */
int pump_absorb_sample(needle_type_t needle_type, float amount_ul, int mode)
{
    int steps = 0, motor_id = 0, absorb_para = 0;
    thrift_motor_para_t liquid_pump_motor_para = {0};
    char *fault_idx = 0;

    if (needle_type == NEEDLE_TYPE_R2) {
        motor_id = MOTOR_NEEDLE_R2_PUMP;
        fault_idx = MODULE_FAULT_NEEDLE_R2_PUMP;
        absorb_para = 128;
    } else if (needle_type == NEEDLE_TYPE_S) {
        motor_id = MOTOR_NEEDLE_S_PUMP;
        fault_idx = MODULE_FAULT_NEEDLE_S_PUMP ;
        absorb_para = 64;
    } else {
        LOG("liquid_circuit: needle_type invalid type is %d.\n", needle_type);
        return -1;
    }

    thrift_motor_para_get(motor_id, &liquid_pump_motor_para);
    steps = (int)(amount_ul * absorb_para);
    steps = mode ? steps : -steps;
    LOG("liquid_circuit:motor %d ready move steps is: %d.\n", motor_id, steps);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(motor_id,
                            CMD_MOTOR_MOVE_STEP,
                            steps,
                            liquid_pump_motor_para.speed,
                            liquid_pump_motor_para.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: motor wait timeout\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)fault_idx);
    }
    FAULT_CHECK_END();
    return 1;    
}

/**
 * @brief: 5ml柱塞泵电机参数设置与控制。
 * @param: motor_id
 * @param: amount_ul 吸液体积 微升。
 * @param: mode 0表示泵供液，1表示泵进液，2在换清洗液时增加电机的速度加速度。
 * @return: 返回电机执行结果
 */
int pump_5ml_absorb_clearer(char motor_id, float amount_ul, int mode)
{
    int steps = 0, ret = -1;
    thrift_motor_para_t liquid_pump_motor_para = {0};

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    thrift_motor_para_get(motor_id, &liquid_pump_motor_para);
    steps = liq_ul_to_steps(amount_ul, mode);
    FAULT_CHECK_END();

    if (mode == 0 && amount_ul > 200) {
        liquid_pump_motor_para.speed = 20000;
        liquid_pump_motor_para.acc = 50000;
    } else if (mode == 2) {
        liquid_pump_motor_para.speed = 40000;
        liquid_pump_motor_para.acc = 80000;
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(motor_id,
                            CMD_MOTOR_MOVE_STEP,
                            steps,
                            liquid_pump_motor_para.speed,
                            liquid_pump_motor_para.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
        LOG("liquid_circuit: motor wait timeout\n");
    } else {
        ret = 0;
    }
    device_status_count_add(DS_PUMP_5ML_USED_COUNT, 1);
    FAULT_CHECK_END();
    return ret;
}

/**
 * @brief: 暂存池特殊清洗液打液
 */
static void stage_pool_special_clearer(float ul)
{
    int timer = (int)ul > LIQ_SIGNAL_PERF_VARIABLE ? 2100:500;

    LOG("liquid_circuit: stage_pool_special_clearer ul = %f time = %d\n", ul, timer);
    valve_set(VALVE_SV9, ON);
    usleep(1000*200);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, ul, 0);
    sleep_in_check(timer);
    valve_set(VALVE_SV9, OFF);
    device_status_count_add(DS_PUMP_5ML_USED_COUNT, 1);
}

/**
 * @brief: 洗针池特殊清洗液打液
 */
static void needles_special_clearer_pool_perf(float ul)
{
    int timer = (int)ul > LIQ_SIGNAL_PERF_VARIABLE ? 2100:500;
    
    LOG("liquid_circuit: needles_special_clearer_pool_perf ul = %f\n",ul);
    valve_set(VALVE_SV10, ON);
    usleep(1000*200);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, ul, 0);
    sleep_in_check(timer);
    valve_set(VALVE_SV10, OFF);
    device_status_count_add(DS_PUMP_5ML_USED_COUNT, 1);
}

/**
 * @brief: 特殊清洗液回收.
 */
static void speccial_clear_recycle(void)
{
    valve_set(VALVE_SV10, ON);
    usleep(100*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);
    usleep(2100*1000);
    valve_set(VALVE_SV10, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV5, ON);
    usleep(100*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: liquid pump move timeout\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    } else {
        pump_cur_steps = 0;
    }
    FAULT_CHECK_END();
    usleep(200*1000);
    valve_set(VALVE_SV5, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV9, ON);
    usleep(100*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);
    usleep(2100*1000);
    valve_set(VALVE_SV9, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV5, ON);
    usleep(100*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: liquid pump move timeout\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    }
    FAULT_CHECK_END();
    usleep(100*1000);
    pump_cur_steps = 0;
    valve_set(VALVE_SV5, OFF);
}

/**
 * @brief: 特殊清洗液预进液.
 */
static void special_clear_preperfusion(float ul)
{
    int timer = (int)ul > LIQ_SIGNAL_PERF_VARIABLE ? 2100:500;

    LOG("liquid_circuit :special_clear_preperfusion ul = %f timer = %d .\n", ul, timer);
    valve_set(VALVE_SV4, ON);
    usleep(100*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, ul, 1);
    sleep_in_check(timer);
    valve_set(VALVE_SV4, OFF);
}

/**
 * @brief: s管路排气泡。
 */
static void s_bubble_release(void)
{
    int i = 0;
    thrift_motor_para_t liq_motor_param = {0};

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (motor_timedwait(MOTOR_NEEDLE_S_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: S pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
    }
    FAULT_CHECK_END();
    valve_set(VALVE_SV2, ON);
    valve_set(VALVE_SV11, ON);
    valve_set(DIAPHRAGM_PUMP_F2, ON);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    thrift_motor_para_get(MOTOR_NEEDLE_S_PUMP, &liq_motor_param);
    for (i=0; i<2; i++) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, LIQ_PERF_FULL_STEPS, liq_motor_param.speed, liq_motor_param.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit:needle s pump move timeout.\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        FAULT_CHECK_END();
        sleep_in_check(1300);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -LIQ_PERF_FULL_STEPS, liq_motor_param.speed, liq_motor_param.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit:needle s pump move timeout.\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        FAULT_CHECK_END();
        sleep_in_check(1300);
    }
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    valve_set(VALVE_SV2, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV1, ON);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);

    for (i=0; i<3; i++) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, LIQ_PERF_FULL_STEPS, liq_motor_param.speed, liq_motor_param.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit:needle s pump move timeout.\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        FAULT_CHECK_END();
        sleep_in_check(1000);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -LIQ_PERF_FULL_STEPS, liq_motor_param.speed, liq_motor_param.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit:needle s pump move timeout.\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        FAULT_CHECK_END();
        sleep_in_check(1000);
    }

    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    valve_set(VALVE_SV1, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV11, OFF);
    valve_set(DIAPHRAGM_PUMP_F2, OFF);
}

/**
 * @brief: r2管路排气泡。相关部件真值表: SV6 =0 SV1 = 0 泵M3 = 1
 */
static void r2_bubble_release(void)
{
    int i = 0;
    thrift_motor_para_t liq_motor_param = {0};

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_PUMP, 0);
    if (motor_timedwait(MOTOR_NEEDLE_R2_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: R2 pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    valve_set(VALVE_SV12, ON);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(100*1000);
    valve_set(VALVE_SV3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, ON);
    sleep_in_check(500);
    thrift_motor_para_get(MOTOR_NEEDLE_R2_PUMP, &liq_motor_param);
    for (i=0; i<3; i++) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, LIQ_PERF_FULL_STEPS, liq_motor_param.speed, liq_motor_param.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit:needle R2 pump move timeout.\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
        }
        FAULT_CHECK_END();

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, -LIQ_PERF_FULL_STEPS, liq_motor_param.speed, liq_motor_param.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit:needle R2 pump move timeout.\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
        }
        FAULT_CHECK_END();
    }
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    valve_set(VALVE_SV3, OFF);
    valve_set(VALVE_SV12, OFF);

}

/**
 * @brief: 以线程模式运行的隔膜泵的虚拟流量控制函数，通过模拟pwm方式控制流量。
 * @param: liquid_pump_ctl_t 其id为泵索引，switch为退出流量模式标志。
 */ 
static void visual_flowmeter(void *arg)
{
    liquid_pump_ctl_t *param = (liquid_pump_ctl_t *)arg;
    int close_time = 0, open_time = 0, i = 0, cycl_time = 0;

    for (i=0; i < sizeof(liquid_pump_ctl)/sizeof(liquid_pump_ctl[0]); i++) {
        if (param->pump_id == liquid_pump_ctl[i].pump_id) {
            liquid_pump_ctl[i].pump_switch = 1;
            liquid_pump_ctl[i].pump_flag = param->pump_flag;
            LOG("liquid_circuit: get pump id = %d\n",liquid_pump_ctl[i].pump_id);
            break;
        }
    }

    if (param->pump_id == DIAPHRAGM_PUMP_Q1 || param->pump_id == DIAPHRAGM_PUMP_Q2) {
        if (liquid_pump_ctl[i].pump_flag) {
            open_time = 1;
            cycl_time = 180000;
        } else {
            open_time = 30000;
            cycl_time = PUMP_SINGLE_CYCLE;
        }
    } else if (param->pump_id == DIAPHRAGM_PUMP_Q3 || param->pump_id == DIAPHRAGM_PUMP_Q4) {
        open_time = 1;
        cycl_time = 180000;
    } else {
        open_time = PUMP_SINGLE_CYCLE;
    }
    close_time = cycl_time - open_time;

    LOG("liquid_circuit: open_time = %d.\n", open_time);
    while (liquid_pump_ctl[i].pump_switch) {
        valve_set(param->pump_id, ON);
        usleep(open_time);
        valve_set(param->pump_id, OFF);
        usleep(close_time);
    }

    LOG("liquid_circuit: perfusion done.\n");

}


void liquid_pump_pwm_open(liquid_pump_ctl_t *para)
{
    work_queue_add(visual_flowmeter, (void *)para);
}

void liquid_pump_close(int        pump_id)
{
    int i = 0;

    valve_set(pump_id, OFF);//在此立即处理关闭指令
    for (i=0; i < sizeof(liquid_pump_ctl)/sizeof(liquid_pump_ctl[0]); i++) {
        if (pump_id == liquid_pump_ctl[i].pump_id) {
            liquid_pump_ctl[i].pump_switch = 0;
            break;
        }
    }
}

/**
 * @brief: 液路模块状态重置。
 */
void liquid_circuit_reinit(void)
{
    clot_cotl.orderno[0] = 0;
    clot_cotl.orderno[1] = 0;
    slip_liq_cir_noise_set();
    LOG("liquid circuit : reinit done\n");
}

/**
 * @brief: 检测液路所有传感器的状态，错误状态触发后即上报，状态未清除则不处理，用户清状态的方法：上位机下发可用更换耗材，或者人为改变传感器至正确状态
 */
static void liq_sensor_status_check(void)
{
    slip_liquid_circuit_t *lcm_t = &liquid_circuit_monitor;
    static uint8_t report[COMPO_MAX];
    int str_release_flag = get_straight_release_para();

    /* 废液桶 */
    if (!gpio_get(WASTESENSOR_IDX) && (str_release_flag == 0)) {
        lcm_t->waste_status++;
    } else {
        lcm_t->waste_status = 0;
        report[WASTE_TANK] = 0;
    }
    if (lcm_t->waste_status > 5) {
        if (!report[WASTE_TANK]) {
            LOG("liquid_circuit: waste tank is full!\n");
            if (get_machine_stat() == MACHINE_STAT_RUNNING || module_start_stat_get() == MODULE_CMD_START) {
                if (g_cls[CLEAN_TYPE_WASTE].report == 0) {
                    g_cls[CLEAN_TYPE_WASTE].report = 1;//此标志源为上位机下发跟换后置位
                    FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_NONE, MODULE_FAULT_WASTE_HIGH);
                }
            }
            if (thrift_salve_heartbeat_flag_get()) {//确认连接后进行报警重置
                LOG("liquid_circuit: waste tank full fault has reported!\n");
                report[WASTE_TANK] = 1;
            }
        }
        lcm_t->waste_status = 0;
    }
    /* 普通清洗液桶 */
    if (gpio_get(WASHSENSOR_IDX)) {
        lcm_t->normal_status++;
    } else {
        lcm_t->normal_status = 0;
        report[NORMAL_CLEAN_TANK] = 0;
    }
    if (lcm_t->normal_status > 5) {
        if (!report[NORMAL_CLEAN_TANK]) {
            LOG("liquid_circuit:normal clearer is less!\n");
            if (get_machine_stat() == MACHINE_STAT_RUNNING || module_start_stat_get() == MODULE_CMD_START) {
                if (g_cls[CLEAN_TYPE_NORMAL].report == 0) {
                    g_cls[CLEAN_TYPE_NORMAL].report = 1;//此标志源为上位机下发跟换后置位
                    FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_NONE, MODULE_FAULT_NORMAL_CLEAN_LESS);
                }
            }
            if (thrift_salve_heartbeat_flag_get()) {//确认连接后进行报警重置
                LOG("liquid_circuit: normal clearer is less fault has reported!\n");
                report[NORMAL_CLEAN_TANK] = 1;
            }
        }
        lcm_t->normal_status = 0;
    }
    /* 溢流瓶 */
    if (gpio_get(OVERFLOW_BOT_IDX)) {
        lcm_t->overflow_status++;
    } else {
        lcm_t->overflow_status = 0;
        report[OVERFLOW_BOT] = 0;
    }
    if (lcm_t->overflow_status > 5 ) {
        if (!report[OVERFLOW_BOT]) {
            LOG("liquid_circuit: overflow tank is full!\n");
            FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_STOP_ALL, MODULE_FAULT_OVERFLOWBOT_FULL);
            if (thrift_salve_heartbeat_flag_get()) {//确认连接后进行报警重置
                LOG("liquid_circuit: overflow full fault has reported!\n");
                report[OVERFLOW_BOT] = 1;
            }
        }
        lcm_t->overflow_status = 0;
    }
    /* 特殊清洗液瓶到位 */
    if (gpio_get(SPCL_CLEAR_IDX)) {
        if (!report[SPECIAL_CLEAN_BOT]) {
            LOG("liquid_circuit: special clearer unpos!\n");
            if (get_machine_stat() == MACHINE_STAT_RUNNING || module_start_stat_get() == MODULE_CMD_START || g_cls[CLEAN_TYPE_SPECIAL].report == 0) {
                g_cls[CLEAN_TYPE_SPECIAL].report = 1;//此处保留微动开关在待机时报警防止空瓶或防止错位导致开始后进加样停
                FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_NONE, MODULE_FAULT_SPE_CLEAN_EMPTY);
            }
            if (thrift_salve_heartbeat_flag_get()) {//确认连接后进行报警重置
                LOG("liquid_circuit: special clearer unpos fault has reported!\n");
                report[SPECIAL_CLEAN_BOT] = 1;
            }
        }
    } else {
        report[SPECIAL_CLEAN_BOT] = 0;
    }
}

/**
 * @brief: 清洗液A、B气泡检测。
 * @param: arg 使用为clean_liquid_type_t类型。
 */
static void liquid_bubble_check(void *arg)
{
    int bub_errcount = 0;
    int idx = (*(clean_liquid_type_t *)arg == CLEAN_TYPE_NORMAL) ? BUBBLESENSOR_NORM_IDX : BUBBLESENSOR_SPCL_IDX;

    while (liquid_circuit_monitor.bub_chk_switch) {
        if (gpio_get(idx) != 0) {
            bub_errcount++;
            if (idx == BUBBLESENSOR_SPCL_IDX) {
                usleep(300*1000);
            } else {
                usleep(450*1000);
            }
        } else {
            bub_errcount = 0;
        }

        if (idx == BUBBLESENSOR_NORM_IDX) {
            if (bub_errcount > 4) {
                if (bubble_check_flag == 1) {
                    liquid_circuit_monitor.bubble_n_status = 1;
                    LOG("liquid_circuit: normmal clearer bubble have checked.\n");
                    FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, MODULE_FAULT_NOR_CLEAN_EMPTY);
                    bubble_check_flag = 0;
                } else {
                    if (machine_maintence_state_get() == 1) {
                        LOG("liquid_circuit: maintence normmal clearer bubble have checked.\n");
                        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, MODULE_FAULT_NOR_CLEAN_EMPTY);
                        bubble_check_flag = 1;
                    } else {
                        LOG("liquid_circuit: normal clearer bubble first checked.\n");
                        bubble_check_flag = 1;
                    }
                }
                return;
            }
        } else if (idx == BUBBLESENSOR_SPCL_IDX) {
            if (bub_errcount > 5) {
                liquid_circuit_monitor.bubble_s_status = 1;
                LOG("liquid_circuit: special clearer bubble have checked.\n");
                FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_NONE, MODULE_FAULT_SPECIAL_CLEAR_LESS);
                valve_set(VALVE_SV4, ON);
                usleep(200*1000);
                pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_S_PERF_VARIABLE, 0);//检测到气泡后回退清洗液至桶中
                usleep(500*1000);
                valve_set(VALVE_SV4, OFF);
                return;
            } else {
                liquid_circuit_monitor.bubble_s_status = 0;
            }
        } else {
            LOG("liquid_circuit: sensor index invalid\n");
        }
    }
}

/**
 * @brief: 试剂仓排冷凝水。
 */
static void liq_reag_table_drainage(void)
{
    valve_set(VALVE_SV6, ON);
    valve_set(DIAPHRAGM_PUMP_F4, ON);
    usleep(5000*1000);
    valve_set(DIAPHRAGM_PUMP_F4, OFF);
    usleep(500*1000);
    valve_set(VALVE_SV6, OFF);
}

/**
 * @brief: F1，F4联合排废。
 * @param: flag 吸样后开排废 1.开 0关
 */
void liq_s_handle_sampler(int flag)
{
    if (flag) {
        valve_set(DIAPHRAGM_PUMP_F1, ON);
        valve_set(DIAPHRAGM_PUMP_F4, ON);
    } else {
        valve_set(DIAPHRAGM_PUMP_F1, OFF);
        valve_set(DIAPHRAGM_PUMP_F4, OFF);
    }
}

/**
 * @brief: 特殊清洗液打液，在线检测。
 */
static void special_clearer_preperfusion(void)
{
    clean_liquid_type_t type = CLEAN_TYPE_SPECIAL;

    if (special_clr_wait_maintence_done_flag) {
        return;
    }//等待上电维护或自检完成，供液出于初始状态后进行供液维护。

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (pump_cur_steps < LIQ_PERF_HALF_STEPS &&  maintance_flag != 1 && liquid_circuit_monitor.bubble_s_status == 0) {
        LOG("liquid_circuit: special preperfusion start...\n");
        pump_5ml_inuse_manage(1);
        liquid_circuit_monitor.bub_chk_switch = 1;
        work_queue_add(liquid_bubble_check, (void *)&type);
        valve_set(VALVE_SV4, ON);
        usleep(200*1000);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_S_PERF_VARIABLE, 1);
        usleep(2100*1000);
        valve_set(VALVE_SV4, OFF);
        liquid_circuit_monitor.bub_chk_switch = 0;
        pump_5ml_inuse_manage(0);
        LOG("liquid_circuit: special preperfusion end!\n");
    }
    FAULT_CHECK_END();
}

/**
 * @brief: 特殊清洗液管路清洗（洗针池、暂存池）.
 */
void liquid_access_clear(void)
{
    int i = 0, j = 0;
    thrift_motor_para_t liq_r2_z_motor_para = {0};
    thrift_motor_para_t liq_r2_y_motor_para = {0};
    pos_t r2_para = {0};

    maintance_flag = 1;
    valve_set(VALVE_SV4, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    } else {
        pump_cur_steps = 0;
    }
    FAULT_CHECK_END();
    usleep(100*1000);
    valve_set(VALVE_SV4, OFF);
    valve_set(VALVE_SV12,ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(50*1000);
    valve_set(VALVE_SV12,OFF);//排空洗针池

    for (i=0; i<3; i++) {
        speccial_clear_recycle();//回收
    }
    /* 管路清洗 */
    for (j=0; j<2; j++) {
        valve_set(VALVE_SV8, ON);
        usleep(50*1000);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 5000, 1);
        valve_set(VALVE_SV10, ON);
        valve_set(VALVE_SV12, ON);
        usleep(50*1000);
        valve_set(DIAPHRAGM_PUMP_F3, ON);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_Q2, ON);
        usleep(5000*1000);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 1875, 0);
        valve_set(DIAPHRAGM_PUMP_Q2, OFF);
        valve_set(VALVE_SV8, OFF);
        valve_set(VALVE_SV10, OFF);
        valve_set(VALVE_SV12, OFF);
        valve_set(DIAPHRAGM_PUMP_F3, OFF);
        valve_set(VALVE_SV9, ON);
        for (i=0; i<2; i++) {
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 700, 0);
            usleep(200*1000);
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 100, 0);
            usleep(200*1000);
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 800, 1);
        }
        valve_set(VALVE_SV9, OFF);

        /* SV5清洗 */
        valve_set(VALVE_SV5, ON);
        for (i=0; i<2; i++) {
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 1500, 0);
            usleep(50*1000);
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 1500, 1);
        }
        valve_set(VALVE_SV5, OFF);

        /* R2到洗针位 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_reset(MOTOR_NEEDLE_R2_Z, 1);
        if (motor_timedwait(MOTOR_NEEDLE_R2_Z,MOTOR_DEFAULT_TIMEOUT) != 0) {
           LOG("liquid_circuit: R2 Z axis move faild\n");
           FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_reset(MOTOR_NEEDLE_R2_Y, 1);
        if (motor_timedwait(MOTOR_NEEDLE_R2_Y,MOTOR_DEFAULT_TIMEOUT) != 0) {
           LOG("liquid_circuit: R2 Y axis move faild\n");
           FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Y);
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        thrift_motor_para_get(MOTOR_NEEDLE_R2_Y, &liq_r2_y_motor_para);
        get_special_pos(MOVE_R2_CLEAN, 0, &r2_para, FLAG_POS_UNLOCK);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, r2_para.y, liq_r2_y_motor_para.speed, liq_r2_y_motor_para.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit: R2 Y axis move faild\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Y);
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        thrift_motor_para_get(MOTOR_NEEDLE_R2_Z, &liq_r2_z_motor_para);
        get_special_pos(MOVE_R2_CLEAN, 0, &r2_para, FLAG_POS_UNLOCK);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, r2_para.z + 3000, liq_r2_z_motor_para.speed, liq_r2_z_motor_para.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit: R2 Z axis move faild\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
        }
        FAULT_CHECK_END();

        /* 废液排空 */
        valve_set(VALVE_SV12, ON);
        usleep(100*1000);
        valve_set(DIAPHRAGM_PUMP_F3, ON);
        usleep(200*1000);
        valve_set(VALVE_SV10, ON);
        usleep(100*1000);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);//抽空气柱
        valve_set(VALVE_SV10, OFF);
        valve_set(VALVE_SV8, ON);
        valve_set(VALVE_SV3, ON);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit: liquid pump move faild\n");
            FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
        } else {
            pump_cur_steps = 0;
        }
        FAULT_CHECK_END();
        valve_set(VALVE_SV8, OFF);
        valve_set(VALVE_SV3, OFF);
        usleep(300*1000);
        valve_set(DIAPHRAGM_PUMP_F3, OFF);
        valve_set(VALVE_SV12, OFF);
        for (i=0; i<3; i++) {//进行三次排空
            valve_set(VALVE_SV12, ON);
            valve_set(DIAPHRAGM_PUMP_F3, ON);
            usleep(100*1000);
            valve_set(VALVE_SV10, ON);
            usleep(100*1000);
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);//抽空气柱
            usleep(100*1000);
            valve_set(VALVE_SV10, OFF);
            valve_set(VALVE_SV8, ON);
            valve_set(VALVE_SV3, ON);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("liquid_circuit: liquid pump move faild\n");
                FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
            } else {
                pump_cur_steps = 0;
            }
            FAULT_CHECK_END();
            valve_set(VALVE_SV8, OFF);
            valve_set(VALVE_SV3, OFF);
            usleep(500*1000);
            valve_set(VALVE_SV9, ON);
            usleep(100*1000);
            pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);//抽空气柱
            usleep(100*1000);
            valve_set(VALVE_SV9, OFF);
            valve_set(VALVE_SV8, ON);
            valve_set(VALVE_SV3, ON);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("liquid_circuit: S_pump move faild\n");
                FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
            } else {
                pump_cur_steps = 0;
            }
            FAULT_CHECK_END();
            valve_set(VALVE_SV8, OFF);
            valve_set(VALVE_SV3, OFF);
            usleep(100*1000);
            valve_set(DIAPHRAGM_PUMP_F3, OFF); 
        }
    }

    maintance_flag = 0;
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_Z, 1);
    if (motor_timedwait(MOTOR_NEEDLE_R2_Z,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: r2 Z axis motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_Y, 1);
    if (motor_timedwait(MOTOR_NEEDLE_R2_Y,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: r2 Y axis motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Y);
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    report_reagent_supply_consume(WASH_B, 0, LIQUID_SPEC_PIPE_CLR);
    FAULT_CHECK_END();
}

/**
 * @brief: 暂存池自清洗流程。
 * @param: stage_pool_clear_type 自清洗类型。
 * @return：返回执行结果。
 */
int stage_pool_self_clear(liq_slave_numb_t stage_pool_clear_type)
{
    switch (stage_pool_clear_type) {
        case STEGE_POOL_PRE_CLEAR:
            valve_set(VALVE_SV11, ON);
            valve_set(DIAPHRAGM_PUMP_F2, ON);
            usleep(500*1000);
            valve_set(DIAPHRAGM_PUMP_F2, OFF);
            usleep(100*1000);
            valve_set(VALVE_SV11, OFF);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_reagent_supply_consume(WASH_B, 0, LIQUID_STAGE_POOL_PRECLR);
            FAULT_CHECK_END();
            break;
        case STEGE_POOL_LAST_CLEAR:
            valve_set(VALVE_SV11, ON);
            usleep(100*1000);
            valve_set(DIAPHRAGM_PUMP_F2, ON);
            usleep(100*1000);
            valve_set(DIAPHRAGM_PUMP_Q3, ON);
            usleep(10*1000);
            valve_set(VALVE_SV11, OFF);
            usleep(230*1000);
            valve_set(VALVE_SV11, ON);
            usleep(250*1000);
            valve_set(VALVE_SV11, OFF);
            usleep(230*1000);
            valve_set(DIAPHRAGM_PUMP_Q3, OFF);
            usleep(10*1000);
            valve_set(VALVE_SV11,ON);
            usleep(1900*1000);
            valve_set(VALVE_SV11, OFF);
            usleep(100*1000);
            valve_set(DIAPHRAGM_PUMP_F2, OFF);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_reagent_supply_consume(WASH_B, 0, LIQUID_SPEC_PIPE_LASTCLR);
            FAULT_CHECK_END();
            break;
        default:
            LOG("liquid_circuit: stage clear type invalid type = %d.\n", stage_pool_clear_type);
            return -1;
       }
    return 0;
}

/**
 * @brief: 特殊清洗液管路填充。
 */
void special_clear_pipe_line_prefill(void)
{
    int i = 0;

    valve_set(VALVE_SV4, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    } else {
        pump_cur_steps = 0;
    }
    FAULT_CHECK_END();
    usleep(100*1000);
    valve_set(VALVE_SV4, OFF);

    valve_set(VALVE_SV12,ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(50*1000);
    valve_set(VALVE_SV12,OFF);//抽洗针池

    for (i=0; i<2; i++) {
        speccial_clear_recycle();//回收
        sleep_in_check(500);
    }
    LOG("liquid_circuit: special clearer recycle done\n");

    valve_set(VALVE_SV12, ON);//填充
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    for (i=0; i<3; i++) {
        special_clear_preperfusion(PIPE_PILL_R2_STPES_TRANS_CAPA);
        sleep_in_check(500);
        needles_special_clearer_pool_perf(PIPE_PILL_R2_STPES_TRANS_CAPA);
        sleep_in_check(500);
    }

    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV12, OFF);
    usleep(100*1000);

    special_clear_preperfusion(PIPE_PILL_S_STPES_TRANS_CAPA);
    usleep(100*1000);
    stage_pool_special_clearer(PIPE_PILL_S_STPES_TRANS_CAPA);
    usleep(100*1000);

    special_clear_preperfusion(LIQ_S_PERF_VARIABLE);
    LOG("liquid_circuit: pipe line prefill perfusion work done.\n");

}

/**
 * @brief: 普通清洗液管路填充。需要配合洗针流程
 */
void normal_clear_pipe_line_prefill(void)
{
    thrift_motor_para_t needle_s_motor_para = {0};
    liquid_pump_ctl_t pump_q1 = {DIAPHRAGM_PUMP_Q1, 0, 1};

    LOG("liquid_circuit: pipe_fill work start.\n");
    s_bubble_release();
    r2_bubble_release();/* 排气泡 */
    thrift_motor_para_get(MOTOR_NEEDLE_S_Z, &needle_s_motor_para);

    valve_set(DIAPHRAGM_PUMP_F1, ON);
    valve_set(DIAPHRAGM_PUMP_F4, ON);
    usleep(200*1000);
    valve_set(VALVE_SV7, ON);
    usleep(50*1000);
    liquid_pump_pwm_open(&pump_q1);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, needle_s_motor_para.speed, needle_s_motor_para.acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: S_Z axis move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, needle_s_motor_para.speed, needle_s_motor_para.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();
    sleep_in_check(1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q1);
    valve_set(VALVE_SV7, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F1, OFF);
    valve_set(DIAPHRAGM_PUMP_F4, OFF);
    LOG("liquid_circuit: pipe_fill work done.\n");
}

/**
 * @brief: 液路系统开机维护。
 * @para: mode 为后续功能扩展预留工作模式。
 * @return ：返回执行结果
 */
int liquid_self_maintence_interface(int mode)
{
    int i = 0;
    thrift_motor_para_t liq_s_z_motor_para = {0};

    r2_clean_mutex_lock(1);//开机维护时也需要加锁防止更换操作进行
    for (i=DIAPHRAGM_PUMP_Q1; i<=DIAPHRAGM_PUMP_F4; i++) {
        valve_set(i, OFF);
    }
    for (i=VALVE_SV1; i<=VALVE_SV12; i++) {
        valve_set(i, OFF);
    }

    maintance_flag = 1;
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
    if (motor_timedwait(MOTOR_NEEDLE_R2_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (motor_timedwait(MOTOR_NEEDLE_S_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();
    valve_set(VALVE_SV4, ON);
    usleep(200*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    } else {
        pump_cur_steps = 0;
    }
    FAULT_CHECK_END();
    sleep_in_check(1000);
    valve_set(VALVE_SV4, OFF);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    if (motor_timedwait(MOTOR_NEEDLE_S_Z,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(MOTOR_NEEDLE_S_Z, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();

    thrift_motor_para_get(MOTOR_NEEDLE_S_Z, &liq_s_z_motor_para);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z,
                            CMD_MOTOR_MOVE_STEP,
                            14000 + NEEDLE_S_CLEAN_POS, 
                            liq_s_z_motor_para.speed,
                            liq_s_z_motor_para.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: R2 z move wait timeout!\n");
        FAULT_CHECK_DEAL(MOTOR_NEEDLE_S_Z, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    LOG("liquid_circuit: liquid self maintance start...\n");
    special_clear_pipe_line_prefill();/* 特殊清洗液管路填充 */
    normal_clear_pipe_line_prefill();/* 普通清洗液管路填充 */
    valve_set(VALVE_SV11, ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F2, ON);
    valve_set(DIAPHRAGM_PUMP_Q3, ON);
    sleep_in_check(800);
    valve_set(DIAPHRAGM_PUMP_Q3, OFF);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_F2, OFF);
    valve_set(VALVE_SV11, OFF);
    stage_pool_self_clear(STEGE_POOL_LAST_CLEAR);/* 暂存池清洗 */
    valve_set(VALVE_SV12, ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    valve_set(DIAPHRAGM_PUMP_Q4, ON);
    sleep_in_check(1000);
    valve_set(DIAPHRAGM_PUMP_Q4, OFF);
    valve_set(VALVE_SV12, OFF);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q4, ON);//在此处增加液面重置
    usleep(150*1000);
    valve_set(DIAPHRAGM_PUMP_Q4, OFF);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    liquid_circuit_reinit();
    maintance_flag = 0;
    special_clr_wait_maintence_done_flag = 0;
    liquid_circuit_monitor.bubble_n_status = 0;
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    report_reagent_supply_consume(WASH_B, 0, LIQUID_PIPELINE_PERF);
    FAULT_CHECK_END();
    r2_clean_mutex_lock(0);
    LOG("liquid_circuit: mode_self maintance work done.\n");
    return 0;
}

/**
 * @brief: 关闭液路系统所有泵阀。
 */
static void liquid_motor_init(void)
{
    int i = 0;

    for (i=DIAPHRAGM_PUMP_Q1; i<=DIAPHRAGM_PUMP_F4; i++) {
        valve_set(i, OFF);
    }
    for (i=VALVE_SV1; i<=VALVE_SV12; i++) {
        valve_set(i, OFF);
    }/* 关闭所有泵阀 */
}

/**
 * @brief: 清洗液A,B气泡检测开启。
 * @param: type 为清洗液类型。
 */
void normal_bubble_status_check(clean_liquid_type_t *type)
{
    maintance_flag = 1;
    liquid_circuit_monitor.bub_chk_switch = 1;
    work_queue_add(liquid_bubble_check, (void *)type);
}

/**
 * @brief: 清洗液气泡检测功能关闭。
 */
void normal_bubble_check_end(void)
{
    liquid_circuit_monitor.bub_chk_switch = 0;
    maintance_flag = 0;
}

/**
 * @brief: 5ml特殊清洗液泵状态重置。
 */
void liquid_onpower_5ml_pump_manage(void)
{
    clean_liquid_type_t type = CLEAN_TYPE_SPECIAL;

    maintance_flag = 1;
    LOG("liquid_circuit: 5ml pump reset!\n");
    pump_5ml_inuse_manage(1);
    valve_set(VALVE_SV4, ON);
    usleep(200*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    } else {
        pump_cur_steps = 0;
    }
    FAULT_CHECK_END();

    liquid_circuit_monitor.bub_chk_switch = 1;
    work_queue_add(liquid_bubble_check, (void *)&type);//自检维护打液也检测气泡
    if (pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1) == 0) {
        pump_cur_steps = LIQ_PERF_FULL_STEPS;
    }
    usleep(2100*1000);
    valve_set(VALVE_SV4, OFF);
    liquid_circuit_monitor.bub_chk_switch = 0;
    pump_5ml_inuse_manage(0);
    liquid_circuit_reinit();//在此增加凝块底噪获取
    maintance_flag = 0;
    special_clr_wait_maintence_done_flag = 0;
}

/**
 * @brief: 用于触发浮子开关后更换清洗液B的处理,更换结构后需进行一次样本针洗针操做进行气泡检测。
 * @return ：返回执行结果
 */
static int normal_clearer_changed_handle(void)
{
    LOG("liquid_circuit: normal clr change handle start.\n");
    if (gpio_get(WASHSENSOR_IDX)) {
        LOG("liquid_circuit: normal clearer still less!\n");
        return -1;
    }
    pump_5ml_inuse_manage(1);
    r2_clean_mutex_lock(1);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    valve_set(VALVE_SV12, ON);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q4, ON);
    usleep(7400*1000);
    valve_set(VALVE_SV12, OFF);
    usleep(230*1000);                //液面重置
    valve_set(DIAPHRAGM_PUMP_Q4, OFF);
    usleep(300*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    FAULT_CHECK_END();
    r2_clean_mutex_lock(0);
    pump_5ml_inuse_manage(0);
    LOG("liquid_circuit: normal clr change handle end.\n");
    return 0;
}

/**
 * @brief: 用于清洗液A更换后的处理。
 * @return ：返回执行结果
 */
static int special_clearer_changed_handle(void)
{
    int i = 0;
    static int first = 1;

    if (gpio_get(SPCL_CLEAR_IDX)) {
        LOG("liquid_circuit: special clearer unposition!\n");
        maintance_flag = 0;
        return -1;
    }

    pump_5ml_inuse_manage(1);//特殊清洗特更换时不能进行缓存打液
    r2_clean_mutex_lock(1);//更换时不能进行开机维护打液
    LOG("liquid_circuit: specl clr change handle start!\n");
    valve_set(VALVE_SV4, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 |MODULE_FAULT_STOP_ALL);
    if (first) {
        motor_reset(MOTOR_CLEARER_PUMP, 1);
        if (motor_timedwait(MOTOR_CLEARER_PUMP, MOTOR_DEFAULT_TIMEOUT) != 0) {
            LOG("liquid_circuit: 5ml_pump move faild\n");
            FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
        } else {
            pump_cur_steps = 0;
            first = 0;
        }
    } else {
        if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("liquid_circuit: 5ml_pump move faild\n");
            FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
        } else {
            pump_cur_steps = 0;
        }
    }
    FAULT_CHECK_END();
    usleep(10*1000);
    valve_set(VALVE_SV4, OFF);

    valve_set(VALVE_SV12, ON);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    valve_set(VALVE_SV4, ON);
    usleep(10*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 2);
    usleep(2000*1000);
    valve_set(VALVE_SV4, OFF);
    valve_set(VALVE_SV10, ON);
    usleep(100*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 0);
    usleep(100*1000);
    valve_set(VALVE_SV10, OFF);

    valve_set(VALVE_SV4, ON);
    usleep(10*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 2);
    usleep(2000*1000);
    valve_set(VALVE_SV4, OFF);

    valve_set(VALVE_SV10, ON);
    usleep(100*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS-1000, 0);
    valve_set(VALVE_SV10, OFF);
    pump_5ml_inuse_manage(0);

    /* 增加洗针池清洗 */
    for (i=0;i<3;i++) {
        valve_set(DIAPHRAGM_PUMP_Q4, ON);
        usleep(150*1000);
        valve_set(DIAPHRAGM_PUMP_Q4, OFF);
        usleep(80*1000);
    }
    valve_set(VALVE_SV12, OFF);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_Q4, ON);
    usleep(150*1000);
    valve_set(DIAPHRAGM_PUMP_Q4, OFF);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    LOG("liquid_circuit: specl clr change handle end!\n");
    r2_clean_mutex_lock(0);

    return 0;
}

/**
 * @brief: 清洗液重新装载后更新上位机传回的信息并执行更换流程。
 * @param: info 上位机回传的清洗液信息。
 */
void clean_liquid_para_set(consum_info_t *info)
{
    clean_liquid_sta_t *cls = NULL;

    LOG("liqduid_circuit: get clearer type: %d.\n", info->type);
    if (info->type == WASH_A) {
        cls = &g_cls[CLEAN_TYPE_SPECIAL];
    } else if (info->type == WASH_B) {
        cls = &g_cls[CLEAN_TYPE_NORMAL];
    } else if (info->type == WASTE_WATER) {
        cls = &g_cls[CLEAN_TYPE_WASTE];
        cls->ready = info->enable;
        cls->report = 0;
        LOG("liquid_circuit : waste tank stage reset done!\n");
        goto out;
    } else {
        goto out;
    }

    if (info->binit == true) {//此标志表示是否需要更新清洗液瓶操作
        LOG("liquid circuit: clearer no need to change handle.\n");
        cls->ready = info->enable;
        cls->report = 0;
        goto out;
    }

    if (get_machine_stat() == MACHINE_STAT_RUNNING) {
        /* 运行时进行更换需等待 */
        if (machine_maintence_state_get()) {
            LOG("self maintence runing!\n");
            do {/* 若在自检时则等待自检结束 */
               usleep(50*1000);
            } while (machine_maintence_state_get());
            LOG("running maintence done start clr prefusion.\n");
        } else {
            if (R2_CLEAN_NONE == get_r2_clean_flag()) {
                LOG("R2 non-occupy.\n");
            } else {
                LOG("waiting for r2 cycle finish.\n");
                do {/* 若在R2洗针时则等待洗针结束 */
                    usleep(50*1000);
                } while (get_r2_clean_flag());
                LOG("r2 cycle finished.\n");
            }
        }
    }

    cls->ready = info->enable;
    cls->report = 0;
    if (cls->ready && get_machine_stat() != MACHINE_STAT_STOP) {
        if (info->type == WASH_A) {
            special_clearer_changed_handle();
            liquid_circuit_monitor.bubble_s_status = 0;
        } else if (info->type == WASH_B) {
            normal_clearer_changed_handle();
        } else {
            LOG("liq_cir:type invalid.\n");
        }
    }

out:
    LOG("clearer change handle finished\n");

}

/**
 * @brief: 普通清洗液管路排空。
 * @return ：返回执行结果
 */
int pipe_remain_release(void)
{
    int i = 0, ret = -1;
    liquid_pump_ctl_t pump_para_for_q1 = {DIAPHRAGM_PUMP_Q1, 0, 1};
    liquid_pump_ctl_t pump_para_for_q2 = {DIAPHRAGM_PUMP_Q2, 0, 1};
    liquid_pump_ctl_t pump_para_for_q3 = {DIAPHRAGM_PUMP_Q3, 0, 0};
    liquid_pump_ctl_t pump_para_for_q4 = {DIAPHRAGM_PUMP_Q4, 0, 0};
    pos_t rt_para = {0}, s_para = {0};
    thrift_motor_para_t motor_r2_y = {0};
    thrift_motor_para_t motor_r2_z = {0};
    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &motor_r2_y);
    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &motor_r2_z);
    motor_time_sync_attr_t motor_s = {0};

    motor_s.v0_speed = 100;
    motor_s.vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].speed;
    motor_s.speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].speed;
    motor_s.max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].acc;
    motor_s.acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].acc;

    if (reset_all_motors() < 0) {
        LOG("liquid_circuit: reset all motors faild.\n");
        return -1;
    }

    get_special_pos(MOVE_R2_CLEAN, 0, &rt_para, FLAG_POS_UNLOCK);
    get_special_pos(MOVE_S_CLEAN, 0, &s_para, FLAG_POS_NONE);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP,
        rt_para.y, motor_r2_y.speed, motor_r2_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("auto_cal: R2.y move failed!\n");
        return -1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_s.step = abs(s_para.x);
    motor_s.acc = calc_motor_move_in_time(&motor_s, STARTUP_TIMES_S_X);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, s_para.x,
                                s_para.y, motor_s.speed, motor_s.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        ret = -1;
    }
    FAULT_CHECK_END();

    LOG("remain release work start.\n");
    maintance_flag = 1;
    valve_set(VALVE_SV4, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    } else {
        pump_cur_steps = 0;
    }
    FAULT_CHECK_END();
    usleep(100*1000);
    valve_set(VALVE_SV4, OFF);
    valve_set(VALVE_SV12,ON);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(50*1000);
    valve_set(VALVE_SV12,OFF);//抽洗针池

    for (i=0; i<2; i++) {
        speccial_clear_recycle();//回收
        usleep(500*1000);
    }
    valve_set(VALVE_SV10,ON);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);
    usleep(1500*1000);
    valve_set(VALVE_SV10,OFF);
    valve_set(VALVE_SV4,ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_CLEARER_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: liquid pump move timeout\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL1, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    }
    FAULT_CHECK_END();
    pump_cur_steps = 0;
    usleep(200*1000);
    valve_set(VALVE_SV4,OFF);
    LOG("liquid_circuit: special clearer recycle done\n");

    valve_set(VALVE_SV12,ON);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(100*1000);
    for (i=0; i<2; i++) {
        valve_set(VALVE_SV8,ON);
        valve_set(VALVE_SV10,ON);
        usleep(50*1000);
        valve_set(DIAPHRAGM_PUMP_Q2,ON);
        usleep(50*1000);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_SPCL_CLEAR_FULL_STEPS, 1);
        usleep(1000*1000);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_STOP_ALL);
        motor_reset(MOTOR_CLEARER_PUMP, 1);
        if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
           LOG("liquid_circuit: pump motor wait timeout!\n");
           FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
        }
        FAULT_CHECK_END();
        usleep(100*1000);
        pump_cur_steps = 0;
        valve_set(VALVE_SV8,OFF);
        valve_set(VALVE_SV10,OFF);
        valve_set(DIAPHRAGM_PUMP_Q2,OFF);
        usleep(50*1000);
    }
    valve_set(VALVE_SV12,OFF);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);

    valve_set(VALVE_SV12,ON);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    for (i=0;i<2;i++) {
        valve_set(VALVE_SV10,ON);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, PIPE_PILL_S_STPES_TRANS_CAPA, 1);
        valve_set(VALVE_SV10,OFF);
        valve_set(VALVE_SV9,ON);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, PIPE_PILL_S_STPES_TRANS_CAPA, 0);
        usleep(500*1000);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, PIPE_PILL_S_STPES_TRANS_CAPA + 30, 1);
        valve_set(VALVE_SV9,OFF);
        valve_set(VALVE_SV10,ON);
        pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, PIPE_PILL_S_STPES_TRANS_CAPA + 30, 0);
        valve_set(VALVE_SV10,OFF);
    }
    valve_set(VALVE_SV12,OFF);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    LOG("liquid_circuit: release 5ml pump clearer perfusion done\n");
    /* 特殊清洗液公用管路排空 */
    valve_set(VALVE_SV12,ON);
    valve_set(DIAPHRAGM_PUMP_F3,ON);
    valve_set(VALVE_SV8,ON);
    valve_set(VALVE_SV10,ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q2,ON);
    usleep(10000*1000);
    valve_set(VALVE_SV8,OFF);
    valve_set(VALVE_SV10,OFF);
    valve_set(DIAPHRAGM_PUMP_Q2,OFF);
    LOG("liquid_circuit: release manage 5ml pipe line done.\n");

    valve_set(VALVE_SV11,ON);
    valve_set(DIAPHRAGM_PUMP_F2,ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    liquid_pump_pwm_open(&pump_para_for_q3);
    FAULT_CHECK_END();
    usleep(9000*1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q3);
    usleep(200*1000);
    valve_set(VALVE_SV11,OFF);
    valve_set(DIAPHRAGM_PUMP_F2,OFF);
    LOG("liquid_circuit: release Q3 pipe line work done.\n");

    valve_set(VALVE_SV12,ON);
    valve_set(DIAPHRAGM_PUMP_F3,ON);
    usleep(200*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    liquid_pump_pwm_open(&pump_para_for_q4);
    FAULT_CHECK_END();
    usleep(9000*1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q4);
    valve_set(VALVE_SV12,OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3,OFF);
    LOG("liquid_circuit: release Q4 pipe line work done.\n");

    valve_set(DIAPHRAGM_PUMP_F1,ON);
    valve_set(DIAPHRAGM_PUMP_F4,ON);
    usleep(400*1000);
    valve_set(VALVE_SV1,ON);
    usleep(100*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    liquid_pump_pwm_open(&pump_para_for_q1);
    FAULT_CHECK_END();
    usleep(10000*1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q1);
    usleep(500*1000);
    valve_set(VALVE_SV1,OFF);
    usleep(50*1000);
    valve_set(VALVE_SV2,ON);
    usleep(100*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    liquid_pump_pwm_open(&pump_para_for_q1);
    FAULT_CHECK_END();
    usleep(5000*1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q1);
    usleep(400*1000);
    valve_set(VALVE_SV2,OFF);
    usleep(50*1000);
    valve_set(VALVE_SV7,ON);
    usleep(50*1000);
    for (i=0;i<8;i++) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        liquid_pump_pwm_open(&pump_para_for_q1);
        FAULT_CHECK_END();
        usleep(500*1000);
        liquid_pump_close(DIAPHRAGM_PUMP_Q1);
        usleep(200*1000);
    }
    valve_set(VALVE_SV7,OFF);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_F1,OFF);
    valve_set(DIAPHRAGM_PUMP_F4,OFF);
    LOG("liquid_circuit: release Q1 pipe line work done.\n");

    get_special_pos(MOVE_R2_CLEAN, 0, &rt_para, FLAG_POS_UNLOCK);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z,
                            CMD_MOTOR_MOVE_STEP,
                            rt_para.z + NEEDLE_R2_NOR_CLEAN_STEP,
                            motor_r2_z.speed,
                            motor_r2_z.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: R2_Z motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
    }
    FAULT_CHECK_END();
    valve_set(DIAPHRAGM_PUMP_F3,ON);
    valve_set(VALVE_SV3,ON);
    liquid_pump_pwm_open(&pump_para_for_q2);
    usleep(5000*1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q2);
    valve_set(VALVE_SV3,OFF);
    usleep(1000*1000);
    valve_set(DIAPHRAGM_PUMP_F3,OFF);
    LOG("liquid_circuit: release Q2 pipe line work done.\n");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_Z, 0);
    if (motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: R2_z motor wait timeout!\n");
        FAULT_CHECK_DEAL(MOTOR_NEEDLE_R2_Z, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    ret = 0;
    FAULT_CHECK_END();
    if (reset_all_motors() < 0) {
        LOG("liquid_circuit: reset all motors faild.\n");
        ret = -1;
    }
    maintance_flag = 0;

    LOG("remain release work done.\n");
    return ret;
}

static void press_record_file_init(void)
{
    if (record_file_fd == NULL) {
        record_file_fd = fopen(PRESS_DATA_RECORD_PATH, "a");
        if (record_file_fd == NULL) {
            LOG("open file faild.\n");
        }
    }
}

/**
 * @brief: 凝块检测数值异常后的订单删除处理。
 * @para: check_result 检测结果
 */
static void clot_check_handle_for_cur_order(int check_result)
{
    int orderno_handle_flag = 0;
    uint32_t check_orderno = 0;
    needle_s_cmd_t cmd_s = 0;

    if (clot_cotl.orderno[1] != 0) {
        check_orderno = clot_cotl.orderno[1];
        cmd_s = clot_cotl.cmd[1];
        clot_cotl.orderno[1] = 0;
        LOG("clot check: handle last cycle order %d.\n",check_orderno);
    } else {
        check_orderno = clot_cotl.orderno[0];
        cmd_s = clot_cotl.cmd[0];
        LOG("clot check: handle current order %d .\n",check_orderno);
    }
    orderno_handle_flag = sq_clot_check_flag_get(check_orderno, check_result);

    if (orderno_handle_flag == HANDLING_MODE_DO_NOT_DETECT_THIS_SAMPLE) {
        LOG("clot check : delete order number is %d\n", check_orderno);
        if (cmd_s == NEEDLE_S_SP) {
            delete_tube_order((void *)&check_orderno);
            LOG("delete for one tube.\n");
        } else if (cmd_s >= NEEDLE_S_DILU1_SAMPLE && cmd_s <= NEEDLE_S_DILU2_R1_TWICE) {
            liq_det_r1_set_delay_detach(check_orderno);
        } else {
            liq_det_set_cup_detach(check_orderno);
        }
    }
}

/**
 * @brief: 凝块检测数值判断。
 * @para: ul 吸样量的大小。
 * @return: 返回判断结果。
 */
static int clot_judge_func(float ul)
{
    int judge_result = 0;
    float sampler_min_data = 0;
    float clot_min_data = 0;
    float empty_min_data = 0;
    clot_para_t para = clot_data;
    static int clot_set_alarm_flag;
    static int clot_value_judge_flag;

    /* 峰值差异较小时直接回退不做判断 */
    if (abs(para.press_max / para.press_min) > 0.5) {
        LOG("clot judge : invalid value, out of expected range.\n");
        clot_value_judge_flag++;
        if (clot_value_judge_flag > 50) {
            /* 此处是否增加器件工作异常报警？ */
            LOG("error: press sensor may shut down!!!\n");
        }
        return judge_result;
    } else {
        clot_value_judge_flag = 0;
    }

    if (abs(para.press_min) >= 10400) {
        clot_set_alarm_flag += 1;
        /* 堵塞报警,连续两次触发堵塞阈值后进行报警 */
        if (clot_set_alarm_flag > 1) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, MODULE_FAULT_NEEDLE_S_CHK_CLOT);
            judge_result = 1;
        }
    } else {
        clot_set_alarm_flag = 0;
        if (ul <= 25 && ul >= 15) {/* 在不同吸样量使用不同曲线进行阈值判断，该曲线来源于各吸样量下的压力数据采集的拟合 */

        } else if (ul > 25 && ul <= 450) {
            if (ul > 25 && ul <= 120) {
                sampler_min_data = -14.96*ul - 1342.15;
                clot_min_data = -30.77*ul - 965.28;
                empty_min_data = -1.984*ul - 1920.14;
            } else {
                sampler_min_data = -0.000128*ul*ul*ul + 0.1392*ul*ul - 50.696 * ul - 2817.44;
                clot_min_data = -0.000136*ul*ul*ul + 0.13736*ul*ul - 43.008*ul - 5181.584;
                empty_min_data =-0.000176*ul*ul*ul + 0.176*ul*ul - 55.776 *ul -1597.848;
            }

            if (abs(sampler_min_data - para.press_min) > abs(clot_min_data - para.press_min)) {
                if (ul > 50 && ul < 190) {
                    judge_result = 1;
                    LOG("err: clot checked.\n");/* 凝块压力检测异常 在50 - 190ul吸样体积进行订单删除 */
                }
            }
            if (abs(sampler_min_data - para.press_min) > abs(empty_min_data - para.press_min)) {
                /* 空吸由于压力检测模型不稳定，不删订单 */
                LOG("err: empty checked.\n");
            }
        }
    }

    LOG("clot data: sampler calc value = %f clot calc value = %f, vol %f.\n", sampler_min_data, clot_min_data, ul);

    return judge_result;
}

/**
 * @brief: 凝块压力数据获取后的装载。
 * @para: data 进样器获取的压力数据。
 */
void set_clot_data(clot_para_t *data)
{
    int i = 0;

    clot_data.press_max = data->press_max;
    clot_data.press_min = data->press_min;
    clot_data.intg_value = data->intg_value;
    clot_data.count_sum_flag = data->count_sum_flag;
    clot_data.data_count = data->data_count;
    for (i=0; i<=data->count_sum_flag; i++) {
        clot_data.recorde_conut[i] = data->recorde_conut[i];
        clot_data.sigle_press_sum[i] = data->sigle_press_sum[i];
    }
    pthread_mutex_lock(&clot_data_mutex);
    pthread_cond_signal(&sem_clot_data);
    pthread_mutex_unlock(&clot_data_mutex);

}

/**
 * @brief: 凝块检测的启动线程。
 * @para: timeout等待超时时间
 * @return：返回条件等待结果0为成功
 */
static int clot_check_thread_condwait(int timeout)
{
    int ret = -1;
    struct timeval cur_time = {0};
    struct timespec range_time = {0};

    gettimeofday(&cur_time, NULL);
    range_time.tv_sec = cur_time.tv_sec + timeout/1000;
    range_time.tv_nsec = cur_time.tv_usec * 1000 + (timeout%1000)*1000000;

    pthread_mutex_lock(&clot_data_mutex);
    ret = pthread_cond_timedwait(&sem_clot_data, &clot_data_mutex, &range_time);//等待数据处理完成
    pthread_mutex_unlock(&clot_data_mutex);

    return ret;
}

/**
 * @brief: 凝块检测的启动线程。
 */
static void clot_check_thread(void *arg)
{
    int i = 0;
    int check_result = 0;
    clot_para_t *check_data = &clot_data;

    if (clot_cotl.stage) {
        clot_cotl.check_flag = 1;
    }

    sem_wait(&sem_clot_check);//减少检测流程耗时
    usleep(220*1000);//增加采集时间
    slip_set_clot_data_from_sampler(0, 0);
    if (clot_check_thread_condwait(OUTTIME_FOR_DATAWAIT) != 0) {
        memset(&clot_cotl, 0, sizeof(clot_check_t));
        LOG("wait clot data from sampler timeout.\n");
        return;
    }

    if (check_data->data_count > 0) {
        clot_write_log("%s clot get data:vol %f noise %f max_value %f  min_value %f count %d integral value %f count&value {", \
            log_get_time(), clot_cotl.vol, g_presure_noise_get(), check_data->press_max, check_data->press_min, check_data->data_count, check_data->intg_value);
        for (i=0; i<=check_data->count_sum_flag; i++) {
            clot_write_log("(%d, %f) ",check_data->recorde_conut[i] - (uint16_t)g_presure_noise_get(), check_data->sigle_press_sum[i]);
        }
        clot_write_log("}\n--------------------------------------------------------------\n");

        if (check_data->data_count != 0) {
            check_result = clot_judge_func(clot_cotl.vol);
        } else {
            LOG("clot data get faild.\n");
        }

        clot_check_handle_for_cur_order(check_result);
    }
    memset(&clot_cotl, 0, sizeof(clot_check_t));
    LOG("clot check end...\n");
}

/**
 * @brief: 凝块检测接口。
 * @para: uol 吸样量的大小。
 * @para: stage 为开启和结束标志。
 */
void liquid_clot_check_interface(double vol, int stage)
{
#if 1
    int check_flag = 0; /* 暂屏蔽凝块检测，20250718 */
#else
    int check_flag = get_clot_check_flag();
#endif

    if (check_flag) {
        clot_cotl.stage = stage;
        if (stage && (clot_cotl.check_flag == 0)) {
            clot_cotl.vol = (float)vol;
            if (slip_set_clot_data_from_sampler(1, clot_cotl.orderno[0]) == 0) {
                LOG("clot check start...\n");
                work_queue_add(clot_check_thread, NULL);
            }
        } else {
            if (clot_cotl.check_flag) {
                sem_post(&sem_clot_check);
            }
        }
    } else {
        LOG("clot check has been closed.\n");
    }
}

/**
 * @brief: 试剂针普通清洗通电自检流程。
 */
int r2_normal_clean_onpower_selfcheck(void)
{
    pos_t rt_para = {0};
    thrift_motor_para_t motor_r2_y = {0};
    thrift_motor_para_t motor_r2_z = {0};
    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &motor_r2_y);
    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &motor_r2_z);

    motor_reset(MOTOR_NEEDLE_R2_Z, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("motor R2-Zaxis wait timeout.\n");
        return -1;
    }

    motor_reset(MOTOR_NEEDLE_R2_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("motor R2-Yaxis wait timeout.\n");
        return -1;
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    get_special_pos(MOVE_R2_CLEAN, 0, &rt_para, FLAG_POS_UNLOCK);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP,
        rt_para.y, motor_r2_y.speed, motor_r2_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("auto_cal: R2.y move failed!\n");
        return -1;
    }

    get_special_pos(MOVE_R2_CLEAN, 0, &rt_para, FLAG_POS_UNLOCK);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z,
                            CMD_MOTOR_MOVE_STEP,
                            rt_para.z,
                            motor_r2_z.speed,
                            motor_r2_z.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: R2_Z motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
    }
    FAULT_CHECK_END();

    valve_set(VALVE_SV12, ON);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(100*1000);
    valve_set(VALVE_SV3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, ON);
    sleep(8);
    valve_set(DIAPHRAGM_PUMP_Q2, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV3, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV12, OFF);
    motor_reset(MOTOR_NEEDLE_R2_Z, 0);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("motor R2-Zaxis wait timeout.\n");
        return -1;
    }

    motor_reset(MOTOR_NEEDLE_R2_Y, 0);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("motor R2-Yaxis wait timeout.\n");
        return -1;
    }

    return 0;
}

/**
 * @brief: 样本针内壁清洗通电自检流程。
 */
void s_noraml_clean_onpower_selfcheck(void)
{

    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT * 2)) {
          FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
    }

    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
    }

    valve_set(DIAPHRAGM_PUMP_F1, ON);
    valve_set(DIAPHRAGM_PUMP_F4, ON);
    usleep(200*1000);
    valve_set(VALVE_SV1, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    usleep(5000*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV1, OFF);
    usleep(800*1000);
    valve_set(VALVE_SV7, ON);
    usleep(200*1000);
    liquid_pump_pwm_open(&pump_para_for_check);
    usleep(2000*1000);
    liquid_pump_close(DIAPHRAGM_PUMP_Q1);
    valve_set(VALVE_SV7, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F1, OFF);
    valve_set(DIAPHRAGM_PUMP_F4, OFF);

}

/**
 * @brief: 特殊清洗液填充通电自检流程。
 */
int spcl_cleaner_fill_onpower_selfcheck(void)
{
    int i = 0;

    valve_set(VALVE_SV4, ON);
    usleep(100*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP,MOTOR_DEFAULT_TIMEOUT) != 0) {
        LOG("liquid_circuit: pump motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
        valve_set(VALVE_SV4, OFF);
        return -1;
    }
    pump_cur_steps = 0;
    FAULT_CHECK_END();
    valve_set(VALVE_SV4, OFF);
    usleep(20*1000);
    valve_set(VALVE_SV12, ON);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(100*1000);
    for (i=0; i<3; i++) {
        valve_set(VALVE_SV4, ON);
        if (pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 4500, 1) < 0) {
            liquid_motor_init();
            LOG("prefusion faild.\n");
            return -1;
        }
        usleep(2100*1000);
        valve_set(VALVE_SV4, OFF);
        usleep(100*1000);
        valve_set(VALVE_SV10, ON);
        if (pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 4500, 0) < 0) {
            liquid_motor_init();
            LOG("prefusion faild.\n");
            return -1;
        }
        usleep(2100*1000);
        valve_set(VALVE_SV10, OFF);
    }
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV12, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV4, ON);
    usleep(100*1000);
    if (pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 900, 1) < 0) {
        liquid_motor_init();
        LOG("prefusion faild.\n");
        return -1;
    }
    usleep(2100*1000);
    valve_set(VALVE_SV4, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV11, ON);
    usleep(100*1000);
    valve_set(VALVE_SV9, ON);
    usleep(100*1000);
    if (pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 900, 0) < 0) {
        liquid_motor_init();
        LOG("prefusion faild.\n");
        return -1;
    }
    usleep(2100*1000);
    valve_set(VALVE_SV9, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV11, OFF);
    return 0;
}

/**
 * @brief: 洗针池通电自检流程。
 */
void wash_pool_onpower_selfcheck(void)
{
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q4, ON);
    sleep(5);
    valve_set(VALVE_SV12, ON);
    sleep(3);
    valve_set(DIAPHRAGM_PUMP_Q4, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV12, OFF);
}

/**
 * @brief: 暂存池通电自检流程。
 */
void stage_pool_clean_onpower_selfcheck(void)
{
    valve_set(DIAPHRAGM_PUMP_F2, ON);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_Q3, ON);
    sleep(7);
    valve_set(VALVE_SV11, ON);
    usleep(200*1000);
    valve_set(VALVE_SV11, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV11, ON);
    usleep(500*1000);
    valve_set(DIAPHRAGM_PUMP_Q3, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F2, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV11, OFF);
}

void press_data_add_in_list(int count, float *buf, int orderno)
{
    press_recort_t *data_node= NULL;

    if (record_file_fd == NULL) {
        LOG("ready to wrire file can not be use.\n");
        return;
    }
    if (ftell(record_file_fd) > PRESS_RECORD_FILE_MAX_SIZE) {
        fclose(record_file_fd);
        rename(PRESS_DATA_RECORD_PATH, PRESS_DATA_RECORD_PATH".old");
        record_file_fd = fopen(PRESS_DATA_RECORD_PATH, "w");
        if (record_file_fd == NULL) {
            LOG("ready to wrire file can not be use.\n");
            return;
        }
    }

    data_node = (press_recort_t *)malloc(sizeof(press_recort_t));
    data_node->data_buffer =(float *)malloc(sizeof(float)*count);
    data_node->cnt = count;
    data_node->order_no = orderno;
    memcpy(data_node->data_buffer, buf, sizeof(float)*count);
    pthread_mutex_lock(&press_data_mutex);
    list_add_tail(&data_node->data_sibling, &press_record_list);
    pthread_mutex_unlock(&press_data_mutex);
}

static void press_data_write(void)
{
    int i = 0, idx = 0, write_cnt = 0;
    struct timeval tv;
    struct tm tm;
    press_recort_t *node= NULL;
    press_recort_t *n= NULL;

    if (!list_empty_careful(&press_record_list)) {
        list_for_each_entry_safe(node, n, &press_record_list, data_sibling) {
            pthread_mutex_lock(&press_data_mutex);
            gettimeofday(&tv, NULL);
            localtime_r(&tv.tv_sec, &tm);
            fprintf(record_file_fd, "%02d-%02d\t%02d:%02d:%02d\t",
            tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            fprintf(record_file_fd, "order num: %d\n",node->order_no);
            for (i=0; i<(node->cnt/6); i++) {
                fprintf(record_file_fd, "%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t", node->data_buffer[idx], node->data_buffer[idx+1],\
                    node->data_buffer[idx+2], node->data_buffer[idx+3],node->data_buffer[idx+4],node->data_buffer[idx+5]);
                idx+=6;
            }
            fprintf(record_file_fd, "\n-----------------------------------------------------\n");
            list_del(&node->data_sibling);
            free(node->data_buffer);
            free(node);
            pthread_mutex_unlock(&press_data_mutex);
            write_cnt++;
        }
        LOG("write count record is %d.\n", write_cnt);
    }
}

/* 液路监控线程 */
static void *liquid_circuit_task(void *arg)
{
    static long long last_drainage_time = 0;

    while (1) {
        /* 检查液路相关组件传感器状态 */
        liq_sensor_status_check();
        /* 试剂仓定时排冷凝水 */
        if ((get_time() - last_drainage_time) / 1000 > CONST_DRAINAGE_TIME) {
            if (get_machine_stat() == MACHINE_STAT_STANDBY) {
                liq_reag_table_drainage();
                last_drainage_time = get_time();
                LOG("liquid_circuit: current time is %lld, last recorded time is %lld.\n", get_time(), last_drainage_time);
            }
        }
        special_clearer_preperfusion();/* 特殊清洗液容量监控 */
        press_data_write();
        sleep(1);
    }

    return NULL;
}

int liquid_circuit_init(void)
{
    pthread_t liquid_circuit_thread;

    sem_init(&sem_clot_check, 0, 0);
    sem_init(&sem_clearer_handle, 0, 0);
    pthread_cond_init(&sem_clot_data, NULL);
    pthread_mutex_init(&perf_mutex, NULL);
    pthread_mutex_init(&clot_data_mutex, NULL);
    pthread_mutex_init(&clr_change_mutex, NULL);
    pthread_mutex_init(&press_data_mutex, NULL);
    liquid_motor_init();/* 停止所有泵和电机 */
    INIT_LIST_HEAD(&press_record_list);
    press_record_file_init();

    if (0 != pthread_create(&liquid_circuit_thread, NULL, liquid_circuit_task, NULL)) {
        LOG("liquid_circuit thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

