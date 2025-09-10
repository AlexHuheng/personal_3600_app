#ifndef __MODULE_LIQUID_CIRCUIT_H__
#define __MODULE_LIQUID_CIRCUIT_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <semaphore.h>
#include <log.h>
#include <list.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <misc_log.h>

#include "work_queue.h"
#include "slip/slip_node.h"
#include "slip/slip_process.h"
#include "slip_cmd_table.h"
#include "slip/slip_msg.h"
#include "common.h"
#include "thrift_service_software_interface.h"
#include "module_common.h"
#include "movement_config.h"
#include "module_monitor.h"
#include "h3600_needle.h"
#include "module_sampler_ctl.h"
#include "module_cup_mix.h"
#include "module_needle_s.h"
#include "module_cup_monitor.h"
#include "h3600_maintain_utils.h"
#include "thrift_handler.h"
#include "h3600_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BS1_COUNT_MAX 5   /* 特殊清洗液气泡传感器BS1灌注5次即可到达气泡传感器 */
#define BS2_COUNT_MAX 10  /* 普通清洗液气泡传感器BS2灌注11次即可到达气泡传感器 */
#define CIRCUIT_DELAY_MS (500 * 1000)
#define CLOT_DETECT_THREOLD_DEFAULT (210) /* 凝块探测默认阀值 */
#define PUMP_SINGLE_CYCLE 150000 /* 隔膜泵单周期长度，单位us */
#define ERROR_COUNT_MAX 5 /* 通信异常上报计数 */
#define CONST_DRAINAGE_TIME 1800 /* 设置试剂仓定时排水时间 */
#define LIQ_SWITCH_ON  1 
#define LIQ_SWITCH_OFF 0
#define LIQ_PERF_HALF_STEPS 16000 /* 5ml柱塞泵半量程步长 */
#define LIQ_PERF_FULL_STEPS 32000 /* 5ml柱塞泵满量程步长 */
#define LIQ_HALF_METER 1920
#define LIQ_S_PERF_VARIABLE 2499   /* 特殊清洗液单次供给量 */
#define LIQ_SIGNAL_PERF_VARIABLE 140   /* 特殊清洗液单次供给量 */
#define LIQ_SPCL_CLEAR_FULL_STEPS 5000 /* 特殊清洗液5ml泵容量 */
#define PIPE_PILL_R2_STPES_TRANS_CAPA  4684 /* 管路填充时的容量转换对应泵步长为27000 */
#define PIPE_PILL_S_STPES_TRANS_CAPA  770 /* 管路填充时的容量转换对应泵步长为4800 */
#define SPCL_CLR_RECHECKED 2 //维护时特殊清洗液第二次检测到气泡
#define OUTTIME_FOR_DATAWAIT 4000 //凝块数据等待超时时间

#define PRESS_DATA_RECORD_PATH LOG_DIR"press_record.log"

#define PRESS_RECORD_FILE_MAX_SIZE 20*1024*1024 //最大长度为20M

typedef enum {
    BUBBLE_SENSOR = 0,          /* 特殊清洗液气泡传感器 */
    CLOT_SENSOR,                /* 凝块传感器 */
    SWITCH
} liq_sensor_t;

typedef enum {
    CLEAN_TYPE_NORMAL = 0,  /* 普通清洗液 */
    CLEAN_TYPE_SPECIAL, /* 特殊清洗液 */
    CLEAN_TYPE_WASTE,   /* 废液 */
    CLAER_TYPE_MAX
} clean_liquid_type_t;

typedef enum {
    NORMAL_CLEAN_TANK,  /* 普通清洗液桶 */
    SPECIAL_CLEAN_BOT,  /* 特殊清洗液瓶 */
    WASTE_TANK,         /* 废液桶 */
    OVERFLOW_BOT,       /* 溢流瓶 */
    BARCKET,            /* 托架 */
    COMPO_MAX
} liquid_component_t;

typedef enum {
    STEGE_POOL_PRE_CLEAR, /* 暂存池前清洗 */
    STEGE_POOL_LAST_CLEAR, /* 暂存池后清洗 */
} liq_slave_numb_t;

typedef struct {
    liq_sensor_t sensor_idx;
    int stage;
}__attribute__((packed))liq_sensor_stage_t;

