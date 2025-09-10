#ifndef _SLIP_CMD_TABLE_H
#define _SLIP_CMD_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "slip/slip_port.h"
#include "slip/slip_msg.h"

#define SLIP_NODE_A9_LINUX      200     /* 主控板Linux测 */
#define SLIP_NODE_A9_RTOS       201     /* 主控板Rtos测 */
#define SLIP_NODE_SAMPLER       202     /* 进样器 */
#define SLIP_NODE_OPTICAL_1     203     /* 光学1 */
#define SLIP_NODE_OPTICAL_2     204     /* 光学2 */
#define SLIP_NODE_MAGNECTIC     205     /* 磁珠 */
#define SLIP_NODE_TEMP_CTRL     206     /* 温控 */
#define SLIP_NODE_LIQUID_DETECT_1      207     /* 液面探测1：样本针 */
#define SLIP_NODE_LIQUID_DETECT_2      208     /* 液面探测2：试剂针2 */
#define SLIP_NODE_LIQUID_DETECT_3      209     /* 液面探测3：试剂针1 */
#define SLIP_NODE_LIQUID_CIRCUIT       210     /* 液路探测 */
#define SLIP_NODE_OPTICAL_HIL   211     /* 光学HIL筛查 */

/* 以下两项仅用于FPGA版本获取作索引值，无实际node */
#define SLIP_NODE_FPGA_A9   212     /* 主控板FPGA */
#define SLIP_NODE_FPGA_M7   213     /* 进样器FPGA */

#define SLIP_NODE_AUTO_DILU_INSIDE      214     /* 机内稀释仪 */


#define OPT_SENSOR_INDEX_BASE              (1)
#define OPT_SENSOR_INDEX_END               (48)

#define OPT_SENSOR_PHY_INDEX_BASE           (103)


#define LEVEL_SENSOR_INDEX_BASE            (231)
#define LEVEL_SENSOR_INDEX_END             (240)

#define LEVEL_SENSOR_PHY_INDEX_BASE        (158)


#define BUBBLE_SENSOR_INDEX_BASE            (301)
#define BUBBLE_SENSOR_INDEX_END             (310)

#define BUBBLE_SENSOR_PHY_INDEX_BASE        (88)


#define FLOAT_IO_INDEX_BASE                 (201)
#define FLOAT_IO_INDEX_END                  (215)

#define FLOAT_IO_PHY_INDEX_BASE             (70)

#define ULTRASOUND_INDEX                    (271)
#define ULTRASOUND_PHY_INDEX                (87)

#define STREAM_SOURCE_INDEX                 (291)
#define STREAM_SOURCE_PHY_INDEX             (86)

#define SOLENOID_VALVE_INDEX_BASE           (101)
#define SOLENOID_VALVE_INDEX_END            (178)

#define SOLENOID_VALVE_PHY_INDEX_BASE       (2002)

#define PE_DATA_LEN      (6)

#define MOTOR_TYPE      ((uint8_t)0x02)

#define MOTOR_RESET_SUBTYPE             ((uint8_t)0x00)
#define MOTOR_ALL_RESET_SUBTYPE         ((uint8_t)0x01)
#define MOTOR_STEP_SUBTYPE              ((uint8_t)0x02)
#define MOTOR_SPEED_SUBTYPE             ((uint8_t)0x03)
#define MOTOR_STOP_SUBTYPE              ((uint8_t)0x04)
#define MOTOR_BINDING_PE_SUBTYPE        ((uint8_t)0x05)
#define MOTOR_GET_POSITION_SUBTYPE      ((uint8_t)0x06)
#define MOTOR_RESET_CTL_SUBTYPE         ((uint8_t)0x07)
#define MOTOR_STEP_CTL_SUBTYPE          ((uint8_t)0x08)
#define MOTOR_SPEED_CTL_SUBTYPE         ((uint8_t)0x09)
#define MOTOR_GET_MIX_CIRCLE_SUBTYPE    ((uint8_t)0x0a)
#define MOTOR_ALL_STOP_SUBTYPE          ((uint8_t)0x0b)
#define MOTOR_ALL_POWER_CTL_SUBTYPE     ((uint8_t)0x0c)
#define MOTOR_LIQ_DETECT_SUBTYPE        ((uint8_t)0x0d)
#define MOTOR_STEP_CTL_DUAL_SUBTYPE     ((uint8_t)0x0e)
#define MOTOR_RESET_STEPS_SUBTYPE       ((uint8_t)0x0f)

