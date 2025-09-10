#include "module_magnetic_bead.h"

static mag_param_t mag_param[MAGNETIC_CH_NUMBER] = {0}; /* 磁珠检测参数 */
static mag_data_cache_t mag_data_cache = {0}; /* 磁珠数据缓存 */

/* 初始化磁珠模块数据 */
int reinit_magnetic_data()
{
    int i = 0;

    clot_data_init();
    memset(&mag_param, 0, sizeof(mag_param));

    for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
        slip_magnetic_pwm_duty_set(i, 0);
    }

    return 0;
}

/* 重置某磁珠通道数据 */
int clear_one_magnetic_data(magnetic_pos_t index)
{
    LOG("clear index:%d, order_no:%d\n", index, mag_param[index].mag_order_no);
    memset(&mag_param[index], 0, sizeof(mag_param[0]));

    return 0;
}

/*
功能：上位机获取磁珠AD数据
参数：
    index：磁珠通道索引
返回值：
    -1：失败
    其它：磁珠AD数据
*/
int thrift_mag_data_get(magnetic_pos_t index)
{
    slip_magnetic_bead_t magnetic_bead = {0};
    int ret = 0;

    ret = slip_magnetic_bead_get(&magnetic_bead);
    if (ret == 0) {
        return magnetic_bead.ad_data[index];
    } else {
        return -1;
    }
}

/*
功能：启动磁珠检测
参数：
    index：磁珠通道索引
返回值：
    无
*/
void magnetic_detect_start(magnetic_pos_t index)
{
    LOG("start index:%d\n", index);

    clot_param_get()[index].clot_percent = mag_param[index].clot_percent;
    clot_param_get()[index].max_time = mag_param[index].max_time;
    clot_param_get()[index].min_time = mag_param[index].min_time;
    clot_param_get()[index].order_no = mag_param[index].mag_order_no;
    clot_param_get()[index].enable = 1;
}

/*
功能：设置磁珠检测参数
参数：
    index：磁珠通道索引
    test_cup_magnectic：测试订单的磁珠参数
    order_no：测试杯订单号
    cuvette_serialno：反应杯盘序列号
    cuvette_strno：反应杯盘批号
返回值：
    无
*/
void magnetic_detect_data_set(magnetic_pos_t index, struct magnectic_attr *test_cup_magnectic,
    uint32_t order_no, uint32_t cuvette_serialno, const char *cuvette_strno)
{
    LOG("set index:%d, order_no:%d, power:%d, percent:%d, max_time:%d, min_time:%d\n", index, order_no, 
        test_cup_magnectic->magnectic_power, test_cup_magnectic->mag_beed_clot_percent, 
        test_cup_magnectic->mag_beed_max_detect_seconds, test_cup_magnectic->mag_beed_min_detect_seconds);

    clear_one_magnetic_data(index);
    mag_param[index].clot_percent = (float)(test_cup_magnectic->mag_beed_clot_percent/100.0);
    mag_param[index].power = test_cup_magnectic->magnectic_power;
    mag_param[index].max_time = test_cup_magnectic->mag_beed_max_detect_seconds;
    mag_param[index].min_time = test_cup_magnectic->mag_beed_min_detect_seconds;
    mag_param[index].mag_order_no = order_no;
    mag_param[index].cuvette_serialno = cuvette_serialno;
    strncpy(mag_param[index].cuvette_strno, cuvette_strno, strlen(cuvette_strno));

    slip_magnetic_pwm_duty_set(index, mag_param[index].power);
}

/*
功能：获取磁珠检测状态
参数：
    index：磁珠通道索引
返回值：
    mag_work_stat_t
*/
mag_work_stat_t magnetic_detect_state_get(magnetic_pos_t index)
{
    return mag_param[index].state;
}

