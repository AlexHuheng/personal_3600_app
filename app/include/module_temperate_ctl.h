#ifndef __MODULE_TEMPERATE_CTL_H__
#define __MODULE_TEMPERATE_CTL_H__

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
#include "module_upgrade.h"
#include "h3600_maintain_utils.h"

/* 通信异常上报计数 */
#define TEMP_ERROR_COUNT_MAX     5

enum THRIFT_TEMPERATURE_SENSOR /* from  Defs_types.h*/
{
    THRIFT_REAGENTCASE = 1, /* 试剂仓仓体 */
    THRIFT_REAGENTPIPE = 2, /* 试剂针2 */
    THRIFT_INCUBATIONAREA = 3, /* 孵育池 */
    THRIFT_MAGNETICBEAD = 4, /* 磁珠池 */
    THRIFT_ENVIRONMENTAREA = 5, /* 仪器内部环境温度 */
    THRIFT_REAGENTCASEGLASS = 6, /* 试剂仓玻璃片 */
    THRIFT_OPTICAL1 = 7, /* 光学池1 */
    THRIFT_OPTICAL2 = 8 /* 光学池2 */
};

typedef enum
{
    TEMP_NEEDLE_R2,             /* 试剂针2 */
    TEMP_MAGNETIC,              /* 磁珠 */
    TEMP_OPTICAL1,              /* 光学检测1 */
    TEMP_OPTICAL2,              /* 光学检测2 */
    TEMP_REAGENT_SCAN,          /* 试剂仓(玻璃片) */
    TEMP_INCUBATION,            /* 孵育位    */
    TEMP_REAGENT_CONTAINER1,     /* 试剂仓(帕尔贴1) */
    TEMP_REAGENT_CONTAINER2,     /* 试剂仓(帕尔贴2) */

    TEMP_MAX,
}temperate_target_t;            /* 温控目标对象 */

/* 风扇PWM索引 */
typedef enum
{
    FAN_PWM_0 = 0,  /* 检测室散热风扇1 */
    FAN_PWM_1,      /* 检测室散热风扇2 */
    FAN_PWM_2,      /* 帕尔贴散热风扇1 */
    FAN_PWM_3,      /* 帕尔贴散热风扇2 */
    FAN_PWM_MAX
}fan_pwm_index_t;

/* 风扇异常标志位 0~3路 */
#define FUN_ERROR_FLAG_BASE (0x01)
#define FUN_ERROR_FLAG0 (FUN_ERROR_FLAG_BASE<<0) /* 风扇异常标志位：第0路 */
#define FUN_ERROR_FLAG1 (FUN_ERROR_FLAG_BASE<<1) /* 风扇异常标志位：第1路 */
#define FUN_ERROR_FLAG2 (FUN_ERROR_FLAG_BASE<<2) /* 风扇异常标志位：第2路 */
#define FUN_ERROR_FLAG3 (FUN_ERROR_FLAG_BASE<<3) /* 风扇异常标志位：第3路 */
#define FUN_ERROR_FLAG4 (FUN_ERROR_FLAG_BASE<<4) /* 风扇异常标志位：第4路 */
#define FUN_ERROR_FLAG5 (FUN_ERROR_FLAG_BASE<<5) /* 风扇异常标志位：第5路 */

/*  heat:0~6; cool: 6~7 */
#define	PID_PARAM_INDEX_HEAT0 (0) /* 试剂针2 */
#define	PID_PARAM_INDEX_HEAT1 (1) /* 磁珠 */
#define PID_PARAM_INDEX_HEAT2 (2) /* 光学检测1 */
#define	PID_PARAM_INDEX_HEAT3 (3) /* （保留）光学检测2; H3600只有一个光学检测池 */
#define	PID_PARAM_INDEX_HEAT4 (4) /* 试剂仓(玻璃片);没有温度传感器，固定占空比 */
#define	PID_PARAM_INDEX_HEAT5 (5) /* 孵育位 */ 

#define	PID_PARAM_INDEX_COOL0 (6) /* 试剂仓(帕尔贴) 帕尔贴温度传感器1 */
#define	PID_PARAM_INDEX_COOL1 (7) /* 试剂仓(帕尔贴) 帕尔贴温度传感器1 */

/* 传感器 温度通道：0~9路 */
#define SENSOR_TEMP_CHNNEL0 (0)     /* 保留 */
#define SENSOR_TEMP_CHNNEL1 (1)     /* 保留 */
#define SENSOR_TEMP_CHNNEL2 (2)     /* 保留 */
#define SENSOR_TEMP_CHNNEL3 (3)     /* 试剂仓(帕尔贴)1 */

#define SENSOR_TEMP_CHNNEL4 (4)     /* 环境温度 */
#define SENSOR_TEMP_CHNNEL5 (5)     /* 磁珠检测位 */
#define SENSOR_TEMP_CHNNEL6 (6)     /* 光学检测位1 */
#define SENSOR_TEMP_CHNNEL7 (7)     /* 孵育仓温度 */

#define SENSOR_TEMP_CHNNEL8 (8)     /* 试剂针2(新) 新伊藤加热针 */
#define SENSOR_TEMP_CHNNEL9 (9)     /* （镜像SENSOR_TEMP_CHNNEL2）仅用于调试监控 */
#define SENSOR_TEMP_CHNNEL10 (10)   /* （镜像SENSOR_TEMP_CHNNEL5）仅用于调试监控 */
#define SENSOR_TEMP_CHNNEL11 (11)   /* （镜像SENSOR_TEMP_CHNNEL8）仅用于调试监控 */

