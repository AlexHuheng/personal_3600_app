#ifndef H3600_REACT_CUP_H
#define H3600_REACT_CUP_H

#include <list.h>

typedef enum{
    TEST_CUP,           /* 测试杯 */
    DILU_CUP,           /* 稀释杯 */
}cup_type_t;            /* 反应杯类型 */

typedef enum{
    CUP_ACTIVE,         /* 已激活 */
    CUP_INACTIVE,       /* 未激活 */
}cup_active_t;          /* 杯激活状态 */

typedef enum{
    POS_INVALID, /* 无效位 */
    POS_CUVETTE_SUPPLY_INIT,    /* 反应杯盘进杯位 */
    POS_PRE_PROCESSOR,          /* 前处理 (样本针加样位) */
    POS_PRE_PROCESSOR_MIX1,     /* 前处理 (混匀位1) */
    POS_PRE_PROCESSOR_MIX2,     /* 前处理 (混匀位2) */
    POS_INCUBATION_WORK_1 = 5,  /* 孵育模块孵育位1 */
    POS_INCUBATION_WORK_2,      /* 孵育模块孵育位2 */
    POS_INCUBATION_WORK_3,      /* 孵育模块孵育位3 */
    POS_INCUBATION_WORK_4,      /* 孵育模块孵育位4 */
    POS_INCUBATION_WORK_5,      /* 孵育模块孵育位5 */
    POS_INCUBATION_WORK_6,      /* 孵育模块孵育位6 */
    POS_INCUBATION_WORK_7,      /* 孵育模块孵育位7 */
    POS_INCUBATION_WORK_8,      /* 孵育模块孵育位8 */
    POS_INCUBATION_WORK_9,      /* 孵育模块孵育位9 */
    POS_INCUBATION_WORK_10,     /* 孵育模块孵育位10 */
    POS_INCUBATION_WORK_11,     /* 孵育模块孵育位11 */
    POS_INCUBATION_WORK_12,     /* 孵育模块孵育位12 */
    POS_INCUBATION_WORK_13,     /* 孵育模块孵育位13 */
    POS_INCUBATION_WORK_14,     /* 孵育模块孵育位14 */
    POS_INCUBATION_WORK_15,     /* 孵育模块孵育位15 */
    POS_INCUBATION_WORK_16,     /* 孵育模块孵育位16 */
    POS_INCUBATION_WORK_17,     /* 孵育模块孵育位17 */
    POS_INCUBATION_WORK_18,     /* 孵育模块孵育位18 */
    POS_INCUBATION_WORK_19,     /* 孵育模块孵育位19 */
    POS_INCUBATION_WORK_20,     /* 孵育模块孵育位20 */
    POS_INCUBATION_WORK_21,     /* 孵育模块孵育位21 */
    POS_INCUBATION_WORK_22,     /* 孵育模块孵育位22 */
    POS_INCUBATION_WORK_23,     /* 孵育模块孵育位23 */
    POS_INCUBATION_WORK_24,     /* 孵育模块孵育位24 */
    POS_INCUBATION_WORK_25,     /* 孵育模块孵育位25 */
    POS_INCUBATION_WORK_26,     /* 孵育模块孵育位26 */
    POS_INCUBATION_WORK_27,     /* 孵育模块孵育位27 */
    POS_INCUBATION_WORK_28,     /* 孵育模块孵育位28 */
    POS_INCUBATION_WORK_29,     /* 孵育模块孵育位29 */
    POS_INCUBATION_WORK_30,     /* 孵育模块孵育位30 */
    POS_MAGNECTIC_WORK_1 = 35,  /* 磁珠模块检测位1 */
    POS_MAGNECTIC_WORK_2,       /* 磁珠模块检测位2 */
    POS_MAGNECTIC_WORK_3,       /* 磁珠模块检测位3 */
    POS_MAGNECTIC_WORK_4,       /* 磁珠模块检测位4 */
    POS_OPTICAL_MIX,            /* 光学模块混匀位        */
    POS_OPTICAL_WORK_1 = 40,    /* 光学模块检测位1 */
    POS_OPTICAL_WORK_2,         /* 光学模块检测位2 */
    POS_OPTICAL_WORK_3,         /* 光学模块检测位3 */
    POS_OPTICAL_WORK_4,         /* 光学模块检测位4 */
    POS_OPTICAL_WORK_5,         /* 光学模块检测位5 */
    POS_OPTICAL_WORK_6,         /* 光学模块检测位6 */
    POS_OPTICAL_WORK_7,         /* 光学模块检测位7 */
    POS_OPTICAL_WORK_8,         /* 光学模块检测位8 */
    POS_REACT_CUP_DETACH,       /* 丢杯位 */
    POS_REAGENT_TABLE_R2_IN,    /* 试剂仓内圈（R2取试剂位）*/
    POS_REAGENT_TABLE_R2_OUT,   /* 试剂仓外圈（R2取试剂位）*/
    POS_REAGENT_TABLE_S_IN,     /* 试剂仓内圈（S取试剂位）*/
    POS_REAGENT_TABLE_S_OUT,    /* 试剂仓外圈（S取试剂位）*/
    POS_REAGENT_TABLE_MIX_IN,   /* 试剂仓内圈（试剂混匀）*/
    POS_REAGENT_TABLE_MIX_OUT,  /* 试剂仓外圈（试剂混匀）*/

    POS_MAX,       /* 最大索引 */
}cup_pos_t;                     /* 反应杯位置 */

