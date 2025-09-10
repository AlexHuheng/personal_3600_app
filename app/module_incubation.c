#include "module_incubation.h"

/* 孵育参数 */
static incubation_param_t incubation_param[INCUBATION_POS_MAX] = {0};

/* 初始化孵育数据 */
int reinit_incubation_data()
{
    memset(&incubation_param, 0, sizeof(incubation_param));

    return 0;
}

/* 重置某孵育通道数据 */
int clear_one_incubation_data(incubation_pos_t index)
{
    LOG("clear index:%d, order_no:%d\n", index, incubation_param[index].order_no);
    memset(&incubation_param[index], 0, sizeof(incubation_param[0]));

    return 0;
}

/*
功能：启动孵育
参数：
    index：孵育通道索引
返回值：
    无
*/
void incubation_start(incubation_pos_t index)
{
    LOG("start index:%d\n", index);
    incubation_param[index].enable = 1;
}

/*
功能：设置孵育参数
参数：
    index：孵育通道索引
    order_no：测试杯订单号
    time：孵育时间
    cup_type: 杯子类型
返回值：
    无
*/
void incubation_data_set(incubation_pos_t index, uint32_t order_no, uint32_t time, cup_type_t cup_type)
{
    LOG("set index:%d, order_no:%d, time:%d, cup:%d\n", index, order_no, time, cup_type);

    clear_one_incubation_data(index);
    incubation_param[index].order_no = order_no;
    incubation_param[index].time = time;
    incubation_param[index].cup_type = cup_type;
}

/*
功能：获取孵育状态
参数：
    index：孵育通道索引
返回值：
    incubation_work_stat_t
*/
incubation_work_stat_t incubation_state_get(incubation_pos_t index)
{
    return incubation_param[index].state;
}

/* 孵育完成抓走策略： 从当前孵育完的测试杯当中， 选取 离孵育超时时刻 最近的测试杯 */
static int check_incubation_used_work(incubation_pos_t index, long long *before_time)
{
    long long timeout_time = 0; /* 孵育超时的时刻 */
    long long leave_time = 0; /* 离 孵育超时的剩余时长 */

    if (incubation_param[index].state == INCUBATION_FINISH) {  /* 孵育完成 */
        LOG("already order:%d, ch:%d cup incubation finish\n", incubation_param[index].order_no, index);
        timeout_time = incubation_param[index].start_time + incubation_param[index].time + INCUBATION_TIMEOUT_EXTRA;
        leave_time = timeout_time - time(NULL);

        if (*before_time == 0) {
            *before_time = leave_time;
        }

        if (leave_time <= *before_time) {
            *before_time = leave_time;
            return 1;
        }
    }

    return 0;
}

/* 孵育超时抓走策略： 从当前孵育超时的测试杯当中，选取最先开始孵育的测试杯 */
static int check_incubation_timeout_work(incubation_pos_t index, long long *before_time)
{
    if (incubation_param[index].state == INCUBATION_TIMEOUT) {  /* 孵育超时 */
        LOG("already order:%d, ch:%d cup incubation timeout\n", incubation_param[index].order_no, index);
        if (*before_time == 0) {
            *before_time = incubation_param[index].start_time;
        }

        if (incubation_param[index].start_time <= *before_time) {
            *before_time = incubation_param[index].start_time;
            return 1;
        }
    }

    return 0;
}

/*
功能：获取一个当前应该输出的孵育位(孵育完成)
参数：
返回值：
    incubation_pos_t
*/
incubation_pos_t incubation_finish_output_get()
{
    int i = 0;
    incubation_pos_t index = INCUBATION_POS_INVALID;
    long long before_time = 0;

    for (i=0; i<INCUBATION_POS_MAX; i++) {
        if (1 == check_incubation_used_work(i, &before_time)) {
            index = i;
        }
    }

    LOG("output index:%d\n", index);
    return index;
}

/*
功能：获取一个当前应该输出的孵育位(孵育超时)
参数：
返回值：
    incubation_pos_t
*/
incubation_pos_t incubation_timeout_output_get()
{
    int i = 0;
    incubation_pos_t index = INCUBATION_POS_INVALID;
    long long before_time = 0;

    for (i=0; i<INCUBATION_POS_MAX; i++) {
        if (1 == check_incubation_timeout_work(i, &before_time)) {
            index = i;
        }
    }

    LOG("output index:%d\n", index);
    return index;
}

/*
功能：设置孵育位上 订单号为order的杯子为无效状态
参数：
    order_no：订单号
返回值：
    无
*/
void incubation_inactive_by_order(uint32_t order_no)
{
    int i = 0;

    for (i=0; i<INCUBATION_POS_MAX; i++) {
        if (order_no == incubation_param[i].order_no) {
            incubation_param[i].inactive = 1;
        }
    }
}

/* 孵育主任务 */
static void *incubation_task(void *arg)
{
    int i = 0;
    time_t cur_tm = 0;

    LOG("start\n");
    while (1) {
        cur_tm = time(NULL);

        for (i=0; i<INCUBATION_POS_MAX; i++) {
            if (module_fault_stat_get() & MODULE_FAULT_STOP_ALL) {
                reinit_incubation_data();
            }

            if (incubation_param[i].enable == 1) {
                if (incubation_param[i].state == INCUBATION_UNUSED) {
                    incubation_param[i].start_time = time(NULL);
                    if (incubation_param[i].cup_type == DILU_CUP) { /* 若是稀释杯，则直接孵育超时 */
                        incubation_param[i].state = INCUBATION_TIMEOUT;
                        LOG("order:%d, ch:%d dilu cup incubation timeout\n", incubation_param[i].order_no, i);
                    } else {
                        incubation_param[i].state = INCUBATION_RUNNING;
                        report_order_state(incubation_param[i].order_no,  OD_INCUBATING);
                        LOG("order:%d, ch:%d cup incubation start\n", incubation_param[i].order_no, i);
                    }
                } else if (incubation_param[i].state == INCUBATION_FINISH && cur_tm-incubation_param[i].start_time >= incubation_param[i].time+INCUBATION_TIMEOUT_EXTRA) {
                    fault_incubation_t fault_data = {incubation_param[i].order_no, MODULE_FAULT_INCUBATION_TIMEOUT};

                    /* 先上报结果，再置完成状态，避免极端情况下，结果数据被外部线程清除 */
                    LOG("order:%d, ch:%d cup incubation timeout\n", incubation_param[i].order_no, i);
                    FAULT_CHECK_DEAL(FAULT_INCUBATION_MODULE, MODULE_FAULT_NONE, (void*)&fault_data);
                    incubation_param[i].state = INCUBATION_TIMEOUT;
                } else if (incubation_param[i].state == INCUBATION_RUNNING && cur_tm-incubation_param[i].start_time >= incubation_param[i].time) {
                    LOG("order:%d, ch:%d cup incubation finish\n", incubation_param[i].order_no, i);
                    incubation_param[i].state = INCUBATION_FINISH;
                }

                if (incubation_param[i].inactive == 1) {
                    if (incubation_param[i].state != INCUBATION_TIMEOUT) {
                        incubation_param[i].state = INCUBATION_TIMEOUT;
                        LOG("order:%d, ch:%d inactive cup incubation timeout\n", incubation_param[i].order_no, i);
                    }
                }
            }
        }

        sleep(1);
    }

    return NULL;
}

int incubation_init()
{
    pthread_t incubation_main_thread = {0};

    reinit_incubation_data();

    if (0 != pthread_create(&incubation_main_thread, NULL, incubation_task, NULL)) {
        LOG("incubation_task thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

