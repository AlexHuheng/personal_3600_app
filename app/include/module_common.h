#ifndef __MODULE_COMMON_H__
#define __MODULE_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "h3600_cup_param.h"
#include "thrift_service_software_interface.h"
#include <pthread.h>
#include <iconv.h>

#define SP_CAP_RES_STEP     3000

#define BIT(n)                  (1UL << (n))
#define TBIT(v,b)               ((v) & (b))

#define CUP_ANY                 BIT(0)
#define CUP_NONE                BIT(1)
#define CUP_DILU_DISALLOW       BIT(2)
#define CUP_TS_DISALLOW         BIT(3)
#define CUP_MASK                (CUP_ANY | CUP_NONE | CUP_DILU_DISALLOW | CUP_TS_DISALLOW)

#define __pk    __attribute__((__packed__))

#define PRINT_FRAG_TIME(str) \
    do {    \
		struct timeval tv, result;\
		gettimeofday(&tv,NULL);\
        result.tv_sec = tv.tv_sec - get_module_base_time()->tv_sec;   \
        result.tv_usec = tv.tv_usec - get_module_base_time()->tv_usec;    \
        LOG("[%s] endtime: %lld\n", str, (long long)result.tv_sec * 1000 + (long long)result.tv_usec / 1000);\
    } while (0)

typedef enum
{
    POS_0 = 0,
    POS_1,
    POS_2,
    POS_3,
    POS_4,
    POS_5,
    POS_6,
    POS_7,
    POS_8,
    POS_9,
    POS_10,
    POS_11,
    POS_12,
    POS_13,
    POS_14,
    POS_15,
    POS_16,
    POS_17,
    POS_18,
    POS_19,
    POS_20,
    POS_21,
    POS_22,
    POS_23,
    POS_24,
    POS_25,
    POS_26,
    POS_27,
    POS_28,
    POS_29,
    POS_30,
    POS_31,
    POS_32,
    POS_33,
    POS_34,
    POS_35,
    POS_36,
    POS_37,
    POS_38,
    POS_39,
    POS_40,
    POS_41,
    POS_42,
    POS_43,
    POS_44,
    POS_45,
    POS_46,
    POS_47,
    POS_48,
    POS_49,
    POS_50,
    POS_51,
    POS_52,
    POS_53,
    POS_54,
    POS_55,
    POS_56,
    POS_57,
    POS_58,
    POS_59,
}pos_index_t;

typedef struct
{
    int leave_flag;
    pthread_mutex_t mutex_leave;
    pthread_cond_t cond_leave;
}leave_param_t;

typedef enum
{
    /* 样本针 */
    LEAVE_S_FRAG0,  /* 样本针保留信号 */
    LEAVE_S_CLEAN,  /* 样本针到达洗针池; 抓手到达原点前需等待此信号 */

    /* 试剂针2 */
    LEAVE_R2_FRAG8,    /* R2到达洗针池; C.FRAG18开始前 需等待此信号 */

    /* 抓手 */
    LEAVE_C_FRAG18,     /* C在 发送移动至丢杯指令后，样本针 转动前处理加样位前 需等待此信号*/
    LEAVE_C_FRAG26,     /* R2到达洗针池; C.FRAG26开始前 需等待此信号 */
    LEAVE_C_FRAG35,     /* R2到达洗针池; C.FRAG35开始前 需等待此信号 */
    LEAVE_C_FRAG8,      /* C在 分杯稀释第三周期移动至避让位; 样本针移动至取样位时 需等待此信号 */

    /* 液面探测 */
    LEAVE_S_LIQUID,  		/* S液面探测反馈 */
    LEAVE_S_DILU_LIQUID,    /* S稀释液液面探测反馈 */
    LEAVE_R2_LIQUID,     	/* R2液面探测反馈 */

    /* 其它 */
    LEAVE_S_PUSH,              /* 盖帽穿刺 压帽电机限位光电触发 */
    LEAVE_S_DILU_TRANS_READY,  /* C在判定是否启用第6时间片时，需要等待样本针更新稀释转移信息 */
    LEAVE_C_MIX_DET_POS_READY, /* C检测或混匀杯状态准备完成（特殊之处：启动R2获取下一周期试剂仓坐标） */
    LEAVE_C_GET_INIT_POS_READY,/* C在进杯时更新完信息后样本针再获取信息 */

    LEAVE_CLEAR_SOME_DONE, /* 仪器复位时，排微分离器等完成；仪器复位函数退出前需等待此信号 */

    LEAVE_MAX,
}enum_leave_index_t;

