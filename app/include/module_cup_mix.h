#ifndef __MODULE_CUP_MIX_H__
#define __MODULE_CUP_MIX_H__

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
#include <math.h>

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
#include "device_status_count.h"

#define MIX_DELAY_INTERVAL (10000) /* us */
#define MIX_ONE_TIME ((3000*1000)/MIX_DELAY_INTERVAL) /* 混匀左右翻转间隔：3000ms */

/* 混匀位置索引 */
typedef enum
{
    MIX_POS_INVALID = -1,
    MIX_POS_INCUBATION1 = 0, /* 孵育混匀1 */
    MIX_POS_INCUBATION2,     /* 孵育混匀2 */
    MIX_POS_OPTICAL1,        /* 光学混匀1 */
    MIX_POS_MAX
}cup_mix_pos_t;

/* 混匀工作状态 */
typedef enum{
    CUP_MIX_UNUSED = 0,
    CUP_MIX_RUNNING,
    CUP_MIX_FINISH,
}cup_mix_work_stat_t;

/* 混匀流程参数 */
typedef struct
{
    int motor_id; /* 混匀电机号 */
    char *move_fault_id; /* 混匀运行故障ID */
    char *speed_fault_id; /* 混匀转速故障ID */

    pthread_mutex_t mutex;
    pthread_cond_t cond;
}mix_param_t;

/* 混匀状态参数 */
typedef struct
{
    uint32_t enable; /* 启动混匀索引: 0:没有需要混匀； 1：需启动混匀*/
    cup_mix_work_stat_t state;  /* 混匀状态: 0:无； 1：正在混匀; 2：混匀完成 */
    uint32_t rate; /* 混匀速率 */
    uint32_t time; /* 混匀时长（ms） */
    uint32_t order_no; /* 订单号 */
    uint32_t stop;  /* 停止标志: 1：触发停止 */
    long long start_time; /* 混匀开始时间 */
}mix_status_t;

cup_mix_pos_t pos_cup_trans_mix(cup_pos_t pos);
void reinit_cup_mix_data();
int clear_one_cup_mix_data(cup_mix_pos_t index);
void cup_mix_start(cup_mix_pos_t index);
void cup_mix_stop(cup_mix_pos_t index);
void cup_mix_data_set(cup_mix_pos_t index, uint32_t order_no, uint32_t rate, uint32_t time);
cup_mix_work_stat_t cup_mix_state_get(cup_mix_pos_t index);
cup_mix_pos_t cup_mix_incubation_output_get();
int mix_poweron_test(cup_mix_pos_t index);
int cup_mix_init();

#ifdef __cplusplus
}
#endif

#endif

