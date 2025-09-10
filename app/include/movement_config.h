#ifndef H5000_MOTOR_CONFIG_H
#define H5000_MOTOR_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 电机属性 */
#define MAX_MOTOR_NUM           30  /* 支持电机数量 */
#define MOTOR_MOVE_POS_NUM      5   /* 电机支持设置的位置数，这里可以使用变速 */
#define POS_MOTOR_UNIQ          0   /* 电机默认速度存储位置 */
#define MOTOR_V0_DEF_SPEED      100 /* 电机计算同时到达时默认初速度 */

#define MOTOR_DEFAULT_TIMEOUT   20000

/* 样本针S */
#define MOTOR_NEEDLE_S_X        11
#define MOTOR_NEEDLE_S_Y        12
#define MOTOR_NEEDLE_S_Z        14

/* 试剂针R2 */
#define MOTOR_NEEDLE_R2_Y       13
#define MOTOR_NEEDLE_R2_Z       15

/* 抓手 */
#define MOTOR_CATCHER_X          21
#define MOTOR_CATCHER_Y          22
#define MOTOR_CATCHER_Z          24

/* 备用电爪 */
#define MOTOR_CATCHER_MOTOR     25

/* 孵育混匀 & 光学混匀 */
#define MOTOR_MIX_1             16
#define MOTOR_MIX_2             17
#define MOTOR_MIX_3             18
/* 试剂仓电机 */
#define MOTOR_REAGENT_TABLE     23

/* 电机运动码 */
#define CMD_MOTOR_RST           0
#define CMD_MOTOR_MOVE          CMD_MOTOR_MOVE_STEP
#define CMD_MOTOR_MOVE_STEP     1
#define CMD_MOTOR_MOVE_SPEED    2
#define CMD_MOTOR_STOP          3
#define CMD_MOTOR_LIQ_DETECT    4
#define CMD_MOTOR_DUAL_MOVE     5
#define CMD_MOTOR_GROUP_RESET   6
#define CMD_MOTOR_RST_STEP      7

/* 柱塞泵光电 */
#define PE_RST_S_PUMP           1000    /* 样本针柱塞泵光电 */
#define PE_RST_R2_PUMP          1001    /* 试剂针柱塞泵光电 */
#define PE_RST_PERF_PUMP        1002    /* 液路5ml柱塞泵光电 */

/* 混匀光电 */
#define PE_MIX1                 1003    /* 混匀光电1 */
#define PE_MIX2                 1004    /* 混匀光电2 */
#define PE_MIX3                 1005    /* 混匀光电3 */

/* 试剂仓 */
#define PE_RST_REAG_TABLE       1006    /* 试剂仓复位光电 */
#define PE_REGENT_TABLE_GATE    1007    /* 试剂仓盖光电 */

/* 抓手复位光电 */
#define PE_GRAP_X               1010    /* 抓手X复位光电 */
#define PE_GRAP_Y               1011    /* 抓手Y复位光电 */
#define PE_GRAP_Z               1012    /* 抓手Z复位光电 */

#define PE_REACT_CUP_CHECK      1013    /* 反应杯检测光电 */

/* 样本/试剂针复位光电 */
#define PE_NEEDLE_R2_Y          1014    /* 试剂针Y复位光电 */
#define PE_NEEDLE_R2_Z          1015    /* 试剂针Z复位光电 */
#define PE_NEEDLE_S_X           1016    /* 样本针X复位光电 */
#define PE_NEEDLE_S_Y           1017    /* 样本针Y复位光电 */
#define PE_NEEDLE_S_Z           1018    /* 样本针Z复位光电 */

/* 反应杯盘 */
#define MICRO_SWITCH_BUCKLE     1019    /* 反应杯盘卡扣微动开关 */
#define MICRO_GATE_CUVETTE      1020    /* 反应杯盘到位行程开关 */

/* 稀释瓶 */
#define PE_DILU_1               1021    /* 稀释瓶1到位光电 */
#define PE_DILU_2               1022    /* 稀释瓶2到位光电 */

#define PE_UP_CAP               1024    /* 上翻盖 */

