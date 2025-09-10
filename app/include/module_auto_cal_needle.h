#ifndef __MODULE_AUTO_CALC_NEEDLE_H__
#define __MODULE_AUTO_CALC_NEEDLE_H__

#include "module_liquid_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S_AUTO_CAL_V            1000        /* S自动校准的探测速度 */
#define R2_AUTO_CAL_V           300         /* R2自动校准探测速度 */
#define S_X_AUTO_CAL_V          150         /* X校准时的速度 */
#define S_Y_AUTO_CAL_V          100         /* Y校准时的速度 */
#define AUTO_CAL_Z_SPEED        10000       /* S&R2 Z轴校准时速度 */
#define AUTO_CAL_Z_ACC          50000       /* S&R2 Z轴校准时加速度 */
#define AUTO_CAL_S_Y_THR        1500        /* 自动标定样本针的阈值 */
#define AUTO_CAL_S_X_THR        1500        /* 自动标定样本针X的阈值 */
#define AUTO_CAL_S_Z_THR        1500        /* 自动标定样本针Z的阈值 */
#define AUTO_CAL_R2_THR         800         /* 自动标定试剂针的阈值 */
#define AUTO_CAL_R2_THR1        1000        /* 自动标定试剂针的阈值 */
#define AUTO_CAL_R2_Z_THR       2000        /* 自动标定试剂针Z的阈值 */
#define XY_AVOID_OFFSET         100         /* XY避让脉冲 */
#define R2_RETRY_MAX            30
#define AUTO_CAL_TURN_MAX       2           /* 自动查找工装的轮次 */
#define AUTO_CAL_COUNT_MAX      10          /* 校准时复探次数 */
#define AUTO_CAL_TIMEOUT        30000       /* 自动校准超时时间 */

#define AUTO_CAL_S_SAMPLE_X_D   555         /* 样本针取样位1/10/60X向直径512 */
#define AUTO_CAL_S_SAMPLE_Y_D   370         /* 样本针取样位1/10/60Y向直径341 */
#define AUTO_CAL_S_REAG_X_D     898         /* 样本针缓存/试剂内外X向直径927 */
#define AUTO_CAL_S_REAG_Y_D     602         /* 样本针缓存/试剂内外Y向直径619 */
#define AUTO_CAL_S_ADD_X_D      575         /* 样本针加样/混匀X向直径608 */
#define AUTO_CAL_S_ADD_Y_D      332         /* 样本针加样/混匀Y向直径355 */
#define AUTO_CAL_R2_CLEAN_Y_D   903         /* 试剂针洗针位Y向直径907 */
#define AUTO_CAL_R2_MIX_Y_D     332         /* 试剂针光学Y向直径323 */
#define AUTO_CAL_R2_MAG_Y_D     333         /* 试剂针磁珠Y向直径337 */
#define AUTO_CAL_R2_REAG_Y_D    582         /* 试剂针试剂内外Y向直径 */

/* 自动标定工装和手动标定工装Z向差异 */
#define S_SAMPLE_1_DIFF         (-1560)
#define S_SAMPLE_10_DIFF        (-1520)
#define S_SAMPLE_60_DIFF        (-1540)
#define S_ADD_SAMPLE_DIFF       1110
#define S_MIX_1_DIFF            1110
#define S_MIX_2_DIFF            1110
#define S_TEMP_DIFF             3360
#define S_INS_DIFF              (-15170)
#define S_OUT_DIFF              (-15160)
#define R2_INS_DIFF             (-4025)
#define R2_OUT_DIFF             (-4020)
#define R2_MIX_DIFF             490
#define R2_MAG_1_DIFF           490
#define R2_MAG_2_DIFF           490
#define R2_CLEAN_DIFF           1090

#define S_DILU_1_X_DIFF         4555        /* 稀释液1X坐标差异，基于自动标定的缓存位X */
#define S_DILU_1_Y_DIFF         0           /* 稀释液1Y坐标差异，基于自动标定的缓存位Y */
#define S_DILU_1_Z_DIFF         7765        /* 稀释液1Z坐标差异，基于自动标定的缓存位Z */
#define S_DILU_2_X_DIFF         7575        /* 稀释液2X坐标差异，基于自动标定的缓存位X */
#define S_DILU_2_Y_DIFF         0           /* 稀释液2Y坐标差异，基于自动标定的缓存位Y */
#define S_DILU_2_Z_DIFF         7765        /* 稀释液2Z坐标差异，基于自动标定的缓存位Z */

/* 自动标定探测时返回的错误码，不能复用正常探测所定义的错误码，因为自动标定时正常返回的脉冲可能为一个较小的正值或负值 */
#define AT_EMAXSTEP             (-5001)     /* 探测最大步长 */
#define AT_ETIMEOUT             (-5002)     /* 探测超时 */
#define AT_ECOLLSION            (-5003)     /* 探测撞针 */
#define AT_EMOVE                (-5004)     /* 电机控制失败 */
#define AT_EMSTOP               (-5005)     /* 手动停止 */
#define AT_ENOTMATCH            (-5006)     /* 数据不匹配停止 */
#define AT_EIO                  (-5007)     /* 光电检测失败(电机不在预期位置) */
#define AT_ENOTFOUND            (-5008)     /* 未查找到工装 */
#define AT_EARG                 (-5009)     /* 无效参数 */
#define AT_ESAMPLER             (-5010)     /* 进样器锁控制失败 */

