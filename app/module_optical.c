#include "module_optical.h"

static pthread_rwlock_t optrwLock = PTHREAD_RWLOCK_INITIALIZER;

/* 光学本底 */
static int bk_neg_flag_group[OPTICAL_CHANNEL_MAX][OPTICAL_MAX_WORK_STATIONS] = {0};
static uint16_t bk_data_group[OPTICAL_CHANNEL_MAX][OPTICAL_MAX_WORK_STATIONS] = {0};

/* 光学slip缓存 */
static slip_optical_data_t optical_data_group[OPTICAL_WORK_GROUP_NUM] = {0};

/* 光学检测工作参数 */
static struct optical_work_attr optical_work_stations[OPTICAL_MAX_WORK_STATIONS] = {0};

static int optical_led_calc_flag = 0; /* 光学校准完成标志。 0：未完成 1：已完成 */
static int optical_led_cmd_flag = 0; /* 光学slip指令是否正在执行。 0：未执行 1：执行中 */
static int led_off_flag = 0; /* 光学灯是否关闭标志. 0:未关闭(默认值) 1：关闭 */
static FILE *optical_data_fd = NULL;
static long long optical_can_recv_time[2] = {0}; /* 记录光学1、光学2收到can数据的时间 */
static char optical_version[3][64] = {0}; /* 记录光学1、光学2、HIL筛查子板上报的版本号 */

char* optical_version_get(uint8_t board_id)
{
    int index = 0;
 
    if (board_id == SLIP_NODE_OPTICAL_1) {
        index = 0;
    } else if (board_id == SLIP_NODE_OPTICAL_2) {
        index = 1;
    } else if (board_id == SLIP_NODE_OPTICAL_HIL) {
        index = 2;
    }

    return optical_version[index];
}

