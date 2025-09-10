#ifndef __MODULE_NEEDLE_R2_H__
#define __MODULE_NEEDLE_R2_H__

#ifdef __cplusplus
extern "C" {
#endif

#define R2_PUMP_AIR_STEP    1280
#define NEEDLE_R2_MORE      8
#define NEEDLE_R2_LESS      2
#define R2_ADD_MORE_FRAG    45

typedef struct
{
    module_param_t needle_r2_param;
    pos_t calc_pos;
    double take_ul;
    double curr_ul;
    clean_type_t clean_type;
    needle_pos_t r2_reagent_pos;
    mix_status_t mix_stat;
    struct magnectic_attr mag_attr;
    magnetic_pos_t mag_index;
    char cuvette_strno[16];
    int32_t cuvette_serialno;
}NEEDLE_R2_CMD_PARAM;

typedef enum
{
    R2_NORMAL_MODE = 0,
    R2_FAST_MODE = 1,
}r2_aging_speed_mode_t;

typedef enum
{
    R2_NO_CLEAN_MODE = 0,
    R2_NORMAL_CLEAN_MODE = 1,
}r2_aging_clean_mode_t;

int needle_r2_init(void);
int needle_r2_add_test(int add_ul);
int needle_r2_muti_add_test(int add_ul);
int needle_r2_poweron_check(int times, r2_aging_speed_mode_t mode, r2_aging_clean_mode_t clean_mode);
void* needle_r2_heat_test(int xitu, float take_ul1, int clean_type, int loop_cnt, int mode_flag, int pow_xi, int pow_nor ,int pow_spec, int predict_clean);

#ifdef __cplusplus
}
#endif

#endif

