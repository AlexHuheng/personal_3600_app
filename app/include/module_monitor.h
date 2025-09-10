#ifndef H3600_MODULE_MONITOR_H
#define H3600_MODULE_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MACHINE_STAT_STANDBY,   /* 待机 */
    MACHINE_STAT_RUNNING,   /* 运行 */
    MACHINE_STAT_STOP,      /* 停机 */
}machine_stat_t;        /* 仪器运行状态 */

typedef enum {
    SAMPLER_ADD_START,  /* 正常加样 */
    SAMPLER_ADD_STOP,   /* 加样停 */
}sampler_stat_t;   /* 加样停状态 */

/* 故障处理相关定义 */
typedef int (*fault_callback)(void *data);

#define MODULE_FAULT_BASE_INDEX (0x0001)
typedef enum {
    MODULE_FAULT_NONE = 0,              /* 正常状态 */
    MODULE_FAULT_LEVEL1 = MODULE_FAULT_BASE_INDEX<<0, /* 加样停:第一层部件故障（进杯盘、进样器、S、R2不在试剂盘内）：停止第一层的运动件 */
    MODULE_FAULT_LEVEL2 = MODULE_FAULT_BASE_INDEX<<1, /* 检测进行:第二层部件故障（抓手、S、R2在试剂盘内、试剂盘）：停止第一层和第二层的运动件 */
    MODULE_FAULT_STOP_ALL = MODULE_FAULT_BASE_INDEX<<2, /* 所有动作停止：停止第一层、第二层的运动件、停止检测 */
}module_fault_stat_t;

/* 故障处理大类型 */
typedef enum
{
    FAULT_COMMON,               /* 通用异常，保留 */

    FAULT_SAMPLER,              /* 对应01异常大类：进样器异常 */
    FAULT_NEEDLE_S,             /* 对应02异常大类：样本针异常 */
    FAULT_CATCHER,              /* 对应07异常大类：抓手异常 */
    FAULT_MIX,                  /* 对应10异常大类：混匀异常 */
    FAULT_INCUBATION_MODULE,    /* 对应11异常大类：孵育模块异常 */
    FAULT_MAGNETIC_MODULE,      /* 对应12异常大类：磁珠模块异常 */
    FAULT_NEEDLE_R2,            /* 对应13异常大类：启动试剂针异常 */
    FAULT_OPTICAL_MODULE,       /* 对应14异常大类：光学模块异常 */
    FAULT_CUVETTE_MODULE,       /* 对应15异常大类：进杯盘异常 */
    FAULT_LIQ_CIRCUIT_MODULE,   /* 对应18异常大类：液路模块异常 */
    FAULT_COMPLETE_MACHINE,     /* 对应19异常大类：整机仪器异常 */
    FAULT_REAGENT_TABLE,        /* 对应20异常大类：试剂盘异常 */
    FAULT_CONNECT,              /* 对应21异常大类：主控板与子板连接异常 */
    FAULT_TEMPRARTE,            /* 对应23异常大类：温控模块 */

    FAULT_CODE_MAX
}fault_type_t;

typedef struct
{
    fault_type_t type; /* 故障类型 */
    fault_callback hander; /* 故障处理函数： 若返回值大于等于0，则故障已消除 若返回值小于0，则故障仍然存在 */
}fault_node_t;

#define FAULT_CODE_LEN  10   /* 错误码占6+1个字节，为字符串 */