static void log_optical_data(const char *format, ...)
{
    static int fd = -1;
    static char log_optical_enable = 0;

    if (fd == -1) {
        fd = open("/tmp/log_optical", O_RDONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            return;
        }
    }

    read(fd, &log_optical_enable, 1);
    lseek(fd, 0, SEEK_SET);

    if (log_optical_enable == '1') {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

/* 判断是否校准光学LED电流 */
static int check_optical_calc()
{
    static int fd = -1;
    static char optical_calc_flag = 0;

    if (fd == -1) {
        fd = open("/tmp/optical_calc_flag", O_RDONLY | O_CREAT);
        if (fd < 0) {
            return -1;
        }
    }

    read(fd, &optical_calc_flag, 1);
    lseek(fd, 0, SEEK_SET);

    LOG("optical calc flag: %c\n", optical_calc_flag);
    if (optical_calc_flag == '1') {
        return 1;
    } else {
        return 0;
    }
}

/* 光学波长 转 数据寻址索引 */
static optical_wave_t wave_trans_to_enum(int wave)
{
    switch (wave) {
    case 340:
        return OPTICAL_WAVE_340;
    case 405:
        return OPTICAL_WAVE_405;
    case 570:
        return OPTICAL_WAVE_570;
    case 660:
        return OPTICAL_WAVE_660;
    case 800:
        return OPTICAL_WAVE_800;
    default:
        return OPTICAL_BACKGROUND;
    }

    return OPTICAL_BACKGROUND;
}

/* 光学子板组号 转 数据寻址索引 */
static int group_id_to_index(uint16_t group_id)
{
    int index = 0;

    switch (group_id) {
    case OPTICAL_GROUP_1:
        index = 0;
        break;
    case OPTICAL_GROUP_2:
        index = 1;
        break;
    default:
        LOG("not support group id: %d\n", group_id);
        break;
    }

    return index;
}

/* 初始化光学模块数据 */
int reinit_optical_data(void)
{
    memset(&optical_work_stations, 0, sizeof(optical_work_stations));

    return 0;
}

/* 重置某光学通道数据 */
int clear_one_optical_data(optical_pos_t index)
{
    LOG("clear index:%d, order_no:%d\n", index, optical_work_stations[index].order_no);
    memset(&optical_work_stations[index], 0, sizeof(optical_work_stations[0]));

    return 0;
}

/*
功能：启动光学检测
参数：
    index：光学通道索引
返回值：
    无
*/
void optical_detect_start(optical_pos_t index)
{
    LOG("start index:%d\n", index);
    optical_work_stations[index].enable = 1;
}

/*
功能：设置光学检测参数
参数：
    index：光学通道索引
    test_cup_optical：测试订单的光学参数
    order_no：测试杯订单号
    cuvette_serialno：反应杯盘序列号
    cuvette_strno：反应杯盘批号
返回值：
    无
*/
void optical_detect_data_set(optical_pos_t index, struct optical_attr *test_cup_optical, 
    uint32_t order_no, uint32_t cuvette_serialno, const char *cuvette_strno)
{
    LOG("set index:%d, order_no:%d, main_wave:%d, vice_wave:%d, main_sec:%d, vice_sec:%d\n", index, order_no, 
        test_cup_optical->main_wave, test_cup_optical->vice_wave, test_cup_optical->optical_main_seconds, test_cup_optical->optical_vice_seconds);

    clear_one_optical_data(index);
    optical_work_stations[index].main_wave_ch = wave_trans_to_enum(test_cup_optical->main_wave);
    optical_work_stations[index].vice_wave_ch = wave_trans_to_enum(test_cup_optical->vice_wave);
    optical_work_stations[index].main_wave_ad_cnt = test_cup_optical->optical_main_seconds*10;   /*秒转换为次数*/
    optical_work_stations[index].vice_wave_ad_cnt = test_cup_optical->optical_vice_seconds*10;
    optical_work_stations[index].order_no = order_no;
    optical_work_stations[index].cuvette_serialno = cuvette_serialno;
    strncpy(optical_work_stations[index].cuvette_strno, cuvette_strno, strlen(cuvette_strno));
}

/*
功能：获取光学检测状态
参数：
    index：光学通道索引
返回值：
    optical_work_stat_t
*/
optical_work_stat_t optical_detect_state_get(optical_pos_t index)
{
    return optical_work_stations[index].works_state;
}

/* 光学丢杯策略： 从当前光学检测完的测试杯当中，选取最先开始光学检测的测试杯 */
static int check_optical_finish_work(optical_pos_t index, long long *before_time)
{
    if (optical_work_stations[index].works_state == OPTICAL_FINISH) {
        LOG("already order:%d, ch:%d cup optical finish\n", optical_work_stations[index].order_no, index);
        if (*before_time == 0) {
            *before_time = optical_work_stations[index].start_time;
        }

        if (optical_work_stations[index].start_time <= *before_time) {
            *before_time = optical_work_stations[index].start_time;
            return 1;
        }
    }

    return 0;
}

/*
功能：获取一个当前应该输出的光学位
参数：
返回值：
    optical_pos_t
*/
optical_pos_t optical_detect_output_get()
{
    int i = 0;
    optical_pos_t index = OPTICAL_POS_INVALID;
    long long before_time = 0;

    for (i=0; i<OPTICAL_MAX_WORK_STATIONS; i++) {
        if (1 == check_optical_finish_work(i, &before_time)) {
            index = i;
        }
    }

    LOG("output index:%d\n", index);
    return index;
}

/* 
功能：接收 光学电流数据
参数：
返回值：
    无
*/
void slip_optical_curr_calc_async(const slip_port_t *port, slip_msg_t *msg)
{
    slip_optical_data_t *optical_report_data = (slip_optical_data_t *)msg->data;
    int i = 0;

    for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
        LOG("**group[%d]**calc_curr[%d] %x %x %x %x %x %x %x %x\n", optical_report_data->group_id, i,
            optical_report_data->ad_data[i][0], optical_report_data->ad_data[i][1],
            optical_report_data->ad_data[i][2], optical_report_data->ad_data[i][3],
            optical_report_data->ad_data[i][4], optical_report_data->ad_data[i][5],
            optical_report_data->ad_data[i][6], optical_report_data->ad_data[i][7]);

        /* 波长405/660需要通过校准值是否为0 来判断 是否校准成功；其它波长不需要判断是否校准成功，且不希望为0值 */
        if (i == OPTICAL_WAVE_405 || i == OPTICAL_WAVE_660) {
            if(optical_report_data->ad_data[i][0] < 256) {
                h3600_conf_get()->optical_curr_data[i] = optical_report_data->ad_data[i][0];
            }
        } else {
            if(optical_report_data->ad_data[i][0]>0 && optical_report_data->ad_data[i][0]<256) {
                h3600_conf_get()->optical_curr_data[i] = optical_report_data->ad_data[i][0];
            }
        }
    }

    set_optical_value();
    optical_led_calc_flag = 1;
}

/* 
功能：接收 光学本底数据
参数：
返回值：
    无
*/
void slip_optical_get_bkdata_async(const slip_port_t *port, slip_msg_t *msg)
{
    slip_optical_data_t *optical_report_data = (slip_optical_data_t *)msg->data;
    int i = 0, j = 0;
    int group = 0;

    group = group_id_to_index(optical_report_data->group_id);

    log_optical_data("===================back data========================\n");
    for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
        for (j=0; j<OPTICAL_WORK_STATIONS; j++) {
            if (optical_report_data->ad_data[i][j] > 64000) {
                optical_report_data->ad_data[i][j] = 65536 - optical_report_data->ad_data[i][j];
                bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+j] = 1;
            }

            bk_data_group[i][group*OPTICAL_WORK_STATIONS+j] = optical_report_data->ad_data[i][j];
        }

        log_optical_data("**group[%d]**bk_wave[%d] %d %d %d %d %d %d %d %d\n", optical_report_data->group_id, i,
            optical_report_data->ad_data[i][0], optical_report_data->ad_data[i][1],
            optical_report_data->ad_data[i][2], optical_report_data->ad_data[i][3], 
            optical_report_data->ad_data[i][4], optical_report_data->ad_data[i][5],
            optical_report_data->ad_data[i][6], optical_report_data->ad_data[i][7]);

        log_optical_data("**group[%d]**bk_flag[%d] %d %d %d %d %d %d %d %d\n",  optical_report_data->group_id, i,
            bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+0], bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+1],
            bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+2], bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+3],
            bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+4], bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+5],
            bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+6], bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+7]);
    }
}