/* 垃圾桶 */
#define PE_WASTE_FULL           1026    /* 垃圾桶满 */
#define PE_WASTE_CHECK          1027    /* 垃圾桶有无 */

/* 液路模块传感器 */
#define SPCL_CLEAR_IDX          1028    /* 特殊清洗液微动开关 */
#define OVERFLOW_BOT_IDX        1029    /* 溢流瓶浮子 */
#define BUBBLESENSOR_SPCL_IDX   1030    /* 特殊清洗液气泡传感器 */
#define BUBBLESENSOR_NORM_IDX   1033    /* 普通清洗液气泡传感器 */
#define WASTESENSOR_IDX         1035    /* 废液桶浮子 */
#define WASHSENSOR_IDX          1036    /* 普通清洗液浮子 */

/* 直流电机 */
#define BLDC_IDX_START          2931
#define BLDC_IDX_RSV            2932
#define BLDC_IDX_OUT            2933
#define BLDC_IDX_IN             2934
#define BLDC_IDX_MOVE           2935
#define BLDC_IDX_STOP           2936
#define BLDC_TEST               2937    /* 杯盘进杯 */
#define BLDC_FORWARD_TEST       2938    /* 进杯盘电机正转 */
#define BLDC_REVERSE_TEST       2939    /* 进杯盘电机反转 */
#define BLDC_STOP_TEST          2940    /* 进杯盘电机停转 */
#define BLDC_TEST_MAX           2950    /* watch out */

/* 阀 */
#define VALVE_SV1               2001  /* 样本针柱塞泵填充阀SV 1      */
#define VALVE_SV2               2002  /* 样本针柱塞泵排气泡阀SV 2 */
#define VALVE_SV3               2003  /* 试剂针柱塞泵阀SV 3 */
#define VALVE_SV4               2004  /* 特殊清洗液柱塞泵阀SV 4 */
#define VALVE_SV5               2005  /* 特殊清洗液进液阀SV 5 */
#define VALVE_SV6               2006  /* 试剂仓排冷凝水阀SV 6 */
#define VALVE_SV7               2007  /* 拭子清洗液阀SV 7 */
#define VALVE_SV8               2008  /* 特殊清洗液管路阀SV 8 */
#define VALVE_SV9               2009  /* 暂存池特殊清洗液阀SV 9 */
#define VALVE_SV10              2010  /* 洗针池特殊清洗液阀SV10 */
#define VALVE_SV11              2011  /* 暂存池排废阀SV11 */
#define VALVE_SV12              2012  /* 洗针池排废阀SV12 */

#define DIAPHRAGM_PUMP_Q1       2013  /* 样本针管路填充泵Q1 */
#define DIAPHRAGM_PUMP_Q2       2014  /* 试剂针管路填充泵Q2 */
#define DIAPHRAGM_PUMP_Q3       2015  /* 暂存池清洗泵Q3 */
#define DIAPHRAGM_PUMP_Q4       2016  /* 洗针池清洗泵Q4 */
#define DIAPHRAGM_PUMP_F1       2017  /* 拭子排废泵F1 */
#define DIAPHRAGM_PUMP_F2       2018  /* 暂存池排废泵F2 */
#define DIAPHRAGM_PUMP_F3       2019  /* 洗针池排废泵F3 */
#define DIAPHRAGM_PUMP_F4       2020  /* 拭子排废泵2F4 */

/* 泵 */
#define MOTOR_NEEDLE_S_PUMP     26  /* 样本针柱塞泵M2(500ul) */
#define MOTOR_NEEDLE_R2_PUMP    27  /* 试剂针柱塞泵M3(250ul) */
#define MOTOR_CLEARER_PUMP      28  /* 特殊清洗液柱塞泵M1(5ml) */