#define MODULE_CLASS_FAULT_SAMPLER              "01"        /* 异常大类：进样器异常 */
#define MOUDLE_FAULT_SAMPLER_SCANNER            "01-001"    /* 常规扫码器通信异常 */
#define MOUDLE_FAULT_REAG_SCANNER               "01-002"    /* 试剂仓扫码器通信异常 */
#define MODULE_FAULT_SAMPLER_NO_PIERCE_BY_HAT   "02-001"    /* 样本戴帽但配置不支持穿刺 */
#define MODULE_FAULT_ELETRO_1_CTL_FAILED        "03-001"    /* 电磁铁1运行异常 */
#define MODULE_FAULT_ELETRO_2_CTL_FAILED        "03-002"    /* 电磁铁2运行异常 */
#define MODULE_FAULT_ELETRO_3_CTL_FAILED        "03-003"    /* 电磁铁3运行异常 */
#define MODULE_FAULT_ELETRO_4_CTL_FAILED        "03-004"    /* 电磁铁4运行异常 */
#define MODULE_FAULT_ELETRO_5_CTL_FAILED        "03-005"    /* 电磁铁5运行异常 */
#define MODULE_FAULT_ELETRO_6_CTL_FAILED        "03-006"    /* 电磁铁6运行异常 */

#define MODULE_CLASS_FAULT_NEEDLE_S             "02"        /* 异常大类：样本针异常 */
#define MODULE_FAULT_NEEDLE_S_X                 "01-001"    /* 样本针X轴运行异常 */
#define MODULE_FAULT_NEEDLE_S_Y                 "02-001"    /* 样本针Y轴运行异常 */
#define MODULE_FAULT_NEEDLE_S_Z                 "03-001"    /* 样本针Z轴运行异常(复位失败/超时) */
#define MODULE_FAULT_NEEDLE_S_Z_COLLIDE         "03-002"    /* 样本针Z轴运行异常(撞针) */
#define MODULE_FAULT_NEEDLE_S_Z_EARG            "03-003"    /* 样本针Z轴探测参数错误 */
#define MODULE_FAULT_NEEDLE_S_MAXSTEP           "03-004"    /* 样本针Z轴探测异常(最大步长) */
#define MODULE_FAULT_NEEDLE_S_STOP              "03-005"    /* 样本针Z轴探测异常(连续3次错误) */
#define MODULE_FAULT_NEEDLE_S_DETECT            "03-006"    /* 样本针Z轴探测异常(空探) */
#define MODULE_FAULT_NEEDLE_S_STOP1             "03-007"    /* 样本针Z轴探测异常(连续3次错误，未使用) */
#define MODULE_FAULT_NEEDLE_S_COMMUNICATE       "03-008"    /* 样本针Z轴探测异常(通信故障) */
#define MODULE_FAULT_NEEDLE_S_XY                "03-009"    /* 样本针XY轴运行异常 */
#define MODULE_FAULT_NEEDLE_S_CHK_CLOT          "03-010"    /* 样本凝块报警 */
#define MODULE_FAULT_NEEDLE_S_CLOT_ERR          "03-011"    /* 样本凝块探测器故障 */
#define MODULE_FAULT_NEEDLE_S_AC_RATIO          "03-012"    /* 样本抗凝比例异常 */
#define MODULE_FAULT_NEEDLE_S_PUMP              "03-013"    /* 样本针柱塞泵运行异常 */
#define MODULE_FAULT_NEEDLE_S_REMAIN            "03-014"    /* 样本针余量探测失败 */
#define MODULE_FAULT_NEEDLE_S_DILU_MAXSTEP      "03-015"    /* 样本针Z轴稀释液探测异常(最大步长) */
#define MODULE_FAULT_NEEDLE_S_DILU_DETECT       "03-016"    /* 样本针Z轴稀释液探测异常(空探) */
#define MODULE_FAULT_NEEDLE_S_DILU_COMM         "03-017"    /* 样本针Z轴稀释液探测异常(通信故障) */
#define MODULE_FAULT_NEEDLE_S_GET_REAGENT       "03-018"    /* 样本针获取试剂仓失败 */