/* 磁珠丢杯策略： 从当前磁珠检测完的测试杯当中，选取最先开始磁珠检测的测试杯 */
static int check_magnectic_finish_work(magnetic_pos_t index, long long *before_time)
{
    if (mag_param[index].state == MAG_FINISH || mag_param[index].state == MAG_TIMEOUT) {
        LOG("already ch:%d order:%d magnectic detect finish or timeout\n", index, mag_param[index].mag_order_no);
        if (*before_time == 0) {
            *before_time = mag_param[index].start_time;
        }

        if (mag_param[index].start_time <= *before_time) {
            *before_time = mag_param[index].start_time;
            return 1;
        }
    }

    return 0;
}

/*
功能：获取一个当前应该输出的磁珠位
参数：
返回值：
    magnetic_pos_t
*/
magnetic_pos_t magnetic_detect_output_get()
{
    int i = 0;
    magnetic_pos_t index = MAGNETIC_POS_INVALID;
    long long before_time = 0;

    for (i=0; i<MAGNETIC_POS_MAX; i++) {
        if (1 == check_magnectic_finish_work(i, &before_time)) {
            index = i;
        }
    }

    LOG("output index:%d\n", index);
    return index;
}

static int slip_magnetic_pwm_period_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint8_t *data = (uint8_t *)arg;
    uint8_t *result = (uint8_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    *data = *result;

    return 0;
}

/* 
功能：设置磁珠驱动力周期 
参数：
    data:磁珠驱动力周期
返回值：
    -1：失败
     0：成功
*/
int slip_magnetic_pwm_period_set(uint16_t data)
{
    uint16_t slip_data = 0;
    uint8_t result = 0;

    slip_data = htons(data);
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_PWM_PERIOD_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_PWM_PERIOD_SET_SUBTYPE, slip_magnetic_pwm_period_set_result, (void*)&result, 10000)) {
        return result;
    }

    return -1;
}

static int slip_magnetic_pwm_period_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint16_t *data = (uint16_t *)arg;
    uint16_t *result = (uint16_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    *data = ntohs(*result);
    LOG("get mag period:%d\n", *data);

    return 0;
}

/*
功能：获取磁珠驱动力周期  
参数：无
返回值：
    -1：失败
     其它：磁珠周期
*/
int slip_magnetic_pwm_period_get()
{
    uint16_t result = {0};

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_PWM_PERIOD_GET_SUBTYPE, 0, NULL);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_PWM_PERIOD_GET_SUBTYPE, slip_magnetic_pwm_period_get_result, (void*)&result, 10000)) {
        return result;
    }

    return -1;
}

static int slip_magnetic_pwm_duty_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    magnetic_pwm_duty_result_t *data = (magnetic_pwm_duty_result_t *)arg;
    magnetic_pwm_duty_result_t *result = (magnetic_pwm_duty_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：设置单个通道的磁珠驱动力
参数：
    index:磁珠检测通道索引
    data：驱动力索引 0:正常 1:弱 2:强 
返回值：
    -1：失败
    0：成功
*/
int slip_magnetic_pwm_duty_set(magnetic_pos_t index, uint8_t data)
{
    slip_magnetic_pwm_duty_t slip_data = {0};
    magnetic_pwm_duty_result_t result = {0};

    slip_data.index = (uint8_t)(MAGNETIC_POS_MAX-index-1);
    slip_data.data = data;

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_PWM_DUTY_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_PWM_DUTY_SET_SUBTYPE, slip_magnetic_pwm_duty_set_result, (void*)&result, 10000)) {
        return result.status;
    }

    return -1;
}

static int slip_magnetic_pwm_enable_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    magnetic_pwm_duty_result_t *data = (magnetic_pwm_duty_result_t *)arg;
    magnetic_pwm_duty_result_t *result = (magnetic_pwm_duty_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：设置磁珠驱动 开关
参数：
    index:磁珠检测通道索引
    enable：0:关闭 1：使能
返回值：
    -1：失败
    0：成功
*/
int slip_magnetic_pwm_enable_set(magnetic_pos_t index, uint8_t enable)
{
    slip_magnetic_pwm_enable_t slip_data = {0};
    magnetic_pwm_duty_result_t result = {0};

    slip_data.index = (uint8_t)(MAGNETIC_POS_MAX-index-1);
    slip_data.enable = enable;

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_PWM_ENABLE_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_PWM_ENABLE_SET_SUBTYPE, slip_magnetic_pwm_enable_set_result, (void*)&result, 10000)) {
        return result.status;
    }

    return -1;
}