/* 蜂鸣器 */
#define VALVE_BUZZER            2025    /* 24V蜂鸣器 */
/* LED */
#define LED_CUVETTE_IN_R        2037    /* 内杯盘拉出指示LED 红色 */
#define LED_CUVETTE_IN_G        2038    /* 内杯盘拉出指示LED 绿色 */
#define LED_CUVETTE_IN_Y        2039    /* 内杯盘拉出指示LED 黄色 */
#define LED_CTL_STATUS_R        2042    /* 状态指示灯 红色 */
#define LED_CTL_STATUS_G        2041    /* 状态指示灯 绿色 */
#define LED_CTL_STATUS_Y        2040    /* 状态指示灯 黄色 */

#define LED_CTL_STATUS_BREATH_Y 2048    /* 状态指示灯(呼吸模式) 黄色; 0:关闭 1：开启 */
#define LED_CTL_STATUS_BREATH_G 2049    /* 状态指示灯(呼吸模式) 绿色; 0:关闭 1：开启 */
#define LED_CTL_STATUS_BREATH_R 2050    /* 状态指示灯(呼吸模式) 红色; 0:关闭 1：开启 */

#define CPLD_LED_ON             0
#define CPLD_LED_OFF            1

/* 进样器IO 电磁铁 、LED等 */
#define SAMPLER_PE_START                3001

#define TEMP_ELE_CTL_START              SAMPLER_PE_START
#define TEMP_ELE_CTL_CHANNEL1           (TEMP_ELE_CTL_START+0) /* 电磁铁通道1 */
#define TEMP_ELE_CTL_CHANNEL2           (TEMP_ELE_CTL_START+1) /* 电磁铁通道2 */
#define TEMP_ELE_CTL_CHANNEL3           (TEMP_ELE_CTL_START+2) /* 电磁铁通道3 */
#define TEMP_ELE_CTL_CHANNEL4           (TEMP_ELE_CTL_START+3) /* 电磁铁通道4 */
#define TEMP_ELE_CTL_CHANNEL5           (TEMP_ELE_CTL_START+4) /* 电磁铁通道5 */
#define TEMP_ELE_CTL_CHANNEL6           (TEMP_ELE_CTL_START+5) /* 电磁铁通道6 */
#define TEMP_ELE_CTL_CHANNEL_END        3007

#define TEMP_LED_STAT_START             3008
#define TEMP_LED_STAT_CHANNEL1          (TEMP_LED_STAT_START+0) /* LED通道1 */
#define TEMP_LED_STAT_CHANNEL2          (TEMP_LED_STAT_START+1) /* LED通道2 */
#define TEMP_LED_STAT_CHANNEL3          (TEMP_LED_STAT_START+2) /* LED通道3 */
#define TEMP_LED_STAT_CHANNEL4          (TEMP_LED_STAT_START+3) /* LED通道4 */
#define TEMP_LED_STAT_CHANNEL5          (TEMP_LED_STAT_START+4) /* LED通道5 */
#define TEMP_LED_STAT_CHANNEL6          (TEMP_LED_STAT_START+5) /* LED通道6 */
#define TEMP_LED_STAT_CHANNEL_END       3014

#define TEMP_KEY_REAG_LED_STATE         3015    /* 试剂仓按键指示灯 */
#define TEMP_KEY_REAG_BUTTON_SIGNAL     3016    /* 试剂仓按键获取 */
#define TEMP_CLOT_PRESSUARE_DIFF_VALUE  3018    /* 凝块压差值获取 */
#define TEMP_SAMPLE_TUBE_HAT_CHECK      3019    /* 管帽光电获取 */
#define TEMP_SAMPLE_TUBE_POS_CHECK      3050    /* 位置号光电获取 */

#define SAMPLER_IO_INDEX_BASE           (3201)
#define SAMPLER_TRASH_FULL_LED          SAMPLER_IO_INDEX_BASE    /* 废料桶满料光电 */

/* 复用IO编号(service调试使用) */
/* 混匀电机的光电 */
#define MOTOR_MIX_CIRCLE_START  4001
#define MOTOR_MIX_CIRCLE_MIX1  (MOTOR_MIX_CIRCLE_START+0) /* 孵育混匀光电计数1 */
#define MOTOR_MIX_CIRCLE_MIX2  (MOTOR_MIX_CIRCLE_START+1) /* 孵育混匀光电计数2 */
#define MOTOR_MIX_CIRCLE_MIX3  (MOTOR_MIX_CIRCLE_START+2) /* 光学混匀光电计数1 */
#define MOTOR_MIX_CIRCLE_END    4010