/* 
功能：接收 光学常规数据
参数：
返回值：
    无
*/
void slip_optical_get_data_async(const slip_port_t *port, slip_msg_t *msg)
{
    int i = 0, j = 0;
    slip_optical_data_t *optical_report_data = (slip_optical_data_t *)msg->data;
    int group = 0;

    group = group_id_to_index(optical_report_data->group_id);
    optical_can_recv_time[group] = sys_uptime_sec();

    log_optical_data("===================normal data========================\n");
    for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
        log_optical_data("**group[%d]*******wave[%d] %d %d %d %d %d %d %d %d\n", optical_report_data->group_id, i,
            optical_report_data->ad_data[i][0], optical_report_data->ad_data[i][1],
            optical_report_data->ad_data[i][2], optical_report_data->ad_data[i][3],
            optical_report_data->ad_data[i][4], optical_report_data->ad_data[i][5],
            optical_report_data->ad_data[i][6], optical_report_data->ad_data[i][7]);

        for (j=0; j<OPTICAL_WORK_STATIONS; j++) {
            if (led_off_flag == 0) { /* 开灯（默认，常态） */
                if (bk_neg_flag_group[i][group*OPTICAL_WORK_STATIONS+j] == 0) {
                    optical_report_data->ad_data[i][j] = optical_report_data->ad_data[i][j] - bk_data_group[i][group*OPTICAL_WORK_STATIONS+j];
                } else {
                    optical_report_data->ad_data[i][j] = optical_report_data->ad_data[i][j] + bk_data_group[i][group*OPTICAL_WORK_STATIONS+j];
                }
            } else if (led_off_flag == 1) { /* 关灯 */
                optical_report_data->ad_data[i][j] = bk_data_group[i][group*OPTICAL_WORK_STATIONS+j];
            }
        }

        pthread_rwlock_wrlock(&optrwLock);
        for (j=0; j<OPTICAL_WORK_STATIONS; j++) {
            optical_data_group[group].ad_data[i][j] = optical_report_data->ad_data[i][7-j];
        }
        pthread_rwlock_unlock(&optrwLock);

        log_optical_data("**group[%d]**true wave[%d] %d %d %d %d %d %d %d %d\n", optical_report_data->group_id, i,
            optical_report_data->ad_data[i][0], optical_report_data->ad_data[i][1],
            optical_report_data->ad_data[i][2], optical_report_data->ad_data[i][3], 
            optical_report_data->ad_data[i][4], optical_report_data->ad_data[i][5],
            optical_report_data->ad_data[i][6], optical_report_data->ad_data[i][7]);
    }
}

/* 
功能：接收 光学子板版本号
参数：
返回值：
    无
*/
void slip_optical_get_version_async(const slip_port_t *port, slip_msg_t *msg)
{
    optical_firmware_result_t *data = (optical_firmware_result_t *)msg->data;
    int version_len = 0;

    version_len = (msg->cmd_type.sub_type == OTHER_FPGA_VERSION_GET_SUBTYPE) ? 30 : 14;

    if (data->board_id == SLIP_NODE_OPTICAL_1) {
        memset(optical_version[0], 0, sizeof(optical_version[0]));
        strncpy(optical_version[0], &data->version[1], version_len-1);
        LOG("optical board_id:%d, version:%s\n", data->board_id, optical_version[0]);
    } else if(data->board_id == SLIP_NODE_OPTICAL_2) {
        memset(optical_version[1], 0, sizeof(optical_version[1]));
        strncpy(optical_version[1], &data->version[1], version_len-1);
        LOG("optical board_id:%d, version:%s\n", data->board_id, optical_version[1]);
    } else if(data->board_id == SLIP_NODE_OPTICAL_HIL) {
        memset(optical_version[2], 0, sizeof(optical_version[2]));
        strncpy(optical_version[2], &data->version[1], version_len-1);
        LOG("optical board_id:%d, version:%s\n", data->board_id, optical_version[2]);
    }
}