static int slip_magnetic_bead_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    slip_magnetic_bead_t *dst_data = (slip_magnetic_bead_t *)arg;
    slip_magnetic_bead_t *result = (slip_magnetic_bead_t *)msg->data;
    int i = 0;

    if (msg->length == 0) {
        return 1;
    }

    for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
        #if 0 /* H3600的小磁珠板 */
        dst_data->ad_data[MAGNETIC_CH_NUMBER] = ntohs(result->ad_data[i]);
        #else /* H5000的大磁珠板 */
        dst_data->ad_data[MAGNETIC_CH_NUMBER-i-1] = ntohs(result->ad_data[i]);
        #endif
    }

    log_mag_data("mag addata: %d, %d, %d, %d\n", dst_data->ad_data[0], dst_data->ad_data[1], dst_data->ad_data[2], dst_data->ad_data[3]);

    return 0;
}

/* 
功能：获取磁珠AD数据
参数：
    result:磁珠AD数据 
返回值：
    -1：失败
    0：成功
*/
int slip_magnetic_bead_get(slip_magnetic_bead_t* result)
{
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_BEAD_GET_SUBTYPE, 0, NULL);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_BEAD_GET_SUBTYPE, slip_magnetic_bead_get_result, (void*)result, 2000)) {
        return 0;
    }

    return -1;
}

static int slip_magnetic_pwm_driver_level_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    magnetic_pwm_duty_result_t *data = (magnetic_pwm_duty_result_t *)arg;
    magnetic_pwm_duty_result_t *result = (magnetic_pwm_duty_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：设置磁珠驱动力表
参数：
    index: 驱动力索引
    data：驱动力 
返回值：
    -1：失败
    0：成功
*/
int slip_magnetic_pwm_driver_level_set(int index, uint8_t data)
{
    slip_magnetic_pwm_duty_t slip_data = {0};
    magnetic_pwm_duty_result_t result = {0};

    if (PWM_DRIVER_LEVEL_INDEX_NORMAL == index) {
        slip_data.index = 0;
    } else if (PWM_DRIVER_LEVEL_INDEX_WEAK == index) {
        slip_data.index = 1;
    } else if (PWM_DRIVER_LEVEL_INDEX_STRONG == index) {
        slip_data.index = 2;
    }

    slip_data.data = data;

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_PWM_DRIVER_LEVEL_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_PWM_DRIVER_LEVEL_SET_SUBTYPE, slip_magnetic_pwm_driver_level_set_result, (void*)&result, 10000)) {
        return result.status;
    }

    return -1;
}

static int slip_magnetic_pwm_driver_level_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    magnetic_pwm_duty_result_t *data = (magnetic_pwm_duty_result_t *)arg;
    magnetic_pwm_duty_result_t *result = (magnetic_pwm_duty_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：获取磁珠驱动力表
参数：
    index: 驱动力索引
返回值：
    -1：失败
    其它：驱动力值
*/
int slip_magnetic_pwm_driver_level_get(int index)
{
    uint8_t slip_data = PWM_DRIVER_LEVEL_INDEX_NORMAL;
    magnetic_pwm_duty_result_t result = {0};

    if (PWM_DRIVER_LEVEL_INDEX_NORMAL == index) {
        slip_data = 0;
    } else if (PWM_DRIVER_LEVEL_INDEX_WEAK == index) {
        slip_data = 1;
    } else if (PWM_DRIVER_LEVEL_INDEX_STRONG == index) {
        slip_data = 2;
    }

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_MAGNETIC_PWM_DRIVER_LEVEL_GET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_MAGNETIC_PWM_DRIVER_LEVEL_GET_SUBTYPE, slip_magnetic_pwm_driver_level_get_result, (void*)&result, 10000)) {
        return result.status;
    }

    return -1;
}

