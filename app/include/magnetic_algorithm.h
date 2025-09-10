#ifndef __MAGNETIC_ALGORITHM_H__
#define __MAGNETIC_ALGORITHM_H__

#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "log.h"
#include "module_common.h"
#include "common.h"

#define MAGNETIC_CH_NUMBER 4 /* AD通道数/磁珠检测工位数量 (PWM组数) */
#define CLOT_TIME_LOW_LIMIT 3 /* 强凝阈值 0.1*30=3s */
#define CLOT_TIME_HIGH_LIMIT 100  /* 不凝阈值 0.1*1000=100s*/

#define CLOT_ALARM_BASE (0x01)
#define CLOT_ALARM_NORMAL (CLOT_ALARM_BASE<<0) /* 没有告警 */
#define CLOT_ALARM_NOBEAD (CLOT_ALARM_BASE<<1) /* 没有磁珠 或 没有样本 */
#define CLOT_ALARM_LOW (CLOT_ALARM_BASE<<2) /* 强凝 */
#define CLOT_ALARM_HIGH (CLOT_ALARM_BASE<<3) /* 不凝 */

#define MAX_DATA_FILE LOG_DIR"max_data.txt"
#define MAX_DATA_FILE_MAX_SIZE (1024*1024*20) /* 20M */
#define SAVE_FILE_DATA_SIZE 6 /* 同时写入文件的数据个数 */

#define MAG_ERROR_FILE LOG_DIR"mag_error.txt"
#define MAG_ERROR_FILE_MAX_SIZE (1024*1024*2) /* 5M */

#define GET_MAG_DATA_INTERVAL 20 /* 获取 磁珠数据的间隔(ms) */

#define CLEAN_DATA_NUMBER  2 /* 数据清洗 迭代次数 */

#define NO_BEAD_THRESHOLD  200 /* 无钢珠信号阈值 */

typedef struct
{
    uint8_t enable; /* 使能标识 0:停止 1:启动 */
    uint32_t max_time;	/* 最大检测时间 *//* 不凝阈值 */
    uint32_t min_time; /* 最小检测时间 *//* 强凝阈值 */
    float clot_percent; /* 凝固判定百分比 */

    /***********************/
    uint8_t alarm; /* 告警标识 */
    uint8_t status; /* 检测状态： 0：未开始 1：正在检测 2：检测完成 */
    float clot_time; /* 计算出的凝固时间 */

    /************************/
    int *m_alldata; /* 所有数据队列 */
    int *m_alldata_clean; /* 所有数据队列（清洗数据之后的数据）*/
    int m_alldata_cnt;

    int *m_data; /* 结果数据队列 */ 
    int m_data_cnt;

    int *m_startdata; /* 启动数据队列 */
    int m_startdata_cnt;    
    int startdata_total; /* 启动数据的累积和 */

    float m_max; /* 启动数据 */

    uint8_t clottime_flag; /* 凝固时间 计算完成标志 */
    uint8_t startdata_flag; /* 启动数据 计算完成标志 */
    uint8_t startdata_recalc; /* 启动数据 重新计算标志 */
    int clot_cnt;  /* 凝固次数，现要求连续20个点（即2秒）均小于最大值的50%方可认定为凝固 */

    int start_index; /* 启动数据开始计算位置 */
    int end_index; /* 启动数据结束计算位置 */
    int max_unusual; /* m_data（峰值）异常次数（数据清洗时使用） */
    int order_no;   /* 订单号 */

    long long start_time; /* 开始检测时间 */
}CLOT_DATA;

/* 一个波段的峰值信息 */
typedef struct
{
    int idx_max; /* 峰值位置 */
    int idx_start; /* 波段起始位置 */
    int idx_end; /* 波段结束位置 */
    int data; /* 峰值 */
}max_param_t;

void clot_data_init(void);
CLOT_DATA* clot_param_get(void);
void calc_clottime_all(const uint16_t *data);
void clot_data_free(CLOT_DATA *clot_data);
void log_mag_data(const char *format, ...);

#endif