/* 
功能：设置 光学板的参数
参数：
    optical_sw：通信子命令
返回值：
    无
*/
int slip_optical_set(uint8_t optical_sw)
{
    LOG("optical_sw = %d\n", optical_sw);
    slip_port_t *port = NULL;
    int i = 0;

    /* 0x00:初始值 0x01:取常规AD值(开灯) 0x02:取本底 0x08:复位芯片 0x03:关灯并停止采集、上报数据 0x04:关灯但继续采集、上报数据 */
    uint8_t cmd_onoff[6] = {0x0, 0x1, 0x2, 0x8, 0x03, 0x04};

    /* 校准LED电流(7F->7F00->32512, C4->C400->50176), 28->2800->10240) */
    uint8_t cmd_calc_max[6] = {0x4, 0x7F, 0xC4, 0xC4, 0x7F, 0xC4};
    uint8_t cmd_calc_min[6] = {0x5, 0x28, 0x28, 0x28, 0x28, 0x28};

    /* 设置LED电流 */
    uint8_t cmd_curr_set[6] = {0x3, 0x0, 0x0, 0x0, 0x0, 0x0};
    port = slip_port_get_by_path(slip_node_path_get_by_node_id(SLIP_NODE_OPTICAL_1));

    switch(optical_sw) {
    case OPTICAL_1_POWEROFF_AD:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, &cmd_onoff[5], 1);
        led_off_flag = 1;
        break;
    case OPTICAL_1_POWEROFF:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, &cmd_onoff[4], 1);
        led_off_flag = 1;
        break;
    case OPTICAL_2_POWEROFF:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_2, &cmd_onoff[4], 1);
        break;
    case OPTICAL_1_POWERON:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, &cmd_onoff[1], 1);
        led_off_flag = 0;
        break;
    case OPTICAL_2_POWERON:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_2, &cmd_onoff[1], 1);
        break;
    case OPTICAL_1_BK_GET:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, &cmd_onoff[2], 1);
        break;
    case OPTICAL_2_BK_GET:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_2, &cmd_onoff[2], 1);
        break;
    case OPTICAL_CURR_CALC:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, cmd_calc_min, 6);
        usleep(1000*100);
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, cmd_calc_max, 6);
        break;
    case OPTICAL_CURR_SET:
        for (i=0; i<5; i++) {
            cmd_curr_set[i+1] = h3600_conf_get()->optical_curr_data[i];
        }
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, cmd_curr_set, 6);
        break;
    case OPTICAL_HIL_POWEROFF:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_HIL, &cmd_onoff[4], 1);
        break;
    case OPTICAL_HIL_POWERON:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_HIL, &cmd_onoff[1], 1);
        break;
    case OPTICAL_HIL_BK_GET:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_HIL, &cmd_onoff[2], 1);
        break;
    case OPTICAL_HEART_BEAT:
        break;
    case OPTICAL_1_RESET:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_1, &cmd_onoff[3], 1);
        break;
    case OPTICAL_2_RESET:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_2, &cmd_onoff[3], 1);
        break;
    case OPTICAL_HIL_RESET:
        ll_can_send(port->fd, SLIP_NODE_OPTICAL_HIL, &cmd_onoff[3], 1);
        break;
    default:
        LOG("no such cmd for optical!\n");
        break;
    }

    return 0;
}

/* 
功能：获取波长x的光学信号值
参数：
    index：光学检测通道
返回值：
    光学信号值
*/
int thrift_optical_data_get(int wave, optical_pos_t index)
{
    int data = 0;
    int group = 0;
    int channel_sub = 0;

    group = index / OPTICAL_WORK_STATIONS;
    channel_sub = index % OPTICAL_WORK_STATIONS;

    data = optical_data_group[group].ad_data[wave][channel_sub];

    return data;
}

/* 初始化光学子板的参数 */
static void optical_led_set(void *args)
{
    int cmd = 0;

    if (optical_led_cmd_flag == 1) {
        LOG("last cmd is running!\n");
        return ;
    }

    optical_led_cmd_flag = 1;
    if (args) {
        cmd = *(int *)args;
    }

    LOG("optical m0 init start, args:%p, cmd:%d\n", args, cmd);

//    slip_optical_set(OPTICAL_HIL_RESET);
//    slip_optical_set(OPTICAL_2_RESET);
    slip_optical_set(OPTICAL_1_RESET);
    sleep(20);

    if (check_optical_calc() == 1 || cmd == 1) {
        slip_optical_set(OPTICAL_CURR_CALC);
        sleep(30);
    } else {
        slip_optical_set(OPTICAL_CURR_SET);
        sleep(1);
    }
//    slip_optical_set(OPTICAL_HIL_POWERON);
//    slip_optical_set(OPTICAL_2_POWERON);
    slip_optical_set(OPTICAL_1_POWERON);
//    optical_ad_data_check();
    LOG("optical m0 init end\n");
    optical_led_cmd_flag = 0;
}

/* 
功能：启动 光学LED电流校准
参数：
    无
返回值：
    0:始终成功
*/
int thrift_optical_led_calc_start()
{
    static int cmd = 0;

    cmd = 1;
    optical_led_calc_flag = 0;
    work_queue_add(optical_led_set, (void *)&cmd);

    return 0;
}

/* 
功能：获取 光学校准是否完成
参数：
返回值：
    光学校准完成标志
*/
int optical_led_calc_flag_get()
{
    return optical_led_calc_flag;
}

int optical_led_cmd_flag_get()
{
    return optical_led_cmd_flag;
}

/* 
功能：获取 光学LED电流校准值
参数：
    wave：波长索引（0~4）
返回值：
    电流校准值
*/
int thrift_optical_led_calc_get(int wave)
{
    LOG("wave:%d\n", wave);
    if (wave >= sizeof(h3600_conf_get()->optical_curr_data)/sizeof(h3600_conf_get()->optical_curr_data[0])) {
        return 0;
    } else {
        return h3600_conf_get()->optical_curr_data[wave];
    }
}