#define MODULE_CLASS_FAULT_C                    "07"        /* 异常大类：抓手异常 */
#define MODULE_FAULT_C_X                        "01-001"    /* 抓手模块X轴运行异常 */
#define MODULE_FAULT_C_Y                        "02-001"    /* 抓手模块Y轴运行异常 */
#define MODULE_FAULT_C_Z                        "03-001"    /* 抓手模块Z轴运行异常 */
#define MODULE_FAULT_C_XY                       "02-002"    /* 抓手模块XY轴运行异常 */
#define MODULE_FAULT_C_PRE_PE                   "03-002"    /* 抓手在常规加样位抓杯异常 */
#define MODULE_FAULT_C_PRE_MIX_PE               "03-003"    /* 抓手在常规混匀位抓杯异常 */
#define MODULE_FAULT_C_INCU_PE                  "03-004"    /* 抓手在孵育位抓杯异常 */
#define MODULE_FAULT_C_OPTI_MIX_PE              "03-005"    /* 抓手在光学混匀位抓杯异常 */
#define MODULE_FAULT_C_OPTI_PE                  "03-006"    /* 抓手在光学检测位抓杯异常 */
#define MODULE_FAULT_C_TRASH_PE                 "03-007"    /* 抓手在垃圾桶放杯异常 */
#define MODULE_FAULT_C_CUVETTE_SUPPLY_PE        "03-008"    /* 抓手在进杯盘抓杯异常 */
#define MODULE_FAULT_C_PRE_PE_R                 "03-009"    /* 抓手在常规加样位放杯异常 */
#define MODULE_FAULT_C_PRE_MIX_PE_R             "03-010"    /* 抓手在常规混匀位放杯异常 */
#define MODULE_FAULT_C_INCU_PE_R                "03-011"    /* 抓手在孵育位放杯异常 */
#define MODULE_FAULT_C_OPTI_MIX_PE_R            "03-012"    /* 抓手在光学混匀位放杯异常 */
#define MODULE_FAULT_C_OPTI_PE_R                "03-013"    /* 抓手在光学检测位放杯异常 */
#define MODULE_FAULT_C_MAG_PE                   "03-014"    /* 抓手在磁珠检测位抓杯异常 */
#define MODULE_FAULT_C_MAG_PE_R                 "03-015"    /* 抓手在磁珠检测位放杯异常 */
#define MODULE_FAULT_C_CONNECT                  "04-001"    /* 电爪通信异常 */

#define MODULE_CLASS_FAULT_MIX                  "10"        /* 异常大类：混匀异常 */
#define MODULE_FAULT_MIX1                       "01-001"    /* 孵育混匀1运行异常 */
#define MODULE_FAULT_MIX2                       "02-001"    /* 孵育混匀2运行异常 */
#define MODULE_FAULT_MIX3                       "03-001"    /* 光学混匀运行异常 */
#define MODULE_FAULT_SPEED_MIX1                 "01-002"    /* 孵育混匀1转速异常 */
#define MODULE_FAULT_SPEED_MIX2                 "02-002"    /* 孵育混匀2转速异常 */
#define MODULE_FAULT_SPEED_MIX3                 "03-002"    /* 光学混匀转速异常 */

#define MODULE_CLASS_FAULT_INCUBATION           "11"        /* 异常大类：孵育模块异常 */
#define MODULE_FAULT_INCUBATION_FULL            "01-001"    /* 孵育模块孵育位满异常 */
#define MODULE_FAULT_INCUBATION_MIX_FULL        "01-002"    /* 孵育模块孵育混匀位满异常 */
#define MODULE_FAULT_INCUBATION_TIMEOUT         "01-003"    /* 孵育模块孵育超时异常 */

#define MODULE_CLASS_FAULT_MAG                  "12"        /* 异常大类：磁珠模块异常 */
#define MODULE_FAULT_MAG_DETECT_1               "01-001"    /* 磁珠检测模块信号异常(通道1) */
#define MODULE_FAULT_MAG_DETECT_2               "01-002"    /* 磁珠检测模块信号异常(通道2) */
#define MODULE_FAULT_MAG_DETECT_3               "01-003"    /* 磁珠检测模块信号异常(通道3) */
#define MODULE_FAULT_MAG_DETECT_4               "01-004"    /* 磁珠检测模块信号异常(通道4) */