/* 反应杯工位的锁定标志 */
#define FLAG_POS_NONE 0
#define FLAG_POS_LOCK 1
#define FLAG_POS_UNLOCK 2

typedef enum
{
    MOVE_C_PRE, /* 抓手的前处理区域 */
    MOVE_C_MIX, /* 抓手的混匀区域 */
    MOVE_C_INCUBATION, /* 抓手的孵育区域 */
    MOVE_C_OPTICAL_MIX, /* 抓手的启动混匀区域 */
    MOVE_C_OPTICAL,     /* 抓手的光学检测区域 */
    MOVE_C_MAGNETIC,  /* 抓手的磁珠区域 */
    MOVE_C_DETACH, /* 抓手的丢杯区域 */
    MOVE_C_NEW_CUP,

    MOVE_R2_REAGENT, /* 试剂针2的取试剂区域 */
    MOVE_R2_MIX, /* 试剂针2的混匀区域 */
    MOVE_R2_MAGNETIC,  /* 试剂针2的磁珠区域 */
    MOVE_R2_CLEAN, /* 试剂针2的洗针区域 */

    MOVE_S_SAMPLE_NOR,  /* 样本针S吸样位 */
    MOVE_S_DILU,      /* 稀释液位置1~3 */
    MOVE_S_ADD_CUP_PRE, /* 样本针S常规加样位 */
    MOVE_S_ADD_CUP_MIX, /* 样本针S常规加样位（混匀） */
    MOVE_S_ADD_REAGENT,/* 试剂仓取样位（内）（外）*/
    MOVE_S_CLEAN,       /* 样本针S洗针位 */
    MOVE_S_TEMP,        /* 样本针S暂存位 */

    MOVE_REAGENT_TABLE_FOR_S,   /* 样本针S使用试剂盘旋转位置 */
    MOVE_REAGENT_TABLE_FOR_R2,  /* R2使用试剂盘旋转位置 */
    MOVE_REAGENT_TABLE_FOR_MIX, /*试剂混匀使用试剂盘旋转位置*/
    MOVE_REAGENT_TABLE_FOR_SCAN,/*试剂扫描准备位置*/

}move_pos_t;

#define H3600_CONF_POS_C_PRE              (1-1)
#define H3600_CONF_POS_C_MIX_1            (2-1)
#define H3600_CONF_POS_C_MIX_2            (3-1)
#define H3600_CONF_POS_C_INCUBATION1      (4-1)
#define H3600_CONF_POS_C_INCUBATION10     (5-1)
#define H3600_CONF_POS_C_INCUBATION30     (6-1)
#define H3600_CONF_POS_C_MAG_1            (7-1)
#define H3600_CONF_POS_C_MAG_4            (8-1)
#define H3600_CONF_POS_C_OPTICAL_MIX      (9-1)
#define H3600_CONF_POS_C_OPTICAL_1        (10-1)
#define H3600_CONF_POS_C_OPTICAL_8        (11-1)
#define H3600_CONF_POS_C_DETACH           (12-1)
#define H3600_CONF_POS_C_CUVETTE          (13-1)

#define H3600_CONF_POS_S_SAMPLE_NOR_1       (1-1)
#define H3600_CONF_POS_S_SAMPLE_NOR_10      (2-1)
#define H3600_CONF_POS_S_SAMPLE_NOR_60      (3-1)
#define H3600_CONF_POS_S_SAMPLE_ADD_PRE     (4-1)
#define H3600_CONF_POS_S_SAMPLE_ADD_MIX1    (5-1)
#define H3600_CONF_POS_S_SAMPLE_ADD_MIX2    (6-1)
#define H3600_CONF_POS_S_REAGENT_TABLE_IN   (7-1)
#define H3600_CONF_POS_S_REAGENT_TABLE_OUT  (8-1)
#define H3600_CONF_POS_S_CLEAN              (9-1)
#define H3600_CONF_POS_S_TEMP               (10-1)
#define H3600_CONF_POS_S_DILU_1             (11-1)
#define H3600_CONF_POS_S_DILU_2             (12-1)

#define H3600_CONF_POS_R2_REAGENT_IN        (1-1)
#define H3600_CONF_POS_R2_REAGENT_OUT       (2-1)
#define H3600_CONF_POS_R2_MIX_1             (3-1)
#define H3600_CONF_POS_R2_MAG_1             (4-1)
#define H3600_CONF_POS_R2_MAG_4             (5-1)
#define H3600_CONF_POS_R2_CLEAN             (6-1)