#define MOTOR_SET_ENCODER_VALUE_SUBTYPE ((uint8_t)0x10)
#define MOTOR_GET_ENCODER_VALUE_SUBTYPE ((uint8_t)0x11)


#define MOTOR_START_INDEX               1

#define IO_TYPE         ((uint8_t)0x03)
#define IO_SET_SUBTYPE                  ((uint8_t)0x00)
#define IO_GET_SUBTYPE                  ((uint8_t)0x01)
#define IO_GET_PE                       ((uint8_t)0x02)

#define CONFIGURE_TYPE  ((uint8_t)0x04)
#define CONFIG_SET_SUBTYPE              ((uint8_t)0x00)
#define CONFIG_GET_SUBTYPE              ((uint8_t)0x01)
#define CONFIG_SET_CUR_SUBTYPE          ((uint8_t)0x02)

#define OTHER_TYPE      ((uint8_t)0x05)
#define OTHER_PWM_SET_SUBTYPE                       ((uint8_t)0x00)
#define OTHER_PWM_GET_SUBTYPE                       ((uint8_t)0x01)
#define OTHER_BLDC_TIMEOUT_SET_SUBTYPE              ((uint8_t)0x07)
#define OTHER_BUBBLE_SENSOR_GET_SUBTYPE             ((uint8_t)0x08)
#define OTHER_BOARDS_FIRMWARE_GET_SUBTYPE           ((uint8_t)0x09)
#define OTHER_BOARD_ERROR_GET_SUBTYPE               ((uint8_t)0x0C)
#define OTHER_FPGA_VERSION_GET_SUBTYPE              ((uint8_t)0x0E)


#define UPGRADE_TYPE    ((uint8_t)0x09)
#define UPGRADE_BEGIN_SUBTYPE                       ((uint8_t)0x01)
#define UPGRADE_ACK_BEGIN_SUBTYPE                   ((uint8_t)0x02)
#define UPGRADE_WORKING_SUBTYPE                     ((uint8_t)0x03)
#define UPGRADE_ACK_WORKING_SUBTYPE                 ((uint8_t)0x04)
#define UPGRADE_STOP_SUBTYPE                        ((uint8_t)0x05)
#define UPGRADE_ACK_STOP_SUBTYPE                    ((uint8_t)0x06)
#define UPGRADE_ACTIVE_SUBTYPE                      ((uint8_t)0x07)
#define UPGRADE_ACK_ACTIVE_SUBTYPE                  ((uint8_t)0x08)
#define UPGRADE_GET_SUBAREA                         ((uint8_t)0x09)
#define UPGRADE_ACK_GET_SUBAREA                     ((uint8_t)0x0A)
#define UPGRADE_SWITCH_SUBAREA                      ((uint8_t)0x0B)
#define UPGRADE_ACK_SWITCH_SUBAREA                  ((uint8_t)0x0C)
#define UPGRADE_RESET_SUBTYPE                       ((uint8_t)0x10)


/* 液面探测 slip命令 */
#define OTHER_LIQUID_DETECT_GET_SUBTYPE             ((uint8_t)0x10)
#define OTHER_LIQUID_DETECT_TYPE_SET_SUBTYPE        ((uint8_t)0x11)
#define OTHER_LIQUID_DETECT_ABS_SET_SUBTYPE         ((uint8_t)0x12)
#define OTHER_LIQUID_DETECT_STATE_SET_SUBTYPE       ((uint8_t)0x13)
#define OTHER_LIQUID_DETECT_RESULT_GET_SUBTYPE      ((uint8_t)0x14)
#define OTHER_LIQUID_DETECT_DEBUG_SET_SUBTYPE       ((uint8_t)0x15)
#define OTHER_LIQUID_DETECT_POS_SET_SUBTYPE         ((uint8_t)0x16)
#define OTHER_LIQUID_DETECT_ALL_DATA_SUBTYPE        ((uint8_t)0x17)
#define OTHER_LIQUID_OUT_MONITOR_SUBTYPE            ((uint8_t)0x18)
#define OTHER_LIQUID_RCD_SUBTYPE                    ((uint8_t)0x19)
#define OTHER_LIQUID_COLLSION_BARRIER_SUBTYPE       ((uint8_t)0x1A)