/* 试剂仓分内、外圈各18个瓶子 */
typedef enum{
    POS_REAGENT_TABLE_NONE,     /* 无运动位/试剂盘复位位 */
    POS_REAGENT_TABLE_I1 = 1,   /* 试剂仓分内、外圈各18个瓶子 */
    POS_REAGENT_TABLE_O1,
    POS_REAGENT_TABLE_I2,
    POS_REAGENT_TABLE_O2,
    POS_REAGENT_TABLE_I3,
    POS_REAGENT_TABLE_O3,
    POS_REAGENT_TABLE_I4,
    POS_REAGENT_TABLE_O4,
    POS_REAGENT_TABLE_I5,
    POS_REAGENT_TABLE_O5,
    POS_REAGENT_TABLE_I6,
    POS_REAGENT_TABLE_O6,
    POS_REAGENT_TABLE_I7,
    POS_REAGENT_TABLE_O7,
    POS_REAGENT_TABLE_I8,
    POS_REAGENT_TABLE_O8,
    POS_REAGENT_TABLE_I9,
    POS_REAGENT_TABLE_O9,
    POS_REAGENT_TABLE_I10 = 19,
    POS_REAGENT_TABLE_O10,
    POS_REAGENT_TABLE_I11,
    POS_REAGENT_TABLE_O11,
    POS_REAGENT_TABLE_I12,
    POS_REAGENT_TABLE_O12,
    POS_REAGENT_TABLE_I13,
    POS_REAGENT_TABLE_O13,
    POS_REAGENT_TABLE_I14,
    POS_REAGENT_TABLE_O14,
    POS_REAGENT_TABLE_I15,
    POS_REAGENT_TABLE_O15,
    POS_REAGENT_TABLE_I16,
    POS_REAGENT_TABLE_O16,
    POS_REAGENT_TABLE_I17,
    POS_REAGENT_TABLE_O17,
    POS_REAGENT_TABLE_I18,
    POS_REAGENT_TABLE_O18,
    POS_REAGENT_DILU_1 = 37,
    POS_REAGENT_DILU_2
}needle_pos_t;

typedef enum{
    SP_NO_HAT = 0,
    SP_WITH_HAT,
}sp_hat_t;          /* 样本管戴帽情况 */

