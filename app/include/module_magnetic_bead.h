#ifndef __MODULE_MAGNETIC_BEAD_H__
#define __MODULE_MAGNETIC_BEAD_H__

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
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/syscall.h>

#include "common.h"
#include "module_common.h"
#include "work_queue.h"
#include "log.h"
#include "list.h"
#include "slip/slip_node.h"
#include "slip/slip_port.h"
#include "slip/slip_msg.h"
#include "slip/slip_process.h"
#include "slip_cmd_table.h"
#include "thrift_service_software_interface.h"
#include "module_monitor.h"
#include "magnetic_algorithm.h"
#include "thrift_handler.h"
#include "module_upgrade.h"
#include "device_status_count.h"

/* 通信异常上报计数 */
#define MAG_ERROR_COUNT_MAX     5

#define PWM_DRIVER_LEVEL_INDEX_NORMAL 0
#define PWM_DRIVER_LEVEL_INDEX_WEAK 1
#define PWM_DRIVER_LEVEL_INDEX_STRONG 2

#define MAG_DATA_CACHE_SIZE_MAX 50 /* 缓存磁珠数据的最大个数 */

#define MAGNETIC_TEST_DATA_COUNT 500 /* 通电质检流程的采集信号值的最大个数 */

/* 磁珠位置索引 */
typedef enum{
    MAGNETIC_POS_INVALID = -1,
    MAGNETIC_POS0 = 0,
    MAGNETIC_POS1,
    MAGNETIC_POS2,
    MAGNETIC_POS3,
    MAGNETIC_POS_MAX,
}magnetic_pos_t;

/* 磁珠检测工作状态 */
typedef enum{
    MAG_UNUSED = 0,
    MAG_RUNNING,    
    MAG_FINISH,
    MAG_TIMEOUT,
}mag_work_stat_t;

/* 磁珠检测参数 */
typedef struct
{
    mag_work_stat_t state;
    uint32_t max_time;	/* 最大检测时间 */
    uint32_t min_time; /* 最小检测时间 */
    float clot_percent; /* 凝固判定百分比 */
    uint32_t power; /* 磁珠驱动力 */
    long long start_time; /* 开始检测时间 */

    int mag_order_no;       /* 磁珠检测通道x的订单号 */
    int cuvette_serialno;   /* 反应杯盘序列号 */
    char cuvette_strno[16];  /* 反应杯盘批号 */
}mag_param_t;

typedef struct
{
    uint16_t ad_data[MAGNETIC_CH_NUMBER];
}__attribute__((packed))slip_magnetic_bead_t;

typedef struct
{
    uint8_t index;
    uint8_t data;
}__attribute__((packed))slip_magnetic_pwm_duty_t;

typedef struct
{
    uint8_t index;
    uint8_t enable;
}__attribute__((packed))slip_magnetic_pwm_enable_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    /* 执行结果 */
    uint8_t status;
}__attribute__((packed)) magnetic_pwm_duty_result_t;

/* 循环数组：用于缓存磁珠原始数据 */
typedef struct
{
    sem_t sem_rx;
    pthread_mutex_t mutex_data;

    int head;
    int tail;
    slip_magnetic_bead_t magnetic_bead_list[MAG_DATA_CACHE_SIZE_MAX];
}mag_data_cache_t;

int thrift_mag_data_get(magnetic_pos_t index);
int slip_magnetic_bead_get(slip_magnetic_bead_t *result);
int slip_magnetic_pwm_period_set(uint16_t data);
int slip_magnetic_pwm_period_get();
int slip_magnetic_pwm_duty_set(magnetic_pos_t index, uint8_t data);
int slip_magnetic_pwm_enable_set(magnetic_pos_t index, uint8_t enable);
int slip_magnetic_pwm_driver_level_get(int index);
int slip_magnetic_pwm_driver_level_set(int index, uint8_t data);
int all_magnetic_pwm_ctl(uint8_t enable);
int magnetic_poweron_test(int test_type, char *fail_str);

int reinit_magnetic_data();
int clear_one_magnetic_data(magnetic_pos_t index);
void magnetic_detect_start(magnetic_pos_t index);
void magnetic_detect_data_set(magnetic_pos_t index, struct magnectic_attr *test_cup_magnectic,
    uint32_t order_no, uint32_t cuvette_serialno, const char *cuvette_strno);
mag_work_stat_t magnetic_detect_state_get(magnetic_pos_t index);
magnetic_pos_t magnetic_detect_output_get();
int magnetic_bead_init(void);

#ifdef __cplusplus
}
#endif

#endif