/* 温控模块 slip命令 */
#define OTHER_TEMPERATE_CTL_GET_SUBTYPE             ((uint8_t)0x20)
#define OTHER_TEMPERATE_CTL_PWM_SET_SUBTYPE         ((uint8_t)0x21)
#define OTHER_TEMPERATE_CTL_GOAL_SET_SUBTYPE        ((uint8_t)0x22)
#define OTHER_TEMPERATE_CTL_MAXPOWER_SET_SUBTYPE    ((uint8_t)0x23)
#define OTHER_TEMPERATE_CTL_SENSOR_CALI_SET_SUBTYPE ((uint8_t)0x24)
#define OTHER_TEMPERATE_CTL_SENSOR_CALI_GET_SUBTYPE ((uint8_t)0x25)
#define OTHER_TEMPERATE_CTL_SENSOR_TYPE_SET_SUBTYPE ((uint8_t)0x26)
#define OTHER_TEMPERATE_CTL_GOAL_CALI_SET_SUBTYPE   ((uint8_t)0x27)
#define OTHER_TEMPERATE_CTL_GOAL_CALI_GET_SUBTYPE   ((uint8_t)0x28)
#define OTHER_TEMPERATE_CTL_GOAL_MODE_SET_SUBTYPE   ((uint8_t)0x29)
#define OTHER_TEMPERATE_CTL_FAN_ENABLE_SET_SUBTYPE  ((uint8_t)0x2a)
#define OTHER_TEMPERATE_CTL_FAN_DUTY_SET_SUBTYPE    ((uint8_t)0x2b)

/* 磁珠模块 slip命令 */
#define OTHER_MAGNETIC_BEAD_GET_SUBTYPE             ((uint8_t)0x31)
#define OTHER_MAGNETIC_PWM_DUTY_SET_SUBTYPE         ((uint8_t)0x32)
#define OTHER_MAGNETIC_PWM_ENABLE_SET_SUBTYPE       ((uint8_t)0x33)
#define OTHER_MAGNETIC_PWM_DRIVER_LEVEL_SET_SUBTYPE   ((uint8_t)0x34)
#define OTHER_MAGNETIC_PWM_DRIVER_LEVEL_GET_SUBTYPE   ((uint8_t)0x35)
#define OTHER_MAGNETIC_PWM_PERIOD_SET_SUBTYPE       ((uint8_t)0x36)
#define OTHER_MAGNETIC_PWM_PERIOD_GET_SUBTYPE       ((uint8_t)0x37)

/* 液路模块 slip命令 */
#define OTHER_LIQUID_CIRCUIT_GET_SUBTYPE            ((uint8_t)0x40)
#define OTHER_LIQUID_CIRCUIT_CLOT_SUBTYPE           ((uint8_t)0x41)
#define OTHER_LIQUID_CIRCUIT_PARAM_SET_SUBTYPE      ((uint8_t)0x42)

/* 光学模块 slip命令 */
#define OTHER_OPTICAL_DATA_GET_SUBTYPE              ((uint8_t)0x51)
#define OTHER_OPTICAL_DATA_SET_SUBTYPE              ((uint8_t)0x52)
#define OTHER_OPTICAL_BKDATA_GET_SUBTYPE            ((uint8_t)0x53)
#define OTHER_OPTICAL_CURR_SET_SUBTYPE              ((uint8_t)0x54)
#define OTHER_OPTICAL_CURR_CALC_SUBTYPE             ((uint8_t)0x55)
#define OTHER_OPTICAL_VERSION_GET_SUBTYPE           ((uint8_t)0x56)

/* 进样器FPGA加载结果 */
#define OTHER_FPGA_LOAD_GET_SUBTYPE                 ((uint8_t)0x62)


/* 事件通知 */
#define OTHER_EVENT_SUBTYPE                       ((uint8_t)0x61)