typedef enum{
    REAG_SCAN_MODE = 0xA5,
    NO_SCAN_MODE = 0x5A,
}reag_scan_t;          /* 试剂信息扫描 */

typedef enum{
    DISABLE_SP_CLEAN,
    ENABLE_SP_CLEAN,
}sp_atten_t;          /* 特殊清洗 */

typedef enum{
    ATTR_DISABLE,
    ATTR_ENABLE,
}attr_enable_t;          /* 参数使能 */

typedef enum{
    CUP_STAT_UNUSED,    /* 未执行 */
    CUP_STAT_USED,      /* 已执行 */
    CUP_STAT_TIMEOUT,   /* 执行超时 */
    CUP_STAT_RUNNING,   /* 进行中 */
    CUP_STAT_HALT,      /* 暂停 */
}cup_stat_t;            /* 反应杯执行状态 */

typedef enum{
    OPTICAL_WAVE_340,     /* 波长340 */
    OPTICAL_WAVE_660,     /* 波长660 */
    OPTICAL_WAVE_800,     /* 波长800 */
    OPTICAL_WAVE_570,     /* 波长570 */
    OPTICAL_WAVE_405,     /* 波长405 */
    OPTICAL_BACKGROUND,   /* 本底噪声 */
}optical_wave_t;          /* 光学检测波长 */

typedef enum {
    STANARD_CUP,    /* 标准杯 */
    EP_CUP,         /* EP管 */
    PP_1_8,         /* 蓝头采血管1.8毫升，PP材质 */
    PP_2_7,         /* 蓝头采血管2.7毫升，PP材质 */
    BT_V3,          /* 3ML试剂瓶，用于试剂仓质控 */
    BT_V7,          /* 7ML试剂瓶，用于试剂仓质控 */
    BT_V15,         /* 15ML试剂瓶，用于试剂仓质控 */
} sample_tube_type_t;

typedef enum {
    BT_UNKNOWN = 0,         /* 未知 */
    BT_REAGENT_7_ML,        /* 试剂瓶7ml */
    BT_REAGENT_15_ML,       /* 试剂瓶15ml */
    BT_REAGENT_25_ML,       /* 试剂瓶25ml(清洗液B) */
    BT_REAGENT_3_ML,        /* 试剂瓶3ml(乏因子血浆,光学质控品) */
    BT_REAGENT_200_ML,      /* 试剂瓶200ml(清洗液A) */
    BT_TUBE_MICRO,          /* 日立杯 */
    BT_TUBE_EP15,           /* 子弹头 */
    BT_TYPE_MAX
} bottle_type_t; 

typedef enum {
    POS_DILUENT_1,
    POS_DILUENT_2,
}pos_diluent_t;

typedef enum {
    EMERENCY_ORDER_TYPE,    	/* 急诊订单 */
    RERUN_ORDER_TYPE,         	/* 复查订单 */
    GENERAL_ORDER_TYPE,         /* 常规订单 */
} sample_order_type_t;

typedef enum {
    NORMAL_SAMPLE,
    REAGENT_QC_SAMPLE,
} tube_type_t;

struct sample_tube{                 /* 样本管信息 */
    uint32_t slot_id;               /* 样本槽id 1-6    */
    uint32_t sample_tube_id;        /* 样本编号 */
    tube_type_t rack_type;          /* 样本管类型 */
    uint32_t check_priority;        /* 检测优先级 */
    uint32_t rack_idx;              /* 样本架id */
    uint32_t sample_index;          /* 样本管位置 */
    sp_hat_t sp_hat;                /* 是否戴帽 戴帽 &非带帽 */
    sp_atten_t sp_spec_clean;       /* 特殊清洗使能 */
    cup_stat_t sp_spec_clean_stat;  /* 特殊清洗状态 */
    uint8_t check_anticoagulation_ratio; /* 是否启用抗凝比例检查: 1:启用；0:不启用 */
    uint8_t check_clot;             /* 是否启用凝块检查: 1:启用；0:不启用 */
    sample_tube_type_t type;        /* 样本管类型 */
    double sample_volume;         /* 该样本管需要的样本总量 单位微升 */
    uint32_t diluent_volume;        /* 稀释液量 */
    pos_diluent_t diluent_pos;      /* 稀释液位置 */
    cup_stat_t r_x_add_stat;        /* 加液状态 */
    int sq_report;
    uint8_t test_cnt;               /* 当前样本管测试项数 */
    uint8_t is_last;                /* 是否为本管最后一个订单的测试杯(穿刺和筛查需要判断是否为最后一个测试杯) */
};

