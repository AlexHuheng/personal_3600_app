#ifndef __MODULE_NEEDLE_S_H__
#define __MODULE_NEEDLE_S_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "thrift_handler.h"

/* 样本针吸样类型 */
typedef enum
{
    NEEDLE_S_NONE = 0,
    NEEDLE_S_NORMAL_SAMPLE,
    NEEDLE_S_R1_SAMPLE,
    NEEDLE_S_DILU_SAMPLE,
    NEEDLE_S_R1_DILU_SAMPLE,
    NEEDLE_S_R1_ONLY,
    NEEDLE_S_DILU1_SAMPLE,
    NEEDLE_S_DILU2_R1,
    NEEDLE_S_DILU2_R1_TWICE,
    NEEDLE_S_DILU3_MIX,
    NEEDLE_S_DILU3_DILU_MIX,
    NEEDLE_S_SP,
    NEEDLE_S_SINGLE_SP,
}needle_s_cmd_t;

/* 样本针吸样类型 */
typedef enum
{
    SAMPLE_NORMAL = 0,
    SAMPLE_TEMP,
    SAMPLE_QC,
}sample_type_t;

typedef struct
{
    needle_s_cmd_t cmd;
    module_param_t needle_s_param;
    uint32_t orderno;
    pos_t calc_pos;
    double take_ul;
    double take_dilu_ul;
    double take_mix_ul;
    double take_r1_ul;
    double mix_curr_ul;
    double curr_ul;
    attr_enable_t stemp_pre_clean_enable;   /* 暂存池前清洗 */
    attr_enable_t stemp_post_clean_enable;  /* 暂存池后清洗 */
    attr_enable_t pre_clean_enable;
    clean_type_t pre_clean_type;
    clean_type_t now_clean_type;
    needle_pos_t r1_reagent_pos;
    needle_pos_t s_dilu_pos;
    sample_type_t sample_type;
    needle_pos_t qc_reagent_pos;
    cup_mix_pos_t mix_pos;
    mix_status_t mix_stat;
    sample_tube_type_t tube_type;
}NEEDLE_S_CMD_PARAM;

typedef struct
{
    uint32_t sample_tube_id;
    double sample_left_ul;    /* 样本管内未吸剩余缓存量 */
    double stemp_left_ul;   /* 暂存池剩余样本量 */
}sample_stamp_stat_t;

#define NEEDLE_S_DILU_MORE          10      /* 样本针吸稀释液多吸10ul */
#define NEEDLE_S_SAMPLE_MORE        15      /* 样本针吸样本多吸15ul */
#define NEEDLE_S_SAMPLE_LESS        2       /* 样本针吸样本多吸2ul(带适配器) */
#define NEEDLE_S_R1_MORE            8       /* 样本针吸R1多吸8ul */
#define NEEDLE_S_SP_MORE            45      /* 样本针穿刺暂存多吸35ul */
#define NEEDLE_S_SP_COMP            15      /* 样本针穿刺暂存池吐样补偿15ul */

#define NEEDLE_S_Z_REMOVE_SPEED     65000   /* 液面探测完成后上提速度 */
#define NEEDLE_S_Z_REMOVE_ACC       240000  /* 液面探测完成后上提加速度 */

#define STEM_TEMP_MAX_UL            (450 - STEM_TEMP_DEAD_UL)   /* 暂存池最大缓存量 */
#define STEM_TEMP_DEAD_UL           125                         /* 缓存池死腔体积 */
#define WASTE_SAMPLE_UL             15                          /* 普通加样多吸的样本量 */

#define CHECK_AR            BIT(0)
#define CHECK_CLOT          BIT(1)
#define CHECK_HIL           BIT(2)

typedef struct {
    uint32_t                rack_idx;           /* 试管架号 */
    uint32_t                pos_idx;            /* 在试管架上的位置号 */
    uint32_t                tube_id;            /* 样本tubeid */
    uint32_t                order_no;           /* 测试订单号 */
    uint32_t                sq_report;          /* 需要上报的种类 */
    double                  d;                  /* 抗凝筛查：采血管内径mm */
    double                  v;                  /* 抗凝筛查：采血量ml */
    double                  f;                  /* 抗凝筛查：浮动采血量百分比 */
    sample_quality_t        sq;                 /* 具体参数 */
    struct list_head        sqsibling;
} sq_info_t;

typedef enum
{
    S_NORMAL_MODE = 0,
    S_FAST_MODE = 1,
}s_aging_speed_mode_t;

typedef enum
{
    S_NO_CLEAN_MODE = 0,
    S_NORMAL_CLEAN_MODE = 1,
}s_aging_clean_mode_t;

void set_clear_needle_s_pos(uint8_t num);
needle_s_cmd_t get_needle_s_cmd(void);
sample_type_t get_needle_s_qc_type(void);
int needle_s_add_test(int add_ul);
int needle_s_dilu_add_test(int dilu_add_ul, int add_ul);
int needle_s_sp_add_test(int add_ul);
int needle_s_muti_add_test(int add_ul);
int needle_s_sp_muti_add_test(int add_ul);
int needle_s_poweron_check(int times, s_aging_speed_mode_t mode, s_aging_clean_mode_t clean_mode);
int needle_s_sp_aging_test(int tube_cnt, int max_pos);
int needle_s_avoid_catcher(void);
int needle_s_init(void);

void sq_list_clear(void);
void sq_add_tail(sq_info_t * info);
void sq_del_node(sq_info_t * info);
void sq_del_node_all(int rack_idx, int tube_id);
void report_list_show(void);

int sq_clot_check_flag_get(uint32_t orderno, int check_result);

#ifdef __cplusplus
}
#endif

#endif

