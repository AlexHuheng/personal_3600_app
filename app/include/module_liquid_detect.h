#ifndef __MODULE_LIQUID_DETECT_H__
#define __MODULE_LIQUID_DETECT_H__

#include <log.h>
#include <list.h>
#include <misc_log.h>
#include <lowlevel.h>
#include <work_queue.h>
#include <h3600_needle.h>
#include <h3600_cup_param.h>
#include <h3600_maintain_utils.h>
#include <module_common.h>
#include <module_cup_mix.h>
#include <module_needle_s.h>
#include <module_reagent_table.h>
#include <module_liquied_circuit.h>
#include <module_cup_monitor.h>
#include <module_catcher.h>
#include <module_monitor.h>
#include <module_temperate_ctl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BYTE0(dwTemp)       ( *( (char *)(&dwTemp)    ) )
#define BYTE1(dwTemp)       ( *( (char *)(&dwTemp) + 1) )
#define BYTE2(dwTemp)       ( *( (char *)(&dwTemp) + 2) )
#define BYTE3(dwTemp)       ( *( (char *)(&dwTemp) + 3) )

#define LIQUID_DETECT_MAX       3           /* 余量探测次数 */
#define REMIAN_INFO_MAX         30
#define BT_EP_CUP_UL_MAX        1750
#define BT_STAND_CUP_UL_MAX     2800
#define BT_REAGENT_7_ML_MAX     9000
#define BT_REAGENT_15_ML_MAX    23000
#define BT_REAGENT_25_ML_MAX    26500
#define SEND_DATA_MAX           15          /* M0上传一次上传数据量 */
#define LIQUID_DETECT_DATA_MAX  2048

#define PIERCE_HAT_STEP         7000        /* 穿刺针从管口上方穿过帽子的脉冲 */

#define S_1MM_TO_STEP           0.00264                 /* S/R1 1个脉冲对应0.00264mm */
#define R2_1MM_TO_STEP          0.01                    /* r2 1个脉冲对应0.01mm */
#define S_1MM_STEPS             (1 / S_1MM_TO_STEP)     /* 针下降1mm需要移动的脉冲 */
#define R2_1MM_STEPS            (1 / R2_1MM_TO_STEP)    /* 针下降1mm需要移动的脉冲 */
#define needle_z_1mm_steps(n)   (int)((n == NEEDLE_TYPE_R2) ? R2_1MM_STEPS : S_1MM_STEPS)
#define MIN_MULTIPLE            2                       /* 用于计算最小虚拟脉冲 */
#define MAX_MULTIPLE            4                       /* 用于计算最大虚拟脉冲 */
#define VACUTAINER_1UL_RATIO    5.5         /* 采血管1ul和脉冲数对应系数 */
#define ADP_LOW_LIMIT           5000        /* 带适配器管的系数分界线 */
#define ADP_1UL_RATIO           7           /* 适配器1ul和脉冲数对应系数 */
#define ADP_LOW_1UL_RATIO       10          /* 适配器1ul和脉冲数对应系数,低液量 */
#define S_COMP_STEP_MIN         500         /* 样本针最低补偿脉冲 */
#define DETECT_DYNA_STEP_MM     15          /* 动态屏蔽位距离液面高度mm */
#define MIRCO_THR_DEFAULT       2300        /* 试剂仓质控品探测默认阈值,试剂仓质控功能增加，防止老机器无此阈值 */
#define S_X_STEP_MM_RATIO       0.013
#define S_Y_STEP_MM_RATIO       0.019

#define S_CONTACT_V             25000       /* 穿刺针多段液面探测速度模式阶段的速度 */
#define S_CONTACT_V_SLOW        15000       /* 穿刺针新液面探测模式下的探测速度 */
#define S_REMAIN_CONTACT_V      5000        /* 穿刺针余量探测速度 */
#define R2_CONTACT_V            3000        /* R2新液面探测模式下的探测速度 */
#define R2_REMAIN_CONTACT_V     2000        /* R2余量探测速度 */

#define S_TO_TOP                (-2000)     /* 样本针标定位距离瓶口脉冲 */
#define S_TO_BOTTOM             30500       /* 样本针标定位距离瓶底脉冲 */
#define S_TO_MIRCO_TOP          (-2000)     /* 样本针标定位距离微量管瓶口脉冲 */
#define S_TO_STAND_BOTTOM       13350       /* 样本针标定位距离瓶底脉冲(日立杯) */
#define S_TO_EP_BOTTOM          14100       /* 样本针标定位距离瓶底脉冲(子弹头) */
#define S_DILU_TO_TOP           (-7500)     /* 样本针标定位距离稀释液瓶口脉冲 */
#define S_DILU_TO_BOTTOM        15530       /* 样本针标定位距离稀释液瓶底脉冲 */
#define S_DULU_TO_BOTTOM1       8480        /* 样本针标定距离稀释液位微量管底脉冲 */