typedef struct {
    clean_liquid_type_t comp_idx;   /* 清洗液类型 */
    int remain_stage;               /* 清洗液当前状态 */
}__attribute__((packed))perfusion_monitor_t;

typedef enum buffer_bottle_sta {
    BB_NOT_FULL,
    BB_FULL
} buffer_bottle_sta_t;

typedef struct {
    int check_flag;      /* 检测标志置1后 不可重入 */
    int stage;           /* 吸样前为1，吸样后为0 */
    float vol;           /* 吸样体积 */
    uint32_t orderno[2]; /* 当前周期的订单号 */
    needle_s_cmd_t cmd[2]; /* 暂存问题订单的cmd */
}clot_check_t;

typedef struct {
    int pump_id;
    int pump_switch;
    int pump_flag; //此标志为1表示开启Q1流量模式
} __attribute__((packed))liquid_pump_ctl_t;

typedef struct {
    uint8_t bub_chk_switch;     /* 气泡检测开关 为1表示开启       ,0表示关闭*/
    uint8_t bubble_s_status;    /* bs1 特殊清洗液气泡传感器; 1:检测到气泡 */
    uint8_t bubble_n_status;    /* bs2 普通清洗液气泡传感器; 1:检测到气泡 */
    uint8_t waste_status;       /* 废液桶 0 正常, 大于1 为检测异常并计数 */
    uint8_t normal_status;      /* 普通清洗液桶状态：大于1 为检测异常并计数 */
    uint8_t overflow_status;    /* 溢流瓶状态 */
}__attribute__((packed))slip_liquid_circuit_t;

typedef struct
{
    uint8_t enable;    /* 凝块检测开关 0：关闭 1：开启 */
    uint16_t threold;  /* 凝块阀值 */
}__attribute__((packed))slip_liquid_circuit_param_set_t;

typedef struct {
    uint8_t ready;      /* 是否可用 */
    uint8_t report;     /* 错误是否已上报 */
} clean_liquid_sta_t;

typedef struct
{
    int order_no;
    float press_max;
    float press_min;//获取当前数据流的最大/小值
    uint8_t count_sum_flag;//跳变后进行数据记录
    float sigle_press_sum[20];//在采集到最小压力值后，获取单个正压峰内积分值
    uint16_t recorde_conut[20];//记录波动部分的峰值。
    uint16_t data_count;//记录数据个数
    float intg_value;//描述吸样特性的积分值
}clot_para_t;

typedef struct {
    int order_no;
    int cnt;
    float *data_buffer;
    struct list_head data_sibling;
}press_recort_t;

int liquid_circuit_init(void);
void clean_liquid_para_set(consum_info_t * info);
void liquid_circuit_reinit(void);
void special_clear_pipe_line_prefill(void);
int liquid_self_maintence_interface(int mode);
int pump_absorb_sample(needle_type_t needle_type, float amount_ul, int mode);
int pump_5ml_absorb_clearer(char motor_id, float amount_ul, int mode);
void liquid_pump_pwm_open(liquid_pump_ctl_t *para);
void liquid_pump_close(int        pump_id);
void liquid_onpower_5ml_pump_manage(void);
int stage_pool_self_clear(liq_slave_numb_t stage_pool_clear_type);
void liq_s_handle_sampler(int flag);
void liquid_access_clear(void);
void pump_5ml_inuse_manage(int value);
int pipe_remain_release(void);

void normal_bubble_status_check(clean_liquid_type_t *type);
void normal_bubble_check_end(void);

void liquid_clot_check_interface(double vol, int stage);

void set_clot_data(clot_para_t *data);
void g_presure_noise_set(float noise_data);
void set_cur_sampler_ordno_for_clot_check(uint32_t ordno, int cycle_flag, needle_s_cmd_t cmd);

int r2_normal_clean_onpower_selfcheck(void);
void s_noraml_clean_onpower_selfcheck(void);
int spcl_cleaner_fill_onpower_selfcheck(void);
void wash_pool_onpower_selfcheck(void);
void stage_pool_clean_onpower_selfcheck(void);
void pump_cur_steps_set(int data);
int normal_clearer_bubble_stage_notify(void);
void r2_clean_mutex_lock(int lock);
void clearer_change_ture_maintence_handle(void);
void press_data_add_in_list(int count, float *buf, int orderno);

#ifdef __cplusplus
}
#endif

#endif