/* 温控使能 */
#define TEMPERATURE_CTL_ENABLE_START  4011
#define TEMPERATURE_CTL_ENABLE_R2          (TEMPERATURE_CTL_ENABLE_START+0) /* 温控使能.试剂针2 */
#define TEMPERATURE_CTL_ENABLE_MAGNETIC    (TEMPERATURE_CTL_ENABLE_START+1) /* 温控使能.磁珠 */
#define TEMPERATURE_CTL_ENABLE_OPTCAL1     (TEMPERATURE_CTL_ENABLE_START+2) /* 温控使能.光学检测1 */
#define TEMPERATURE_CTL_ENABLE_OPTCAL2     (TEMPERATURE_CTL_ENABLE_START+3) /* 温控使能.光学检测2 */
#define TEMPERATURE_CTL_ENABLE_REAGENTSCAN (TEMPERATURE_CTL_ENABLE_START+4) /* 温控使能.试剂仓(玻璃片) */
#define TEMPERATURE_CTL_ENABLE_INCLUATION  (TEMPERATURE_CTL_ENABLE_START+5) /* 温控使能.孵育位 */
#define TEMPERATURE_CTL_ENABLE_REAGENTCOOL (TEMPERATURE_CTL_ENABLE_START+6) /* 温控使能.试剂仓(帕尔贴) */
#define TEMPERATURE_CTL_ENABLE_END    4020

/* 温控目标温度 */
#define TEMPERATURE_CTL_GOAL_START  4021
#define TEMPERATURE_CTL_GOAL_R2          (TEMPERATURE_CTL_GOAL_START+0) /* 温控目标温度.试剂针2 */
#define TEMPERATURE_CTL_GOAL_MAGNETIC    (TEMPERATURE_CTL_GOAL_START+1) /* 温控目标温度.磁珠 */
#define TEMPERATURE_CTL_GOAL_OPTCAL1     (TEMPERATURE_CTL_GOAL_START+2) /* 温控目标温度.光学检测1 */
#define TEMPERATURE_CTL_GOAL_OPTCAL2     (TEMPERATURE_CTL_GOAL_START+3) /* 温控目标温度.光学检测2 */
#define TEMPERATURE_CTL_GOAL_REAGENTSCAN (TEMPERATURE_CTL_GOAL_START+4) /* 温控目标温度.试剂仓(玻璃片) */
#define TEMPERATURE_CTL_GOAL_INCLUATION  (TEMPERATURE_CTL_GOAL_START+5) /* 温控目标温度.孵育位 */
#define TEMPERATURE_CTL_GOAL_REAGENTCOOL (TEMPERATURE_CTL_GOAL_START+6) /* 温控目标温度.试剂仓(帕尔贴) */
#define TEMPERATURE_CTL_GOAL_END  4030

/* 磁珠驱动力 */
#define MAG_DRIVER_LEVEL_START  4031
#define MAG_DRIVER_LEVEL_NORMAL  (MAG_DRIVER_LEVEL_START+0) /* 磁珠驱动力.正常 */
#define MAG_DRIVER_LEVEL_WEAK    (MAG_DRIVER_LEVEL_START+1) /* 磁珠驱动力.弱 */
#define MAG_DRIVER_LEVEL_STRONG  (MAG_DRIVER_LEVEL_START+2) /* 磁珠驱动力.强 */
#define MAG_DRIVER_LEVEL_END  4035

/* 磁珠周期 */
#define MAG_PERIOD_LEVEL_START  4036
#define MAG_PERIOD_LEVEL_SET    (MAG_PERIOD_LEVEL_START+0) /* 磁珠周期 */
#define MAG_PERIOD_LEVEL_END  4040