/* 样本针 */
#define S_V7_TO_TOP             18730       /* 基准值:样本针标定位距离7ML瓶瓶口脉冲 */
#define S_V15_TOP_DIFF          (-4000)     /* 样本针标定位距离15ML瓶瓶口高度，基于S_V7_TO_TOP */
#define S_IN_V7_BTM             36395       /* 样本针标定位距离内圈非镀膜7ML瓶底部脉冲 */
#define S_IN_V15_BTM            36205       /* 样本针标定位距离内圈非镀膜15ML瓶底部脉冲 */
#define S_OUT_V7_BTM            36405       /* 样本针标定位距离外圈非镀膜7ML瓶底部脉冲 */
#define S_OUT_V15_BTM           36215       /* 样本针标定位距离外圈非镀膜15ML瓶底部脉冲 */
#define S_V7_BTM_DIFF           (-3)        /* 镀膜7ML瓶底部脉冲差异，基于非镀膜瓶，不区分内外圈 */
#define S_V15_BTM_DIFF          (-30)       /* 镀膜15ML瓶底部脉冲差异，基于非镀膜瓶，不区分内外圈 */
#define S_MICRO_TO_TOP          19700       /* 样本针标定位距离微量杯(日立杯+EP管)口脉冲 */
#define S_IN_STAND_BTM          35540       /* 样本针标定位距离内圈日立杯底部脉冲 */
#define S_IN_EP_BTM             35595       /* 样本针标定位距离内圈EP管底部脉冲 */
#define S_OUT_STAND_BTM         35560       /* 样本针标定位距离外圈日立杯底部脉冲 */
#define S_OUT_EP_BTM            35620       /* 样本针标定位距离外圈EP管底部脉冲 */

/* 试剂针 */
#define R2_V7_TO_TOP            5210        /* 基准值:试剂针标定位距离7ML瓶瓶口脉冲 */
#define R2_V15_TOP_DIFF         (-1200)     /* 试剂针标定位距离15ML瓶瓶口高度，基于R2_V7_TO_TOP */
#define R2_IN_V7_BTM            9655        /* 试剂针标定位距离内圈非镀膜7ML瓶底部脉冲 */
#define R2_IN_V15_BTM           9600        /* 试剂针标定位距离内圈非镀膜15ML瓶底部脉冲 */
#define R2_OUT_V7_BTM           9650        /* 试剂针标定位距离外圈非镀膜7ML瓶底部脉冲 */
#define R2_OUT_V15_BTM          9590        /* 试剂针标定位距离外圈非镀膜15ML瓶底部脉冲 */
#define R2_V7_BTM_DIFF          (-8)        /* 镀膜7ML瓶底部脉冲差异，基于非镀膜瓶，不区分内外圈 */
#define R2_V15_BTM_DIFF         (-13)       /* 镀膜15ML瓶底部脉冲差异，基于非镀膜瓶，不区分内外圈 */

#define NORMAL_TIMEOUT          2000        /* 样本针正常探测超时时间 */
#define REMAIN_TIMEOUT          10000       /* 样本针余量探测超时时间 */

#define DEV_UART_DEBUG_PATH         "/dev/ttyS4"
#define DEV_UART_DILU_SCANNER_PATH  "/dev/ttyS5"

#define UART_DEBUG_FLAG_START   0xff    /* 调试串口通信 开始标志 */
#define UART_DEBUG_FLAG_END     0x0d    /* 调试串口通信 结束标志 */
#define UART_DEBUG_CMD_ENABLE   0x00    /* 设置调试开关 */
#define UART_DEBUG_RECV_TIMEOUT 1000    /* 数据接收超时时间 ms*/

typedef enum liquid_detect_liq_pos {
    REAG_TABLE_INSIDE = 0,  /* 液体在试剂仓内 */
    REAG_TABLE_OUTSIDE      /* 液体在试剂仓外(样本或稀释液) */
} liquid_detect_liq_pos_t;