#define SCANNER_TYPE    ((uint8_t)0x10)                     /* 扫码器命令 */
#define SCANNER_VERSION_SUBTYPE                         ((uint8_t)0x00) /* 扫码器版本 */
#define SCANNER_READ_BARCODE_SUBTYPE                    ((uint8_t)0x01) /* 扫码器条码读取 */
#define SCANNER_READ_COMPENSATE_BARCODE_SUBTYPE         ((uint8_t)0x02) /* 扫码器补偿扫码 */
#define SCANNER_RSTART_SUBTYPE                          ((uint8_t)0x03) /* 扫码器重启指令 */
#define SAMPLER_CANNER_COMMUNICATE_GET_SUBTYPE          ((uint8_t)0x04) /* 扫码器通信确认   */
#define SAMPLER_READ_BARCODE_RESPONSE                   ((uint8_t)0x05) /* 扫码回复   */
#define SCANNER_JP_PARAM_GET_SUBTYPE                    ((uint8_t)0x06) /* 获取扫码成功率 */


/* 进样器相关指令 */
#define SAMPLER_TYPE                                   ((uint8_t)0x11) /* 进样器相关指令 */
#define SAMPLER_RACK_INFOR_REPORT_SUBTYPE              ((uint8_t)0x01) /* M7子板上报样本架信息 */
#define SAMPLER_RACK_INFOR_RESPONSE_SUBTYPE            ((uint8_t)0x02) /* 回复 */
#define SAMPLER_ELE_LOCK_SYBTYPE                       ((uint8_t)0x03) /* 命令电磁铁上锁        */
#define SAMPLER_ELE_LOCK_RESPONSE_SUBTYPE              ((uint8_t)0x04) /* 回复   */
#define SAMPLER_RACKS_TANK_LED_SUBTYPE                 ((uint8_t)0x05) /* 设置样本槽led状态        */
#define SAMPLER_RACKS_TACK_LED_RESPONSE_SUBTYPE        ((uint8_t)0x06) /* 回复   */
#define SAMPLER_SOFT_REINIT_SUBTYPE                    ((uint8_t)0x07) /* 工作状态复位       */
#define SAMPLER_SOFT_REINIT_RESPONSE_SUBTYPE           ((uint8_t)0x08) /* 回复   */
#define SAMPLER_RACK_PULLOUT_REPORT_SUBTYPE            ((uint8_t)0x09) /* 上报槽拉出样本架       */
#define SAMPLER_RACK_PULLOUT_RESPONSE_SUBTYPE          ((uint8_t)0x0a) /* 回复   */
#define SAMPLER_RACK_PUSH_ERROR_REPORT_SUBTYPE         ((uint8_t)0x0b) /* 上报槽进架错误信息       */
#define SAMPLER_RACK_PUSH_ERROR_RESPONSE_SUBTYPE       ((uint8_t)0x0c) /* 回复   */
#define SAMPLER_KEY_REAG_REPORT_SUBTYPE                ((uint8_t)0x0e) /* 上报试剂仓按键       */
#define SAMPLER_KEY_REAG_REPONSE_SUBTYPE               ((uint8_t)0x0f) /* 回复   */
#define SAMPLER_CLOT_GET_SUBTYPE                       ((uint8_t)0x10) /* A9获取凝块数据       */
#define SAMPLER_CLOT_GET_REPONSE_SUBTYPE               ((uint8_t)0x11) /* 回复   */
#define SAMPLER_RACK_PULL_OUT_SUBTYPE                  ((uint8_t)0x12) /* 样本架被拉出，上报A9          */
#define SAMPLER_RACK_PULL_OUT_REPONSE_SUBTYPE          ((uint8_t)0x13) /* 回复   */
#define SAMPLER_RACK_PUSH_IN_ERR_SUBTYPE               ((uint8_t)0x14) /* 样本架推入错误，上报A9          */
#define SAMPLER_RACK_PUSH_IN_ERR_REPONSE_SUBTYPE       ((uint8_t)0x15) /* 回复   */
#define SAMPLER_ELETRO_ERR_SUBTYPE                     ((uint8_t)0x16) /* 电磁铁状态异常，上报A9          */
#define SAMPLER_ELETRO_ERR_REPONSE_SUBTYPE             ((uint8_t)0x17) /* 回复   */

#define SAMPLER_CLOT_NOISE_SUBTYPE                     ((uint8_t)0x18) /* 凝块底噪设置 */

#define SAMPLER_LED_REAG_CTL_SUBTYPE                   ((uint8_t)0x19) /* 控制试剂仓按键灯          */
#define SAMPLER_ELED_REAG_CTL_REPONSE_SUBTYPE          ((uint8_t)0x1a) /* 回复   */

