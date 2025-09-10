#ifndef H3600_NEEDLE_H
#define H3600_NEEDLE_H

typedef enum{
    NEEDLE_TYPE_S = 0,      /* 样本针S */
    NEEDLE_TYPE_S_R1,       /* 样本针S加R1 */
    NEEDLE_TYPE_R2,         /* 试剂针R2 */
    NEEDLE_TYPE_S_DILU,     /* 样本针S 仅用于稀释加样KB或穿刺针吸样 */
    NEEDLE_TYPE_S_BOTH,     /* 穿刺样本针S吐样 */
    NEEDLE_TYPE_MAX,
}needle_type_t;

typedef enum{
    SIDE,
    BOTTOM,
}perfusion_type_t;

typedef enum clean_cmd {
    PRE_START,
    START,
    PRE_CLOSE,
    CLOSE,
    ALL_START,
    ALL_CLOSE,
    ALL_CLEAN,
    ALL_CLEAN_CLOSE,
    SPEC_CLOSE
} clean_cmd_t;

typedef enum{
    R2_CLEAN_NONE = 0,
    R2_NORMAL_CLEAN,
    R2_SPECIAL_CLEAN
}r2_clean_flag_t;

#define ON  1
#define OFF 0

#define NEEDLE_S_MIX_STEP           50
#define NEEDLE_S_MIX_SPEED          1000
#define NEEDLE_S_MIX_TIME_MS        10000

#define NEEDLE_TRANS_STEPS          128
#define NEEDLE_ABSORB_MORE          20
#define NEEDLE_ABSORB_MORE_DILU     10
#define NEEDLE_RELEASE_MORE         5
#define NEEDLE_RELEASE_MORE_R       5

#define NEEDLE_SC_COMP_STEP         2570
#define NEEDLE_R2C_COMP_STEP        3724
#define NEEDLE_S_NOR_CLEAN_STEP     1690
#define NEEDLE_R1_NOR_CLEAN_STEP    1690
#define NEEDLE_R2_NOR_CLEAN_STEP    2426
#define NEEDLE_SPC_COMP_STEP        17300
#define NEEDLS_SPC_ADD_STEP         2000
#define NEEDLE_R2_NORM_CLR_STEPS    150
#define NEEDLE_SP_TEMP_RATIO        12
#define NEEDLE_S_SPECIAL_COMP_STEP  7700

#define PUMP_ACC_NORMAL             35000
#define PUMP_SPEED_NORMAL           35000
#define PUMP_SPEED_SLOWEST          20000
#define PUMP_SPEED_FAST             35000
#define PUMP_SPEED_SLOW             25000
#define PUMP_ACC_FAST               35000
#define PUMP_ACC_MIDDLE             35000
#define PUMP_ACC_SLOW               25000
#define PUMP_ACC_SLOWEST            20000
#define PUMP_SPEED_FASTEST          45000
#define PUMP_ACC_FASTEST            180000

#define S_PUMP_1MM_STEPS            ((int)(h3600_conf.liquid_amount[NEEDLE_TYPE_S_DILU].k_ratio))  /* 固定使用R3的K值 */

#define NEEDLE_S_V0_SPEED           2000    /* 穿刺针Z初速度 */
#define NEEDLE_S_SPEED              60000   /* 穿刺针Z复位速度 */

#define NEEDLE_S_CLEAN_POS          (-3150) /* 20250119重新评估更改挡片后样本针洗针位置 */

#define NEEDLE_PERFUSION_INTERVAL 5 /* 针灌注的等待间隔 */
#define NEEDLE_PERFUSION_TIMEOUT (100/NEEDLE_PERFUSION_INTERVAL) /* 针灌注的超时次数 */

#define WASH_S_NORMAL_PER_TIME  (2258)  /* 样本针内壁清洗的清洗液B用量 */
#define WASH_S_OUT_PER_TIME     (478)   /* 样本针外壁清洗的清洗液B用量 */
#define WASH_S_SPEC_A_PER_TIME  (7568)  /* 样本针特殊清洗的清洗液B用量 */
#define WASH_R2_NORMAL_PER_TIME (3488)  /* R2普通清洗的清洗液B用量 */
#define WASH_R2_SPEC_A_PER_TIME (5707)  /* R2特殊清洗的清洗液B用量 */
#define WASH_R2_SPEC_B_PER_TIME (140)   /* R2特殊清洗的清洗液A用量 */
#define WASH_S_SPEC_B_PER_TIME  (120)   /* 样本针特殊清洗的清洗液A用量 */

#define LIQUID_PIPELINE_PERF     (27109)   /* 管路填充普通清洗液B用量 */
#define LIQUID_SPEC_PIPE_CLR     (18601)   /* 特殊清洗液管路清洗液B用量 */
#define LIQUID_STAGE_POOL_PRECLR (684)   /* 暂存池前清洗液B用量 */
#define LIQUID_SPEC_PIPE_LASTCLR (2122)   /* 暂存池后清洗液B用量 */

int needle_absorb_ul(needle_type_t needle_x, double amount_ul);
int needle_release_ul(needle_type_t needle_x, double amount_ul, int comps_step);
int needle_release_ul_ctl(needle_type_t needle_x, double amount_ul, double cost_time, int comps_step);
int ul_to_step(needle_type_t needle_x, double amount_ul);
int needle_calc_add_pos(needle_type_t needle_x, uint32_t curr_ul, pos_t *pos);
int needle_s_calc_stemp_pos(uint32_t curr_ul, pos_t *pos);
void s_normal_outside_clean(int flag);
void s_normal_inside_clean(void);
void r2_normal_clean(void);
void r2_special_clean(void);
void s_special_clean(double cost_time);
void kb_ul_init(void);
r2_clean_flag_t get_r2_clean_flag(void);
void set_r2_clean_flag(r2_clean_flag_t flag);

#endif