#define MODULE_CLASS_FAULT_NEEDLE_R2            "13"        /* 异常大类：启动试剂针异常 */
#define MODULE_FAULT_NEEDLE_R2_Y                "01-001"    /* 启动试剂针Y轴运行异常 */
#define MODULE_FAULT_NEEDLE_R2_Z                "02-001"    /* 启动试剂针Z轴运行异常(复位失败/超时) */
#define MODULE_FAULT_NEEDLE_R2_Z_COLLIDE        "02-002"    /* 启动试剂针Z轴运行异常(撞针) */
#define MODULE_FAULT_NEEDLE_R2_Z_EARG           "02-003"    /* 启动试剂针Z轴探测参数错误 */
#define MODULE_FAULT_NEEDLE_R2_MAXSTEP          "02-004"    /* 启动试剂针Z轴探测异常(最大步长) */
#define MODULE_FAULT_NEEDLE_R2_STOP             "02-005"    /* 启动试剂针Z轴探测异常(连续3次错误) */
#define MODULE_FAULT_NEEDLE_R2_DETECT           "02-006"    /* 启动试剂针Z轴探测异常(空探) */
#define MODULE_FAULT_NEEDLE_R2_STOP1            "02-007"    /* 启动试剂针Z轴探测异常(连续3次错误，未使用) */
#define MODULE_FAULT_NEEDLE_R2_COMMUNICATE      "02-008"    /* 启动试剂针Z轴探测异常(通信故障) */
#define MODULE_FAULT_NEEDLE_R2_TEMP_CTL         "02-009"    /* 启动试剂针加热异常 */
#define MODULE_FAULT_NEEDLE_R2_PUMP             "02-010"    /* 启动试剂针柱塞泵运行异常 */
#define MODULE_FAULT_NEEDLE_R2_REMAIN           "02-011"    /* 启动试剂针余量探测失败 */
#define MODULE_FAULT_NEEDLE_R2_GET_REAGENT      "02-012"    /* 启动试剂针获取试剂仓失败 */

#define MODULE_CLASS_FAULT_OPTICAL              "14"        /* 异常大类：光学模块异常 */
#define MODULE_FAULT_OPTICAL_CHK_AD1            "01-001"    /* 光学检测模块信号异常(通道1) */
#define MODULE_FAULT_OPTICAL_CHK_AD2            "01-002"    /* 光学检测模块信号异常(通道2) */
#define MODULE_FAULT_OPTICAL_CHK_AD3            "01-003"    /* 光学检测模块信号异常(通道3) */
#define MODULE_FAULT_OPTICAL_CHK_AD4            "01-004"    /* 光学检测模块信号异常(通道4) */
#define MODULE_FAULT_OPTICAL_CHK_AD5            "01-005"    /* 光学检测模块信号异常(通道5) */
#define MODULE_FAULT_OPTICAL_CHK_AD6            "01-006"    /* 光学检测模块信号异常(通道6) */
#define MODULE_FAULT_OPTICAL_CHK_AD7            "01-007"    /* 光学检测模块信号异常(通道7) */
#define MODULE_FAULT_OPTICAL_CHK_AD8            "01-008"    /* 光学检测模块信号异常(通道8) */

#define MODULE_CLASS_FAULT_CUVETTE_SUPPLY       "15"        /* 异常大类：进杯盘异常 */
#define MODULE_FAULT_CUVETTE_PULL_OUT           "01-001"    /* 反应杯盘被拉出 */
#define MODULE_FAULT_CUVETTE_LOW                "01-002"    /* 反应杯不足(加样停) */
#define MODULE_FAULT_BUCKLE_ERROR               "01-003"    /* 杯盘卡扣未压紧 */
#define MODULE_FAULT_CUVETTE_LOW1               "01-004"    /* 无反应杯可用(进杯失败，已废弃) */
#define MODULE_FAULT_CUVETTE_LOW2               "01-005"    /* 无反应杯可用(卡扣未扣紧，已废弃) */

