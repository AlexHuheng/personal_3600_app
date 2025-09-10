#ifndef THRIFT_SERVICE_SOFTWARE_INTERFACE_H
#define THRIFT_SERVICE_SOFTWARE_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "movement_config.h"
#include "h3600_cup_param.h"
#include "h3600_needle.h"
#include "thrift_handler.h"

#define H3600_CONF "/root/maccura/h_etc/h3600_conf.json"
#define H3600_CONF_OLD "/root/maccura/h_etc/h3600_conf_old.json"
#define MAX_MOTOR_POS_NUM 128

/* 读写KB值时的注射器ID定义,来源于 thrift原始定义文件 */
#define EDI_SAMPLE_NORMAL_ADDING                0          // 样本针常规加样（单次吸/吐液)
#define EDI_R1_ADDING                           1          // 孵育试剂针加样
#define EDI_R2_ADDING                           2          // 启动试剂针加样
#define EDI_SAMPLE_DILUENT_ADDING               3          // 样本针稀释加样（同时吸取稀释液和样本）
#define EDI_SAMPLE_DILUENT_ADDINGWITH_PUNCTUR   4          // 样本针稀释吐样（穿刺针特有，同时吐出稀释液和样本）

#define EDI_SAMPLE_NORMAL_5UL                   0
#define EDI_SAMPLE_NORMAL_10UL                  1
#define EDI_SAMPLE_NORMAL_15UL                  2
#define EDI_SAMPLE_NORMAL_25UL                  3
#define EDI_SAMPLE_NORMAL_50UL                  4
#define EDI_SAMPLE_NORMAL_100UL                 5
#define EDI_SAMPLE_NORMAL_150UL                 6
#define EDI_SAMPLE_NORMAL_200UL                 7
#define EDI_SAMPLE_DILU_R3_5UL                  8
#define EDI_SAMPLE_DILU_R3_10UL                 9
#define EDI_SAMPLE_DILU_R3_15UL                 10
#define EDI_SAMPLE_DILU_R3_25UL                 11
#define EDI_SAMPLE_DILU_R3_50UL                 12
#define EDI_SAMPLE_DILU_R4_15UL                 13
#define EDI_SAMPLE_DILU_R4_20UL                 14
#define EDI_SAMPLE_DILU_R4_50UL                 15
#define EDI_SAMPLE_DILU_R4_100UL                16
#define EDI_SAMPLE_DILU_R4_150UL                17
#define EDI_SAMPLE_DILU_R4_200UL                18
#define EDI_R2_ADDING_20UL                      19
#define EDI_R2_ADDING_50UL                      20
#define EDI_R2_ADDING_100UL                     21
#define EDI_R2_ADDING_150UL                     22
#define EDI_R2_ADDING_200UL                     23
#define EDI_ADDINGWITH_MAX                      24

typedef struct {
    double k_ratio;
    double b_ratio;
}thrift_liquid_amount_para_t;

typedef struct {
    double liq_default_value;               /* 默认加样值 */
    double liq_true_value;                  /* 实际加样值 */
    thrift_liquid_amount_para_t liq_kb_param; /* 加样KB值 */
}thrift_liquid_kbs_t;

typedef struct {
    int speed;
    int acc;
}thrift_motor_para_t;

typedef struct
{
    int32_t mode;
    int32_t iuserdata;
}throughput_mode_t;

