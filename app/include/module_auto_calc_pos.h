#ifndef __MODULE_AUTO_CALC_POS_H__
#define __MODULE_AUTO_CALC_POS_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    pos_t cur; /* 当前坐标位置 */

    pos_t t1_src; /* 0~4s，源工位的坐标 */
    pos_t t1_dst; /* 0~4s，目的工位的坐标 */

}calc_pos_param_t;

typedef struct
{
    int32_t i_cali_id;          /* 标定ID */
    int32_t i_type;          /* 控制命令 0.标定 1.停止 2.写入           */

    int32_t userdata;
}auto_cali_param_t;

enum {
    ENG_DEBUG_NEEDLE = 0,
    ENG_DEBUG_GRIP,
    ENG_DEBUG_TABLE,
    ENG_DEBUG_NEEDLE_S = 10,
    ENG_DEBUG_NEEDLE_R2,
    ENG_DEBUG_GRIP1,
    ENG_DEBUG_TABLE1
};

typedef enum {
    INCUBATION_POOL_OLD,
    INCUBATION_POOL_NEW
} incubation_pool_type_t;

#define CALC(func) catcher_auto_check_pos_##func

/* 自动标定外部调用函数 */
int eng_gripper_auto_cali_func(int cali_id);
void grip_auto_calc_stop_flag_set(uint8_t v);
int eng_gripper_write_auto_calc_pos(void);

#ifdef __cplusplus
}
#endif

#endif