typedef enum obj_location {
    CENTER = 0,
    WEST,
    WESTNORTH,
    NORTH,
    EASTNORTH,
    EAST,
    EASTSOUTH,
    SOUTH,
    WESTSOUTH,
    OLMAX
} obj_location_t;

typedef enum auto_cal_status {
    AUTO_CAL_INIT = 0,
    AUTO_CAL_PASS,      /* 粗校完成 */
    AUTO_CAL_DONE,      /* 精校完成 */
} auto_cal_status_t;

typedef enum auto_cal_r2_l {
    LEFT_SIDE = 0,
    MIDDLE,
    RIGHT_SIDE
} auto_cal_r2_l_t;

typedef enum auto_cal_dir {
    DIR_INIT = -1,
    FORWARD,        /* 轴正向运动探测 */
    REVERSE,        /* 轴反向运动探测 */
} auto_cal_dir_t;

typedef enum auto_cal_mode {
    SERVICE_MODE = 0,   /* service自动标定 */
    ENG_MODE            /* 工程师模式自动标定 */
} auto_cal_mode_t;

typedef enum liquid_detect_auto_cal_idx {
    POS_REAG_INSIDE = 0,
    POS_REAG_OUTSIDE,
    POS_SAMPLE_1,
    POS_SAMPLE_10,
    POS_SAMPLE_60,
    POS_ADD_SAMPLE = 5,
    POS_SAMPLE_MIX_1,
    POS_SAMPLE_MIX_2,
    POS_SAMPLE_TEMP,
    POS_R2_MIX,
    POS_R2_MAG_1 = 10,
    POS_R2_MAG_4,
    POS_R2_CLEAN,
    POS_R2_INSIDE,
    POS_R2_OUTSIDE,
    POS_AUTO_CAL_MAX
} __pk auto_cal_idx_t;

typedef enum auto_cal_pos_idx {
    POS_REAG_INSIDE_X = 0,
    POS_REAG_INSIDE_Y,
    POS_REAG_INSIDE_Z,
    POS_REAG_OUTSIDE_X,
    POS_REAG_OUTSIDE_Y,
    POS_REAG_OUTSIDE_Z,
    POS_SAMPLE_1_X,
    POS_SAMPLE_1_Y,
    POS_SAMPLE_1_Z,
    POS_SAMPLE_10_X,
    POS_SAMPLE_10_Y = 10,
    POS_SAMPLE_10_Z,
    POS_SAMPLE_60_X,
    POS_SAMPLE_60_Y,
    POS_SAMPLE_60_Z,
    POS_ADD_SAMPLE_X,
    POS_ADD_SAMPLE_Y,
    POS_ADD_SAMPLE_Z,
    POS_SAMPLE_MIX_1_X,
    POS_SAMPLE_MIX_1_Y,
    POS_SAMPLE_MIX_1_Z = 20,
    POS_SAMPLE_MIX_2_X,
    POS_SAMPLE_MIX_2_Y,
    POS_SAMPLE_MIX_2_Z,
    POS_SAMPLE_TEMP_X,
    POS_SAMPLE_TEMP_Y,
    POS_SAMPLE_TEMP_Z,
    POS_R2_MIX_Y,
    POS_R2_MIX_Z,
    POS_R2_MAG_1_Y,
    POS_R2_MAG_1_Z = 30,
    POS_R2_MAG_4_Y,
    POS_R2_MAG_4_Z,
    POS_R2_CLEAN_Y,
    POS_R2_CLEAN_Z,
    POS_R2_INSIDE_Y,
    POS_R2_INSIDE_Z,
    POS_R2_INSIDE_TABLE,
    POS_R2_OUTSIDE_Y,
    POS_R2_OUTSIDE_Z,
    POS_R2_OUTSIDE_TABLE
} __pk auto_cal_pos_idx_t;

typedef struct auto_cal_pos {
    int y;
    int reag;
    int diff;
} __pk auto_cal_pos_t;

typedef struct auto_cal_s_pos {
    int x0;
    int x1;
    int x_mid;
    int x_diff;
    int y0;
    int y1;
    int y_mid;
    int y_diff;
} __pk auto_cal_s_pos_t;

typedef struct auto_cal_r2_pos {
    int y0;
    int y1;
    int y_mid;
    int y_diff;
    int reag;
} __pk auto_cal_r2_pos_t;

/* 用于坐标写入记录 */
typedef struct auto_cal_rcd {
    pos_t pos;
    int reag_table;
} __pk auto_cal_rcd_t;

typedef struct auto_cal_task {
    int ret;
    sem_t auto_cal_sem;
} auto_cal_task_t;

int thrift_auto_cal_func(void);
int thrift_auto_cal_single_func(auto_cal_idx_t target_idx);
int eng_auto_cal_func(int32_t id);
int auto_cal_func_test(void);
void auto_cal_reinit_data(void);
void auto_cal_reinit_data_one(needle_type_t needle);

#ifdef __cplusplus
}
#endif

#endif