int magnetic_report_result(int order_no, void *data, int detect_pos, int cuvette_serialno, const char *cuvette_strno)
{
    order_result_t od_result = {0};
    CLOT_DATA *mag_data = NULL;

    memset(&od_result, 0, sizeof(od_result));

    mag_data = (CLOT_DATA *)data;
    od_result.clot_time = (int)(mag_data->clot_time*10);
    if (mag_data->alarm & CLOT_ALARM_LOW) {
        od_result.result_state = ABNORMAL;
    } else if (mag_data->alarm & CLOT_ALARM_HIGH) {
        od_result.result_state = UN_CLOT;
    } else if (mag_data->alarm & CLOT_ALARM_NOBEAD) {
        od_result.result_state = NO_BEAD;
    } else if (mag_data->alarm & CLOT_ALARM_NORMAL) {
        od_result.result_state = NORMAL;
    }

    od_result.AD_data = mag_data->m_alldata_clean;//mag_data->m_alldata;  /* 磁珠法上报原始数据 */
    od_result.AD_size = mag_data->m_alldata_cnt;
    od_result.sub_AD_data = NULL;
    od_result.sub_AD_size = 0;
    od_result.detect_pos = detect_pos+1;
    od_result.cuvette_serialno = cuvette_serialno;
    strncpy(od_result.cuvette_strno, cuvette_strno, strlen(cuvette_strno));
    device_status_count_add(DS_MAG1_USED_COUNT+detect_pos, 1);

    report_order_result(order_no, &od_result);
    return report_order_state(order_no,  OD_COMPLETION);
}

/* 检查 磁珠检测工作状态 */
static void check_magnetic_status()
{
    int i = 0;
    CLOT_DATA *clot_param = clot_param_get();

    for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
        if (clot_param[i].status == 1 && mag_param[i].state == MAG_UNUSED) {
            LOG("ch:%d order:%d magnectic detect start\n", i, mag_param[i].mag_order_no);
            mag_param[i].state = MAG_RUNNING;
            mag_param[i].start_time = clot_param[i].start_time;
            report_order_state(mag_param[i].mag_order_no,  OD_DETECTING);
        } else if (clot_param[i].status == 2 && mag_param[i].state == MAG_RUNNING) {
            /* 先上报结果，再置完成状态，避免极端情况下，结果数据被外部线程清除 */
            magnetic_report_result(mag_param[i].mag_order_no, &clot_param[i], i, mag_param[i].cuvette_serialno, mag_param[i].cuvette_strno);
            if (clot_param[i].alarm & CLOT_ALARM_HIGH){
                LOG("ch:%d order:%d magnectic detect timeout\n", i, mag_param[i].mag_order_no);
                mag_param[i].state = MAG_TIMEOUT;
            } else {
                LOG("ch:%d order:%d magnectic detect finish\n", i, mag_param[i].mag_order_no);
                mag_param[i].state = MAG_FINISH;
            }
            clot_data_free(&clot_param[i]);
        }
    }
}

/****************************************
控制 所有磁珠驱动力
enable: 0：关闭 1：使能
****************************************/
int all_magnetic_pwm_ctl(uint8_t enable)
{
    int i = 0;

    for (i=0; i<MAGNETIC_POS_MAX; i++) {
        if (slip_magnetic_pwm_enable_set(i, enable) == -1) {
            LOG("mag pwm ctl (%d) ch fail\n", i);
            return -1;
        }
    }

    return 0;
}