typedef enum liquid_detect_circle {
    CIRCLE_INNER = 0,   /* 试剂仓内圈 */
    CIRCLE_OUTER,       /* 试剂仓外圈 */
    CIRCLE_OTHER        /* 试剂仓外(样本或者稀释液) */
} liquid_detect_circle_t;

typedef enum liquid_detect_ret {
    ENONE = 0,          /* 无错误 */
    EARG,               /* 参数错误 */
    EMAXSTEP,           /* 最大步长 */
    ECOLLSION,          /* 撞针 */
    ENOTHING,           /* 空吸 */
    ETIMEOUT,           /* 通信超时 */
    EMOVE,              /* Z运行错误 */
    ESWITCH,            /* 触发切瓶 */
    EMAX
} liquid_detect_ret_t;

typedef enum liquid_detect_m0_ret {
    LIQ_INIT = 0,       /* 初始状态 */
    LIQ_LEAVE_IN,       /* 探测到液面 */
    LIQ_LEAVE_OUT,      /* 离开液面 */
    LIQ_COLLSION_IN,    /* 发生撞针 */
    LIQ_COLLSION_OUT,   /* 解除撞针 */
    LIQ_REACH_MAX       /* 抵达底部位置 */
} liquid_detect_m0_ret_t;

typedef enum liquid_detect_mode {
    NORMAL_DETECT_MODE = 0, /* 常规探测模式 */
    REMAIN_DETECT_MODE,     /* 余量探测模式 */
    DEBUG_DETECT_MODE       /* Service等调试接口调用模式 */
} liquid_detect_mode_t;

typedef enum liquid_detect_thr {
    REAG_TABLE_IN = 0,
    REAG_TABLE_OUT,
    S_SAMPLE,
    S_SAMPLE_HAT,
    S_SAMPLE_ADP,
    S_DILU,
    S_MIRCO
} liquid_detect_thr_t;

typedef enum liquid_remain_detect_ret {
    RET_OK = 0,
    RET_IGNORE,
    RET_ERROR
} liquid_remain_detect_ret_t;

/* 调用液面探测所需参数 */
typedef struct liquid_detect_arg {
    needle_type_t needle;               /* 针类型 */
    int order_no;                       /* 订单号，切瓶使用 */
    int s_cur_step;                     /* 样本针当前脉冲 */
    int coated;                         /* 是否镀膜瓶(无需填充该参数，仅为兼容余量探测设置) */
    float take_ul;                      /* 吸液量 */
    attr_enable_t hat_enable;           /* 样本管是否带帽 */
    liquid_detect_mode_t mode;          /* 探测模式 */
    needle_pos_t reag_idx;              /* 试剂在试剂仓的位置(1~38) */
    bottle_type_t bt;                   /* 试剂仓内容器瓶型(v7/v15/ep/stand) */
    sample_tube_type_t tube;            /* 管型，用于判定是否带适配器等 */
} __pk liquid_detect_arg_t;

typedef struct liquid_detect_calibrate_step {
    int z_step;                         /* 工装标定的脉冲 */
    int top_step;                       /* 到瓶口的脉冲(工装标定z+固定值) */
    int real_maxstep;                   /* 该位置的真实底部脉冲 */
} __pk liquid_detect_cs_t;

/* 执行液面探测所需的参数 */
typedef struct detect_arg {
    int motor_id;                   /* 探测使用的电机号 */
    int contact_speed;              /* 速度模式探测液面的速度 */
    int move_mode;                  /* 液面探测运动模式(三段式或者一段式) */
    int timeout;                    /* 运动超时时间 */
    int step_step;                  /* 探测阶段步长模式运动的脉冲(动态屏蔽)，默认为瓶口位置 */
    int speed_step;                 /* 探测阶段速度模式运动的脉冲，默认为real_maxstep - top_step */
    int stop_step;                  /* a/2减速停的脉冲(已废弃) */
    int last_step;                  /* 记录的上次探测的脉冲(未做余量探测前为0) */
    int dv_step;                    /* 触发切瓶脉冲，用于控制死腔量dead_volume */
    int dyna_maxstep;               /* 动态最大步长，限制Z向运动 */
    liquid_detect_thr_t thr;        /* 探测阈值 */
    liquid_detect_cs_t cal_step;    /* 相关位置标定数据 */
} __pk liquid_detect_para_t;