int thrift_optical_led_calc_set(int wave, int data)
{
    LOG("wave:%d, calc:%d\n", wave, data);

    if (wave >= sizeof(h3600_conf_get()->optical_curr_data)/sizeof(h3600_conf_get()->optical_curr_data[0]) || 
        data>255 || data<0) {
        return -1;
    } else {
        h3600_conf_get()->optical_curr_data[wave] = data;
        set_optical_value();
        return 0;
    }
}

/* 
功能：控制 所有光学led灯
参数：
    enable: 0：光灯， 1：开灯 
返回值：
    -1：失败
    0：成功
*/
int all_optical_led_ctl(uint8_t enable)
{
    int ret1 = 0, ret2 = 0;

    /* 当控制LED灯时，若光学模块初始化没有完成，则强制返回成功。避免造成LED电流等设置失败 */
    if (optical_led_cmd_flag_get() == 1) {
        LOG("optical init not done, force break\n");
        return 0;
    }

    /* 只有光学子板1 才能控制LED灯的开关 */
    if (enable == 1) {
        ret1 = slip_optical_set(OPTICAL_1_POWERON);
//        ret2 = slip_optical_set(OPTICAL_2_POWERON);
    } else if (enable == 0) {
        ret1 = slip_optical_set(OPTICAL_1_POWEROFF);
//        ret2 = slip_optical_set(OPTICAL_2_POWEROFF);
    }

    if (ret1 == -1 || ret2 == -1) {
        LOG("optical led %d, %d fail\n", ret1, ret2);
        return -1;
    } else {
        return 0;
    }
}

static int optical_data_file_init()
{
    if (optical_data_fd == NULL) {
        optical_data_fd = fopen(OPTICAL_DATA_FILE, "a");
        if (optical_data_fd == NULL) {
            LOG("open optical data file fail\n");
            return -1;
        }
    }

    return 0;
}

static int save_optical_origin_data(int index)
{
    int i = 0;
    int num1 = 0, num2 = 0;
    int idx = 0;
    FILE *tmp_fp = NULL;
    struct timeval tv;
    struct tm tm;

    if (optical_work_stations[index].order_no == 0) {
        return 0;
    }

    if (optical_data_fd != NULL) {
        if (ftell(optical_data_fd) > OPTICAL_DATA_FILE_MAX_SIZE) {
            fclose(optical_data_fd);
            if ((tmp_fp = fopen(OPTICAL_DATA_FILE".0", "r")) != NULL) {
                fclose(tmp_fp);
                remove(OPTICAL_DATA_FILE".0");
            }
            LOG("over optical data file, tmp_fp: %p\n", tmp_fp);
            rename(OPTICAL_DATA_FILE, OPTICAL_DATA_FILE".0");
            optical_data_fd = fopen(OPTICAL_DATA_FILE, "w");
            if (optical_data_fd == NULL) {
                LOG("reopen optical data file fail\n");
                return -1;
            }
        }

        fprintf(optical_data_fd, "bk_flag\t");
        idx = 0;
        num1 = optical_work_stations[index].res_ad.main_wave_len/OPTICAL_SAVE_FILE_DATA_SIZE;
        num2 = optical_work_stations[index].res_ad.main_wave_len%OPTICAL_SAVE_FILE_DATA_SIZE;
        for (i=0; i<num1; i++) {
            fprintf(optical_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                optical_work_stations[index].res_ad.bk_flag[idx], optical_work_stations[index].res_ad.bk_flag[idx+1], optical_work_stations[index].res_ad.bk_flag[idx+2],
                optical_work_stations[index].res_ad.bk_flag[idx+3], optical_work_stations[index].res_ad.bk_flag[idx+4], optical_work_stations[index].res_ad.bk_flag[idx+5]);
                idx += OPTICAL_SAVE_FILE_DATA_SIZE;
        }

        for (i=0; i<num2; i++) {
            fprintf(optical_data_fd, "%d\t", optical_work_stations[index].res_ad.bk_flag[idx]);
            idx++;
        }

        fprintf(optical_data_fd, "\nbk_data\t");
        idx = 0;
        num1 = optical_work_stations[index].res_ad.main_wave_len/OPTICAL_SAVE_FILE_DATA_SIZE;
        num2 = optical_work_stations[index].res_ad.main_wave_len%OPTICAL_SAVE_FILE_DATA_SIZE;
        for (i=0; i<num1; i++) {
            fprintf(optical_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                optical_work_stations[index].res_ad.bk_data[idx], optical_work_stations[index].res_ad.bk_data[idx+1], optical_work_stations[index].res_ad.bk_data[idx+2],
                optical_work_stations[index].res_ad.bk_data[idx+3], optical_work_stations[index].res_ad.bk_data[idx+4], optical_work_stations[index].res_ad.bk_data[idx+5]);
                idx += OPTICAL_SAVE_FILE_DATA_SIZE;
        }

        for (i=0; i<num2; i++) {
            fprintf(optical_data_fd, "%d\t", optical_work_stations[index].res_ad.bk_data[idx]);
            idx++;
        }

        fprintf(optical_data_fd, "\nwave_data\t");
        idx = 0;
        num1 = optical_work_stations[index].res_ad.main_wave_len/OPTICAL_SAVE_FILE_DATA_SIZE;
        num2 = optical_work_stations[index].res_ad.main_wave_len%OPTICAL_SAVE_FILE_DATA_SIZE;
        for (i=0; i<num1; i++) {
            fprintf(optical_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                optical_work_stations[index].res_ad.main_wave_ad[idx], optical_work_stations[index].res_ad.main_wave_ad[idx+1], optical_work_stations[index].res_ad.main_wave_ad[idx+2],
                optical_work_stations[index].res_ad.main_wave_ad[idx+3], optical_work_stations[index].res_ad.main_wave_ad[idx+4], optical_work_stations[index].res_ad.main_wave_ad[idx+5]);
                idx += OPTICAL_SAVE_FILE_DATA_SIZE;
        }

        for (i=0; i<num2; i++) {
            fprintf(optical_data_fd, "%d\t", optical_work_stations[index].res_ad.main_wave_ad[idx]);
            idx++;
        }

        fprintf(optical_data_fd, "\n");

        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm);

        fprintf(optical_data_fd, "==time:%02d-%02d %02d:%02d:%02d, order:%d, detect_pos:%d, wave:%d\n",
            tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
            optical_work_stations[index].order_no, index, optical_work_stations[index].main_wave_ch);

        fflush(optical_data_fd);
    }

    return 0;
}

