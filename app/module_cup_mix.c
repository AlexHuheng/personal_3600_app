#include "module_cup_mix.h"

/* 混匀流程参数 */
static mix_param_t mix_param[MIX_POS_MAX] = 
{
    [MIX_POS_INCUBATION1] = {MOTOR_MIX_1, MODULE_FAULT_MIX1, MODULE_FAULT_SPEED_MIX1, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER},
    [MIX_POS_INCUBATION2] = {MOTOR_MIX_2, MODULE_FAULT_MIX2, MODULE_FAULT_SPEED_MIX2, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER},
    [MIX_POS_OPTICAL1]    = {MOTOR_MIX_3, MODULE_FAULT_MIX3, MODULE_FAULT_SPEED_MIX3, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER}
};

/* 混匀状态参数 */
static mix_status_t mix_status[MIX_POS_MAX] = {0}; 

/* 杯位置与试剂混匀位置转换 */
cup_mix_pos_t pos_cup_trans_mix(cup_pos_t pos)
{
    cup_mix_pos_t mix_pos = MIX_POS_INVALID;
    switch (pos) {
    case POS_PRE_PROCESSOR_MIX1:
        mix_pos = MIX_POS_INCUBATION1;
        break;
    case POS_PRE_PROCESSOR_MIX2:
        mix_pos = MIX_POS_INCUBATION2;
        break;
    default:
        break;
    }
    return mix_pos;
}


/* 初始化混匀模块数据 */
void reinit_cup_mix_data()
{
    memset(&mix_status, 0, sizeof(mix_status));
}

/* 重置某混匀通道数据 */
int clear_one_cup_mix_data(cup_mix_pos_t index)
{
    LOG("clear index:%d, order_no:%d\n", index, mix_status[index].order_no);
    memset(&mix_status[index], 0, sizeof(mix_status[0]));

    return 0;
}

/*
功能：启动混匀
参数：
    index：混匀索引
返回值：
    无
*/
void cup_mix_start(cup_mix_pos_t index)
{
    pthread_mutex_lock(&mix_param[index].mutex);
    pthread_cond_broadcast(&mix_param[index].cond);
    pthread_mutex_unlock(&mix_param[index].mutex);

    device_status_count_add(DS_INCUBATION_MIX1_USED_COUNT+index, 1);
    LOG("start index:%d\n", index);
}

/*
功能：停止混匀
参数：
    index：混匀索引
返回值：
    无
*/
void cup_mix_stop(cup_mix_pos_t index)
{
    LOG("stop index:%d\n", index);
    mix_status[index].stop = 1;
}

/*
功能：设置混匀参数
参数：
    index：混匀索引
    order_no：测试杯订单号
    rate：混匀速度
    time：混匀时长（ms）
返回值：
    无
*/
void cup_mix_data_set(cup_mix_pos_t index, uint32_t order_no, uint32_t rate, uint32_t time)
{
    LOG("set index:%d, order_no:%d, rate:%d, time:%d\n", index, order_no, rate, time);

    clear_one_cup_mix_data(index);
    mix_status[index].order_no = order_no;
    mix_status[index].rate = rate;
    mix_status[index].time = time;
}

/*
功能：获取混匀状态
参数：
    index：混匀索引
返回值：
    0: 未混匀
    1：正在混匀
    2：混匀完成
*/
cup_mix_work_stat_t cup_mix_state_get(cup_mix_pos_t index)
{
    return mix_status[index].state;
}

/* 孵育混匀抓走策略： 选取最先开始混匀的测试杯 */
static int check_mix_incu_finish_work(cup_mix_pos_t index, long long *before_time)
{
    if (mix_status[index].state == CUP_MIX_FINISH) {
        if (*before_time == 0) {
            *before_time = mix_status[index].start_time;
        }

        if (mix_status[index].start_time <= *before_time ) {
            *before_time = mix_status[index].start_time;
            return 1;
        }
    }

    return 0;
}

/*
功能：获取一个当前应该输出的孵育混匀位
参数：
返回值：
    cup_mix_pos_t
*/
cup_mix_pos_t cup_mix_incubation_output_get()
{
    int i = 0;
    cup_mix_pos_t index = MIX_POS_INVALID;
    long long before_time = 0;

    for (i=0; i<MIX_POS_OPTICAL1; i++) {
        if (1 == check_mix_incu_finish_work(i, &before_time)) {
            index = i;
        }
    }

    LOG("output index:%d\n", index);
    return index;
}
/* 
功能：service通电质检接口
参数：
    index：混匀索引
返回值：
    -1：失败
    0：成功
*/
int mix_poweron_test(cup_mix_pos_t index)
{
    int speed = 0;
    uint32_t mix_time = 0;
    int mix_circle = 0;
    int cnt = 0;
    uint32_t step_total = 0, predict_circle = 0;
    int acc = h3600_conf_get()->motor[mix_param[index].motor_id].acc;
    int ret = 0;
    long long start_time = 0; /* 混匀开始时间 */

    LOG("start index:%d\n", index);

    cnt = 0;
    start_time = get_time();
    speed = 13056;
    mix_time = 10000;

    /* 预估 混匀圈数，需要分为大于3000ms，大于1000ms，小于1000ms分别计算 */
    if (mix_time > 3000) {
        step_total = (mix_time/3000.0) * (abs(speed)*2 + 0.5*acc*pow(1, 2));
    } else if (mix_time > 1000) {
        step_total = abs(speed)*(mix_time/1000.0-1) + 0.5*acc*pow(1, 2);
    } else {
        step_total = 0.5*acc*pow(mix_time/1000.0, 2);
    }
    predict_circle = step_total / 800;

    motor_mix_circle_get_timedwait(mix_param[index].motor_id, 5000);/* 读取从而清零混匀计数 */
    LOG("cup mix start, index:%d, time:%d, speed:%d, acc:%d\n", index, mix_time, speed, acc);
    while (get_time() - start_time < mix_time) {
        if (cnt%MIX_ONE_TIME == 0) {
            speed = 0 - speed;
            motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_STOP, 0, 0, 5000);
            motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_MOVE_SPEED, 0, speed, 5000);
        }
        usleep(MIX_DELAY_INTERVAL);
        cnt++;
    }
    motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_STOP, 0, 0, 5000);

    mix_circle = motor_mix_circle_get_timedwait(mix_param[index].motor_id, 5000);
    LOG("cup mix index:%d, mix_circle:%d, predict_circle:%d\n", 
        index, mix_circle, predict_circle);

    /* 混匀次数不足处理 */
    if (mix_circle < predict_circle/2) {
        ret = -1;
    }

    /* 混匀结束，则复位 */
    if (motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_RST, 0, 1, 5000)) {
        ret = -1;
    }

    /* 混匀复位结束后，需要再转1/4圈(200步)，以确保磁铁再最下方 */
    motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_MOVE_STEP, 200, speed, 3000);

    LOG("cup mix finish, index:%d, ret:%d\n", index, ret);

    return ret;
}