/* 定时获取磁珠数据的回调函数 */
static void timer_mag_action(int signo)
{
    static uint32_t count = 0;
    static uint32_t lost_comm = 0;
    static int report_err_flag = 0;
    static int flag = 0;
    CLOT_DATA *clot_param = clot_param_get();
    uint8_t get_flag = 0;
    slip_magnetic_bead_t magnetic_bead = {0};
    int ret = -1;
    int i = 0;

    if (flag == 0 && signo == SIGUSR1) {
        flag = 1;
       
        /* 若发生 检测停故障，则强行停止所有检测中的磁珠项目 */
        if (module_fault_stat_get() & MODULE_FAULT_STOP_ALL) {
            for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
                clot_param[i].enable = 0;
            }
        }

        if (count%(1000/GET_MAG_DATA_INTERVAL) == 0) {
            get_flag = 2;
        }

        for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
            if (clot_param[i].enable == 1) {
                get_flag = 1;
                break;
            }
        }

        if (get_flag == 1) {
            ret = slip_magnetic_bead_get(&magnetic_bead);
            if (ret == 0) {
                pthread_mutex_lock(&mag_data_cache.mutex_data);
                memcpy(&mag_data_cache.magnetic_bead_list[mag_data_cache.head], &magnetic_bead, sizeof(magnetic_bead));
                if (++mag_data_cache.head >= MAG_DATA_CACHE_SIZE_MAX) {
                    mag_data_cache.head = 0;
                }

                if (mag_data_cache.head == mag_data_cache.tail) {
                    if (++mag_data_cache.tail >= MAG_DATA_CACHE_SIZE_MAX) {
                        mag_data_cache.tail = 0;
                    }
                }
                pthread_mutex_unlock(&mag_data_cache.mutex_data);

                sem_post(&mag_data_cache.sem_rx);
            }
        } else if(get_flag == 2) {
             ret = slip_magnetic_bead_get(&magnetic_bead);
        }

        if (get_flag==1 || get_flag==2) {
            if (ret == -1 ) {
                if (lost_comm == 0) {
                    LOG("connect error start\n");
                }

                if (module_is_upgrading_now(SLIP_NODE_MAGNECTIC, OTHER_MAGNETIC_BEAD_GET_SUBTYPE) == 0 && ++lost_comm >= MAG_ERROR_COUNT_MAX) {
                    if (thrift_salve_heartbeat_flag_get() == 1 && report_err_flag == 0) {
                        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, (void *)MODULE_FAULT_MAG_CONNECT);
                        report_err_flag = 1;
                    }
                }
            } else {
                if (lost_comm > 0) {
                    LOG("connect error(count:%d, flag:%d) resume\n", lost_comm, report_err_flag);
                }

                lost_comm = 0;
                report_err_flag = 0;
            }
        }

        count++;

        flag = 0;
    }
}

/* 定时获取磁珠数据 */
static int create_timer()
{
    timer_t tid = 0;
    struct sigevent se = {0};
    struct itimerspec ts = {0}, ots = {0};
    struct sigaction sigIntHandler = {0};

    sigIntHandler.sa_handler =  timer_mag_action;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGUSR1,&sigIntHandler,NULL);

    memset(&se, 0, sizeof(se));
    se.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
    se.sigev_signo = SIGUSR1;
    se._sigev_un._tid = syscall(SYS_gettid);
    //se.sigev_value.sival_int = 111;
    LOG("timer tid: %d\n", syscall(SYS_gettid));

    if (timer_create(CLOCK_MONOTONIC, &se,&tid)<0) {
        return 0;
    }

    ts.it_value.tv_sec = GET_MAG_DATA_INTERVAL/1000;
    ts.it_value.tv_nsec = (GET_MAG_DATA_INTERVAL%1000)*1000000;
    ts.it_interval.tv_sec = GET_MAG_DATA_INTERVAL/1000;
    ts.it_interval.tv_nsec = (GET_MAG_DATA_INTERVAL%1000)*1000000;
    if (timer_settime(tid,TIMER_ABSTIME, &ts, &ots) < 0) {
        return 0;
    }

    return 0;
}

/* 磁珠数据处理任务 */
static void *mag_data_deal_task(void *arg)
{
    slip_magnetic_bead_t magnetic_bead = {0};

    LOG("start\n");
    while (1) {
        sem_wait(&mag_data_cache.sem_rx);

        pthread_mutex_lock(&mag_data_cache.mutex_data);
        memcpy(&magnetic_bead, &mag_data_cache.magnetic_bead_list[mag_data_cache.tail], sizeof(magnetic_bead));
        if (++mag_data_cache.tail >= MAG_DATA_CACHE_SIZE_MAX) {
            mag_data_cache.tail = 0;
        }
        pthread_mutex_unlock(&mag_data_cache.mutex_data);

        calc_clottime_all(magnetic_bead.ad_data);
        check_magnetic_status();
    }

    return NULL;
}