/* 上报光学检测结果 */
int optical_report_result(int order_no, void *data, int detect_pos, int cuvette_serialno, const char *cuvette_strno)
{
    order_result_t od_result = {0};
    struct optical_ad_data *optical_data = NULL;

    memset(&od_result, 0, sizeof(od_result));

    optical_data = (struct optical_ad_data *)data;
    od_result.result_state = NORMAL; /* 光学法上报原始数据，不做异常判定 */
    od_result.clot_time = 0;    /* 光学法凝固时间填0 */
    od_result.AD_data = optical_data->main_wave_ad;
    od_result.AD_size = optical_data->main_wave_len;
    od_result.sub_AD_data = optical_data->vice_wave_ad;
    od_result.sub_AD_size = optical_data->vice_wave_len;
    od_result.detect_pos = detect_pos+1;
    od_result.cuvette_serialno = cuvette_serialno;
    strncpy(od_result.cuvette_strno, cuvette_strno, strlen(cuvette_strno));
    device_status_count_add(DS_OPTICAL1_USED_COUNT+detect_pos, 1);
    report_order_result(order_no, &od_result);
    return report_order_state(order_no,  OD_COMPLETION);
}

/* 接收M0子板发过来的光学采集数据 */
static void *optical_rdata_task(void *arg)
{
    int i = 0;
    int group = 0;
    int finish_flag = 0;
    int retry_flag[2] = {0};
    long long optical_can_recv_time_diff[2] = {0};

    LOG("start\n");
    while (1) {
        if (module_fault_stat_get() & MODULE_FAULT_STOP_ALL) {
            reinit_optical_data();
        }

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        /* 运行时，若超过60s，没收到光学数据，则报错并尝试重启光学子板 */
        /* 待机时，若超过300s，没收到光学数据，则尝试重启光学子板 */
        if (thrift_salve_heartbeat_flag_get()==1 && get_power_off_stat()==0) {
            optical_can_recv_time_diff[0] = sys_uptime_sec() - optical_can_recv_time[0];
            if (get_machine_stat()==MACHINE_STAT_RUNNING && (optical_can_recv_time_diff[0]>60)) {
                if (retry_flag[0] == 0) {
                    retry_flag[0] = 1;
                    LOG("optical recv data timeout,stat:%d, time:%lld\n", get_machine_stat(), optical_can_recv_time_diff[0]);
                    FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_OPTICAL_CONNECT);
                    work_queue_add(optical_led_set, (void *)NULL);
                }
            } else if (get_machine_stat()==MACHINE_STAT_STANDBY && (optical_can_recv_time_diff[0]>300)) {
                if (retry_flag[1] == 0) {
                    retry_flag[1] = 1;
                    LOG("optical recv data timeout,stat:%d, time:%lld\n", get_machine_stat(), optical_can_recv_time_diff[0]);
                    work_queue_add(optical_led_set, (void *)NULL);
                }
            } else {
                retry_flag[0] = 0;
                retry_flag[1] = 0;
            }
        } else {
            retry_flag[0] = 0;
            retry_flag[1] = 0;
        }
        FAULT_CHECK_END();

        for (i=0; i<OPTICAL_MAX_WORK_STATIONS; i++) {
            group = i / OPTICAL_WORK_STATIONS;
            finish_flag = 0;

            if (optical_work_stations[i].enable == 1) {
                if (optical_work_stations[i].works_state == OPTICAL_UNUSED) {
                    optical_work_stations[i].start_time = time(NULL);
                    optical_work_stations[i].works_state = OPTICAL_RUNNING;
                    report_order_state(optical_work_stations[i].order_no,  OD_DETECTING);
                    LOG("order:%d, ch:%d cup optical start\n", optical_work_stations[i].order_no, i);
                } else if (optical_work_stations[i].works_state == OPTICAL_RUNNING) {
                    optical_work_stations[i].res_ad.bk_flag[optical_work_stations[i].res_ad.main_wave_len] = bk_neg_flag_group[optical_work_stations[i].main_wave_ch][i-group*OPTICAL_WORK_STATIONS];
                    optical_work_stations[i].res_ad.bk_data[optical_work_stations[i].res_ad.main_wave_len] = bk_data_group[optical_work_stations[i].main_wave_ch][i-group*OPTICAL_WORK_STATIONS];

                    optical_work_stations[i].res_ad.main_wave_ad[optical_work_stations[i].res_ad.main_wave_len] = optical_data_group[group].ad_data[optical_work_stations[i].main_wave_ch][i-group*OPTICAL_WORK_STATIONS];
                    if (optical_work_stations[i].vice_wave_ad_cnt > 0) {
                        optical_work_stations[i].res_ad.vice_wave_ad[optical_work_stations[i].res_ad.vice_wave_len] = optical_data_group[group].ad_data[optical_work_stations[i].vice_wave_ch][i-group*OPTICAL_WORK_STATIONS];
                    }

                    if (optical_work_stations[i].res_ad.main_wave_len >= optical_work_stations[i].main_wave_ad_cnt) {
                        if (optical_work_stations[i].vice_wave_ad_cnt > 0) {
                            if (optical_work_stations[i].res_ad.vice_wave_len >= optical_work_stations[i].vice_wave_ad_cnt) {
                                finish_flag = 1;
                            } else {
                                optical_work_stations[i].res_ad.vice_wave_len++;
                                optical_work_stations[i].res_ad.main_wave_len = optical_work_stations[i].main_wave_ad_cnt + 1;
                            }
                        } else {
                            finish_flag = 1;
                        }
                    } else {
                        if (optical_work_stations[i].vice_wave_ad_cnt > 0) {
                            if (optical_work_stations[i].res_ad.vice_wave_len >= optical_work_stations[i].vice_wave_ad_cnt) {
                                optical_work_stations[i].res_ad.vice_wave_len = optical_work_stations[i].vice_wave_ad_cnt + 1;
                            } else {
                                optical_work_stations[i].res_ad.vice_wave_len++;
                            }
                        }
                        optical_work_stations[i].res_ad.main_wave_len++;
                    }

                    if (finish_flag == 1) {
                        /* 先上报结果，再置完成状态，避免极端情况下，结果数据被外部线程清除 */
                        optical_report_result(optical_work_stations[i].order_no, &optical_work_stations[i].res_ad, i, optical_work_stations[i].cuvette_serialno, optical_work_stations[i].cuvette_strno);
                        save_optical_origin_data(i);
                        optical_work_stations[i].works_state = OPTICAL_FINISH;
                        LOG("order:%d, ch:%d cup optical finish\n", optical_work_stations[i].order_no, i);
                    }
                }
            }
        }

        usleep(100000);
    }

    return NULL;
}