typedef enum {
    NORMAL_CLEAN,   /* 普通清洗 */
    SPECIAL_CLEAN,  /* 特殊清洗 */
}clean_type_t;

typedef struct {
    clean_type_t type;      /* 清洗类型 */
    cup_stat_t   status;    /* 清洗状态 */
}clean_info_t;

typedef struct
{
    int x;
    int y;
    int z;
}pos_t;

struct needle_r_x_attr {                /* 试剂针R1 R2 Rx信息 */
    attr_enable_t r_x_add_enable;       /* 使能 样本针S:ENABLE代表需要到采样位吸样本 DISABLE代表针里面有样本 */
                                        /* 试剂针R1:ENABLE代表需要加试剂 DISABLE代表不需要加试剂 */
                                        /* 试剂针R2:只能为ENABLE */
    needle_pos_t needle_pos;            /* 针需取试剂的位置 */
    uint32_t bottle_type;               /* 试剂瓶型，BOTTLE_TYPE */
    double take_ul;                      /* 加液量 单位微升 */
    double take_dilu_ul;                 /* 加稀释液量 单位微升 */
    double take_mix_ul;                  /* 取混合液量 单位微升   */
    cup_stat_t r_x_add_stat;            /* 加液状态 */
    attr_enable_t needle_heat_enable;   /* 试剂针加热使能 */
    clean_info_t prev_clean;            /* 前清洗 */
    clean_info_t post_clean;            /* 后清洗 */
    uint32_t pos_idx;
};

struct incubation_attr{                      /* 孵育参数 */
    attr_enable_t incubation_mix_enable;     /* 混匀使能 */
    uint32_t mix_type;                       /* 混匀类型 0:不混匀 1:短周期混匀 2:长周期混匀 */
    uint32_t mix_rate;                       /* 混匀速率 */
    cup_stat_t incubation_mix_stat;          /* 混匀状态 */
    uint32_t incubation_time;                /* 孵育时间 单位秒 */
    cup_stat_t incubation_stat;              /* 孵育状态 */
    long long incubation_start_time;         /* 孵育开始时间 */
    uint32_t mix_time;                       /* 混匀时间 */
};

struct magnectic_attr{                      /* 磁珠参数 */
    attr_enable_t magnectic_enable;         /* 磁珠检测使能 */
    uint32_t mag_beed_clot_percent;         /* 凝固判定百分比，范围为1-100（对应表示1%至100%,通常为50%:正常凝固，80%:弱凝） */
    uint32_t magnectic_power;               /* 磁珠驱动力 0:正常 1:弱 2:强 */
    uint32_t mag_beed_max_detect_seconds;   /* 最大检测时间，以秒为单位，当达到此时间后仍未凝固时，则需上报结果状态码为"不凝" */
    uint32_t mag_beed_min_detect_seconds;   /* 最小检测时间，以秒为单位，当小于此时间凝固时，则需上报结果状态码为"异常"，另需针对钢珠未起振时，则需上报结果状态为“反应杯无钢珠” */
    cup_stat_t magnectic_stat;              /* 磁珠检测状态 */
    long long mag_beed_start_time;          /* 磁珠检测开始时间 */
};

