#ifndef __MODULE_REAGENT_TABLE_H__
#define __MODULE_REAGENT_TABLE_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <semaphore.h>
#include <log.h>
#include <list.h>
#include <errno.h>

#include "slip/slip_node.h"
#include "slip/slip_process.h"
#include "slip_cmd_table.h"
#include "slip/slip_msg.h"
#include "common.h"
#include "thrift_service_software_interface.h"
#include "module_common.h"
#include "movement_config.h"
#include "work_queue.h"

#include "module_monitor.h"
#include "h3600_cup_param.h"
#include "h3600_needle.h"
#include "h3600_cup_param.h"
#include "movement_config.h"
#include "module_sampler_ctl.h"
#include "h3600_maintain_utils.h"
#include <module_engineer_debug_position.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REAG_IDX_START 0
#define REAG_IDX_END 36
#define DILU_IDX_START 37
#define DILU_IDX_END 39 /* 试剂仓+稀释液所有位置 */

#define ONES_TO_STEPS 666  /*试剂盘内/外圈间隔*/
#define COMP_TO_STEPS 999  /*试剂盘转动整数补偿步偿*/

#define TABLE_FULL_STEPS 24000
#define TABLE_HALF_STEPS 12000
#define REAG_CHANGE_EACH_AREA_MAX 6

#define REAGENT_MIX_SPEED_SLOW 50
#define REAGENT_MIX_SPEED_NORMAL 20
#define REAGENT_MIX_SPEED_FAST 95
#define REAGENT_MIX_TIME_DEFAULT 5

#define MIX_BLDC_INDEX 1 /* 试剂仓混匀电机索引 */
#define BLDC_MAX 4 /* 试剂仓混匀电机设置速度指令 */
#define MOTOR_TABLE_V0SPEED 100 /* 试剂仓电机变速运动电机初始运动速度 */
#define SEC_OF_DAY 86400
#define REAGENT_SPIN_STEP       4000
#define HALF_SECTOR_STEPS       2000

#define SCAN_AUTO_CALB_NEERBY_POS -10200 //运动至预定位置附近与机械沟通暂定该值

typedef enum {
    BLDC_FORWARD,       /* 无刷直流电机顺时针转动(正向) */
    BLDC_REVERSE,       /* 无刷直流电机逆时针转动(反向) */
    BLDC_STOP,          /* 无刷直流电机停止转动 */
} bldc_state_t;

typedef enum {
    REAG_TAB_AREA_A = 0,
    REAG_TAB_AREA_B,
    REAG_TAB_AREA_C,
    REAG_TAB_AREA_D,
    REAG_TAB_AREA_E,
    REAG_TAB_AREA_F,
    REAG_TAB_AREA_MAX
} reag_tab_area_t;

typedef struct {
    int32_t pos_idx;        /* 位置索引1-36(试剂)37-38(稀释液) */
    int32_t remain_flag;    /* 是否余量探测标记 */
    int32_t reag_category;
    int32_t rx;
    time_t time;            /* 最后探测时间 */
    int32_t mix_type;       /* 混匀类型 */
    int32_t bottle_type;    /* 瓶型 */
    int32_t mix_flag;       /* 是否混匀标记 置0为否 置1为是*/ 
    struct list_head possibling;
} reag_consum_info_t;

typedef enum {
    TABLE_IDLE = 0,
    TABLE_MOVING,
//    TABLE_STOPED
} reag_tab_stage_t;

typedef enum {
    TABLE_COMMON_RESET = 0,
    TABLE_ONPOWER_RESET,
    TABLE_COMMON_MOVE,
    TABLE_SCAN_MOVE,
    TABLE_BOTTON_MOVE
} reag_tab_move_t;
    
typedef enum {
        NEEDLE_S = 0,            /* 样本针 */
        NEEDLE_R2,               /* R2试剂针 */
        REAGENT_OPERATION_MIX,   /* 混匀位 */
        REAGENT_SCAN_ENGINEER,      /* 工程师模式扫码 */
} require_shift_pos_t;           /* 试剂仓位置大类 */

typedef enum {
    NODE_NULL = 0,
    REAGENT_TABLE_MOTOR_NODE,     /* 试剂盘电机 */
    MIX_MOROT_NODE,               /* 混匀电机 */
    SCAN_NODE,                    /* 扫码 */
} onpower_selfcheck_node;

typedef struct {
    int diluent_idx;        /* 索引号 */
    bool last_stage;        /* 上次检测的开关状态 */
    bool curr_satge;        /* 当前检测的开关状态 */
    needle_pos_t dilu_idx;  /* 稀释液索引号 */
    int report_flag;        /* 状态变化是否上报 0已上报，1未上报      */
} diluent_monitor_t;

typedef struct reag_table_cotl{
    reag_tab_move_t table_move_type;  /* 运动模式 */
    needle_pos_t table_dest_pos_idx;  /* 位置索引 1-36 */
    require_shift_pos_t req_pos_type; /* 请求运动的部件类型 */
    reagnet_mode_t button_mode;       /* 按键模式转动 */ 
    reag_tab_area_t pan_idx;          /* 试剂盘区域索引 A-F */
    int is_auto_cal;                 /* 是否为自动标定调用 */
    double move_time;                 /* 下传的移动时间 */
} reag_table_cotl_t;

int module_reagent_table_init(void);
int reagent_table_move_interface(reag_table_cotl_t *reag_table_cotl);
void reagent_scan_interface(void);
int reagent_mix_interface(void);
void reag_table_occupy_flag_set(int stage);
reag_tab_stage_t reag_table_stage_check(void);
int reag_table_occupy_flag_get(void);
int reinit_reagent_table_data(void);
void reag_change_idx_get(int *array);
void reag_consum_param_set(reag_consum_info_t *reag_info, int type);
void reag_remain_detected_set(needle_pos_t idx);
int cont_scan_flag_get(void);
int reagent_table_bottle_type_get(needle_pos_t idx);
int transfer_to_enum(int needle_pos);
int reagent_table_onpower_selfcheck_interface(void);
void restart_sem_post(void);
int reagent_table_scan_pos_auto_calibrate(int32_t mode);
int reagent_table_set_pos(void);
void auto_calibrate_stop_set(int32_t flag);
void reagent_scan_engineer_mode(int32_t para);
void reagent_scan_maintence_mode(int pos_idx);

#ifdef __cplusplus
}
#endif

#endif