#define H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN   (1-1)
#define H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT  (2-1)
#define H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN    (3-1)
#define H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT   (4-1)
#define H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_IN  (5-1)
#define H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_OUT (6-1)
#define H3600_CONF_POS_REAGENT_TABLE_FOR_SCAN    (7-1)

/* 时序 */
typedef struct
{
	uint32_t start_time;
	double cost_time;
	uint32_t end_time;
}time_fragment_t;

/* 时序中的时间碎片 索引 */
typedef enum
{
	FRAG0 = 0,
	FRAG1,		
	FRAG2,
	FRAG3,
	FRAG4,
	FRAG5,
	FRAG6,
	FRAG7,
	FRAG8,
	FRAG9,
	FRAG10,
	FRAG11,
	FRAG12,
	FRAG13,
	FRAG14,
	FRAG15,
	FRAG16,
	FRAG17,
	FRAG18,
	FRAG19,
	FRAG20,
	FRAG21,
	FRAG22,
	FRAG23,
	FRAG24,
	FRAG25,
	FRAG26,
	FRAG27,	
	FRAG28,	
	FRAG29,
	FRAG30,
	FRAG31,
	FRAG32,
	FRAG33,
	FRAG34,
	FRAG35,
	FRAG36,
	FRAG37,
	FRAG38,
	FRAG39,
	FRAG40,
	FRAG41,
	FRAG42,
	FRAG43,
	FRAG44,
	FRAG45,
	FRAG46,
	FRAG47,
	FRAG48,
	FRAG49,
	FRAG50,
	FRAG51,
	FRAG52,
}time_frag_index_t;

typedef enum
{
    NEEDLE_S_NORMAL_TIME,
    NEEDLE_S_R1_TIME,
    NEEDLE_S_DILU_TIME,
    NEEDLE_S_DILU_R1_TIME,
    NEEDLE_S_R1_ONLY_TIME,
    NEEDLE_S_P_TIME,
    NEEDLE_S_DILU1_TIME,
    NEEDLE_S_DILU2_TIME,
    NEEDLE_S_DILU3_WITHOUT_DILU_TIME,
    NEEDLE_S_DILU3_WITH_DILU_TIME,
}needle_s_time_t;

typedef struct
{
    pos_t cur; /* 当前坐标位置 */

    pos_t t1_src; /* 0~4s，源工位的坐标 */
    pos_t t1_dst; /* 0~4s，目的工位的坐标 */

    pos_t t2_src; /* 4~8s，源工位的坐标 */
    pos_t t2_dst; /* 4~8s，目的工位的坐标 */

    pos_t t3_src; /* 8~12s，源工位的坐标 */
    pos_t t3_dst; /* 8~12s，目的工位的坐标 */
    
    pos_t t4_src; /* 12~16s，源工位的坐标 */
    pos_t t4_dst; /* 12~16s，目的工位的坐标 */
    
    pos_t t5_src; /* 16~20s，源工位的坐标 */
    pos_t t5_dst; /* 16~20s，目的工位的坐标 */
    
    pos_t t6_src; /* 20~24s，源工位的坐标 */
    pos_t t6_dst; /* 20~24s，目的工位的坐标 */
}module_param_t;


long long get_time();
long long get_time_us();
int sys_uptime_sec(void);
long sys_uptime_usec(void);
const char *log_get_time();
int module_sync_time(struct timeval *base_tv, int sync_time_ms);
int utf8togb2312(const char *sourcebuf, size_t sourcelen, char *destbuf, size_t destlen);
void load_default_conf_from_common(h3600_conf_t *h3600_conf);
void leave_singal_init(int flag);
void leave_singal_send(enum_leave_index_t index);
int leave_singal_timeout_wait(enum_leave_index_t index, int timeout);
void leave_singal_wait(enum_leave_index_t index);
void set_pos(pos_t* pos, int x, int y, int z);
void pos_all_init(const h3600_conf_t* h3600_conf, int show_flag);
void cup_pos_used_clear(void);
cup_pos_t get_special_pos(move_pos_t sub_type, cup_pos_t index, pos_t* pos, int lock_flag);
cup_pos_t get_valid_pos(move_pos_t sub_type, pos_t* pos, int lock_flag);
time_fragment_t *catcher_time_frag_table_get();
time_fragment_t *r2_normal_time_frag_table_get();
time_fragment_t *s_time_frag_table_get(needle_s_time_t st);
time_fragment_t *reagent_time_frag_table_get(void);
void catcher_record_pos_reinit(const h3600_conf_t *h3600_conf);
void pos_record_reag_table_for_r2_set(int which, int new_pos);

#ifdef __cplusplus
}
#endif

#endif