struct optical_attr{                      /* 光学参数 */
    attr_enable_t optical_enable;         /* 光学检测使能 */
    int main_wave;             /* 光学检测主波长 */
    int vice_wave;             /* 光学检测副波长 */
    uint32_t optical_main_seconds;        /* 光学检测主波长检测时间 单位秒 */
    uint32_t optical_vice_seconds;        /* 光学检测副波长检测时间 单位秒 */
    cup_stat_t optical_stat;              /* 光学检测状态 */
    long long optical_start_time;         /* 光学检测开始时间 */
    attr_enable_t optical_mix_enable;     /* 光学混匀使能 */
    uint32_t optical_mix_type;            /* 光学混匀类型       0:不混匀 1:短周期混匀 2:长周期混匀 */
    uint32_t optical_mix_rate;            /* 光学混匀速率, 指振荡混匀电机的转速，单位为rpm（每分钟转速） */
    cup_stat_t optical_mix_stat;          /* 光学混匀状态 */
    uint32_t optical_mix_time;            /* 混匀时间 */
};

typedef enum {
    R1_ADD1 = 0,            /* 第一次加样 */
    R1_ADD2,                /* 第二次加样 */
    R1_ADD_MAX
} add_state_t;

typedef enum {
    PRE_IS_NOT_READY = 0,   /* 未加样完毕 */
    PRE_IS_READY,           /* 加样完毕 */
} pre_ready_t;

struct test_attr{              /* 测试杯参数 */
    struct needle_r_x_attr needle_s;
    struct needle_r_x_attr needle_r1[R1_ADD_MAX];   /* R1加2次试剂 */
    struct needle_r_x_attr needle_r2;
    struct incubation_attr test_cup_incubation;
    struct magnectic_attr test_cup_magnectic;
    struct optical_attr test_cup_optical;
    double curr_ul;                  /* 当前测试杯中的液体量 */
    attr_enable_t add_lk;           /* 是否需要加乏因子血浆 */
    add_state_t r1_add_status;      /* r1加2次试剂的状态记录 */
    pre_ready_t pre_stat;           /* 当前样本加样加试剂完成标志 */
};

/* 稀释杯参数 */
struct dilu_attr {
    double take_ul;              /* 加样量 */
    double dilu_ul;         /* 稀释液量 */
    double take_mix_ul;     /*从杯子取样的加样量，两个杯子时*/
    cup_stat_t add_state;       /* 稀释加液状态 */
    cup_stat_t trans_state;     /* 稀释杯执行状态 */
    needle_pos_t needle_pos;    /* 试剂仓质控 */
};

/* 穿刺清洗池暂存相关信息 */
typedef struct {
    uint8_t     temp_store;     /* 是否需要吸样暂存 */
    uint8_t     temp_take;      /* 是否需要去暂存位取样 */
} sp_para_t;

struct react_cup{               /* 反应杯信息 */
    uint32_t cup_id;            /* 反应杯编号 */
    uint32_t order_no;          /* 订单编号 */
    uint32_t order_state;       /* 订单状态 */
    uint8_t  rerun;             /* 是否为复查 1:是，0:否 */
    uint8_t priority;           /* 优先级 */
    cup_active_t cup_active;
    cup_active_t cup_delay_active;
    cup_type_t cup_type;
    cup_pos_t cup_pos;
    char cuvette_strno[16];     /* 反应杯盘批号 */
    int32_t cuvette_serialno;   /* 反应杯盘序列号 */
    sp_para_t sp_para;          /* 穿刺暂存信息 */
    struct sample_tube cup_sample_tube;
    struct test_attr cup_test_attr;
    struct dilu_attr cup_dilu_attr;
    struct list_head mainsibling;               /* 主监控链表 */
    struct list_head needle_s_sibling;          /* 前处理模块监控链表 */
    struct list_head needle_r2_sibling;         /* 孵育模块监控链表 */
    struct list_head catcher_sibling;           /* 磁珠模块监控链表 */
};

#endif