/* 温控板GPIO */
#define TEMPERATURE_CTL_GPIO_START  4041
#define TEMPERATURE_CTL_GPIO_COLD_WATOR         (TEMPERATURE_CTL_GPIO_START+0) /* 隔膜泵（冷凝水）开关 */
#define TEMPERATURE_CTL_GPIO_NOR_CLEAN_FILL_R2  (TEMPERATURE_CTL_GPIO_START+1) /* 普通清洗液缓存瓶R2压力开关 */
#define TEMPERATURE_CTL_GPIO_NOR_CLEAN_PUSH_R2  (TEMPERATURE_CTL_GPIO_START+2) /* 普通清洗液缓存瓶R2灌液开关 */
#define TEMPERATURE_CTL_GPIO_END  4050

/* 磁珠信号值 */
#define MAG_DATA_START  4051
#define MAG_DATA_1  (MAG_DATA_START+0) /* 磁珠信号值.检测位1 */
#define MAG_DATA_2  (MAG_DATA_START+1) /* 磁珠信号值.检测位2 */
#define MAG_DATA_3  (MAG_DATA_START+2) /* 磁珠信号值.检测位3 */
#define MAG_DATA_4  (MAG_DATA_START+3) /* 磁珠信号值.检测位4 */
#define MAG_DATA_5  (MAG_DATA_START+4) /* 磁珠信号值.检测位5 */
#define MAG_DATA_6  (MAG_DATA_START+5) /* 磁珠信号值.检测位6 */
#define MAG_DATA_END  4060

/* 光学信号值 */
#define OPTICAL_DATA_START  4061
#define OPTICAL_DATA_1  (OPTICAL_DATA_START+0) /* 光学信号值.检测位1 */
#define OPTICAL_DATA_2  (OPTICAL_DATA_START+1) /* 光学信号值.检测位2 */
#define OPTICAL_DATA_3  (OPTICAL_DATA_START+2) /* 光学信号值.检测位3 */
#define OPTICAL_DATA_4  (OPTICAL_DATA_START+3) /* 光学信号值.检测位4 */
#define OPTICAL_DATA_5  (OPTICAL_DATA_START+4) /* 光学信号值.检测位5 */
#define OPTICAL_DATA_6  (OPTICAL_DATA_START+5) /* 光学信号值.检测位6 */
#define OPTICAL_DATA_7  (OPTICAL_DATA_START+6) /* 光学信号值.检测位7 */
#define OPTICAL_DATA_8  (OPTICAL_DATA_START+7) /* 光学信号值.检测位8 */
#define OPTICAL_DATA_9  (OPTICAL_DATA_START+8) /* 光学信号值.检测位9 */
#define OPTICAL_DATA_10  (OPTICAL_DATA_START+9) /* 光学信号值.检测位10 */
#define OPTICAL_DATA_11  (OPTICAL_DATA_START+10) /* 光学信号值.检测位11 */
#define OPTICAL_DATA_12  (OPTICAL_DATA_START+11) /* 光学信号值.检测位12 */
#define OPTICAL_DATA_13  (OPTICAL_DATA_START+12) /* 光学信号值.检测位13 */
#define OPTICAL_DATA_14  (OPTICAL_DATA_START+13) /* 光学信号值.检测位14 */
#define OPTICAL_DATA_15  (OPTICAL_DATA_START+14) /* 光学信号值.检测位15 */
#define OPTICAL_DATA_16  (OPTICAL_DATA_START+15) /* 光学信号值.检测位16 */
#define OPTICAL_DATA_END  4080

/* 清洗液液位 */
#define CLEAN_LIQUID_LEVEL_START  4081
#define CLEAN_LIQUID_LEVEL_SPECIAL  (CLEAN_LIQUID_LEVEL_START+0) /* 清洗液A(特殊清洗液桶). 0:低 1:中 2:高 */
#define CLEAN_LIQUID_LEVEL_NORMAL   (CLEAN_LIQUID_LEVEL_START+1) /* 清洗液B(普通清洗液桶). 0:低 1:中 2:高 */
#define CLEAN_LIQUID_LEVEL_WASTE    (CLEAN_LIQUID_LEVEL_START+2) /* 废液桶. 0:低 1:中 2:高 */
#define CLEAN_LIQUID_LEVEL_END  4090