int optical_ad_data_check()
{
    int i = 0, j = 0;
    int range_data_min[5] = {10000, 10000, 10000, 10000, 10000};    /* 各波长最小AD值 */
    int range_data_max[5] = {65500, 65500, 65500, 65500, 65500};    /* 各波长最大AD值 */
    int group = 0;
    char *fault_id = NULL;
    int group_error_flag[OPTICAL_MAX_WORK_STATIONS] = {0};
    char *optical_ad_fault[OPTICAL_MAX_WORK_STATIONS] = {
        MODULE_FAULT_OPTICAL_CHK_AD1, MODULE_FAULT_OPTICAL_CHK_AD2, MODULE_FAULT_OPTICAL_CHK_AD3, MODULE_FAULT_OPTICAL_CHK_AD4,
        MODULE_FAULT_OPTICAL_CHK_AD5, MODULE_FAULT_OPTICAL_CHK_AD6, MODULE_FAULT_OPTICAL_CHK_AD7, MODULE_FAULT_OPTICAL_CHK_AD8
    };
    int times = 0;
    int ready_count = 0, ready_max = 0;

    pthread_rwlock_wrlock(&optrwLock);
    memset(optical_data_group, 0, sizeof(optical_data_group));
    pthread_rwlock_unlock(&optrwLock);

    /* 清除数据后，需等待一段时间，从而重新获取数据 */
    while (times++ < 50) {  /* 5s */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ready_count = 0;
        ready_max = OPTICAL_WORK_STATIONS;
        for (j=0; j<OPTICAL_WORK_STATIONS; j++) {
            if (optical_data_group[0].ad_data[0][j] > 0 && optical_data_group[OPTICAL_WORK_GROUP_NUM-1].ad_data[OPTICAL_CHANNEL_MAX-1][j] > 0) {
                ready_count++;
            }
        }

        if (ready_count >= ready_max) {
            LOG("optical addata is ready\n");
            break;
        }

        usleep(1000*100);
        FAULT_CHECK_END();
    }

    LOG("optical addata cnt:%d\n", times);
    usleep(1000*100);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    for (group=0; group<OPTICAL_WORK_GROUP_NUM; group++) {
        for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
            if (i == OPTICAL_WAVE_405 || i == OPTICAL_WAVE_660) {
                for (j=0; j<OPTICAL_WORK_STATIONS; j++) {
                    if (optical_data_group[group].ad_data[i][j] < range_data_min[i] || optical_data_group[group].ad_data[i][j] > range_data_max[i]) {
                        LOG("group(%d) check failed! optical_data_group.ad_data[%d][%d] = %d\n", group, i, j, optical_data_group[group].ad_data[i][j]);
                        group_error_flag[group*OPTICAL_WORK_STATIONS+j] = 1;
                    }
                }
            }
        }
    }
    FAULT_CHECK_END();

    for (j=0; j<OPTICAL_MAX_WORK_STATIONS; j++) {
        if (group_error_flag[j] == 1) {
            fault_id = optical_ad_fault[j];
            FAULT_CHECK_DEAL(FAULT_OPTICAL_MODULE, MODULE_FAULT_NONE, (void*)fault_id);
        }
    }

    return 0;
}

