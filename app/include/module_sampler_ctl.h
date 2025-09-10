#ifndef MODULE_SAMPLER_CTL_H
#define MODULE_SAMPLER_CTL_H

#include <stdint.h>
#include <log.h>
#include <arpa/inet.h>
#include <stdio.h>


#include "slip/slip_port.h"
#include "slip/slip_msg.h"
#include "slip_cmd_table.h"
#include "slip/slip_node.h"
#include "h3600_cup_param.h"

#define TUBE_NUM_MAX            10	/* 样本数量 */
#define RACK_NUM_MAX            6	/* 样本架数量 */
#define BARCODE_LENGTH_MAX      200	/* 最大条码长度       */
#define SAMPLE_BAR_MAX_LENGTH   22   /* 样本管条码最大长度 */

typedef enum {
    RACK_CHANNEL_1,
    RACK_CHANNEL_2,
    RACK_CHANNEL_3,
    RACK_CHANNEL_4,
    RACK_CHANNEL_5,
    RACK_CHANNEL_6,
    RACK_CHANNEL_MAX,
} racks_pos_index_t;

/* 样本信息 */
typedef struct
{
    int         rack_number;        /* 样本槽编号（1~6） */
    int         rack_index;         /* 样本架号 */
    int         sample_index;       /* 样本所在样本架上的位置索引 */
    int         scan_status;        /* 扫描结果 */
    int         exist;              /* 样本管是否存在 1:存在, 0:不存在 */
    char        barcode[SCANNER_BARCODE_LENGTH];   /* 样本条码 */
    int         tube_type;          /* 样本管类型 */
    int         with_hat;           /* 样本戴帽 0：不戴帽         1：戴帽   */
}sample_info_t;

typedef struct
{
    /* 扫码器序号 */
    unsigned char type;
    /* 运行模式 */
    unsigned char pt_dd_mode;
}__attribute__((packed)) barcode_async_t;

enum {
    SCANNER_RACKS = 1,      /* 样本扫码器 */
    SCANNER_REAGENT,        /* 试剂仓扫码器 */
    SCANNER_REAGENT_CONT,   /* 连续扫码模式开启标志 */
    CONT_RESULT_GET         /*  */    
};

/* 工作状态 */
enum{
    REPORT_TUBE_EMPTY = 0,     /* 无样本管 */
    REPORT_TUBE_NORMAL,        /* 常规样本管 */
    REPORT_TUBE_ADAPTER,       /* 适配器 */
};

/* 试剂仓按键灯控制 */
enum{
    REAG_BUTTON_LED_CLOSE = 0,    /* 关灯 */
    REAG_BUTTON_LED_OPEN,         /* 开灯 */
    REAG_BUTTON_LED_BLINK,        /* 闪灯 */
};

typedef struct
{
    /* 扫码器控制类型 */
    unsigned char type;
    /* 执行结果 */
    unsigned char status;
    int data_len;
    char data[200];
}__attribute__((packed)) scanner_result_t;

/* 扫描完成后，上报的样本架信息 */
typedef struct __attribute__((packed)) {
    uint8_t rack_number;                /* 样本架编号      1-6 */
    uint32_t rack_id;                   /* 样本架号 */
    uint8_t result_state;               /* 条码获取结果 1：成功           0：失败    */
    uint16_t tube_exist;                /* 样本管是否存在 */
    uint16_t hat;                       /* 是否带帽 */
    uint8_t type[TUBE_NUM_MAX];         /* 样本管类型 */

    char tube_bar[TUBE_NUM_MAX][SAMPLE_BAR_MAX_LENGTH];    /* 条码内容 */
}report_rack_t;

void slip_racks_infor_report(const slip_port_t *port, slip_msg_t *msg);
int scanner_read_barcode_sync(unsigned char type, char *barcode, int len);
int scanner_version(unsigned char type, char *version, int len);
int slip_ele_lock_to_sampler(uint8_t index, uint8_t  status);
int ele_unlock_by_status(void);
int slip_rack_tank_led_to_sampler(uint8_t index, uint8_t  status);
sp_hat_t get_sp_is_carry_hat(struct sample_tube *tube);
int slip_reinit_to_sampler_board(void);
int slip_set_clot_data_from_sampler(uint8_t check_switch, int orderno);
int scanner_compensate_read_barcode(char *barcode, int len, int stage);
void sampler_fault_infor_report(uint8_t index);
void sampler_init(void);
void set_sample_scan_flag(uint8_t flag);
int slip_liq_cir_noise_set(void);
float g_presure_noise_get(void);
int slip_button_reag_led_to_sampler(uint8_t status);
int scanner_restart(void);
int scanner_get_communicate(void);
int scanner_jp_value_get(uint8_t index);
int sampler_power_on_func(void);

/* extern */
extern int get_power_off_stat();

#endif