/* 创建空线程，专注于磁珠定时器的SIG信号回调，若共用其它线程，因SIG优先较高，可能导致其它线程响应不及时 */
static void *sig_timer_task(void *arg)
{
    create_timer();

    LOG("start\n");
    while (1) {
        sleep(60*60*12);
    }

    return NULL;
}

static inline int calc_max(int *data, int data_cnt, int start_pos, int *max_pos)
{
    int data_max = 0;
    int i = 0;

    if (data_cnt < start_pos) {
        return 0;
    }

    for (i=start_pos; i<data_cnt; i++) {
        if (data[i] > data_max) {
            data_max = data[i];
            if (max_pos) {
                *max_pos = i;
            }
        }
    }

    return data_max;
}

/* 
功能：通电质检的数据分析过程
参数：
    data_buff：磁珠数据
返回值：
    -1：失败
    0：成功
*/
int get_test_result(int *data_buff)
{
    int i = 0;
    int zero_count = 0, up_flag = 0, down_flag = 0, zero_flag = 0;
    int buff_cnt = 0;
    int idx_start = 0;
    max_param_t max_buff[MAGNETIC_TEST_DATA_COUNT] = {0};
    int max_idx = 0;
    int ret = 0;
    int error_count = 0;

    /* 寻找峰值 */
    for (i=0; i<MAGNETIC_TEST_DATA_COUNT; i++) {
        if (data_buff[i] <= 10) {/* 将小于等于10的数据规定为 一个波段的低位区间 */
            zero_count++;
        } else {
            zero_count = 0;
        }

        if (zero_count>1 || i==0) {
            zero_flag = 1;
        }

        if (zero_flag == 1 && data_buff[i] > 10 && up_flag == 0) {
            up_flag = 1;
            zero_flag = 0;
        } else if (zero_flag == 1 && data_buff[i] <= 10 && up_flag == 1) {
            down_flag = 1;
            zero_flag = 0;
        }

        //LOG("%d, %d, %d, %d, %d\n", clot_data->m_alldata_clean[i], zero_count, zero_flag, up_flag, down_flag);
        if (up_flag == 1) {
            buff_cnt++;
        }

        if (down_flag == 1) {
            idx_start = i-buff_cnt>=0 ? i-buff_cnt : 0;
            max_buff[max_idx].data = calc_max(data_buff, i, idx_start, &max_buff[max_idx].idx_max);
            max_buff[max_idx].idx_start = idx_start;
            max_buff[max_idx].idx_end = i;
            max_idx++;

            buff_cnt = 0;
            up_flag = 0;
            down_flag = 0;
            zero_flag = 0;
        }
    }

    /* 检查 峰值 */
    LOG("max_idx:%d\n", max_idx);
//    for (i=0; i<max_idx; i++) {
//        LOG("id:%d, index:%d, max:%d\n", i, max_buff[i].idx_max, max_buff[i].data);
//    }

    LOG("start compare\n");
    if (max_idx > 4) { /* 至少应该有4个峰值,且去掉第一个和最后一个，否则异常 */
        for (i=1; i<max_idx-1; i++) {
            LOG("id:%d, index:%d, max:%d\n", i, max_buff[i].idx_max, max_buff[i].data);
            if (max_buff[i].data<1800 && max_buff[i+1].data<1800) { /* 若峰值小于1800，且次数大于3，则异常 */
                LOG("error find max<1800, id:%d, index:%d, max1:%d, max2:%d\n", i, max_buff[i].idx_max, max_buff[i].data, max_buff[i+1].data);
                if (++error_count > 3) {
                    LOG("error count max > 3\n");
                    ret = -1;
                    break;
                }
            }

            if (abs(max_buff[i].data - max_buff[i+1].data) > 1000) { /* 若相邻高低峰差值大于1000，且次数大于3，则异常 */
                LOG("error find diff>1000, id:%d, index:%d, max1:%d, max2:%d\n", i, max_buff[i].idx_max, max_buff[i].data, max_buff[i+1].data);
                if (++error_count > 3) {
                    LOG("error count diff > 3\n");
                    ret = -1;
                    break;
                }
            }
        }
    } else {
        LOG("not find max data\n");
        ret = -1;
    }

    return ret;
}