#define MODULE_CLASS_FAULT_PUMP                 "18"        /* 异常大类：液路模块异常 */
#define MODULE_FAULT_OVERFLOWBOT_FULL           "01-001"    /* 溢流瓶已满 */
#define MODULE_FAULT_WASTE_HIGH                 "01-002"    /* 废液桶已满(液位高) */
#define MODULE_FAULT_NORMAL_CLEAN_LESS          "01-003"    /* 普通清洗液B余量不足(浮子开关触发) */
#define MODULE_FAULT_SPECIAL_CLEAR_LESS         "01-004"    /* 特殊清洗液余量不足(检测到气泡) */
#define MODULE_FAULT_NOR_CLEAN_EMPTY            "01-005"    /* 普通清洗液不可用(检测到气泡) */
#define MODULE_FAULT_SPE_CLEAN_EMPTY            "01-006"    /* 未检测到特殊清洗液瓶 */
#define MODULE_FAULT_CLEARER_PUMP_TIMEOUT       "01-007"    /* 5ml柱塞泵运行异常(超时) */

#define MODULE_CLASS_FAULT_MACHINE              "19"        /* 异常大类：整机仪器异常(主控板与子板连接异常) */
#define MODULE_FAULT_SAMPLER_CONNECT            "01-001"    /* 主控板与进样器模块连接错误 */
#define MODULE_FAULT_TEMP_CONNECT               "01-002"    /* 主控板与温控模块连接错误 */
#define MODULE_FAULT_MAG_CONNECT                "01-003"    /* 主控板与磁珠检测块连接错误 */
#define MODULE_FAULT_OPTICAL_CONNECT            "01-004"    /* 主控板与光学检测模块连接错误 */
#define MODULE_FAULT_LQ_S_CONNECT               "01-005"    /* 主控板与样本针液面探测模块连接错误 */
#define MODULE_FAULT_LQ_R2_CONNECT              "01-006"    /* 主控板与启动试剂针液面探测模块连接错误 */
#define MODULE_FAULT_A9_RTOS_CONNECT            "01-007"    /* 主控板RTOS连接错误 */
#define MODULE_FAULT_PE_WASTE_FULL              "02-001"    /* 垃圾桶满料 */
#define MODULE_FAULT_PE_WASTE_IS_OUT            "02-002"    /* 运行期间垃圾桶被拉出 */

#define MODULE_CLASS_FAULT_REAGENT_TABLE        "20"        /* 异常大类：试剂仓异常 */
#define MODULE_FAULT_REAGENT_TABLE              "01-001"    /* 试剂仓电机运动异常 */
#define MODULE_FAULT_REAGENT_MIX_MOTOR          "01-002"    /* 试剂仓混匀电机运动异常 */
#define MODULT_FAULT_REAGENT_BT_NOT_MATCH       "02-007"    /* 瓶型不匹配 */
#define MOUDLE_FAULT_REAGENT_SCANNER            "02-008"    /* 试剂仓扫码器通信异常 */
#define MOUDLE_FAULT_SCANNER_FAILD              "02-009"    /* 试剂仓扫码失败 */
#define MODULE_FAULT_FAN2                       "03-001"    /* 试剂仓散热风扇1异常 */
#define MODULE_FAULT_FAN3                       "03-002"    /* 试剂仓散热风扇2异常 */
#define MODULE_FAULT_REAGENT_GATE_OPENED        "04-001"    /* 运行期间试剂仓盖打开 */
#define MODULE_FAULT_REAGENT_GATE_MONITOR       "04-002"    /* 制冷期间试剂仓盖打开 */