/* 温度传感器校准 */
#define TEMP_SENSOR_CALI_START  4091
#define TEMP_SENSOR_CALI_ENV            (TEMP_SENSOR_CALI_START+0) /* 环境温度,放大1000倍 */
#define TEMP_SENSOR_CALI_REAGENTCOOL    (TEMP_SENSOR_CALI_START+1) /* 试剂仓(帕尔贴),放大1000倍 */
#define TEMP_SENSOR_CALI_INCLUATION     (TEMP_SENSOR_CALI_START+2) /* 孵育仓,放大1000倍 */
#define TEMP_SENSOR_CALI_REAGENTSCAN    (TEMP_SENSOR_CALI_START+3) /* 试剂仓(玻璃片),放大1000倍 */
#define TEMP_SENSOR_CALI_OPTCAL1        (TEMP_SENSOR_CALI_START+4) /* 光学检测位1,放大1000倍 */
#define TEMP_SENSOR_CALI_R2             (TEMP_SENSOR_CALI_START+5) /* 试剂针2,放大1000倍 */
#define TEMP_SENSOR_CALI_OPTCAL2        (TEMP_SENSOR_CALI_START+6) /* 光学检测位2,放大1000倍 */
#define TEMP_SENSOR_CALI_MAGNETIC       (TEMP_SENSOR_CALI_START+7) /* 磁珠检测位,放大1000倍 */
#define TEMP_SENSOR_CALI_R2_NEW         (TEMP_SENSOR_CALI_START+8) /* 保留,放大1000倍 */
#define TEMP_SENSOR_CALI_RERVER1        (TEMP_SENSOR_CALI_START+9) /* 保留,放大1000倍 */
#define TEMP_SENSOR_CALI_RERVER2        (TEMP_SENSOR_CALI_START+10) /* 保留,放大1000倍 */
#define TEMP_SENSOR_CALI_RERVER3        (TEMP_SENSOR_CALI_START+11) /* 保留,放大1000倍 */
#define TEMP_SENSOR_CALI_END  4102

/* 温度类型 */
#define TEMP_SENSOR_TYPE_START 4103
#define TEMP_SENSOR_TYPE_1        (TEMP_SENSOR_TYPE_START+0) /* 温度类型,放大1000倍 */
#define TEMP_SENSOR_TYPE_END 4104

/* 目标温度校准 */
#define TEMP_GOAL_CALI_START  4105
#define TEMP_GOAL_CALI_R2               (TEMP_SENSOR_CALI_START+0) /* 试剂针2,放大1000倍 */
#define TEMP_GOAL_CALI_MAGNETIC         (TEMP_SENSOR_CALI_START+1) /* 磁珠检测位,放大1000倍 */
#define TEMP_GOAL_CALI_OPTCAL1          (TEMP_SENSOR_CALI_START+2) /* 光学检测位1,放大1000倍 */
#define TEMP_GOAL_CALI_OPTCAL2          (TEMP_SENSOR_CALI_START+3) /* 光学检测位2,放大1000倍 */
#define TEMP_GOAL_CALI_REAGENTSCAN      (TEMP_SENSOR_CALI_START+4) /* 试剂仓(玻璃片),放大1000倍 */
#define TEMP_GOAL_CALI_INCLUATION       (TEMP_SENSOR_CALI_START+5) /* 孵育仓,放大1000倍 */
#define TEMP_GOAL_CALI_REAGENTCOOL      (TEMP_SENSOR_CALI_START+6) /* 试剂仓(帕尔贴),放大1000倍 */
#define TEMP_GOAL_CALI_RERVER1          (TEMP_SENSOR_CALI_START+7) /* 保留,放大1000倍 */
#define TEMP_GOAL_CALI_RERVER2          (TEMP_SENSOR_CALI_START+8) /* 保留,放大1000倍 */
#define TEMP_GOAL_CALI_RERVER3          (TEMP_SENSOR_CALI_START+9) /* 保留,放大1000倍 */
#define TEMP_GOAL_CALI_END  4118