typedef struct {
    thrift_liquid_amount_para_t liquid_amount[NEEDLE_TYPE_MAX];   /* S、R1、R2针的加液量k、b系数 */
    thrift_motor_para_t motor[MAX_MOTOR_NUM + 1];   /* 电机速度、加速度参数 */
    int motor_pos_cnt[MAX_MOTOR_NUM + 1];           /* 电机位置点个数 */
    int motor_pos[MAX_MOTOR_NUM + 1][MAX_MOTOR_POS_NUM]; /* 电机位置点参数 */
    int pierce_support; /* 样本针类型： 0：平头针 1：穿刺针 */
    int throughput_mode; /* 是否启用 通量模式：  0：不启用 1：启用 */
    int pierce_enable;  /* 是否开启穿刺 */
    int straight_release;/* 是否直排 */
    int clot_check_switch;/* 是否开启凝块检测 */
    thrift_master_t thrift_master_server; /* 上位机PC的ip、port */
    thrift_master_t thrift_slave_server; /* 下位机仪器的ip、port */
    char optical_curr_data[5];
    int fan_pwm_duty[4];/* 待机时，散热风扇占空比，0~100% */
    double r0_liq_true_value[8];
    double r2_liq_true_value[5];
    double r3_liq_true_value[5];
    double r4_liq_true_value[6];
    int s_threshold[7];     /* 样本/孵育试剂针探测阈值(内圈/外圈/样本/样本带帽/样本带适配器/稀释液/试剂仓微量管) */
    int r2_threshold[5];    /* 启动试剂针探测阈值(内圈/外圈) */
    int mag_pos_disable[4]; /* 磁珠检测通道x是否禁用      0：未禁用 1：禁用 */
    int optical_pos_disable[8];  /* 光学检测通道x是否禁用      0：未禁用 1：禁用 */
}h3600_conf_t;

h3600_conf_t* h3600_conf_get();

/* 设置指定电机速度及加速度 */
int thrift_motor_para_set(int motor_id, const thrift_motor_para_t *motor_para);
/* 获取指定电机速度及加速度 */
int thrift_motor_para_get(int motor_id, thrift_motor_para_t *motor_para);
/* 设置指定电机指定标定参数步数 */
int thrift_motor_pos_set(int motor_id, int pos, int step);
/* 抓手自动标定后，设置指定电机指定标定参数步数 */
int thrift_motor_pos_grip_set(int motor_id, int pos, int step);
/* 保存抓手标定的值 */
int save_grip_cali_value(void);
/* 获取指定电机的相关标定参数步数 */
int thrift_motor_pos_get(int motor_id, int *pos, int pos_size);
/* 电机复位 */
int thrift_motor_reset(int motor_id, int is_first);
/* 电机正向（步数为正数）或反向（步数为负数）移动 */
int thrift_motor_move(int motor_id, int step);
/* 电机移动到 */
int thrift_motor_move_to(int motor_id, int step);
/* 扫描一维码或二维码 1：常规扫码，2：急诊扫码，3：试剂仓扫码 */
int thrift_read_barcode(int type, char *barcode, int barcode_size);
/* 液面探测 1：样本针，2：R1，3：R2，返回值为液面探测实际运动步数，当探测失败时，返回-1 */
int thrift_liquid_detect(int type);
/* 获取xx针的KB值 */
int get_kb_value(int type, double *k_value, double *b_value);
/* 设置xx针的KB值 */
int set_kb_value(int type, double k_value, double b_value);
int set_optical_value(void);
int thrift_master_server_ipport_set(const char* ip, int port);
thrift_master_t* thrift_master_server_ipport_get();
int thrift_slave_server_ipport_set(const char* ip, int port, int pierce_enable);
thrift_master_t* thrift_slave_server_ipport_get();
void thrift_engineer_position_set();

int h3600_conf_init(void);
int get_straight_release_para(void);
void set_straight_release_para(int val);
int get_clot_check_flag(void);
void set_clot_check_flag(int flag);
int get_throughput_mode(void);
void set_throughput_mode(void *arg);
void thrift_optical_pos_disable_set(int ch, int disable);
int thrift_optical_pos_disable_get(int ch);
void thrift_mag_pos_disable_set(int ch, int disable);
int thrift_mag_pos_disable_get(int ch);

int read_old_conf(void);
h3600_conf_t* h3600_old_conf_get();


#ifdef __cplusplus
}
#endif

#endif