/* 传感器异常标志位 0~9路 */
#define SENSOR_ERROR_FLAG_BASE (0x01)
#define SENSOR_ERROR_FLAG0 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL0)
#define SENSOR_ERROR_FLAG1 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL1)
#define SENSOR_ERROR_FLAG2 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL2)
#define SENSOR_ERROR_FLAG3 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL3)

#define SENSOR_ERROR_FLAG4 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL4)
#define SENSOR_ERROR_FLAG5 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL5)
#define SENSOR_ERROR_FLAG6 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL6)
#define SENSOR_ERROR_FLAG7 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL7)

#define SENSOR_ERROR_FLAG8 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL8)
#define SENSOR_ERROR_FLAG9 (SENSOR_ERROR_FLAG_BASE<<SENSOR_TEMP_CHNNEL9)

#define SENSOR_IC_NUMBER (3) /* 温度传感器个数 */
#define SENSOR_IC_CHANNEL_NUMBER (4)  /* 每个温度传感器的通道数 */
#define FUN_NUMBER (4) /* 风扇个数 */

#define GET_TEMPERATE_INTERVAL 1 /* 从温控板获取温度的间隔, 单位s */

#define TEMP_R2_NORMAL_GOAL 423 /* 42.3度，放大十倍 */

#define TEMP_CTL_OFF 0 /* 关闭 PID温控模式 */
#define TEMP_CTL_NORMAL_ON 1 /* 开启 PID温控模式 */
#define TEMP_CTL_SEC_FULL_ON 0xfe /* 开启 次全功率模式（带最大功率限制+最高温度限制） */
#define TEMP_CTL_FULL_ON 0xff /* 开启 全功率模式（带最大功率限制）*/

/* 保存温度数据的相关定义 */
#define TEMEPRATE_DATA_FILE LOG_DIR"temperate_data.txt"
#define TEMEPRATE_DATA_FILE_MAX_SIZE (1024*1024*10) /* 10M */
#define TEMEPRATE_SAVE_FILE_DATA_SIZE 6 /* 同时写入文件的数据个数 */
#define TEMEPRATE_DATA_LINE_SIZE ((10*60)/GET_TEMPERATE_INTERVAL) /* 一行数据个数，10min容量 */

typedef struct
{
    uint8_t fan_status; /* 风扇状态 */
    uint16_t temperate_status; /* 温度状态 */
    uint16_t sensor_status; /* 传感器状态 */
    int32_t ic_temp_ad[SENSOR_IC_NUMBER*SENSOR_IC_CHANNEL_NUMBER]; /* ic 温度数据 摄氏度，放大了1000倍 */
}__attribute__((packed))slip_temperate_ctl_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    uint8_t enable;
    uint16_t times;
}__attribute__((packed))slip_temperate_ctl_pwm_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    uint32_t goal;
}__attribute__((packed))slip_temperate_ctl_goal_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    uint8_t maxpower; /* 0~100% */
}__attribute__((packed))slip_temperate_ctl_maxpower_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    /* 执行结果 */
    uint8_t status;
}__attribute__((packed)) temperate_ctl_pwm_result_t;

typedef struct
{
    /* GPIO序号 */
    uint8_t gpio_id;
    /* 状态设置 */
    uint8_t status;
}__attribute__((packed)) slip_temperate_ctl_gpio_t;

typedef struct
{
    /* 温度通道序号 */
    uint8_t index;
    int16_t sensor_cali;
}__attribute__((packed))slip_temperate_ctl_sensor_cali_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    uint8_t enable;
}__attribute__((packed))slip_temperate_ctl_fan_enable_t;

typedef struct
{
    /* pwm序号 */
    uint8_t index;
    uint8_t mode;
    uint8_t duty;
}__attribute__((packed))slip_temperate_ctl_fan_duty_t;

int slip_temperate_ctl_pwm_set(temperate_target_t index, uint8_t enable, uint16_t times);
int slip_temperate_ctl_goal_set(temperate_target_t index, uint32_t data);
int slip_temperate_gpio_set(uint8_t index, uint8_t status);
int slip_temperate_gpio_set_direct(uint8_t index, uint8_t status);
int slip_temperate_ctl_maxpower_set(temperate_target_t index, uint8_t data);
int slip_temperate_ctl_get(slip_temperate_ctl_t *result);
int16_t slip_temperate_ctl_sensor_cali_get(uint8_t index);
int slip_temperate_ctl_sensor_cali_set(uint8_t index, int16_t data);
int16_t slip_temperate_ctl_goal_cali_get(uint8_t index);
int slip_temperate_ctl_goal_cali_set(uint8_t index, int16_t data);
int thrift_temp_get(short index);
int slip_temperate_ctl_sensor_type_set(uint8_t type);
int slip_temperate_ctl_goal_mode_set(uint8_t type);
int slip_temperate_ctl_fan_enable_set(fan_pwm_index_t index, uint8_t enable);
int slip_temperate_ctl_fan_duty_set(fan_pwm_index_t index, uint8_t mode, uint8_t duty);
int all_temperate_ctl(uint8_t enable, int regent_table_flag);
int temperate_ctl_init(void);

#ifdef __cplusplus
}
#endif

#endif