/* 
功能：service通电质检接口
参数：
    test_type: 0:本底信号值 1：开灯信号值
    fail_str: 失败内容
返回值：
    -1：失败
    0：成功
*/
int optical_poweron_test(int test_type, char *fail_str)
{
    int i = 0, j = 0;
    int result[OPTICAL_CHANNEL_MAX][OPTICAL_MAX_WORK_STATIONS] = {0};
    int ret = 0;
    int group = 0;
    char result_temp[256] = {0};
    char result_buff[256] = {0};
    char wave_str[][16] = 
    {
        [OPTICAL_WAVE_340] = "340",
        [OPTICAL_WAVE_660] = "660",
        [OPTICAL_WAVE_800] = "800",
        [OPTICAL_WAVE_570] = "570",
        [OPTICAL_WAVE_405] = "405",
    };

    /* 采集数据 */
    LOG("get test addata, test_type:%d\n", test_type);

    /* 分析数据 */
    LOG("analyns test addata\n");
    switch (test_type) {
    case 0:
        for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
            if (i == OPTICAL_WAVE_340 || i == OPTICAL_WAVE_800) {
                continue;
            }

            for (j=0; j<OPTICAL_MAX_WORK_STATIONS; j++) {
                if (bk_data_group[i][j] > 1000) { /* 若本底大于1000，则异常 */
                    LOG("error: wave[%d] ch[%d] data[%d] find >1000 data\n", i, j, bk_data_group[i][j]);
                    result[i][j] = -1;
                }
            }
        }
        break;
    case 1:
        for (group=0; group<OPTICAL_WORK_GROUP_NUM; group++) {
            for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
                if (i == OPTICAL_WAVE_340 || i == OPTICAL_WAVE_800) {
                    continue;
                }

                for (j=0; j<OPTICAL_WORK_STATIONS; j++) { /* 若检测值大于65000 或小于10000，则异常 */
                    if (optical_data_group[group].ad_data[i][j] > 65000 ||
                       optical_data_group[group].ad_data[i][j] < 10000) {
                        LOG("error: wave[%d] ch[%d] data[%d] find >65000 or <10000 data\n", i, j, optical_data_group[group].ad_data[i][j]);
                        result[i][group*OPTICAL_WORK_STATIONS+j] = -1;
                    } else {
                        result[i][group*OPTICAL_WORK_STATIONS+j] = 0;
                    }
                }
            }
        }

        break;
    default:
        LOG("not support type\n");
        break;
    }

    /* 输出结果 */
    for (i=0; i<OPTICAL_CHANNEL_MAX; i++) {
        if (i == OPTICAL_WAVE_340 || i == OPTICAL_WAVE_800) {
            continue;
        }

        for (j=0; j<OPTICAL_MAX_WORK_STATIONS; j++) {
            if (result[i][j] == -1) {
                char temp_buff[32] = {0};

                ret = -1;
                sprintf(temp_buff, "%s:%d,", wave_str[i], j+1);
                strcat(result_temp, temp_buff);
            }
        }
    }

    if (ret == -1) {
        strcat(result_buff, "通道:");
        strcat(result_buff, result_temp);
        if (test_type == 0) {
            strcat(result_buff, "原因:本底超限");
        } else  if (test_type == 1) {
            strcat(result_buff, "原因:信号超限");
        }
        utf8togb2312(result_buff, strlen(result_buff), fail_str, sizeof(result_buff));
    }

    LOG("test result:%d, fail ch:%s\n", ret, result_buff);
    return ret;
}

/* 初始化光学模块 */
int optical_init(void)
{
    pthread_t optical_rdata_thread = {0};

    reinit_optical_data();
    optical_data_file_init();
    work_queue_add(optical_led_set, (void *)NULL);

    if (0 != pthread_create(&optical_rdata_thread, NULL, optical_rdata_task, NULL)) {
        LOG("optical read data thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