#define SAMPLER_PRESSURE_DATA_SUBTYPE                  ((uint8_t)0x1b) /* 压力数据返回 */

#define SAMPLER_BOARD_SCANNER_FAULT_SUBTYPE            ((uint8_t)0x1c) /* 子板扫码器故障上报 */

#define SCANNER_BARCODE_LENGTH 200   /* 条码长度 */
#define SCANNER_VERSION_LENGTH 40   /* 扫码器版本长度 */

#define CMD_SUBTYPE_ANY         ((uint8_t)0xFF) /* 任意子类型 */
#define SLIP_TIMEOUT_DEFAULT    2000            /* 默认超时时间 */

typedef enum {
    LED_MACHINE_ID = 1,
    LED_CUVETTE_INS_ID,
} led_id_t;

typedef enum {
    LED_COLOR_GREEN = 1,
    LED_COLOR_YELLOW,
    LED_COLOR_RED
} led_color_t;

typedef enum {
    LED_NONE_BLINK = 1,
    LED_BLINK,
    LED_OFF
} led_blink_t;

enum control_cmd_result {
    CONTROL_CMD_RESULT_SUCCESSS = 0,
    CONTROL_CMD_RESULT_FAILED = 1,
    CONTROL_CMD_RESULT_RUNNING = 2,
    CONTROL_CMD_RESULT_DROP_STEPS = 3,
    CONTROL_CMD_RESULT_FAULT = 4,
    CONTROL_CMD_RESULT_PARAM_INVALID = 5,
    CONTROL_CMD_RESULT_TIMEOUT = 6
};

typedef struct {
    int index;
    int duty_level; /* range from: 0-20*/
}fan_duty;

typedef struct {
    unsigned char motor_idx; /* 需要执行的电机序号 */
    int pos; /* 需要执行的电机移动位置索引 */
}extra_offest_t;

typedef enum {
    MODE_THRIFT = 0,
    MODE_BUTTON
} reagnet_mode_t;/* 试剂仓按键模式    */

#define TASK_OVER           ((uint8_t)0)    //执行完毕
#define TASK_ERROR          ((uint8_t)1)    //执行失败
#define TASK_RUNNING        ((uint8_t)2)    //任务正在执行
#define TASK_PARAM_ERROR    ((uint8_t)3)    //参数错误
#define TASK_MOTOR_STALL    ((uint8_t)4)    //电机堵转
#define TASK_TIMER_OVER     ((uint8_t)5)    //超时

#define MOTOR_PE_MAX    10

/* 开始按钮 */
#define MOTOR_START             (1011)
/* 试剂仓旋转按钮 */
#define REAGENT_SPIN            (1010)
/* PC开机键 */
#define BUTTON_PC_WOL           (1009)

typedef struct
{
    /* 光电编号 */
    unsigned short pe_id;
    /* 目标速度, 电机运行遇到该光电的目标速度, 设置为0表示停止 */
    int speed;
}__attribute__((packed)) motor_pe_t;

int motor_reset_timedwait(unsigned char motor_id, unsigned char type, int timeout);
int motor_step_timedwait(unsigned char motor_id, int step, int speed, int timeout);
int motor_set_pe_timedwait(unsigned char motor_id, motor_pe_t *pe, int pe_cnt, int msecs);
int motor_speed_timedwait(unsigned char motor_id, int speed, int timeout);
int motor_reset_ctl_timedwait(unsigned char motor_id, unsigned char type, int speed, int acc, int timeout, int slow_step);
int motor_reset_steps_timedwait(unsigned char motor_id, unsigned char type, int speed, int acc, int msecs, int extra_step);
int motor_step_ctl_timedwait(unsigned char motor_id, int step, int speed, int acc, int timeout);
int motor_step_dual_ctl_timedwait(unsigned char motor_id, int position_x, int position_y, int speed, int acc, int msecs, double cost_time);
int motor_speed_ctl_timedwait(unsigned char motor_id, int speed, int acc, int max_step, unsigned char speed_flag, int msecs);
int motor_stop_timedwait(unsigned char motor_id, int timeout);
int motor_timedwait(unsigned char motor_id, long msecs);
int motors_timedwait(unsigned char motor_ids[], int motor_count, long msecs);
int motor_power_ctl_all_timedwait(uint8_t type, uint8_t enable, unsigned char *status, int msecs);
int motor_liq_detect_timedwait(unsigned char motor_id, int step, int speed, int timeout);

