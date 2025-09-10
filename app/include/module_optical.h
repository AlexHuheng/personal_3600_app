#ifndef __MODULE_OPTICAL_H__
#define __MODULE_OPTICAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <list.h>
#include <common.h>
#include <log.h>
#include <slip/slip_node.h>
#include <slip/slip_port.h>
#include <slip/slip_msg.h>
#include <slip/slip_process.h>
#include <slip_cmd_table.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <work_queue.h>
#include <errno.h>

#include "module_common.h"
#include "thrift_service_software_interface.h"
#include "lowlevel.h"
#include "module_monitor.h"
#include "h3600_cup_param.h"
#include "thrift_handler.h"
#include "device_status_count.h"

#define OPTICAL_CHANNEL_MAX         5 /* 光学支持的波长数量 */
#define OPTICAL_WORK_GROUP_NUM      1 /* 光学检测池 个数 */
#define OPTICAL_WORK_STATIONS       8 /* 单个光学检测池的通道数量 */
#define OPTICAL_MAX_WORK_STATIONS   (OPTICAL_WORK_GROUP_NUM*OPTICAL_WORK_STATIONS)

/* 保存光学数据的相关定义 */
#define OPTICAL_DATA_FILE LOG_DIR"optical_data.txt"
#define OPTICAL_DATA_FILE_MAX_SIZE (1024*1024*20) /* 20M */
#define OPTICAL_SAVE_FILE_DATA_SIZE 6 /* 同时写入文件的数据个数 */

/* 光学位置索引 */
typedef enum{
    OPTICAL_POS_INVALID = -1,
    OPTICAL_POS0 = 0,
    OPTICAL_POS1,
    OPTICAL_POS2,
    OPTICAL_POS3,
    OPTICAL_POS4,
    OPTICAL_POS5,
    OPTICAL_POS6,
    OPTICAL_POS7,
    OPTICAL_POS_MAX,
}optical_pos_t;

/* 光学检测工作状态 */
typedef enum{
    OPTICAL_UNUSED = 0,
    OPTICAL_RUNNING,
    OPTICAL_FINISH,
}optical_work_stat_t;

/* 光学slip通信的子命令 */
#define OPTICAL_1_POWEROFF      0   /* 光学检测1-8关闭 */
#define OPTICAL_2_POWEROFF      1   /* 光学检测9-16关闭 */
#define OPTICAL_1_POWERON       3   /* 光学检测1-8开启 */
#define OPTICAL_2_POWERON       4   /* 光学检测9-16开启 */
#define OPTICAL_1_BK_GET        5   /* 光学检测1-8获取本底噪声 */
#define OPTICAL_2_BK_GET        6   /* 光学检测9-16获取本底噪声 */
#define OPTICAL_CURR_SET        7   /* 光学检测设置LED灯电流 */
#define OPTICAL_CURR_CALC       8   /* 光学检测信号自动校准 */
#define OPTICAL_HIL_BK_GET      9   /* 获取筛查本底 */
#define OPTICAL_HIL_POWEROFF    10  /* 筛查检测通道关闭 */
#define OPTICAL_HIL_POWERON     11  /* 筛查检测通道开启 */
#define OPTICAL_HEART_BEAT      12  /* 光学模块通信检查 */
#define OPTICAL_1_RESET         13  /* 光学子板1复位 */
#define OPTICAL_2_RESET         14  /* 光学子板2复位 */
#define OPTICAL_HIL_RESET       15  /* 光学子板HIL复位 */
#define OPTICAL_1_POWEROFF_AD   16  /* 光学检测1-8关闭,但继续采集、上报数据 */

/* 光学数据最大个数 */
#define OPTICAL_DATA_MAX_CNT    5000

/* 光学子板分组号 */
#define OPTICAL_GROUP_1         1
#define OPTICAL_GROUP_2         2
#define OPTICAL_GROUP_HIL       3

struct optical_ad_data {
    int main_wave_ad[OPTICAL_DATA_MAX_CNT];
    int main_wave_len;
    int vice_wave_ad[OPTICAL_DATA_MAX_CNT];
    int vice_wave_len;
    int bk_flag[OPTICAL_DATA_MAX_CNT];
    int bk_data[OPTICAL_DATA_MAX_CNT];
};

struct optical_work_attr {
    uint32_t enable; /* 使能光学: 0:禁止； 1：使能 */
    optical_work_stat_t works_state;
    int order_no;
    int main_wave_ch;
    int vice_wave_ch;
    int main_wave_ad_cnt;
    int vice_wave_ad_cnt;
    struct optical_ad_data res_ad;
    char cuvette_strno[16];  /* 反应杯盘批号 */
    int cuvette_serialno;   /* 反应杯盘序列号 */
    long long start_time;
};

typedef struct
{
    uint16_t group_id;
    uint16_t ad_data[OPTICAL_CHANNEL_MAX][OPTICAL_WORK_STATIONS];
}__attribute__((packed))slip_optical_data_t;

typedef struct
{
    /* 子板通讯ID */
    unsigned char board_id;
    /* 获取结果 */
    char version[64];
}__attribute__((packed)) optical_firmware_result_t;

void slip_optical_get_data_async(const slip_port_t *port, slip_msg_t *msg);
void slip_optical_get_bkdata_async(const slip_port_t *port, slip_msg_t *msg);
void slip_optical_curr_calc_async(const slip_port_t *port, slip_msg_t *msg);
void slip_optical_get_version_async(const slip_port_t *port, slip_msg_t *msg);
int slip_optical_set(uint8_t optical_sw);
int thrift_optical_data_get(int wave, optical_pos_t index);
int thrift_optical_led_calc_start();
int thrift_optical_led_calc_get(int wave);
int thrift_optical_led_calc_set(int wave, int data);
int all_optical_led_ctl(uint8_t enable);
int optical_ad_data_check();
int optical_led_calc_flag_get();
int optical_led_cmd_flag_get();
char* optical_version_get(uint8_t board_id);
int optical_poweron_test(int test_type, char *fail_str);

int reinit_optical_data(void);
int clear_one_optical_data(optical_pos_t index);
void optical_detect_start(optical_pos_t index);
void optical_detect_data_set(optical_pos_t index, struct optical_attr *test_cup_optical, 
       uint32_t order_no, uint32_t cuvette_serialno, const char *cuvette_strno);
optical_work_stat_t optical_detect_state_get(optical_pos_t index);
optical_pos_t optical_detect_output_get();
int optical_init(void);

#ifdef __cplusplus
}
#endif

#endif