/* 目标温度模式 */
#define TEMP_GOAL_MODE_START 4119
#define TEMP_GOAL_MODE_1        (TEMP_GOAL_MODE_START+0) /* 目标温度模式 */
#define TEMP_GOAL_MODE_END 4120

/* 启动光学LED校准 */
#define OPTICAL_CURR_CALC_START 4121
#define OPTICAL_CURR_CALC_1             (OPTICAL_CURR_CALC_START+0) /* 光学LED电流校准 */
#define OPTICAL_CURR_CALC_END 4122

/* 获取光学LED校准值 */
#define OPTICAL_CURR_CALC_WAVE_START 4123
#define OPTICAL_CURR_CALC_WAVE_1        (OPTICAL_CURR_CALC_WAVE_START+0) /* 波长340 */
#define OPTICAL_CURR_CALC_WAVE_2        (OPTICAL_CURR_CALC_WAVE_START+1) /* 波长660 */
#define OPTICAL_CURR_CALC_WAVE_3        (OPTICAL_CURR_CALC_WAVE_START+2) /* 波长810 */
#define OPTICAL_CURR_CALC_WAVE_4        (OPTICAL_CURR_CALC_WAVE_START+3) /* 波长570 */
#define OPTICAL_CURR_CALC_WAVE_5        (OPTICAL_CURR_CALC_WAVE_START+4) /* 波长405 */
#define OPTICAL_CURR_CALC_WAVE_END 4129

/* 操作仪器动作计数文件 */
#define DEVICE_STATUS_COUNT_START 4130
#define DEVICE_STATUS_COUNT_RESET       (DEVICE_STATUS_COUNT_START+0) /* 重置仪器动作计数文件 */
#define DEVICE_STATUS_COUNT_END 4130

/* AUTO CAL */
#define AUTO_CAL_IDX            4150
#define AUTO_CAL_ALL            4151

/* 抓手控制模拟IO */
#define CATCHER_SIM_IO          4999
/* 备用抓手控制模拟IO */
#define CATCHER_ENCODER_SIM_IO  4899
#define CATCHER_ENCODER_ID      4

#define SERVICE_DEBUG_GPIO_END 5000

typedef struct motor_time_sync_attr {
    int v0_speed;
    int vmax_speed;
    int speed;
    int max_acc;
    int acc;
    int step;
}motor_time_sync_attr_t;

typedef struct motors_speed_attr {
    int speed[MOTOR_MOVE_POS_NUM];
    int acc_speed[MOTOR_MOVE_POS_NUM];
}motors_speed_attr_t;

typedef struct motors_step_attr {
    int motor_steps[MOTOR_MOVE_POS_NUM];
}motors_step_attr_t;

void set_power_off_stat(int stat);
int get_power_off_stat();
int all_motor_power_clt(uint8_t enable);
int motors_step_attr_init(void);
void value_set_control(int flag);
int value_get_control();
int valve_set(unsigned short index, unsigned char status);
int motor_move_async(char motor_id, char cmd, int step, int speed);
int motor_move_ctl_async(char motor_id, char cmd, int step, int speed, int acc);
int motor_move_dual_ctl_async(char motor_id, char cmd, int step_x, int step_y, int speed, int acc, double cost_time);
int motors_move_timewait(unsigned char motor_ids[], int motor_count, long msecs);
int motor_move_sync(char motor_id, char cmd, int step, int speed, int timeout);
int motor_move_ctl_sync(char motor_id, char cmd, int step, int speed, int acc, int timeout);
int motor_move_dual_ctl_sync(char motor_id, char cmd, int step_x, int step_y, int speed, int acc, int timeout, double cost_time);
int reset_all_motors(void);
int calc_motor_move_in_time(const motor_time_sync_attr_t *motor_attr, double move_time_s);
int reset_remain_detect_motors(void);
int motor_slow_step_reset_sync(char motor_id, int slow_step, int speed, int acc, int timeout);
int motor_slow_step_reset_async(char motor_id, int slow_step, int speed, int acc);

#ifdef __cplusplus
}
#endif

#endif