/* 
功能：service通电质检接口
参数：
    test_type: 0:底噪信号值 1：磁珠摆动信号值
    fail_str: 失败内容
返回值：
    -1：失败
    0：成功
*/
int magnetic_poweron_test(int test_type, char *fail_str)
{
    int i = 0, j = 0;
    slip_magnetic_bead_t magnetic_bead = {0};
    int data[MAGNETIC_POS_MAX][MAGNETIC_TEST_DATA_COUNT] = {0};
    int result[MAGNETIC_POS_MAX] = {0};
    int ret = 0;
    int error_count = 0;
    char result_buff[256] = {0};
    char result_temp[256] = {0};

    /* 采集数据（2s） */
    LOG("get test addata, test_type:%d\n", test_type);
    for (i=0; i<MAGNETIC_TEST_DATA_COUNT; i++) {
        slip_magnetic_bead_get(&magnetic_bead);
        for (j=0; j<MAGNETIC_POS_MAX; j++) {
            data[j][i] = magnetic_bead.ad_data[j];
        }

        usleep(1);
    }

    /* 分析数据 */
    LOG("analyns test addata\n");
    switch (test_type) {
    case 0:
        for (i=0; i<MAGNETIC_POS_MAX; i++) {
            error_count = 0;

            for (j=0; j<MAGNETIC_TEST_DATA_COUNT; j++) {
                if (data[i][j] > 200) { /* 若底噪大于200，且次数大于3，则异常 */
                    LOG("ch[%d] error find >200 data\n", i);
                    if (++error_count > 3) {
                        LOG("ch[%d] error count noise > 3\n", i);
                        result[i] = -1;
                        break;
                    }
                }
            }
        }
        break;
    case 1:
        for (i=0; i<MAGNETIC_POS_MAX; i++) {
            LOG("poweron test ch:%d\n", i);
            result[i] = get_test_result(data[i]);
        }
        break;
    default:
        LOG("not support type\n");
        break;
    }

    /* 输出结果 */
    for (i=0; i<MAGNETIC_POS_MAX; i++) {
        if (result[i] == -1) {
            char temp_buff[32] = {0};

            ret = -1;
            sprintf(temp_buff, "%d,", i+1);
            strcat(result_temp, temp_buff);
        }
    }

     if (ret == -1) {
        strcat(result_buff, "通道:");
        strcat(result_buff, result_temp);
        if (test_type == 0) {
            strcat(result_buff, "原因:底噪超限");
        } else  if (test_type == 1) {
            strcat(result_buff, "原因:信号超限 或 极差超限");
        }
        utf8togb2312(result_buff, strlen(result_buff), fail_str, sizeof(result_buff));
    }

    LOG("test result:%d, fail ch:%s\n", ret, result_buff);

    return ret;
}

/* 初始化磁珠模块 */
int magnetic_bead_init(void)
{
    pthread_t mag_data_deal_thread = {0}, sig_timer_thread = {0};

    pthread_mutex_init(&mag_data_cache.mutex_data, NULL);
    sem_init(&mag_data_cache.sem_rx, 0, 0);

    reinit_magnetic_data();

    if (0 != pthread_create(&mag_data_deal_thread, NULL, mag_data_deal_task, NULL)) {
        LOG("mag_data_deal_task thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    if (0 != pthread_create(&sig_timer_thread, NULL, sig_timer_task, NULL)) {
        LOG("sig_timer_task thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

