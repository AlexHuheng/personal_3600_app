#ifndef __MODULE_INCUBATION_H__
#define __MODULE_INCUBATION_H__

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

#include "log.h"
#include "module_monitor.h"
#include "h3600_cup_param.h"
#include "thrift_handler.h"

#define INCUBATION_TIMEOUT_EXTRA 300 /* 孵育完成后的孵育超时时长， 单位s */

/* 孵育位置索引 */
typedef enum
{
    INCUBATION_POS_INVALID = -1,
    INCUBATION_POS0 = 0,
    INCUBATION_POS1,
    INCUBATION_POS2,
    INCUBATION_POS3,
    INCUBATION_POS4,
    INCUBATION_POS5,
    INCUBATION_POS6,
    INCUBATION_POS7,
    INCUBATION_POS8,
    INCUBATION_POS9,
    INCUBATION_POS10,
    INCUBATION_POS11,
    INCUBATION_POS12,
    INCUBATION_POS13,
    INCUBATION_POS14,
    INCUBATION_POS15,
    INCUBATION_POS16,
    INCUBATION_POS17,
    INCUBATION_POS18,
    INCUBATION_POS19,
    INCUBATION_POS20,
    INCUBATION_POS21,
    INCUBATION_POS22,
    INCUBATION_POS23,
    INCUBATION_POS24,
    INCUBATION_POS25,
    INCUBATION_POS26,
    INCUBATION_POS27,
    INCUBATION_POS28,
    INCUBATION_POS29,
    INCUBATION_POS_MAX
}incubation_pos_t;

/* 孵育工作状态 */
typedef enum{
    INCUBATION_UNUSED = 0,
    INCUBATION_RUNNING,
    INCUBATION_FINISH,
    INCUBATION_TIMEOUT,
}incubation_work_stat_t;

/* 故障处理回调使用的私有数据类型 */
typedef struct
{
    int order_no;
    char *fault_str;
}fault_incubation_t;

/* 孵育参数 */
typedef struct
{
    uint32_t enable; /* 使能孵育: 0:禁止； 1：使能 */
    incubation_work_stat_t state;  /* 孵育状态 */
    uint32_t time; /* 孵育时间 */
    uint32_t order_no; /* 订单号 */
    cup_type_t cup_type; /* 杯子类型 */
    long long start_time; /* 孵育开始时间 */
    int inactive; /* 杯子是否有效. 0:有效 1：无效 */
}incubation_param_t;

int reinit_incubation_data();
int clear_one_incubation_data(incubation_pos_t index);
void incubation_start(incubation_pos_t index);
void incubation_data_set(incubation_pos_t index, uint32_t order_no, uint32_t time, cup_type_t cup_type);
incubation_work_stat_t incubation_state_get(incubation_pos_t index);
incubation_pos_t incubation_finish_output_get();
incubation_pos_t incubation_timeout_output_get();
void incubation_inactive_by_order(uint32_t order_no);
int incubation_init(void);

#ifdef __cplusplus
}
#endif

#endif