#define MODULE_CLASS_FAULT_TEMPERATE            "21"        /* 异常大类：温控异常 */
#define MODULE_FAULT_FAN0                       "01-001"    /* 检测室散热风扇1异常 */
#define MODULE_FAULT_FAN1                       "01-002"    /* 检测室散热风扇2异常 */

typedef enum {
    MODULE_CMD_STOP,
    MODULE_CMD_START,
}module_start_cmd_t;

typedef enum {
    IND_LED_BLINK_ON,
    IND_LED_BLINK_OFF,
}ind_led_blink_t;        /* LED闪烁状态 */

typedef struct {
    ind_led_blink_t machine_stat_blink;
    ind_led_blink_t cuvette_blink;
    int machine_stat_color;
    int cuvette_color;
}ind_led_blink_stat_t;  /* 仪器状态指示灯显示 */

typedef enum {
    SOUND_OFF,
    SOUND_ON,
}alarm_sound_open_t;        /* 蜂鸣器开关 */

typedef enum {
    SOUND_TYPE_0 = 0, /* 无效 */
    SOUND_TYPE_1 = 1, /* 低频 */
    SOUND_TYPE_2 = 2, /* 中频 */
    SOUND_TYPE_3 = 3, /* 高频 */
}alarm_sound_mode_t;

typedef struct {
    alarm_sound_open_t open;
    alarm_sound_mode_t mode;
    long long start_time;
}alarm_sound_stat_t;

typedef struct {
    int gate_io;
    int reag_io;
    int waste_io;
    int reag_monitor;
    int reag_time;
}ins_enable_io_t;

void ins_io_set(int gate_io, int reag_io, int waste_io, int reag_monitor, int reag_time);
ins_enable_io_t ins_io_get();
struct timeval *get_module_base_time(void);
void clear_module_base_time();
int module_fault_stat_clear(void);
module_fault_stat_t module_fault_stat_get(void);
int fault_deal_add(fault_type_t fault_type, int priority, void *data);
int module_fault_stat_set(module_fault_stat_t stat);
void module_monitor_start(void *arg);
void set_machine_stat(machine_stat_t stat);
machine_stat_t get_machine_stat(void);
void set_detect_period_flag(int flag);
int wait_detect_period_finish(void);
int module_monitor_init(void);
int module_monitor_wait(void);
void module_start_control(module_start_cmd_t module_ctl_cmd);
int module_start_stat_get(void);
void module_sampler_add_set(sampler_stat_t stat);
sampler_stat_t module_sampler_add_get(void);
void machine_led_set(int led_id, int color, int blink_ctl);
void set_alarm_mode(int open, int mode);
int fault_code_generate(char *return_fault_code, char *main_fault_code, char *sub_fault_code);
void auto_cal_stop_flag_set(int v);
int auto_cal_stop_flag_get(void);

/*
捕获 应该处理的故障（自身或其它部件）
注意: FAULT_CHECK_START()和FAULT_CHECK_END() 必须成对使用
*/
#define FAULT_CHECK_START(fault_level) \
    if (module_fault_stat_get() & (fault_level)) {\
        /*LOG("has fault:0x%04x\n", module_fault_stat_get() & (fault_level));\*/ \
    } else {

    /* 处理 自身部件的故障 */
#define FAULT_CHECK_DEAL(fault_code, fault_level, data) \
    do {\
        LOG("fault_code = %d, data = %d, %s, %d.\n", (int)fault_code, (int)data, __func__, __LINE__);\
        if (fault_deal_add((fault_code), 0, (data)) < 0) {\
            module_fault_stat_set((module_fault_stat_t)(module_fault_stat_get() | (fault_level)));\
         }\
    } while (0)

    /* 本次故障处理 结束 */
#define FAULT_CHECK_END() \
    }

#ifdef __cplusplus
}
#endif

#endif