/* 记录液面探测的相关信息 */
typedef struct liquid_detect_rcd {
    int reag_idx;               /* 探测的位置1~38 */
    int real_maxstep;           /* 真实底部脉冲 */
    int detect_step;            /* 试剂探测脉冲(最终停止下来的脉冲) */
    int coated;                 /* 试剂瓶是否镀膜，仅在余量探测成功后更新 */
    float take_ul;              /* 记录吸样量，0表示为余量探测未吸取 */
    needle_type_t needle;       /* 记录针类型 */
    bottle_type_t bottle_type;  /* 记录瓶型，BT_UNKNOWN非余量探测时不更新 */
    struct list_head info_sibling;
} __pk liquid_detect_rcd_t;

typedef struct liquid_detect_remain_arg {
    int reag_idx;
    bottle_type_t bottle_type;
    int reag_category;          /* 0.试剂，1. 乏因子血浆，2. 稀释液，3. 清洗液A，4. 质控品 */
    int rx;
    int coated;                 /* 是否镀膜瓶 */
    struct list_head remain_sibling;
} __pk liquid_detect_remain_arg_t;

/* 记录余量探测相关信息 */
typedef struct liquid_detect_remain_chg {
    int reag_idx;       /* 位置索引1-47 */
    attr_enable_t flag; /* 是否完成余量探测 */
    time_t time;        /* 最后探测时间 */
    struct list_head chg_sibling;
} __pk liquid_detect_remain_chg_t;

typedef enum liquid_detect_remain_dst {
    POS_DETECT = 0, /* 取液位 */
    POS_CLEAN,      /* 洗针位 */
    POS_RESET       /* 复位 */
} __pk liquid_detect_remain_dst_t;

typedef struct liquid_detect_misc {
    int slip_id;
    int result;
    int detect_step;
    int maxstep;
    int stop_flag;
    int motor_id;
    int timeout;
    int idx;        /* 计数器 */
    int err_count;  /* 错误计数 */
    int reverse;    /* 最大步长监控方向(自动标定使用) */
    int auto_cal;   /* 是否自动标定 */
    enum_leave_index_t signo;
    pthread_mutex_t m_lock;
    int detect_data[LIQUID_DETECT_DATA_MAX];
} __pk liquid_detect_misc_t;

typedef struct liquid_detect_step_ul {
    int step_left;  /* 距离真实底部的脉冲 */
    int vol_left;   /* step_left对应的标准液量 */
    float ratio;    /* 系数ul/step */
} __pk liquid_detect_step_ul_t;

typedef struct slip_liquid_detect_get_result {
    uint8_t board_id;
    uint8_t type;
} __pk slip_liquid_detect_get_result_t;

typedef struct slip_liquid_detect_result {
    uint8_t board_id;
    uint8_t status;
} __pk slip_liquid_detect_result_t;

typedef struct slip_liquid_detect_addata {
    int32_t ad_data[4];
} __pk slip_liquid_detect_addata_t;

typedef struct slip_liquid_detect_all_data {
    int32_t data[LIQUID_DETECT_DATA_MAX];
} __pk slip_liquid_detect_all_data_t;

int module_liquid_detect_init(void);
void liquid_detect_remain_add_tail(int idx, int bt, int rc, int rx, int coated);
void liquid_detect_remain_del_all(void);
int liquid_detect_remain_func(void);
void liquid_detect_remain_async(void *arg);
char *needle_type_string(needle_type_t needle);
int liquid_detect_start(liquid_detect_arg_t arg);
int thrift_liquid_detect_start(needle_type_t type);
int liquid_detect_connect_check(needle_type_t needle);
int liquid_detect_result_get(needle_type_t type);
void liquid_detect_err_count_all_clear(void);

int slip_liquid_detect_rcd_set(needle_type_t type, attr_enable_t enable);
int slip_liquid_detect_collsion_barrier_set(needle_type_t type, attr_enable_t enable);
void slip_liquid_detect_get_addata_async(const slip_port_t *port, slip_msg_t *msg);
void slip_liquid_detect_get_result_async(const slip_port_t *port, slip_msg_t *msg);
void slip_liquid_detect_get_all_data_async(const slip_port_t *port, slip_msg_t *msg);

int slip_liquid_detect_thr_set(needle_type_t type, int thr);
int slip_liquid_detect_type_set(needle_type_t needle);
int slip_liquid_detect_state_set(needle_type_t type, attr_enable_t data);
void liquid_detect_maxstep_task(void *arg);
void liquid_detect_motor_init(int motor_x, int motor_y, int motor_z, motor_time_sync_attr_t *attr_x, motor_time_sync_attr_t *attr_y, motor_time_sync_attr_t *attr_z);


#ifdef __cplusplus
}
#endif

#endif