/*
功能：混匀主任务
参数：
    arg：混匀索引
返回值：
    无
*/
static void *cup_mix_task(void *arg)
{
    cup_mix_pos_t index = *((cup_mix_pos_t*)arg);
    int speed = 0;
    uint32_t mix_time = 0;
    int mix_circle = 0;
    int cnt = 0;
    uint32_t step_total = 0, predict_circle = 0;
    int acc = h3600_conf_get()->motor[mix_param[index].motor_id].acc;

    LOG("start index:%d\n", index);
    while (1) {
        LOG("cup mix ready, index:%d\n", index);
        pthread_mutex_lock(&mix_param[index].mutex);
        pthread_cond_wait(&mix_param[index].cond, &mix_param[index].mutex);
        pthread_mutex_unlock(&mix_param[index].mutex);

        cnt = 0;
        mix_status[index].start_time = get_time();
        speed = mix_status[index].rate;
        mix_time = mix_status[index].time;

        /* 预估 混匀圈数，需要分为大于3000ms，大于1000ms，小于1000ms分别计算 */
        if (mix_time > 3000) {
            step_total = (mix_time/3000.0) * (abs(speed)*2 + 0.5*acc*pow(1, 2));
        } else if (mix_time > 1000) {
            step_total = abs(speed)*(mix_time/1000.0-1) + 0.5*acc*pow(1, 2);
        } else {
            step_total = 0.5*acc*pow(mix_time/1000.0, 2);
        }
        predict_circle = step_total / 800;

        motor_mix_circle_get_timedwait(mix_param[index].motor_id, 10000);/* 读取从而清零混匀计数 */
        LOG("cup mix start, index:%d, order:%d, time:%d, speed:%d, acc:%d\n", index, mix_status[index].order_no, mix_time, speed, acc);
        mix_status[index].state = CUP_MIX_RUNNING;
        while (get_time() - mix_status[index].start_time < mix_time) {
            if (module_fault_stat_get() & MODULE_FAULT_LEVEL2) {
                LOG("dectet fault, force break\n");
                break;
            }

            if (mix_status[index].stop == 1) {
                LOG("dectet stop, force break\n");
                break;
            }

            if (cnt%MIX_ONE_TIME == 0) {
                speed = 0 - speed;
                motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_STOP, 0, 0, 10000);
                motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_MOVE_SPEED, 0, speed, 10000);
            }
            usleep(MIX_DELAY_INTERVAL);
            cnt++;
        }
        motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_STOP, 0, 0, 10000);

        mix_circle = motor_mix_circle_get_timedwait(mix_param[index].motor_id, 10000);
        LOG("cup mix index:%d, order:%d, mix_circle:%d, predict_circle:%d\n", 
            index, mix_status[index].order_no, mix_circle, predict_circle);

        /* 混匀次数不足处理 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (mix_circle < predict_circle/2 && mix_status[index].stop == 0) {
            FAULT_CHECK_DEAL(FAULT_MIX, MODULE_FAULT_LEVEL2, (void*)mix_param[index].speed_fault_id);
        }
        FAULT_CHECK_END();
        mix_status[index].state = CUP_MIX_FINISH;

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        /* 混匀结束，则复位 */
        if (motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_RST, 0, 1, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_MIX, MODULE_FAULT_LEVEL2, (void*)mix_param[index].move_fault_id);
        }
        FAULT_CHECK_END();

        /* 混匀复位结束后，需要再转1/4圈(200步)，以确保磁铁再最下方 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_move_sync(mix_param[index].motor_id, CMD_MOTOR_MOVE_STEP, 200, speed, MOTOR_DEFAULT_TIMEOUT);
        FAULT_CHECK_END();

        mix_status[index].stop = 0;
        LOG("cup mix finish, index:%d, order:%d\n", index, mix_status[index].order_no);
    }

    return NULL;
}

/* 初始化混匀模块 */
int cup_mix_init()
{
    int i = 0;
    pthread_t cup_mix_thread[MIX_POS_MAX] = {0};
    static int index[MIX_POS_MAX] = {MIX_POS_INCUBATION1, MIX_POS_INCUBATION2, MIX_POS_OPTICAL1};

    reinit_cup_mix_data();

    for (i=0; i<MIX_POS_MAX; i++) {
        if (0 != pthread_create(&cup_mix_thread[i], NULL, cup_mix_task, &index[i])) {
            LOG("mix(%d)_task thread create failed!, %s\n", i, strerror(errno));
            return -1;
        }
    }

    return 0;
}

