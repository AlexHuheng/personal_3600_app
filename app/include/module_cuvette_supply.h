#ifndef __MODULE_CUVETTE_SUPPLY_H__
#define __MODULE_CUVETTE_SUPPLY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#include <log.h>
#include <list.h>
#include <module_monitor.h>
#include <h3600_cup_param.h>
#include <module_cup_monitor.h>
#include <module_cuvette_supply.h>
#include <slip_cmd_table.h>
#include <movement_config.h>
#include <module_common.h>
#include <module_optical.h>
#include <work_queue.h>
#include <thrift_handler.h>
#include <module_reagent_table.h>

#define BLDC_RADS_LIMIT         6
#define BLDC_NORMAL_TIMEOUT     8
#define BLDC_RESET_TIMEOUT      60

typedef enum bldc_idx {
    CUVETTE_BLDC_INDEX = 0,
} bldc_idx_t;

typedef enum get_mode {
    MAINTAIN_GET_MODE = 0,
    NORMAL_GET_MODE
} reaction_cup_get_mode_t;

/* TBD   不属于本模块定义 */
typedef enum {
    POWER_ON,
    POWER_OFF,
    MACHINE_HALT
} maintain_mode_t;

typedef enum cup_idx {
    REACTION_CUP_NONE = -1,
    REACTION_CUP_INSIDE,
    REACTION_CUP_MAX
} reaction_cup_index_t;

typedef enum cup_state {
    CUP_INIT = 0,       /* 初始状态 */
    CUP_RUNNING,        /* 杯盘运动状态(标识进杯过程静止到运动的状态) */
    CUP_READY,          /* 就绪状态 */
    CUP_GATE,           /* 错误状态：舱门被拉开 */
    CUP_BUCKLE,         /* 错误状态：卡扣未扣紧 */
    CUP_ERROR           /* 错误状态：进杯失败 */
} reaction_cup_state_t;

typedef struct cuvette_supply_para {
    reaction_cup_index_t idx;
    int switch_io;  /* 压带限位微动开关 */
    int gate_io;    /* 舱门光电 */
    int gate_open;  /* 舱门拉开状态? */
    bldc_idx_t bldc_idx;   /* 对应电机索引0 */
    int stime;      /* 电机开始运动时间 */
    int timeout;    /* 电机运动超时时间(s) */
    reaction_cup_state_t state;      /* 进杯状态 */
    int report;     /* 提示信息是否上报 */
    reaction_cup_get_mode_t mode;   /* 0: 复位进杯, 1:正常进杯 */
    int available;  /* 是否可用 */
    int priority;   /* 优先使用权 */
    int led_blink;  /* led是否闪烁 */
    int in_use;     /* 正在使用标志 */
    char strlotno[16];   /* 反应杯盘批号 */
    int serno;      /* 反应杯盘序列号 */
    int re_close;   /* 是否重新推入 */
    int typein;     /* 是否重新录入 */
    int running;    /* 是否正在进杯 */
    int rads;       /* 用于转速日志记录 */
    int gate_running;   /* 是否正在执行舱门相关动作 */
} __pk cuvette_supply_para_t;

int module_cuvette_supply_init(void);
int cuvette_supply_reset(maintain_mode_t mode);
void cuvette_supply_notify(void);
int cuvette_supply_get(reaction_cup_get_mode_t mode);
cuvette_supply_para_t *cuvette_supply_para_get(void);
void cuvette_supply_led_ctrl(reaction_cup_index_t index, int blink);
void cuvette_supply_para_set(consum_info_t *info);
void cuvette_supply_led_ctrl1(void);
void cuvette_supply_fault_clear(void);
int thrift_cuvette_supply_func(int io_idx);


#ifdef __cplusplus
}
#endif

#endif