/* return:
 * 0: 执行成功第二次响应
 * 1: 执行成功第一次响应
 * -1: 失败
*/
typedef int (*waiting_list_callback)(const slip_port_t *port, slip_msg_t *msg, void *user_data);
int result_timedwait(unsigned char type, unsigned char sub_type, waiting_list_callback cb, void *user_data, int timeout_ms);
#define motor_reset(motor_id, type)         motor_reset_timedwait(motor_id, type, 0)
#define motor_step(motor_id, step, speed)   motor_step_timedwait(motor_id, step, speed, 0)
#define motor_speed(motor_id, speed)        motor_speed_timedwait(motor_id, speed, 0)
#define motor_reset_ctl(motor_id, type, speed, acc)  motor_reset_ctl_timedwait(motor_id, type, speed, acc, 0, 0)
#define motor_slow_step_reset_ctl(motor_id, type, speed, acc, step)  motor_reset_ctl_timedwait(motor_id, type, speed, acc, 0, step)
#define motor_step_ctl(motor_id, step, speed, acc)   motor_step_ctl_timedwait(motor_id, step, speed, acc, 0)
#define motor_step_dual_ctl(motor_id, step_x, step_y, speed, acc, cost_time)   motor_step_dual_ctl_timedwait(motor_id, step_x, step_y, speed, acc, 0, cost_time)
#define motor_speed_ctl(motor_id, speed, acc)        motor_speed_ctl_timedwait(motor_id, speed, acc, 0, 0, 0)
#define motor_stop(motor_id)                motor_stop_timedwait(motor_id, 0)
#define motor_reset_steps(motor_id, type, speed, acc, step)  motor_reset_steps_timedwait(motor_id, type, speed, acc, 0, step)
int gpio_set(unsigned short index, unsigned char status);
int gpio_get(unsigned short index);
int indicator_led_set(int led_id, int color, int blink);
int motor_current_pos_timedwait(unsigned char motor_id, int msecs);
int slip_motor_move_timedwait(unsigned char motor_id, int pos, int msec);
int slip_motor_step_timedwait(unsigned char motor_id, int step, int msec);
int slip_motor_step(unsigned char motor_id, int step);
int slip_motor_move_to_timedwait(unsigned char motor_id, int pos, int msec);
int slip_motor_move_to_offset_timedwait(unsigned char motor_id, int pos, int offset, int msec);
int slip_motor_speed_timedwait(unsigned char motor_id, int msec);
int slip_trash_speed_timedwait(unsigned char motor_id, int msec);
int slip_motor_diy_speed_timedwait(unsigned char motor_id, int speed, int max_step, unsigned char speed_flag, int msec);
int slip_motor_reset_async(unsigned char motor_id, unsigned char type, int msec);
int slip_motor_reset_timedwait(unsigned char motor_id, unsigned char type, int msec);
int motor_mix_circle_get_timedwait(unsigned char motor_id, int msecs);
int motor_stop_all_timedwait(int type, unsigned char *status, int msecs);
int slip_bldc_ctl_set(int index, int status);
int slip_bldc_timeout_set(int index, int timeout);
int slip_bldc_rads_get(int index);
int bldc_mix_speed_get(int index);
int boards_firmware_version_get(unsigned char board_id, char *version, int len);
int boards_error_get(void);
int slip_motor_dual_step_ctl_timedwait(unsigned char motor_id, int stepx, int stepy, int speed, int acc, int msec);
int slip_motor_dual_step_timedwait(unsigned char motor_id, int stepx, int stepy, int msec);
int motor_ld_move_sync(unsigned char motor_id, int step, int speed, int c_speed, int msecs);
int encoder_set_value(unsigned char encoder_id, int value, int msecs);
int encoder_get_value(unsigned char encoder_id, int msecs);

void slip_mainboard_init(void);
int get_step_step(unsigned char motor_id);
int get_maxstep(unsigned char motor_id, int pos);
int clear_slip_list_motor();
void wake_up_analyzer_work(void);
int reagent_switch_rotate_ctl(int pan_idx, reagnet_mode_t mode);


#ifdef __cplusplus
}
#endif

#endif
