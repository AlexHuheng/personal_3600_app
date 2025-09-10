#include "module_auto_cal_needle.h"
#include "module_temperate_ctl.h"
#include "module_magnetic_bead.h"
#include "module_engineer_debug_position.h"
#include "module_auto_calc_pos.h"

extern int g_detect_running;
extern liquid_detect_misc_t g_misc_array[NEEDLE_TYPE_MAX];

static int auto_cal_err_flag = 0;   /* 与stop flag不同，针1某位置标定失败不影响针2的当前标定 */
pthread_mutex_t auto_cal_mtx = PTHREAD_MUTEX_INITIALIZER;

static auto_cal_rcd_t g_old_pos[POS_AUTO_CAL_MAX] = {0};
static auto_cal_rcd_t g_new_pos[POS_AUTO_CAL_MAX] = {0};
static auto_cal_rcd_t g_diff[POS_AUTO_CAL_MAX] = {
    {{0, 0, S_INS_DIFF}, 0},
    {{0, 0, S_OUT_DIFF}, 0},
    {{0, 0, S_SAMPLE_1_DIFF}, 0},
    {{0, 0, S_SAMPLE_10_DIFF}, 0},
    {{0, 0, S_SAMPLE_60_DIFF}, 0},
    {{0, 0, S_ADD_SAMPLE_DIFF}, 0},
    {{0, 0, S_MIX_1_DIFF}, 0},
    {{0, 0, S_MIX_2_DIFF}, 0},
    {{0, 0, S_TEMP_DIFF}, 0},
    {{0, 0, R2_MIX_DIFF}, 0},
    {{0, 0, R2_MAG_1_DIFF}, 0},
    {{0, 0, R2_MAG_2_DIFF}, 0},
    {{0, 0, R2_CLEAN_DIFF}, 0},
    {{0, 0, R2_INS_DIFF}, 0},
    {{0, 0, R2_OUT_DIFF}, 0}
};

static int slip_motor_dual_step_timedwait_slow(unsigned char motor_id, int stepx, int stepy, int msec)
{
    int speed, acc;

    speed = 500;
    acc = 5000;

    LOG("motor %d move stepx %d stepy %d\n", motor_id, stepy, stepx);

    if (0 == motor_step_dual_ctl_timedwait(motor_id, stepx, stepy, speed, acc, msec, 0.01)) {
        return stepx;
    }

    return -1;
}

static char *auto_cal_idx_string(auto_cal_idx_t pos_idx)
{
    if (pos_idx == POS_SAMPLE_1) {
        return "Sample_1";
    } else if (pos_idx == POS_SAMPLE_10) {
        return "Sample_10";
    } else if (pos_idx == POS_SAMPLE_60) {
        return "Sample_60";
    } else if (pos_idx == POS_ADD_SAMPLE) {
        return "Sample_add";
    } else if (pos_idx == POS_SAMPLE_MIX_1) {
        return "Sample_mix-1";
    } else if (pos_idx == POS_SAMPLE_MIX_2) {
        return "Sample_mix-2";
    } else if (pos_idx == POS_REAG_INSIDE) {
        return "Sample_reag_in";
    } else if (pos_idx == POS_REAG_OUTSIDE) {
        return "Sample_reag_out";
    } else if (pos_idx == POS_SAMPLE_TEMP) {
        return "Sample_temp";
    } else if (pos_idx == POS_R2_INSIDE) {
        return "R2_reag_in";
    } else if (pos_idx == POS_R2_OUTSIDE) {
        return "R2_reag_out";
    } else if (pos_idx == POS_R2_MIX) {
        return "R2_mix";
    } else if (pos_idx == POS_R2_MAG_1) {
        return "R2_mag-1";
    } else if (pos_idx == POS_R2_MAG_4) {
        return "R2_mag-4";
    } else if (pos_idx == POS_R2_CLEAN) {
        return "R2_clean";
    }

    return "Unknown";
}

void auto_cal_reinit_data_one(needle_type_t needle)
{
    if (needle == NEEDLE_TYPE_S) {
        /* 常规取样位1 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_1] = g_new_pos[POS_SAMPLE_1].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_1] = g_new_pos[POS_SAMPLE_1].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_1] =
            g_new_pos[POS_SAMPLE_1].pos.z + g_diff[POS_SAMPLE_1].pos.z;
        /* 常规取样位10 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_10] = g_new_pos[POS_SAMPLE_10].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_10] = g_new_pos[POS_SAMPLE_10].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_10] =
            g_new_pos[POS_SAMPLE_10].pos.z + g_diff[POS_SAMPLE_10].pos.z;
        /* 常规取样位60 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_60] = g_new_pos[POS_SAMPLE_60].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_60] = g_new_pos[POS_SAMPLE_60].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_60] =
            g_new_pos[POS_SAMPLE_60].pos.z +  + g_diff[POS_SAMPLE_60].pos.z;
        /* 常规加样位 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_PRE] = g_new_pos[POS_ADD_SAMPLE].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_PRE] = g_new_pos[POS_ADD_SAMPLE].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_PRE] =
            g_new_pos[POS_ADD_SAMPLE].pos.z + g_diff[POS_ADD_SAMPLE].pos.z;
        /* 混匀加样位 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_MIX1] = g_new_pos[POS_SAMPLE_MIX_1].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_MIX1] = g_new_pos[POS_SAMPLE_MIX_1].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_MIX1] =
            g_new_pos[POS_SAMPLE_MIX_1].pos.z + g_diff[POS_SAMPLE_MIX_1].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_MIX2] = g_new_pos[POS_SAMPLE_MIX_2].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_MIX2] = g_new_pos[POS_SAMPLE_MIX_2].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_MIX2] =
            g_new_pos[POS_SAMPLE_MIX_2].pos.z + g_diff[POS_SAMPLE_MIX_2].pos.z;
        /* 试剂仓吸样位 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_REAGENT_TABLE_IN] = g_new_pos[POS_REAG_INSIDE].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_REAGENT_TABLE_IN] = g_new_pos[POS_REAG_INSIDE].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_REAGENT_TABLE_IN] =
            g_new_pos[POS_REAG_INSIDE].pos.z + g_diff[POS_REAG_INSIDE].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_REAGENT_TABLE_OUT] = g_new_pos[POS_REAG_OUTSIDE].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_REAGENT_TABLE_OUT] = g_new_pos[POS_REAG_OUTSIDE].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_REAGENT_TABLE_OUT] =
            g_new_pos[POS_REAG_OUTSIDE].pos.z + g_diff[POS_REAG_OUTSIDE].pos.z;
        /* 洗针位 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_CLEAN] = g_new_pos[POS_SAMPLE_TEMP].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_CLEAN] = g_new_pos[POS_SAMPLE_TEMP].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_CLEAN] =
            g_new_pos[POS_SAMPLE_TEMP].pos.z + g_diff[POS_SAMPLE_TEMP].pos.z;
        /* 暂存位 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_TEMP] = g_new_pos[POS_SAMPLE_TEMP].pos.x;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_TEMP] = g_new_pos[POS_SAMPLE_TEMP].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_TEMP] =
            g_new_pos[POS_SAMPLE_TEMP].pos.z + g_diff[POS_SAMPLE_TEMP].pos.z;

        /* 稀释液位 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_1] = g_new_pos[POS_SAMPLE_TEMP].pos.x + S_DILU_1_X_DIFF;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_1] = g_new_pos[POS_SAMPLE_TEMP].pos.y + S_DILU_1_Y_DIFF;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_DILU_1] = g_new_pos[POS_SAMPLE_TEMP].pos.z + S_DILU_1_Z_DIFF;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_2] = g_new_pos[POS_SAMPLE_TEMP].pos.x + S_DILU_2_X_DIFF;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_2] = g_new_pos[POS_SAMPLE_TEMP].pos.y + S_DILU_2_Y_DIFF;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_DILU_2] = g_new_pos[POS_SAMPLE_TEMP].pos.z + S_DILU_2_Z_DIFF;
    } else if (needle == NEEDLE_TYPE_R2) {
        /* 试剂针R2 位置点 */
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_IN] = g_new_pos[POS_R2_INSIDE].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_OUT] = g_new_pos[POS_R2_OUTSIDE].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MIX_1] = g_new_pos[POS_R2_MIX].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MAG_1] = g_new_pos[POS_R2_MAG_1].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MAG_4] = g_new_pos[POS_R2_MAG_4].pos.y;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN] = g_new_pos[POS_R2_CLEAN].pos.y;

        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_REAGENT_IN] = g_new_pos[POS_R2_INSIDE].pos.z + g_diff[POS_R2_INSIDE].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_REAGENT_OUT] = g_new_pos[POS_R2_OUTSIDE].pos.z + g_diff[POS_R2_OUTSIDE].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MIX_1] = g_new_pos[POS_R2_MIX].pos.z + g_diff[POS_R2_MIX].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MAG_1] = g_new_pos[POS_R2_MAG_1].pos.z + g_diff[POS_R2_MAG_1].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MAG_4] = g_new_pos[POS_R2_MAG_4].pos.z + g_diff[POS_R2_MAG_4].pos.z;
        h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN] = g_new_pos[POS_R2_CLEAN].pos.z + g_diff[POS_R2_CLEAN].pos.z;

        /* 试剂仓 */
        h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN] = g_new_pos[POS_R2_INSIDE].reag_table;
        h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT] = g_new_pos[POS_R2_OUTSIDE].reag_table;
    }
}


void auto_cal_reinit_data(void)
{
    auto_cal_reinit_data_one(NEEDLE_TYPE_S);
    auto_cal_reinit_data_one(NEEDLE_TYPE_R2);
}

/* 检查针Z向是否在复位光电内 */
static int auto_cal_z_check(needle_type_t needle)
{
    if (needle == NEEDLE_TYPE_R2) {
        if (gpio_get(PE_NEEDLE_R2_Z) != 0) {
            LOG("R2.z io not detect.\n");
            return -1;
        }
    } else {
        if (gpio_get(PE_NEEDLE_S_Z) != 1) {
            LOG("S.z io not detect.\n");
            return -1;
        }
    }

    return 0;
}

static int auto_cal_sampler_ctrl(auto_cal_idx_t idx, attr_enable_t lock)
{
    if (idx == POS_SAMPLE_1 || idx == POS_SAMPLE_10) {
        if (slip_ele_lock_to_sampler(0, lock) < 0) {
            LOG("auto_cal: sample-1 %s failed.\n", lock == ATTR_ENABLE ? "lock" : "unlock");
            return -1;
        }
        LOG("auto_cal: sample-1 %s successed.\n", lock == ATTR_ENABLE ? "lock" : "unlock");
    } else if (idx == POS_SAMPLE_60) {
        if (slip_ele_lock_to_sampler(5, lock) < 0) {
            LOG("auto_cal: sample-6 %s failed.\n", lock == ATTR_ENABLE ? "lock" : "unlock");
            return -1;
        }
        LOG("auto_cal: sample-6 %s successed.\n", lock == ATTR_ENABLE ? "lock" : "unlock");
    }
    return 0;
}

static int auto_cal_motor_reset(void)
{
    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    if (slip_ele_lock_to_sampler(0, 0) < 0) {
        return AT_ESAMPLER;
    }

    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    if (slip_ele_lock_to_sampler(RACK_NUM_MAX - 1, 0) < 0) {
        return AT_ESAMPLER;
    }

    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    motor_reset(MOTOR_NEEDLE_R2_Z, 1);
    motor_reset(MOTOR_CATCHER_Z, 1);

    /* 等待针的Z轴复位完成 */
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        return AT_EMOVE;
    }
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles R2.z timeout.\n");
        return AT_EMOVE;
    }

    motor_reset(MOTOR_REAGENT_TABLE, 1);    /* 试剂盘上电复位耗时最长大约35s */

    /* 等待抓手的Z轴复位完成 */
    if (0 != motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset graps A.z timeout.\n");
        return AT_EMOVE;
    }

    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    motor_reset(MOTOR_CATCHER_Y, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.y timeout.\n");
        return AT_EMOVE;
    }

    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    motor_reset(MOTOR_CATCHER_X, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT*2)) {
        LOG("reset catcher.x timeout.\n");
        return AT_EMOVE;
    }

    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    motor_reset(MOTOR_NEEDLE_S_Y, 1);
    motor_reset(MOTOR_NEEDLE_R2_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.x timeout.\n");
        return AT_EMOVE;
    }
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles R2.y timeout.\n");
        return AT_EMOVE;
    }

    if (auto_cal_stop_flag_get()) {
        LOG("auto_cal: stop flag = 1, stop.\n");
        return AT_EMSTOP;
    }
    motor_reset(MOTOR_NEEDLE_S_X, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_X, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.y timeout.\n");
        return AT_EMOVE;
    }

    if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT * 2)) {
        LOG("reset reagent table timeout.\n");
        return AT_EMOVE;
    }
    reinit_reagent_table_data();

    return 0;
}

static int auto_cal_r2_reset(int flag)
{
    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].speed,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("R2.z rst timeout.\n");
        return AT_EMOVE;
    } else {
        if (gpio_get(PE_NEEDLE_R2_Z) != 0) {
            LOG("R2.z io not detect.\n");
            return AT_EIO;
        }
    }

    if (flag) {
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        motor_reset(MOTOR_REAGENT_TABLE, 1);    /* 试剂盘上电复位耗时最长大约35s */
    }

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].speed / 2,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].acc / 2,
        MOTOR_DEFAULT_TIMEOUT)) {
        LOG("R2.y rst timeout.\n");
        return AT_EMOVE;
    } else {
        if (gpio_get(PE_NEEDLE_R2_Y) != 0) {
            LOG("R2.y io not detect.\n");
            return AT_EIO;
        }
    }

    if (flag) {
        if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT * 2)) {
            LOG("reagent table rst timeout.\n");
            return AT_EMOVE;
        }
    }
    reag_table_occupy_flag_set(0);
    if (flag) reinit_reagent_table_data();

    return 0;
}

static int auto_cal_s_reset(int flag)
{
    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0,
        h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed,
        h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("S.z rst timeout.\n");
        return AT_EMOVE;
    } else {
        if (gpio_get(PE_NEEDLE_S_Z) != 1) {
            LOG("S.z io not detect.\n");
            return AT_EIO;
        }
    }

    if (flag) {
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        motor_reset(MOTOR_REAGENT_TABLE, 1);    /* 试剂盘上电复位耗时最长大约35s */
    }

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (motor_reset_ctl_timedwait(MOTOR_NEEDLE_S_Y, 2, 10000, 20000, MOTOR_DEFAULT_TIMEOUT, 0) < 0) {
        LOG("S.xy rst timeout.\n");
        return AT_EMOVE;
    } else {
        if (gpio_get(PE_NEEDLE_S_X) != 0 && gpio_get(PE_NEEDLE_S_Y) != 0) {
            LOG("S.xy io not detect.\n");
            return AT_EIO;
        }
    }

    if (flag) {
        if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT * 2)) {
            LOG("reagent table rst timeout.\n");
            return AT_EMOVE;
        }
    }
    reag_table_occupy_flag_set(0);
    if (flag) reinit_reagent_table_data();

    return 0;
}

static int auto_cal_z_reset(needle_type_t needle)
{
    int motor = (needle == NEEDLE_TYPE_R2 ? MOTOR_NEEDLE_R2_Z : MOTOR_NEEDLE_S_Z);

    if (motor_move_ctl_sync(motor, CMD_MOTOR_RST, 0, AUTO_CAL_Z_SPEED, AUTO_CAL_Z_ACC, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("%s.z rst failed.\n", needle_type_string(needle));
        return -1;
    } else {
        if (auto_cal_z_check(needle) < 0) {
            return -1;
        }
    }

    return 0;
}

/* 失败返回AT_EMAXSTEP/AT_ETIMEOUT/AT_EMOVE/AT_ECOLLSION，成功返回探测脉冲 */
static int auto_cal_detect_start(auto_cal_idx_t target_idx, needle_type_t needle, int motor, pos_t std, int cur_step, int offset)
{
    int ret = AT_EMAXSTEP, now_step = 0, ref_step = 0, speed_speed = 0, max_step = 0, thr = 0;
    int reserve_mm = 0, step_step = 0, speed_step = 0;
    liquid_detect_misc_t *misc_para = NULL;

    LOG("%s needle = %s, motor = %d, std = %d - %d - %d, cur_step = %d, offset = %d.\n",
        auto_cal_idx_string(target_idx), needle_type_string(needle), motor,
        (needle == NEEDLE_TYPE_R2 ? std.x : std.y),
        (needle == NEEDLE_TYPE_R2 ? std.y : std.x), 
        std.z, cur_step, offset);

    /* maxstep缩小一点，防止float值影响，maxstep-(step_step+speed_step)>1无法报最大步长超时的情况 */
    if (motor == MOTOR_NEEDLE_S_Z || motor == MOTOR_NEEDLE_R2_Z) {
        ref_step = std.z;
        max_step = std.z + offset - 0.1 * (needle_z_1mm_steps(needle));
        speed_speed = (motor == MOTOR_NEEDLE_S_Z ? S_AUTO_CAL_V : R2_AUTO_CAL_V);
        thr = (motor == MOTOR_NEEDLE_S_Z ? AUTO_CAL_S_Z_THR : AUTO_CAL_R2_Z_THR);
    } else if (motor == MOTOR_NEEDLE_S_X) {
        ref_step = std.y;
        max_step = cur_step + offset - 0.1 / S_X_STEP_MM_RATIO;
        speed_speed = S_X_AUTO_CAL_V;
        thr = AUTO_CAL_S_X_THR;
    } else {
        if (motor == MOTOR_NEEDLE_S_Y) {
            ref_step = std.x;
            thr = AUTO_CAL_S_Y_THR;
        } else {
            ref_step = std.y;
            thr = (target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE ? AUTO_CAL_R2_THR1 : AUTO_CAL_R2_THR);
        }
        max_step = cur_step + offset - 0.1 / S_Y_STEP_MM_RATIO;
        speed_speed = S_Y_AUTO_CAL_V;
    }
    LOG("%s motor = %d, ref_step = %d, max_step = %d, speed_speed = %d, thr = %d.\n",
        auto_cal_idx_string(target_idx), motor, ref_step, max_step, speed_speed, thr);

    now_step = motor_current_pos_timedwait(motor, NORMAL_TIMEOUT);
    if (cur_step != now_step) {
        LOG("step Not macth(now = %d, cur = %d).\n", now_step, cur_step);
        ret = AT_EMAXSTEP;
        goto out;
    }

    if (slip_liquid_detect_type_set(needle) < 0) {
        LOG("%s set arg failed.\n", needle_type_string(needle));
        ret = AT_ETIMEOUT;
        goto out;
    }
    usleep(10 * 1000);
    if (slip_liquid_detect_thr_set(needle, thr) < 0) {
        LOG("%s set arg failed.\n", needle_type_string(needle));
        ret = AT_ETIMEOUT;
        goto out;
    }
    g_detect_running = 1;

    if (motor == MOTOR_NEEDLE_S_X || motor == MOTOR_NEEDLE_S_Y) {
        if (offset > 0) {
            if (motor_speed_timedwait(motor, speed_speed, NORMAL_TIMEOUT) < 0) {
                LOG("%s move failed.\n", needle_type_string(needle));
                ret = AT_EMOVE;
                goto out;
            }
        } else {
            if (motor_speed_timedwait(motor, -speed_speed, NORMAL_TIMEOUT) < 0) {
                LOG("%s move failed.\n", needle_type_string(needle));
                ret = AT_EMOVE;
                goto out;
            }
        }
    } else {
        if (motor == MOTOR_NEEDLE_S_Z || motor == MOTOR_NEEDLE_R2_Z) {
            if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10 || target_idx == POS_SAMPLE_60) {
                /* 取样位3个工装特殊，如果台间差异过大，导致Z向标准位置在外圈正上方，则需要注意步长阶段直接撞针风险 */
                reserve_mm = 12;    /* 工装高低差异7mm+台间差异3mm+探测高度预留2mm */
                step_step = ref_step - reserve_mm * needle_z_1mm_steps(needle);
                speed_step = reserve_mm * needle_z_1mm_steps(needle) + offset;
            } else if (target_idx == POS_SAMPLE_TEMP) {
                /* 工装距离复位位置仅2mm */
                step_step = 0;
                speed_speed = ref_step + offset;
            } else {
                reserve_mm = 5;     /* 台间差异3mm+探测高度预留2mm */
                step_step = ref_step - reserve_mm *needle_z_1mm_steps(needle);
                speed_step = reserve_mm * needle_z_1mm_steps(needle) + offset;
            }
            if (step_step <= 0 && target_idx != POS_SAMPLE_TEMP) {
                LOG("%s cal failed, cant be here.\n", auto_cal_idx_string(target_idx));
                ret = AT_EMOVE;
                goto out;
            }
            LOG("%s.z step_step = %d, speed_step = %d.\n",  needle_type_string(needle), step_step, speed_step);
            if (step_step > 0 && motor_move_ctl_sync(motor, CMD_MOTOR_MOVE_STEP, step_step,
                h3600_conf_get()->motor[motor].speed / 4, h3600_conf_get()->motor[motor].acc / 4, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("%s.z move failed.\n",  needle_type_string(needle));
                ret = AT_EMOVE;
                goto out;
            }
            if (motor_speed_ctl_timedwait(motor, speed_speed, 10000, speed_step, 1, NORMAL_TIMEOUT) < 0) {
                LOG("%s move failed.\n", needle_type_string(needle));
                ret = AT_EMOVE;
                goto out;
            }
        } else {
            if (motor_speed_ctl_timedwait(motor, (offset > 0 ? speed_speed : -speed_speed), 10000, ref_step + offset, 1, NORMAL_TIMEOUT) < 0) {
                LOG("%s move failed.\n", needle_type_string(needle));
                ret = AT_EMOVE;
                goto out;
            }
        }
    }
    if (slip_liquid_detect_rcd_set(needle, ATTR_ENABLE) < 0) {
        LOG("%s set arg failed.\n", needle_type_string(needle));
        ret = AT_ETIMEOUT;
        goto out;
    }
    usleep(10 * 1000);
    if (slip_liquid_detect_state_set(needle, ATTR_ENABLE) < 0) {
        LOG("%s set arg failed.\n", needle_type_string(needle));
        ret = AT_ETIMEOUT;
        goto out;
    }

    misc_para = &g_misc_array[needle == NEEDLE_TYPE_R2 ? NEEDLE_TYPE_R2 : NEEDLE_TYPE_S];
    misc_para->timeout = NORMAL_TIMEOUT;
    misc_para->result = LIQ_INIT;
    misc_para->motor_id = motor;
    misc_para->maxstep = max_step;
    misc_para->idx = 0;
    misc_para->detect_step = 0;
    misc_para->stop_flag = 0;
    misc_para->reverse = (offset < 0 ? 1 : 0);
    misc_para->auto_cal = 1;
    work_queue_add(liquid_detect_maxstep_task, (void *)misc_para);

    /* 等待液面探测结果 */
    if (leave_singal_timeout_wait(misc_para->signo, AUTO_CAL_TIMEOUT) == 1) {
        LOG("%s detect timeout.\n", needle_type_string(needle));
        ret = AT_ETIMEOUT;
        goto out;
    } else {
        LOG("%s detect result = %d.\n", needle_type_string(needle), misc_para->result);
    }
    /* 关闭液面探测，停止电机 */
    misc_para->stop_flag = 1;
    motor_stop_timedwait(motor, NORMAL_TIMEOUT);
    usleep(10 * 1000);
    if (slip_liquid_detect_state_set(needle, ATTR_DISABLE) < 0) {
        LOG("%s detect timeout.\n", needle_type_string(needle));
        ret = AT_ETIMEOUT;
        goto out;
    }

    if (misc_para->result == LIQ_REACH_MAX || misc_para->result == LIQ_COLLSION_IN || misc_para->result == LIQ_INIT) {
        LOG("%s detect failed(%d).\n", needle_type_string(needle), misc_para->result);
        ret = (misc_para->result == LIQ_COLLSION_IN ? AT_ECOLLSION : AT_EMAXSTEP);
        goto out;
    }

    usleep(200 * 1000);
    ret = misc_para->detect_step = motor_current_pos_timedwait(motor, REMAIN_TIMEOUT);

out:
    g_detect_running = 0;
    if (misc_para) misc_para->stop_flag = 1;
    motor_stop_timedwait(motor, NORMAL_TIMEOUT);
    usleep(10 * 1000);
    slip_liquid_detect_state_set(needle, ATTR_DISABLE);
    usleep(10 * 1000);
    slip_liquid_detect_rcd_set(needle, ATTR_DISABLE);

    LOG("%s.%s step = %d.\n", needle_type_string(needle),
        (motor == MOTOR_NEEDLE_S_Z || motor == MOTOR_NEEDLE_R2_Z) ? "z" :
        ((motor == MOTOR_NEEDLE_S_Y || motor == MOTOR_NEEDLE_R2_Y) ? "y" : "x"), ret);

    return ret;
}

static int auto_cal_reag_table_move(needle_type_t needle, auto_cal_idx_t target_idx)
{
    cup_pos_t cup_pos = POS_INVALID;
    reag_table_cotl_t rtc_arg = {0};
    needle_pos_t reag_idx = 0;

    if (gpio_get(PE_NEEDLE_S_Z) != 1 && gpio_get(PE_NEEDLE_R2_Z) != 0) {
        LOG("S.z or R2.Z io not detect.\n");
        return -1;
    }

    if (auto_cal_stop_flag_get()) {
        return -1;
    }
    if (needle == NEEDLE_TYPE_S) {
        cup_pos = (target_idx == POS_REAG_INSIDE ? POS_REAGENT_TABLE_S_IN : POS_REAGENT_TABLE_S_OUT);
    } else {
        cup_pos = (target_idx == POS_R2_INSIDE ? POS_REAGENT_TABLE_R2_IN : POS_REAGENT_TABLE_R2_OUT);
    }
    reag_idx = (((cup_pos == POS_REAGENT_TABLE_S_IN) || (cup_pos == POS_REAGENT_TABLE_R2_IN)) ? 1 : 2);

    rtc_arg.table_move_type = TABLE_COMMON_MOVE;
    rtc_arg.table_dest_pos_idx = reag_idx;
    rtc_arg.req_pos_type = (needle == NEEDLE_TYPE_R2 ? NEEDLE_R2 : NEEDLE_S);
    rtc_arg.move_time = MOTOR_DEFAULT_TIMEOUT;
    rtc_arg.is_auto_cal = 1;
    if (reag_table_occupy_flag_get() == 1) {
        LOG("reagent table occupy get failed(%d)!\n", reag_table_occupy_flag_get());
        return -1;
    }

    reag_table_occupy_flag_set(1);
    return reagent_table_move_interface(&rtc_arg);
}

static int auto_cal_xy_move(pos_t dst)
{
    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (gpio_get(PE_NEEDLE_S_Z) != 1) {
        LOG("S.z io not detect.\n");
        return AT_EIO;
    }

    LOG("S.xy move to dst.x = %d, dst.y = %d.\n", dst.y, dst.x);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, dst.x, dst.y,
        10000, 20000, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        LOG("S.xy move failed.\n");
        return AT_EMOVE;
    }

    return 0;
}

/* 样本针XY单轴运动 */
static int auto_cal_xy_single_move(int motor, int step)
{
    int x_step = 0, y_step = 0;

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }

    if (motor == MOTOR_NEEDLE_S_X) {
        x_step = step;
        y_step = 0;
    } else if (motor == MOTOR_NEEDLE_S_Y) {
        x_step = 0;
        y_step = step;
    } else {
        LOG("not support such motor = %d.\n", motor);
        return AT_EARG;
    }

    LOG("S.%c move step = %d.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y', motor == MOTOR_NEEDLE_S_X ? x_step : y_step);
    if (slip_motor_dual_step_timedwait_slow(MOTOR_NEEDLE_S_Y, y_step, x_step, MOTOR_DEFAULT_TIMEOUT) == -1) {
        LOG("S.%c move failed.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y');
        return AT_EMOVE;
    }

    return 0;
}

static int auto_cal_r2_y_move(int step)
{
    int speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].speed / 2;
    int acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].acc / 2;

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    LOG("R2.y ready move %d steps.\n", step);
    if (step == 0) {
        return AT_EARG;
    }
    if (gpio_get(PE_NEEDLE_R2_Z) != 0) {
        LOG("R2.z io not detect.\n");
        return AT_EIO;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, step, speed, acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("R2.y move failed!\n");
        return AT_EMOVE;
    }

    return 0;
}

/* flag = 1 查找样本针取样位工装标记，此时外圈探测 */
static int auto_cal_xy_pos(auto_cal_idx_t target_idx, int motor, int limit, int d_limit, auto_cal_dir_t dir, int z, pos_t std, int flag)
{
    int y_step = 0, x_step = 0, z_step = 0, cur_step = 0, detect_step = 0;

    if (auto_cal_stop_flag_get()) {
        return AT_EMOVE;
    }
    if (target_idx == POS_ADD_SAMPLE || target_idx == POS_SAMPLE_MIX_1 || target_idx == POS_SAMPLE_MIX_2 ||
        target_idx == POS_SAMPLE_TEMP || target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE || flag == 1) {
        /* 工装外围探测，因此需要先移动出来 */
        if (motor == MOTOR_NEEDLE_S_X) {
            x_step = (dir == REVERSE ? limit : -limit);
            y_step = 0;
        } else {
            x_step = 0;
            y_step = (dir == REVERSE ? limit : -limit);
        }
        if (flag == 1) {
            z_step = z - 3 * S_1MM_STEPS;
        } else {
            z_step = z + 2 * S_1MM_STEPS;
        }
        if (gpio_get(PE_NEEDLE_S_Z) != 1) {
            LOG("S.z io not detect.\n");
            return AT_EIO;
        }
        LOG("S.xy move to offset x_step = %d, y_step = %d, detect_limit = %d.\n", x_step, y_step, d_limit);
        if (slip_motor_dual_step_timedwait_slow(MOTOR_NEEDLE_S_Y, y_step, x_step, MOTOR_DEFAULT_TIMEOUT) == -1) {
            LOG("S.%c move failed.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y');
            return AT_EMOVE;
        }
    } else {
        /* 工装内圈探测，z已经提前下去 */
        z_step = 0;
    }

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (z_step > 0) {
        if (auto_cal_z_check(NEEDLE_TYPE_S) < 0) {
            return AT_EIO;
        }
        LOG("S.z move step = %d.\n", z_step);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, z_step,
            S_CONTACT_V, AUTO_CAL_Z_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("S.z move failed.\n");
            return AT_EMOVE;
        }
    }

    cur_step = motor_current_pos_timedwait(motor, NORMAL_TIMEOUT);
    if (cur_step == -1) {
        return AT_ETIMEOUT;
    }
    LOG("S.%c cur_step = %d.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y', cur_step);

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    detect_step = auto_cal_detect_start(target_idx, NEEDLE_TYPE_S, motor, std, cur_step, (dir == FORWARD ? d_limit : (-d_limit)));
    LOG("S.%c detect_step = %d.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y', detect_step);

    if ((target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10 || target_idx == POS_SAMPLE_60) && flag == 0) {
        /* XY回到参考位置(xy探测前的位置) */
        if (detect_step <= AT_EMAXSTEP) {
            /* 探测失败，未返回具体位置，获取当前位置 */
            cur_step = motor_current_pos_timedwait(motor, NORMAL_TIMEOUT);
            if (cur_step == -1) {
                return AT_ETIMEOUT;
            }
            LOG("S.%c cur_step = %d.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y', cur_step);
        } else {
            cur_step = detect_step;
        }
        if (motor == MOTOR_NEEDLE_S_X) {
            x_step = std.y - cur_step;
            y_step = 0;
        } else if (motor == MOTOR_NEEDLE_S_Y) {
            x_step = 0;
            y_step = std.x - cur_step;
        }
        if (x_step || y_step) {
            if (auto_cal_stop_flag_get()) {
                return AT_EMSTOP;
            }
            LOG("S.xy back to std(%d - %d) x_step = %d, y_step = %d.\n", std.y, std.x, x_step, y_step);
            if (slip_motor_dual_step_timedwait_slow(MOTOR_NEEDLE_S_Y, y_step, x_step, MOTOR_DEFAULT_TIMEOUT) == -1) {
                LOG("S.%c move failed.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y');
                return AT_EMOVE;
            }
        }
    } else {
        /* 外圈探测，避免摩擦不能直接提Z，固定移动XY_AVOID_OFFSET出来 */
        if (motor == MOTOR_NEEDLE_S_X) {
            x_step = (dir == REVERSE ? XY_AVOID_OFFSET : -XY_AVOID_OFFSET);
            y_step = 0;
        } else {
            x_step = 0;
            y_step = (dir == REVERSE ? XY_AVOID_OFFSET : -XY_AVOID_OFFSET);
        }
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        LOG("S.xy move to avoid x_step = %d, y_step = %d.\n", x_step, y_step);
        if (slip_motor_dual_step_timedwait_slow(MOTOR_NEEDLE_S_Y, y_step, x_step, MOTOR_DEFAULT_TIMEOUT) == -1) {
            LOG("S.%c move failed.\n", motor == MOTOR_NEEDLE_S_X ? 'x' : 'y');
            return AT_EMOVE;
        }

        /* 再复位Z */
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED,
            NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("S.z rst failed.\n");
            return AT_EMOVE;
        } else {
            if (gpio_get(PE_NEEDLE_S_Z) != 1) {
                LOG("S.z io not detect.\n");
                return AT_EIO;
            }
        }
    }

    return detect_step;
}

/* 探测完毕，无论成功与否，Y避让，Z复位 */
static int auto_cal_r2_y_single_pos(auto_cal_idx_t target_idx,
    int limit,
    int d_limit,
    auto_cal_dir_t dir,
    int z,
    pos_t std)
{
    int y_step = 0, z_step = 0, cur_step = 0, detect_step = 0, ret = 0;

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (target_idx == POS_R2_MIX || target_idx == POS_R2_MAG_1 || target_idx == POS_R2_MAG_4 ||
        target_idx == POS_R2_CLEAN || target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) {
        /* 工装外围探测，因此需要先移动出来 */
        y_step = (dir == REVERSE ? limit : -limit);
        if (target_idx == POS_R2_MIX || target_idx == POS_R2_MAG_1 || target_idx == POS_R2_MAG_4) {
            z_step = z + 0.6 * R2_1MM_STEPS;
        } else if (target_idx == POS_R2_CLEAN) {
            z_step = z + 1 * R2_1MM_STEPS;
        } else {
            z_step = z + 1 * R2_1MM_STEPS;
        }
        ret = auto_cal_r2_y_move(y_step);
        if (ret < 0) {
            return ret;
        }
    } else {
        LOG("Not support %d.\n", target_idx);
        return AT_EARG;
    }

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (z_step > 0) {
        if (auto_cal_z_check(NEEDLE_TYPE_R2) < 0) {
            return AT_EIO;
        }
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, z_step, AUTO_CAL_Z_SPEED, AUTO_CAL_Z_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("R2.z move failed.\n");
            return AT_EMOVE;
        }
    }

    cur_step = motor_current_pos_timedwait(MOTOR_NEEDLE_R2_Y, NORMAL_TIMEOUT);
    if (cur_step == -1) {
        return AT_ETIMEOUT;
    }
    LOG("R2.y cur_step = %d.\n", cur_step);
    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    detect_step = auto_cal_detect_start(target_idx, NEEDLE_TYPE_R2, MOTOR_NEEDLE_R2_Y, std, cur_step, (dir == FORWARD ? d_limit : (-d_limit)));
    LOG("R2.y detect_step = %d.\n", detect_step);

    /* Y避让,Z复位 */
    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    y_step = (dir == REVERSE ? XY_AVOID_OFFSET : -XY_AVOID_OFFSET);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, y_step,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].speed / 2,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].acc / 2, MOTOR_DEFAULT_TIMEOUT)) {
        return AT_EMOVE;
    }

    if (auto_cal_stop_flag_get()) {
        return AT_EMSTOP;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].speed,
        h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("R2.z rst failed.\n");
        return AT_EMOVE;
    }  else {
        if (gpio_get(PE_NEEDLE_R2_Z) != 0) {
            LOG("R2.z io not detect.\n");
            return AT_EIO;
        }
    }

    return detect_step;
}

static int auto_cal_r2_y_pos(auto_cal_idx_t target_idx,
    pos_t std,
    int *y0,
    int *y1,
    int flag)
{
    int limit = 0, d_limit = 0;

    /* Y正向运动探测得到y0 */
    if (target_idx == POS_R2_CLEAN) {
        limit = AUTO_CAL_R2_CLEAN_Y_D + 3 / S_Y_STEP_MM_RATIO;
    } else if (target_idx == POS_R2_MIX) {
        limit = AUTO_CAL_R2_MIX_Y_D + 3 / S_Y_STEP_MM_RATIO;
    } else if (target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) {
        if (flag) {
            limit = AUTO_CAL_R2_REAG_Y_D / 2 + 3 / S_Y_STEP_MM_RATIO;
        } else {
            limit = AUTO_CAL_R2_REAG_Y_D + 3 / S_Y_STEP_MM_RATIO;
        }
    } else {
        limit = AUTO_CAL_R2_MAG_Y_D + 3 / S_Y_STEP_MM_RATIO;
    }
    d_limit = limit + 1 / S_Y_STEP_MM_RATIO;
    *y0 = auto_cal_r2_y_single_pos(target_idx, limit, d_limit, FORWARD, std.z, std);
    LOG("%s.y detect_step = %d(forward).\n", auto_cal_idx_string(target_idx), *y0);
    if (*y0 <= AT_EMAXSTEP) {
        LOG("%s.y detect failed(forward).\n", auto_cal_idx_string(target_idx));
        return *y0;
    }

    /* Y反向运动探测得到y1,注意此时Y的位置 */
    if (target_idx == POS_R2_CLEAN) {
        limit = AUTO_CAL_R2_CLEAN_Y_D + XY_AVOID_OFFSET + 3 / S_Y_STEP_MM_RATIO;
        d_limit = 6 / S_Y_STEP_MM_RATIO;
    } else if (target_idx == POS_R2_MIX) {
        /* 注意：R2光学混匀位y远端有结构限制 */
        limit = AUTO_CAL_R2_MIX_Y_D + XY_AVOID_OFFSET + 2 / S_Y_STEP_MM_RATIO;
        d_limit = 4 / S_Y_STEP_MM_RATIO;
    } else if (target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) {
        limit = AUTO_CAL_R2_REAG_Y_D + XY_AVOID_OFFSET + 3 / S_Y_STEP_MM_RATIO;
        d_limit = 6 / S_Y_STEP_MM_RATIO;
    } else {
        limit = AUTO_CAL_R2_MAG_Y_D + XY_AVOID_OFFSET + 3 / S_Y_STEP_MM_RATIO;
        d_limit = 5 / S_Y_STEP_MM_RATIO;
    }
    *y1 = auto_cal_r2_y_single_pos(target_idx, limit, d_limit, REVERSE, std.z, std);
    LOG("%s.y detect_step = %d(reverse).\n", auto_cal_idx_string(target_idx), *y1);
    if (*y1 <= AT_EMAXSTEP) {
        LOG("%s.y detect failed(reverse).\n", auto_cal_idx_string(target_idx));
        return *y1;
    }

    if (*y1 - *y0 <= 0) {
        LOG("cant be here, %s.y(%d - %d).\n", auto_cal_idx_string(target_idx), *y0, *y1);
        return AT_ENOTMATCH;
    }

    return 0;
}

/* Z向连续2次探测成功且差值小于0.2mm才认为本次探测成功 */
static int auto_cal_z_ensure(auto_cal_idx_t target_idx, needle_type_t needle, pos_t std)
{
    int ret = AT_ENOTMATCH, idx = 0, err_count = 0, rcd_z[AUTO_CAL_COUNT_MAX] = {0};
    int motor = (needle == NEEDLE_TYPE_R2 ? MOTOR_NEEDLE_R2_Z : MOTOR_NEEDLE_S_Z);
    int offset = 4 * needle_z_1mm_steps(needle);

    for (idx = 0; idx < AUTO_CAL_COUNT_MAX; idx++) {
        if (auto_cal_stop_flag_get()) {
            ret = AT_EMSTOP;
            break;
        }
        if (auto_cal_z_check(needle) < 0) {
            ret = AT_EIO;
            break;
        }

        rcd_z[idx] = auto_cal_detect_start(target_idx, needle, motor, std, 0, offset);
        LOG("%s.z detect_step %d = %d.\n", auto_cal_idx_string(target_idx), idx, rcd_z[idx]);
        if (rcd_z[idx] == AT_EMAXSTEP) {
            /* 连续最大步长2次，则该点位不再继续 */
            if (err_count++ >= 1) {
                ret = AT_EMAXSTEP;
                break;
            }
        } else if (rcd_z[idx] == AT_ECOLLSION || rcd_z[idx] == AT_ETIMEOUT || rcd_z[idx] == AT_EMOVE) {
            /* 严重错误，不再继续 */
            ret = rcd_z[idx];
            break;
        } else {
            /* 探测到，分析数据 */
            err_count = 0;
            if (idx > 0) {
                if (abs(rcd_z[idx] - rcd_z[idx - 1]) < 0.2 * needle_z_1mm_steps(needle)) {
                    ret = (rcd_z[idx] + rcd_z[idx - 1]) / 2;
                    LOG("%s.z detect_step avg(%d - %d) = %d.\n", auto_cal_idx_string(target_idx), idx - 1, idx, ret);
                    break;
                }
            }
        }

        if (auto_cal_z_reset(needle) < 0) {
            ret = AT_EMOVE;
            break;
        }
    }

    /* 无论成功与否，除撞针外，均复位Z */
    if (ret != AT_ECOLLSION) {
        if (auto_cal_z_reset(needle) < 0) {
            /* 收尾时复位失败，不覆盖原有错误码 */
            if (ret >= AT_EMAXSTEP) {
                ret = AT_EMOVE;
            }
        }
    }

    return ret;
}

static int auto_cal_r2_y_pos_ensure(auto_cal_idx_t target_idx, pos_t std, int *y0, int *y1, int flag)
{
    int idx = 0, idy = 0, err_count = 0, ret = 0;
    auto_cal_status_t cal_status = AUTO_CAL_INIT;
    auto_cal_r2_pos_t r2_data[AUTO_CAL_COUNT_MAX] = {0};

    for (idx = 0; idx < AUTO_CAL_COUNT_MAX; idx++) {
        cal_status = AUTO_CAL_INIT;
        ret = auto_cal_r2_reset(0);
        if (ret < 0) {
            return ret;
        }

        /* Y移动到指定位置 */
        ret = auto_cal_r2_y_move(std.y);
        if (ret < 0) {
            return ret;
        }

        /* 探测 */
        ret = auto_cal_r2_y_pos(target_idx, std, y0, y1, flag);
        if (ret < 0) {
            if (ret == AT_EMAXSTEP) {
                if (err_count++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else {
                return ret;
            }
        } else {
            err_count = 0;
            r2_data[idx].y0 = *y0;
            r2_data[idx].y1 = *y1;
            if (idx > 0) {
                /* 至少探测两次，从第二次开始，和前面探测出来的值进行比较，如果相等，则认为正常探测 */
                for (idy = idx - 1; idy >= 0; idy--) {
                    if (abs(r2_data[idx].y0 - r2_data[idy].y0) <= 3 && abs(r2_data[idx].y1 - r2_data[idy].y1) <= 3) {
                        *y0 = (r2_data[idx].y0 + r2_data[idy].y0) / 2;
                        *y1 = (r2_data[idx].y1 + r2_data[idy].y1) / 2;
                        cal_status = AUTO_CAL_PASS;
                        break;
                    }
                }
            }
            if (cal_status == AUTO_CAL_PASS) break;
        }
    }

    return (cal_status == AUTO_CAL_PASS ? 0 : AT_ENOTMATCH);
}

/* 基于针当前位置计算工装的下一个探测点位并移动 */
static int auto_cal_next_pos(auto_cal_idx_t target_idx, needle_type_t needle, pos_t std, pos_t cur, int turn, int dir, int *x, int *y)
{
    float ratio = 0;
    int x_step = 0, y_step = 0, ret = 0;
    pos_t dst = {0};

    /* 按照第一轮3/5,第二轮1个直径范围决定下一探测点 */
    if (turn == 0) {
        ratio = 0.6;
    } else {
        ratio = 1;
    }

    /* 本轮xy轴需要偏移的脉冲 */
    switch (target_idx) {
        case POS_SAMPLE_1:
        case POS_SAMPLE_10:
        case POS_SAMPLE_60:
            x_step = (int)(ratio * AUTO_CAL_S_SAMPLE_X_D);
            y_step = (int)(ratio * AUTO_CAL_S_SAMPLE_Y_D);
            break;
        case POS_ADD_SAMPLE:
        case POS_SAMPLE_MIX_1:
        case POS_SAMPLE_MIX_2:
            x_step = (int)(ratio * AUTO_CAL_S_ADD_X_D);
            y_step = (int)(ratio * AUTO_CAL_S_ADD_Y_D);
            break;
        case POS_SAMPLE_TEMP:
        case POS_REAG_INSIDE:
        case POS_REAG_OUTSIDE:
            if (target_idx == POS_REAG_INSIDE && (dir == EASTNORTH || dir == EAST || dir == EASTSOUTH)) {
                /* 结构限制，向右走多可能会触发物理极限 */
                x_step = (int)(ratio / 2 * AUTO_CAL_S_REAG_X_D);
            } else {
                x_step = (int)(ratio * AUTO_CAL_S_REAG_X_D);
            }
            y_step = (int)(ratio * AUTO_CAL_S_REAG_Y_D);
            break;
        case POS_R2_MIX:
            y_step = (int)(ratio * AUTO_CAL_R2_MIX_Y_D);
            break;
        case POS_R2_MAG_1:
        case POS_R2_MAG_4:
            y_step = (int)(ratio * AUTO_CAL_R2_MAG_Y_D);
            break;
        case POS_R2_CLEAN:
            y_step = (int)(ratio * AUTO_CAL_R2_CLEAN_Y_D);
            break;
        case POS_R2_INSIDE:
        case POS_R2_OUTSIDE:
            y_step = (int)(ratio * AUTO_CAL_R2_REAG_Y_D);
            break;
        default:
            LOG("no such idx = %d.\n", target_idx);
            return AT_EARG;
    }

    /* 此轮的目标位置 */
    if (needle == NEEDLE_TYPE_R2) {
        if (dir == NORTH || dir == SOUTH) {
            dst.y = std.y + (dir == NORTH ? y_step : -y_step);
            LOG("%s turn = %d, dir = %d, dst = %d(std = %d, cur = %d).\n",
                auto_cal_idx_string(target_idx), turn, dir, dst.y, std.y, cur.y);
            /* 到目标位还需要移动的脉冲 */
            *y = dst.y - cur.y;
        } else {
            *x = *y = 0;
        }

        #if 0
        if (target_idx == POS_R2_MIX) {
            if (dir == NORTH) {
                if (dst.y > 16900) {
                    *x = *y = 0;
                    LOG("%s turn = %d, dir = %d, dst_y = %d unreachable.\n", auto_cal_idx_string(target_idx), turn, dir, dst.y);
                }
            }
        }
        #endif
    } else {
        switch (dir) {
            case WEST:
                dst.x = std.y - x_step;
                dst.y = std.x;
                break;
            case WESTNORTH:
                dst.x = std.y - x_step;
                dst.y = std.x - y_step;
                break;
            case NORTH:
                dst.x = std.y;
                dst.y = std.x - y_step;
                break;
            case EASTNORTH:
                dst.x = std.y + x_step;
                dst.y = std.x - y_step;
                break;
            case EAST:
                dst.x = std.y + x_step;
                dst.y = std.x;
                break;
            case EASTSOUTH:
                dst.x = std.y + x_step;
                dst.y = std.x + y_step;
                break;
            case SOUTH:
                dst.x = std.y;
                dst.y = std.x + y_step;
                break;
            case WESTSOUTH:
                dst.x = std.y - x_step;
                dst.y = std.x + y_step;
                break;
            default:
                LOG("Nothing todo.\n");
                return AT_EARG;
        }
        LOG("%s turn = %d, dir = %d, dst = %d - %d(std = %d - %d, cur = %d - %d).\n",
            auto_cal_idx_string(target_idx), turn, dir, dst.x, dst.y, std.y, std.x, cur.y, cur.x);

        /* 到目标位还需要移动的脉冲 */
        *x = dst.x - cur.y;
        *y = dst.y - cur.x;

        if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10 || target_idx == POS_ADD_SAMPLE || target_idx == POS_SAMPLE_TEMP) {
            /* 结构限制，X左侧最远距离为-200 */
            if (dir == WEST || dir == WESTNORTH || dir == WESTSOUTH) {
                if (dst.x < -200) {
                    *x = *y = 0;
                    LOG("%s turn = %d, dir = %d, dst_x = %d unreachable.\n", auto_cal_idx_string(target_idx), turn, dir, dst.x);
                }
            }
        }

        if (target_idx == POS_ADD_SAMPLE || target_idx == POS_SAMPLE_MIX_1 || target_idx == POS_SAMPLE_MIX_2) {
            /* 结构限制，Y上方最远距离为-200 */
            if (dir == WESTNORTH || dir == NORTH || dir == EASTNORTH) {
                if (dst.y < -200) {
                    *x = *y = 0;
                    LOG("%s turn = %d, dir = %d, dst_y = %d unreachable.\n", auto_cal_idx_string(target_idx), turn, dir, dst.y);
                }
            }
        }
    }

    LOG("%s turn = %d, dir = %d, next_pos offset x = %d, y = %d.\n", auto_cal_idx_string(target_idx), turn, dir, *x, *y);
    if (*x == 0 && *y == 0) {
        LOG("%s turn = %d, dir = %d, skip.\n", auto_cal_idx_string(target_idx), turn, dir);
        return 1;
    } else {
        /* 方位移动 */
        if (needle == NEEDLE_TYPE_R2) {
            if (*y != 0) {
                return auto_cal_r2_y_move(*y);
            }
        } else {
            if (*x != 0) {
                if (auto_cal_z_check(NEEDLE_TYPE_S) != 0) {
                    return AT_EIO;
                }
                ret = auto_cal_xy_single_move(MOTOR_NEEDLE_S_X, *x);
                if (ret < 0) {
                    return ret;
                }
            }
            if (*y != 0) {
                if (auto_cal_z_check(NEEDLE_TYPE_S) != 0) {
                    return AT_EIO;
                }
                ret = auto_cal_xy_single_move(MOTOR_NEEDLE_S_Y, *y);
                if (ret < 0) {
                    return ret;
                }
            }
        }
    }

    return 0;
}

/* 探测找到工装，成功返回工装Z向高度并更新对应xy坐标(可能)，失败返回对应错误码 */
static int auto_cal_obj_detect(auto_cal_idx_t target_idx, needle_type_t needle, pos_t *std)
{
    int idx = 0, idy = 0, z = 0, z_diff = 0, offset_max = 0, ret = 0;
    pos_t cur_pos = {0}, backup_pos = {0};
    int x_next = 0, y_next = 0;

    if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10 || target_idx == POS_SAMPLE_60) {
        LOG("not support %s.\n", auto_cal_idx_string(target_idx));
        return AT_EARG;
    }

    memcpy(&cur_pos, std, sizeof(pos_t));
    if (needle == NEEDLE_TYPE_R2) {
        LOG("cur_pos.x = %d, cur_pos.y = %d.\n", cur_pos.x, cur_pos.y);
    } else {
        LOG("cur_pos.x = %d, cur_pos.y = %d.\n", cur_pos.y, cur_pos.x);
    }

    for (idx = 0; idx < AUTO_CAL_TURN_MAX; idx++) {
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        if (idx != 0) {
            /* 进行第二轮探测前，统一基于原始配置内给定的参考值(此处试剂仓不需要复位) */
            if (needle == NEEDLE_TYPE_R2) {
                ret = auto_cal_r2_reset(0);
                if (ret < 0) {
                    return ret;
                }
                ret = auto_cal_r2_y_move(std->y);
                if (ret < 0)  {
                    return ret;
                }
            } else {
                ret = auto_cal_s_reset(0);
                if (ret < 0) {
                    return ret;
                }
                ret = auto_cal_xy_move(*std);
                if (ret < 0) {
                    return ret;
                }
            }
            memcpy(&cur_pos, std, sizeof(pos_t));
        }
        for (idy = CENTER; idy < OLMAX; idy++) {
            if (idx > 0 && idy == CENTER) {
                LOG("%s turn = %d, dir = %d, skip.\n", auto_cal_idx_string(target_idx), idx, idy);
                continue;
            }
            if (auto_cal_stop_flag_get()) {
                return AT_EMSTOP;
            }
            if (idy != 0) {
                ret = auto_cal_next_pos(target_idx, needle, *std, cur_pos, idx, idy, &x_next, &y_next);
                LOG("x_next = %d, y_next = %d.\n", x_next, y_next);
                if (ret < 0) {
                    return ret;
                } else if (ret == 1) {
                    continue;
                } else {
                    /* 记录当前变更后的xy */
                    if (needle == NEEDLE_TYPE_R2) {
                        cur_pos.x += x_next;
                        cur_pos.y += y_next;
                        LOG("cur_pos.x = %d, cur_pos.y = %d.\n", cur_pos.x, cur_pos.y);
                    } else {
                        cur_pos.x += y_next;
                        cur_pos.y += x_next;
                        LOG("cur_pos.x = %d, cur_pos.y = %d.\n", cur_pos.y, cur_pos.x);
                    }
                }
            }
            z = auto_cal_z_ensure(target_idx, needle, *std);
            LOG("%s turn = %d, dir = %d, z detect_step = %d.\n", auto_cal_idx_string(target_idx), idx, idy, z);
            if (z > AT_EMAXSTEP) {
                /* 探测成功，分析数据是否有效根据EVT装配机器统计该批次台间差最大为2.3mm */
                offset_max = 5 * needle_z_1mm_steps(needle);
                z_diff = abs(z - std->z);
                if (z_diff <= offset_max) {
                    /* 在z参考值±3mm内，工装查找成功 */
                    LOG("%s turn = %d, dir = %d, target detected(%d - %d - %d).\n", auto_cal_idx_string(target_idx), idx, idy,
                        needle == NEEDLE_TYPE_R2 ? cur_pos.x : cur_pos.y, needle == NEEDLE_TYPE_R2 ? cur_pos.y : cur_pos.x, z);
                    memcpy(std, &cur_pos, sizeof(pos_t));
                    return z;
                } else if (z_diff > offset_max && z_diff <= 2 * offset_max) {
                    /* 防止后批次台间差过大，无法满足判定条件,备份该值 */
                    LOG("%s turn = %d, dir = %d, target detected(backup).\n", auto_cal_idx_string(target_idx), idx, idy);
                    cur_pos.z = z;
                    memcpy(&backup_pos, &cur_pos, sizeof(pos_t));
                } else {
                    /* 探测差异过大，继续查找 */
                    LOG("%s turn = %d, dir = %d, target detected(z = %d, std.z = %d) out of range.\n",
                        auto_cal_idx_string(target_idx), idx, idy, z, std->z);
                }
            } else {
                /* 探测失败，返回具体的失败错误码 */
                if (z == AT_EMAXSTEP || z == AT_ENOTMATCH) {
                    /* 非致命错误，继续查找 */
                } else {
                    /* 致命错误，不再查找 */
                    return z;
                }
            }
        }
    }

    /* 2轮查找完毕还未找到工装，如果找到一个可能正确的位置backup_pos，则使用该位置 */
    if (backup_pos.y != 0) {
        LOG("%s use backup pos.\n", auto_cal_idx_string(target_idx));
        memcpy(std, &backup_pos, sizeof(pos_t));
        return backup_pos.z;
    }

    return AT_ENOTFOUND;
}

/* 根据3点计算圆心 */
static int auto_cal_center_cal(pos_t *pos, pos_t *center)
{
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;

    /* xy轴传动比不一致，转换成距离mm计算，完毕后再转换成对应脉冲 */
    x0 = pos[0].y * S_X_STEP_MM_RATIO;
    y0 = pos[0].x * S_Y_STEP_MM_RATIO;
    x1 = pos[1].y * S_X_STEP_MM_RATIO;
    y1 = pos[1].x * S_Y_STEP_MM_RATIO;
    x2 = pos[2].y * S_X_STEP_MM_RATIO;
    y2 = pos[2].x * S_Y_STEP_MM_RATIO;

    a = x0 - x1;
    b = y0 - y1;
    c = x0 - x2;
    d = y0 - y2;
    e = (x0 * x0 - x1 * x1 + y0 * y0 - y1 * y1) / 2;
    f = (x0 * x0 - x2 * x2 + y0 * y0 - y2 * y2) / 2;

    center->y = (b * f - d * e) / (b * c - a * d) * (1 / S_X_STEP_MM_RATIO);
    center->x = (c * e - a * f) / (b * c - a * d) * (1 / S_Y_STEP_MM_RATIO);

    if (center->x <= 0 || center->y <= 0) {
        return -1;
    }

    return 0;
}

/* 探测找到工装，成功返回工装Z向高度并更新对应xy坐标(可能)，失败返回对应错误码 */
static int auto_cal_obj_detect1(auto_cal_idx_t target_idx, needle_type_t needle, pos_t *std)
{
    int idx = 0, ret = 0, z, x0 = 0, y0 = 0, dst_x = 0, dst_y = 0, x_step = 0, y_step = 0, d_limit = 0;
    pos_t center_pos = {0}, detect_pos[3] = {0};

    if (target_idx != POS_SAMPLE_1 && target_idx != POS_SAMPLE_10 && target_idx != POS_SAMPLE_60) {
        LOG("not support %s.\n", auto_cal_idx_string(target_idx));
        return AT_EARG;
    }
    LOG("cur_pos.x = %d, cur_pos.y = %d.\n", std->y, std->x);

    /* 先使用y外侧探测得到一个坐标 */
    for (idx = 0; idx < 2; idx++) {
        /* 1计算初始点位 */
        if (idx == 0) {
            /* 根据计算样本针取样位1/10，x在220~330之间y向运动一定可以探测到工装 */
            if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10) {
                dst_x = 250;
            } else {
                dst_x = std->y;
            }
        } else {
            ret = auto_cal_s_reset(0);
            if (ret < 0) {
                return ret;
            }
            ret = auto_cal_xy_move(*std);
            if (ret < 0) {
                return ret;
            }
            if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10) {
                dst_x = 250 + 1 / S_X_STEP_MM_RATIO;
            } else {
                dst_x = std->y + AUTO_CAL_S_SAMPLE_X_D / 2;
            }
        }
        LOG("%s dst = %d - %d(std = %d - %d).\n", auto_cal_idx_string(target_idx), dst_x, std->x, std->y, std->x);

        /* 2移动 */
        x_step = dst_x - std->y;
        LOG("%s x_step = %d.\n", auto_cal_idx_string(target_idx), x_step);
        if (x_step != 0) {
            if (auto_cal_z_check(NEEDLE_TYPE_S) != 0) {
                return AT_EIO;
            }
            ret = auto_cal_xy_single_move(MOTOR_NEEDLE_S_X, x_step);
            if (ret < 0) {
                return ret;
            }
        }

        /* 3探测 */
        y_step = 2.5 * AUTO_CAL_S_SAMPLE_Y_D;
        d_limit = 4 * AUTO_CAL_S_SAMPLE_Y_D;
        y0 = auto_cal_xy_pos(target_idx, MOTOR_NEEDLE_S_Y, y_step, d_limit,
            (target_idx == POS_SAMPLE_1 ? REVERSE : FORWARD), std->z, *std, 1);
        LOG("%s.y-%d detect_step = %d(%s).\n", auto_cal_idx_string(target_idx), idx, y0, (target_idx == POS_SAMPLE_1 ? "reverse" : "forward"));
        if (y0 == AT_EMAXSTEP) {
            continue;
        } else if (y0 < AT_EMAXSTEP) {
            LOG("%s.y-%d detect failed(%s).\n", auto_cal_idx_string(target_idx), idx, (target_idx == POS_SAMPLE_1 ? "reverse" : "forward"));
            return y0;
        }
        detect_pos[0].y = dst_x;
        detect_pos[0].x = y0;
        LOG("%s target detected(y), x = %d, y = %d.\n", auto_cal_idx_string(target_idx), detect_pos[0].y, detect_pos[0].x);
        break;
    }
    if (idx == 2) {
        return AT_ENOTFOUND;
    }

    //* 再使用x外侧探测得到一个坐标 */
    for (idx = 0; idx < 2; idx++) {
        /* 计算初始点位 */
        ret = auto_cal_s_reset(0);
        if (ret < 0) {
            return ret;
        }
        ret = auto_cal_xy_move(detect_pos[0]);
        if (ret < 0) {
            return ret;
        }

        /* y偏移3-6mm移动探测 */
        if (target_idx == POS_SAMPLE_1) {
            dst_y = detect_pos[0].x - (idx + 1) * (3 / S_Y_STEP_MM_RATIO);
        } else {
            dst_y = detect_pos[0].x + (idx + 1) * (3 / S_Y_STEP_MM_RATIO);
        }
        LOG("%s dst = %d - %d(std = %d - %d).\n", auto_cal_idx_string(target_idx), dst_x, dst_y, detect_pos[0].y, detect_pos[0].x);
        y_step = dst_y - detect_pos[0].x;
        LOG("%s y_step = %d.\n", auto_cal_idx_string(target_idx), y_step);
        if (y_step != 0) {
            if (auto_cal_z_check(NEEDLE_TYPE_S) != 0) {
                return AT_EIO;
            }
            ret = auto_cal_xy_single_move(MOTOR_NEEDLE_S_Y, y_step);
            if (ret < 0) {
                return ret;
            }
        }

        /* 探测 */
        x_step = AUTO_CAL_S_SAMPLE_X_D + 6 / S_X_STEP_MM_RATIO;
        d_limit = AUTO_CAL_S_SAMPLE_X_D + 8 / S_X_STEP_MM_RATIO;
        x0 = auto_cal_xy_pos(target_idx, MOTOR_NEEDLE_S_X, x_step, d_limit,
            (target_idx == POS_SAMPLE_60 ? FORWARD : REVERSE), std->z, *std, 1);
        LOG("%s.x-%d detect_step = %d(%s).\n", auto_cal_idx_string(target_idx), idx, x0, (target_idx == POS_SAMPLE_60 ? "forward" : "reverse"));
        if (x0 == AT_EMAXSTEP) {
            continue;
        } else if (x0 < AT_EMAXSTEP) {
            LOG("%s.x-%d detect failed(%s).\n", auto_cal_idx_string(target_idx), idx, (target_idx == POS_SAMPLE_60 ? "forward" : "reverse"));
            return x0;
        }
        detect_pos[idx + 1].y = x0;
        detect_pos[idx + 1].x = dst_y;
        LOG("%s target detected(x-%d), x = %d, y = %d.\n", auto_cal_idx_string(target_idx), idx, detect_pos[idx + 1].y, detect_pos[idx + 1].x);
    }
    if (detect_pos[1].x == 0 || detect_pos[1].y == 0 || detect_pos[2].x == 0 || detect_pos[2].y == 0) {
        return AT_ENOTFOUND;
    } else {
        for (idx = 0; idx < sizeof(detect_pos) / sizeof(detect_pos[0]); idx++) {
            LOG("%s detect_ret = %d - %d.\n", auto_cal_idx_string(target_idx), detect_pos[idx].y, detect_pos[idx].x);
        }
    }

    if (auto_cal_center_cal(detect_pos, &center_pos) < 0) {
        return AT_ENOTFOUND;
    }
    LOG("%s target detected, center.x = %d, center.y = %d.\n", auto_cal_idx_string(target_idx), center_pos.y, center_pos.x);
    ret = auto_cal_s_reset(0);
    if (ret < 0) {
        return ret;
    }
    ret = auto_cal_xy_move(center_pos);
    if (ret < 0) {
        return ret;
    }
    z = auto_cal_z_ensure(target_idx, needle, *std);
    if (z > AT_EMAXSTEP) {
        if (abs(z - std->z) <= 5 * needle_z_1mm_steps(needle)) {
            LOG("%s target detected, confirmed.\n", auto_cal_idx_string(target_idx));
            memcpy(std, &center_pos, sizeof(pos_t));
            return z;
        }
    }

    return AT_ENOTFOUND;
}

static int auto_cal_s(auto_cal_idx_t target_idx,
    pos_t std,
    motor_time_sync_attr_t attr_x,
    motor_time_sync_attr_t attr_y,
    auto_cal_rcd_t *final_pos)
{
    int idx = 0, idy = 0, x_limit = 0, y_limit = 0, d_limit = 0, ret = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, z = 0, z_check = 0, diff_step = 0, xd = 0, yd = 0;
    int x0_err = 0, x1_err = 0, y0_err = 0, y1_err = 0;
    int motor_x = MOTOR_NEEDLE_S_X, motor_y = MOTOR_NEEDLE_S_Y;
    needle_type_t needle = NEEDLE_TYPE_S;
    pos_t target_pos = {0}, cal_pos = {0}, ori_pos = {0};
    auto_cal_s_pos_t s_data[AUTO_CAL_COUNT_MAX] = {0};
    auto_cal_status_t cal_status = AUTO_CAL_INIT;

    memcpy(&ori_pos, &std, sizeof(pos_t));
    /* 相关部件复位 */
    ret = auto_cal_s_reset((target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) ? 1 : 0);
    if (ret < 0) {
        return ret;
    }
    /* 试剂仓就位 */
    if (target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) {
        if (auto_cal_reag_table_move(NEEDLE_TYPE_S, target_idx) < 0) {
            LOG("%s reag table move failed.\n", auto_cal_idx_string(target_idx));
            return AT_EMOVE;
        }
    }
    /* XY就位 */
    ret = auto_cal_xy_move(std);
    if (ret < 0) {
        return ret;
    }
    /* 自动查找工装(可能)并探测Z */
    LOG("%s stand_pos = %d - %d.\n", auto_cal_idx_string(target_idx), std.y, std.x);
    if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10 || target_idx == POS_SAMPLE_60) {
        z = auto_cal_obj_detect1(target_idx, needle, &std);
    } else {
        z = auto_cal_obj_detect(target_idx, needle, &std);
        /* 注意，样本针头形态问题，Z可能不准确(极限切边探测成功),带后续初探成功后再次进行Z脉冲验证 */
    }
    if (z <= 0) {
        LOG("%s.z = %d cal failed, maybe std.z out_of_range.\n", auto_cal_idx_string(target_idx), z);
        return z;
    }
    LOG("%s.z = %d, new stand_pos = %d - %d.\n", auto_cal_idx_string(target_idx), z, std.y, std.x);

    if (target_idx == POS_SAMPLE_1 || target_idx == POS_SAMPLE_10 || target_idx == POS_SAMPLE_60) {
        cal_status = AUTO_CAL_INIT;
        x0_err = x1_err = y0_err = y1_err = 0;
        for (idx = 0; idx < AUTO_CAL_COUNT_MAX; idx++) {
            memset(&cal_pos, 0, sizeof(cal_pos));
            ret = auto_cal_s_reset(0);
            if (ret < 0) {
                return ret;
            }

            target_pos.z = z;
            if (cal_status == AUTO_CAL_INIT) {
                target_pos.x = std.x;
                target_pos.y = std.y;
            } else {
                for (idy = idx - 1; idy >= 0; idy--) {
                    if (s_data[idy].y_mid) {
                        target_pos.x = s_data[idy].y_mid;
                        break;
                    }
                }
                for (idy = idx - 1; idy >= 0; idy--) {
                    if (s_data[idy].x_mid) {
                        target_pos.y = s_data[idy].x_mid;
                        break;
                    }
                }
            }
            ret = auto_cal_xy_move(target_pos);
            if (ret < 0) {
                return ret;
            }

            if (auto_cal_stop_flag_get()) {
                return AT_EMOVE;
            }
            /* 3个取样位Z轴只需要下去一次进行4向探测，因此z先下 */
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, z - 6 * S_1MM_STEPS,
                S_CONTACT_V, AUTO_CAL_Z_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("S.z move failed.\n");
                return AT_EMOVE;
            }

            /* X反向运动探测得到x0 */
            x_limit = AUTO_CAL_S_SAMPLE_X_D;
            x0 = auto_cal_xy_pos(target_idx, motor_x, x_limit, x_limit, REVERSE, z, target_pos, 0);
            LOG("%s.x-%d detect_step = %d(reverse).\n", auto_cal_idx_string(target_idx), idx, x0);
            if (x0 == AT_EMAXSTEP) {
                if (x0_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (x0 < AT_EMAXSTEP) {
                LOG("%s.x-%d detect failed(reverse).\n", auto_cal_idx_string(target_idx), idx);
                return x0;
            }
            x0_err = 0;
            /* X正向运动探测得到x1 */
            x_limit = AUTO_CAL_S_SAMPLE_X_D;
            x1 = auto_cal_xy_pos(target_idx, motor_x, x_limit, x_limit, FORWARD, z, target_pos, 0);
            LOG("%s.x-%d detect_step = %d(forward).\n", auto_cal_idx_string(target_idx), idx, x1);
            if (x1 == AT_EMAXSTEP) {
                if (x1_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (x1 < AT_EMAXSTEP) {
                LOG("%s.x-%d detect failed(forward).\n", auto_cal_idx_string(target_idx), idx);
                return x1;
            }
            x1_err = 0;
            if (x1 - x0 <= 0) {
                LOG("cant be here, %s.x(%d - %d).\n", auto_cal_idx_string(target_idx), x1, x0);
                return AT_ENOTMATCH;
            }
            s_data[idx].x0 = x0;
            s_data[idx].x1 = x1;
            s_data[idx].x_mid = (x0 + x1) / 2;
            s_data[idx].x_diff = x1 - x0;
            /* X向探测成功，此时x已回到对应参考点，此时移动到新的x中点，为后续Y探测准备 */
            diff_step = s_data[idx].x_mid - target_pos.y;
            if (diff_step != 0) {
                ret = auto_cal_xy_single_move(motor_x, diff_step);
                if (ret < 0) {
                    return ret;
                }
            }

            /* Y反向运动探测得到y0 */
            y_limit = AUTO_CAL_S_SAMPLE_Y_D;
            y0 = auto_cal_xy_pos(target_idx, motor_y, y_limit, y_limit, REVERSE, z, target_pos, 0);
            LOG("%s.y-%d detect_step = %d(reverse).\n", auto_cal_idx_string(target_idx), idx, y0);
            if (y0 == AT_EMAXSTEP) {
                if (y0_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (y0 < AT_EMAXSTEP) {
                LOG("%s.y-%d detect failed(reverse).\n", auto_cal_idx_string(target_idx), idx);
                return y0;
            }
            y0_err = 0;
            /* Y正向运动探测得到y1 */
            y_limit = AUTO_CAL_S_SAMPLE_Y_D;
            y1 = auto_cal_xy_pos(target_idx, motor_y, y_limit, y_limit, FORWARD, z, target_pos, 0);
            LOG("%s.y-%d detect_step = %d(forward).\n", auto_cal_idx_string(target_idx), idx, y1);
            if (y1 == AT_EMAXSTEP) {
                if (y1_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (y1 < AT_EMAXSTEP) {
                LOG("%s.y-%d detect failed(forward).\n", auto_cal_idx_string(target_idx), idx);
                return y1;
            }
            y1_err = 0;
            if (y1 - y0 <= 0) {
                LOG("cant be here, %s.y(%d - %d).\n", auto_cal_idx_string(target_idx), y0, y1);
                return AT_ENOTMATCH;
            }
            s_data[idx].y0 = y0;
            s_data[idx].y1 = y1;
            s_data[idx].y_mid = (y0 + y1) / 2;
            s_data[idx].y_diff = y1 - y0;

            if (cal_status == AUTO_CAL_INIT) {
                cal_status = AUTO_CAL_PASS;
            } else {
                cal_status = AUTO_CAL_DONE;
            }
            /* 内部探测，向外挤压 */
            if (s_data[idx].x_diff >= AUTO_CAL_S_SAMPLE_X_D - (int)(0.2 / S_X_STEP_MM_RATIO) &&
                s_data[idx].x_diff <= AUTO_CAL_S_SAMPLE_X_D + (int)(0.3 / S_X_STEP_MM_RATIO)) {
                cal_pos.x = s_data[idx].x_mid;
                LOG("%s.x-%d = %d(%d - %d) Ok(x.d = %d).\n", auto_cal_idx_string(target_idx), idx, x1 - x0, x1, x0, AUTO_CAL_S_SAMPLE_X_D);
            } else {
                if (idx >= AUTO_CAL_COUNT_MAX / 2) {
                    for (idy = idx - 1; idy >= 0; idy--) {
                        if (abs(s_data[idx].x0 - s_data[idy].x0) <= 3 && abs(s_data[idx].x1 - s_data[idy].x1) <= 3) {
                            if (s_data[idx].x_diff >= AUTO_CAL_S_SAMPLE_X_D - (int)(0.4 / S_X_STEP_MM_RATIO) &&
                                s_data[idx].x_diff <= AUTO_CAL_S_SAMPLE_X_D + (int)(0.5 / S_X_STEP_MM_RATIO)) {
                                /* unlikely：探测数据一致但未满足条件，适当放宽(可能给定的标准不够准) */
                                cal_pos.x = (s_data[idx].x_mid + s_data[idy].x_mid) / 2;
                                LOG("%s.x-%d = %d(%d - %d) Ok-1(x.d = %d, match with %d).\n",
                                    auto_cal_idx_string(target_idx), idx, x1 - x0, x1, x0, AUTO_CAL_S_SAMPLE_X_D, idy);
                                break;
                            }
                        }
                    }
                }
            }
            if (s_data[idx].y_diff >= AUTO_CAL_S_SAMPLE_Y_D - (int)(0.2 / S_Y_STEP_MM_RATIO) &&
                s_data[idx].y_diff <= AUTO_CAL_S_SAMPLE_Y_D + (int)(0.3 / S_Y_STEP_MM_RATIO)) {
                cal_pos.y = s_data[idx].y_mid;
                LOG("%s.y-%d = %d(%d - %d) Ok(y.d = %d).\n", auto_cal_idx_string(target_idx), idx, y1 - y0, y1, y0, AUTO_CAL_S_SAMPLE_Y_D);
            } else {
                if (idx >= AUTO_CAL_COUNT_MAX / 2) {
                    for (idy = idx - 1; idy >= 0; idy--) {
                        if (abs(s_data[idx].y0 - s_data[idy].y0) <= 3 && abs(s_data[idx].y1 - s_data[idy].y1) <= 3) {
                            if (s_data[idx].y_diff >= AUTO_CAL_S_SAMPLE_Y_D - (int)(0.4 / S_Y_STEP_MM_RATIO) &&
                                s_data[idx].y_diff <= AUTO_CAL_S_SAMPLE_Y_D + (int)(0.5 / S_Y_STEP_MM_RATIO)) {
                                cal_pos.y = (s_data[idx].y_mid + s_data[idy].y_mid) / 2;
                                LOG("%s.y-%d = %d(%d - %d) Ok-1(y.d = %d, match with %d).\n",
                                    auto_cal_idx_string(target_idx), idx, y1 - y0, y1, y0, AUTO_CAL_S_SAMPLE_Y_D, idy);
                                break;
                            }
                        }
                    }
                }
            }

            if (cal_status == AUTO_CAL_DONE && cal_pos.x != 0 && cal_pos.y != 0) {
                break;
            } else {
                if (cal_pos.x == 0) {
                    LOG("%s.x-%d = %d(%d - %d) mismatch(x.d = %d).\n", auto_cal_idx_string(target_idx), idx, x1 - x0, x0, x1, AUTO_CAL_S_SAMPLE_X_D);
                }
                if (cal_pos.y == 0) {
                    LOG("%s.y-%d = %d(%d - %d) mismatch(y.d = %d).\n", auto_cal_idx_string(target_idx), idx, y1 - y0, y0, y1, AUTO_CAL_S_SAMPLE_Y_D);
                }
            }
        }
        if (idx == AUTO_CAL_COUNT_MAX) {
            LOG("%s cal failed.\n", auto_cal_idx_string(target_idx));
            return AT_ENOTMATCH;
        } else {
            /* 重新复位 */
            ret = auto_cal_s_reset(0);
            if (ret < 0) {
                return ret;
            }
            final_pos->pos.x = cal_pos.x;
            final_pos->pos.y = cal_pos.y;
            final_pos->pos.z = z;
            LOG("%s final_pos: %d - %d - %d(stand_pos: %d - %d - %d).\n", auto_cal_idx_string(target_idx),
                final_pos->pos.x, final_pos->pos.y, final_pos->pos.z, ori_pos.y, ori_pos.x, ori_pos.z);
        }
    } else if (target_idx == POS_ADD_SAMPLE || target_idx == POS_SAMPLE_MIX_1 ||
        target_idx == POS_SAMPLE_MIX_2 || target_idx == POS_SAMPLE_TEMP || target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) {
        cal_status = AUTO_CAL_INIT;
        z_check = 0;
        x0_err = x1_err = y0_err = y1_err = 0;
        for (idx = 0; idx < AUTO_CAL_COUNT_MAX; idx++) {
            memset(&cal_pos, 0, sizeof(cal_pos));
            ret = auto_cal_s_reset(0);
            if (ret < 0) {
                return ret;
            }

            target_pos.z = z;
            if (cal_status == AUTO_CAL_INIT) {
                target_pos.x = std.x;
                target_pos.y = std.y;
            } else {
                for (idy = idx - 1; idy >= 0; idy--) {
                    if (s_data[idy].y_mid) {
                        target_pos.x = s_data[idy].y_mid;
                        break;
                    }
                }
                for (idy = idx - 1; idy >= 0; idy--) {
                    if (s_data[idy].x_mid) {
                        target_pos.y = s_data[idy].x_mid;
                        break;
                    }
                }
            }
            ret = auto_cal_xy_move(target_pos);
            if (ret < 0) {
                return ret;
            }
            /* 粗校完成再次校验Z */
            if (z_check == 0 && cal_status == AUTO_CAL_PASS) {
                z = auto_cal_z_ensure(target_idx, needle, std);
                LOG("%s check z detect_step = %d.\n", auto_cal_idx_string(target_idx), z);
                if (abs(z - std.z) <= 5 * needle_z_1mm_steps(needle)) {
                    /* 在z参考值±3mm内，工装查找成功 */
                    LOG("%s update z step = %d.\n", auto_cal_idx_string(target_idx), z);
                    target_pos.z = z;
                    z_check = 1;
                } else {
                    LOG("%s check z cant be here.\n", auto_cal_idx_string(target_idx));
                    return AT_ENOTMATCH;
                }
            }

            if (target_idx == POS_SAMPLE_TEMP || target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) {
                xd = AUTO_CAL_S_REAG_X_D;
                yd = AUTO_CAL_S_REAG_Y_D;
            } else {
                xd = AUTO_CAL_S_ADD_X_D;
                yd = AUTO_CAL_S_ADD_Y_D;
            }
            /* Y反向运动探测得到y0 */
            y_limit = yd + 3 / S_Y_STEP_MM_RATIO;
            d_limit = yd + 4 / S_Y_STEP_MM_RATIO;
            y0 = auto_cal_xy_pos(target_idx, motor_y, y_limit, d_limit, REVERSE, z, target_pos, 0);
            LOG("%s.y-%d detect_step = %d(reverse).\n", auto_cal_idx_string(target_idx), idx, y0);
            if (y0 == AT_EMAXSTEP) {
                if (y0_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (y0 < AT_EMAXSTEP) {
                LOG("%s.y-%d detect failed(reverse).\n", auto_cal_idx_string(target_idx), idx);
                return y0;
            }
            y0_err = 0;
            /* Y正向运动探测得到y1, 注意此时Y所在的位置, 尝试移动到工装远处3mm处 */
            y_limit = yd + XY_AVOID_OFFSET + 3 / S_Y_STEP_MM_RATIO;
            if (target_idx == POS_ADD_SAMPLE || target_idx == POS_SAMPLE_MIX_1 || target_idx == POS_SAMPLE_MIX_2) {
                if (y0 + XY_AVOID_OFFSET - y_limit < -200) {
                    /* 如果按照预留3mm探测距离移动，已经超过极限位置-200，强制限制在-200位置开始探测 */
                    y_limit = y0 + XY_AVOID_OFFSET + 200;
                    LOG("%s.y(forward) unreachable, set y_limit = %d.\n", auto_cal_idx_string(target_idx), y_limit);
                }

                if (y_limit < (int)(yd + XY_AVOID_OFFSET + 2 / S_Y_STEP_MM_RATIO)) {
                    /* 无法保证充裕的探测距离2mm */
                    LOG("%s.y(forward) cant be here, less than 2mm.\n", auto_cal_idx_string(target_idx));
                    return AT_ENOTMATCH;
                }
            }
            if (target_idx == POS_SAMPLE_TEMP || target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) {
                /* 圆形工装存在偏离直径远的情况 */
                d_limit = yd / 2 + 3 / S_Y_STEP_MM_RATIO;
            } else {
                /* 方形工装最多在工装外3mm处 */
                d_limit = 5 / S_Y_STEP_MM_RATIO;
            }
            y1 = auto_cal_xy_pos(target_idx, motor_y, y_limit, d_limit, FORWARD, z, target_pos, 0);
            LOG("%s.y-%d detect_step = %d(forward).\n", auto_cal_idx_string(target_idx), idx, y1);
            if (y1 == AT_EMAXSTEP) {
                if (y1_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (y1 < AT_EMAXSTEP) {
                LOG("%s.y-%d detect failed(forward).\n", auto_cal_idx_string(target_idx), idx);
                return y1;
            }
            y1_err = 0;
            if (y0 - y1 <= 0) {
                LOG("cant be here, %s.y(%d - %d).\n", auto_cal_idx_string(target_idx), y0, y1);
                return AT_ENOTMATCH;
            }
            s_data[idx].y0 = y0;
            s_data[idx].y1 = y1;
            s_data[idx].y_mid = (y0 + y1) / 2;
            s_data[idx].y_diff = y0 - y1;
            /* 移动到Y中点,为后续X探测做准备 */
            diff_step = (y0 + y1) / 2 - (y1 - XY_AVOID_OFFSET);
            if (diff_step != 0) {
                if (auto_cal_z_check(NEEDLE_TYPE_S) != 0) {
                    return AT_EIO;
                }
                ret = auto_cal_xy_single_move(motor_y, diff_step);
                if (ret < 0) {
                    return ret;
                }
            } 

            /* X反向运动探测得到x0 */
            x_limit = xd + 3 / S_X_STEP_MM_RATIO;
            d_limit = xd + 4 / S_X_STEP_MM_RATIO;
            /* 结构限制,样本针取试剂内圈x只能正向探测 */
            x0 = auto_cal_xy_pos(target_idx, motor_x, x_limit, d_limit,
                (target_idx == POS_REAG_INSIDE ? FORWARD : REVERSE), z, target_pos, 0);
            LOG("%s.x-%d detect_step = %d(reverse).\n", auto_cal_idx_string(target_idx), idx, x0);
            if (x0 == AT_EMAXSTEP) {
                if (x0_err++ >= 1) {
                    return AT_ENOTMATCH;
                } else {
                    continue;
                }
            } else if (x0 < AT_EMAXSTEP) {
                LOG("%s.x-%d detect failed(reverse).\n", auto_cal_idx_string(target_idx), idx);
                return x0;
            }
            x0_err = 0;
            /* X正向运动探测得到x1 */
            if (target_idx == POS_REAG_INSIDE) {
                /* 结构限制,x已正向探测完毕 */
                s_data[idx].x0 = x0;
                s_data[idx].x_mid = x0 + xd / 2;
            } else {
                /* 先尝试移动到距离工装3mm处 */
                x_limit = xd + XY_AVOID_OFFSET + 3 / S_X_STEP_MM_RATIO;
                if (target_idx == POS_SAMPLE_TEMP || target_idx == POS_ADD_SAMPLE) {
                    if (x0 + XY_AVOID_OFFSET - x_limit < -200) {
                        /* 如果按照预留3mm探测距离移动，已经超过极限位置-200，强制限制在-200位置开始探测 */
                        x_limit = x0 + XY_AVOID_OFFSET + 200;
                        LOG("%s.x(forward) unreachable, set x_limit = %d.\n", auto_cal_idx_string(target_idx), x_limit);
                    }

                    if (x_limit < (int)(xd + XY_AVOID_OFFSET + 2 / S_X_STEP_MM_RATIO)) {
                        /* 无法保证充裕的探测距离2mm */
                        LOG("%s.x(forward) cant be here, less than 2mm.\n", auto_cal_idx_string(target_idx));
                        return AT_ENOTMATCH;
                    }
                }
                if (target_idx == POS_SAMPLE_TEMP || target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) {
                    /* 圆形工装存在偏离直径远的情况 */
                    d_limit = xd / 2 + 3 / S_X_STEP_MM_RATIO;
                } else {
                    /* 方形工装最多在工装外3mm处 */
                    d_limit = 5 / S_X_STEP_MM_RATIO;
                }
                x1 = auto_cal_xy_pos(target_idx, motor_x, x_limit, d_limit, FORWARD, z, target_pos, 0);
                LOG("%s.x-%d detect_step = %d(forward).\n", auto_cal_idx_string(target_idx), idx, x1);
                if (x1 == AT_EMAXSTEP) {
                    if (x1_err++ >= 1) {
                        return AT_ENOTMATCH;
                    } else {
                        continue;
                    }
                } else if (x1 < AT_EMAXSTEP) {
                    LOG("%s.x-%d detect failed(forward).\n", auto_cal_idx_string(target_idx), idx);
                    return x1;
                }
                x1_err = 0;
                if (x0 - x1 <= 0) {
                    LOG("cant be here, %s.x(%d - %d).\n", auto_cal_idx_string(target_idx), x0, x1);
                    return AT_ENOTMATCH;
                }
                s_data[idx].x0 = x0;
                s_data[idx].x1 = x1;
                s_data[idx].x_mid = (x0 + x1) / 2;
                s_data[idx].x_diff = x0 - x1;
                if (target_idx == POS_SAMPLE_TEMP && s_data[idx].x_mid > ori_pos.y + xd / 2) {
                    /* 如果标定出的x坐标比给定参考值更靠右，可能将工装放错位置，强行置错 */
                    LOG("wrong place, %s.x cal failed(%d - %d).\n", auto_cal_idx_string(target_idx), s_data[idx].x_mid, ori_pos.y);
                    return AT_ENOTMATCH;
                }
            }

            if (cal_status == AUTO_CAL_INIT) {
                cal_status = AUTO_CAL_PASS;
            } else {
                cal_status = AUTO_CAL_DONE;
            }
            /* 外部探测，向内挤压 */
            if (s_data[idx].y_diff >= yd - (int)(0.2 / S_Y_STEP_MM_RATIO) &&
                s_data[idx].y_diff <= yd + (int)(0.1 / S_Y_STEP_MM_RATIO)) {
                cal_pos.y = s_data[idx].y_mid;
                LOG("%s.y-%d = %d(%d - %d) Ok(y.d = %d).\n", auto_cal_idx_string(target_idx), idx, y0 - y1, y0, y1, yd);
            } else {
                if (idx >= AUTO_CAL_COUNT_MAX / 2) {
                    for (idy = idx - 1; idy >= 0; idy--) {
                        if (abs(s_data[idx].y0 - s_data[idy].y0) <= 3 && abs(s_data[idx].y1 - s_data[idy].y1) <= 3) {
                            if (s_data[idx].y_diff >= yd - (int)(0.5 / S_Y_STEP_MM_RATIO) &&
                                s_data[idx].y_diff <= yd + (int)(0.4 / S_Y_STEP_MM_RATIO)) {
                                cal_pos.y = (s_data[idx].y_mid + s_data[idy].y_mid) / 2;
                                LOG("%s.y-%d = %d(%d - %d) Ok-1(y.d = %d, match with %d).\n",
                                    auto_cal_idx_string(target_idx), idx, y0 - y1, y0, y1, yd, idy);
                                break;
                            }
                        }
                    }
                }
            }
            if (target_idx == POS_REAG_INSIDE) {
                /* 样本针试剂仓内圈x只正向探测，因此无法比较直径 */
                if (idx > 0) {
                    for (idy = idx - 1; idy >= 0; idy--) {
                        if (abs(s_data[idx].x0 - s_data[idy].x0) <= 3) {
                            cal_pos.x = (s_data[idx].x0 + s_data[idy].x0) / 2 + xd / 2;
                            LOG("%s.x-%d Ok.\n", auto_cal_idx_string(target_idx), idx);
                            break;
                        }
                    }
                }
            } else {
                if (s_data[idx].x_diff >= xd - (int)(0.2 / S_X_STEP_MM_RATIO) &&
                    s_data[idx].x_diff <= xd + (int)(0.1 / S_X_STEP_MM_RATIO)) {
                    cal_pos.x = s_data[idx].x_mid;
                    LOG("%s.x-%d = %d(%d - %d) Ok(x.d = %d).\n", auto_cal_idx_string(target_idx), idx, x0 - x1, x0, x1, xd);
                } else {
                    if (idx >= AUTO_CAL_COUNT_MAX / 2) {
                        for (idy = idx - 1; idy >= 0; idy--) {
                            if (abs(s_data[idx].x0 - s_data[idy].x0) <= 3 && abs(s_data[idx].x1 - s_data[idy].x1) <= 3) {
                                if (s_data[idx].x_diff >= xd - (int)(0.5 / S_X_STEP_MM_RATIO) &&
                                    s_data[idx].x_diff <= xd + (int)(0.4 / S_X_STEP_MM_RATIO)) {
                                    cal_pos.x = (s_data[idx].x_mid + s_data[idy].x_mid) / 2;
                                    LOG("%s.x-%d = %d(%d - %d) Ok-1(x.d = %d, match with %d).\n",
                                        auto_cal_idx_string(target_idx), idx, x0 - x1, x0, x1, xd, idy);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (cal_status == AUTO_CAL_DONE && cal_pos.x != 0 && cal_pos.y != 0) {
                break;
            } else {
                if (cal_pos.x == 0 && target_idx != POS_REAG_INSIDE) {
                    LOG("%s.x-%d = %d(%d - %d) mismatch(x.d = %d).\n", auto_cal_idx_string(target_idx), idx, x0 - x1, x0, x1, xd);
                }
                if (cal_pos.y == 0) {
                    LOG("%s.y-%d = %d(%d - %d) mismatch(y.d = %d).\n", auto_cal_idx_string(target_idx), idx, y0 - y1, y0, y1, yd);
                }
            }
        }
        if (idx == AUTO_CAL_COUNT_MAX) {
            LOG("%s cal failed.\n", auto_cal_idx_string(target_idx));
            return AT_ENOTMATCH;
        } else {
            /* 重新复位 */
            ret = auto_cal_s_reset((target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE) ? 1 : 0);
            if (ret < 0) {
                return ret;
            }
            final_pos->pos.x = cal_pos.x;
            final_pos->pos.y = cal_pos.y;
            final_pos->pos.z = z;
            LOG("%s final_pos: %d - %d - %d(stand_pos: %d - %d - %d).\n",
                auto_cal_idx_string(target_idx), final_pos->pos.x, final_pos->pos.y, final_pos->pos.z, ori_pos.y, ori_pos.x, ori_pos.z);
        }
    } else {
        LOG("TBD.\n");
        return AT_EARG;
    }

    return 0;
}

static int auto_cal_r2(auto_cal_idx_t target_idx,
    pos_t std,
    motor_time_sync_attr_t attr_x,
    motor_time_sync_attr_t attr_y,
    auto_cal_rcd_t *final_pos)
{
    int idx = 0, idy = 0, limit = 0, y0 = 0, y1 = 0, z, reag_pos_old = 0, reag_pos_new = 0;
    int adjust_step = 0, max_idx = 0, max_diff = 0, y_mid = 0, min_step = 8, min_offset = 8, ret = 0, y_err = 0;
    needle_type_t needle = NEEDLE_TYPE_R2;
    pos_t target_pos = {0}, cal_pos = {0}, ori_pos = {0};
    auto_cal_r2_l_t location = MIDDLE;
    auto_cal_r2_pos_t r2_data[AUTO_CAL_COUNT_MAX] = {0}, cal_loc[3] = {0};
    auto_cal_r2_pos_t rcd_pos[R2_RETRY_MAX] = {0}, avg_pos[2] = {0};
    auto_cal_status_t cal_status = AUTO_CAL_INIT;

    memcpy(&ori_pos, &std, sizeof(pos_t));
    /* 相关部件复位 */
    ret = auto_cal_r2_reset((target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) ? 1 : 0);
    if (ret < 0) {
        return ret;
    }
    /* 试剂仓就位 */
    if (target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) {
        reag_pos_old = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][target_idx == POS_R2_INSIDE ? H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN : H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT];
        LOG("reag_table init_pos = %d.\n", reag_pos_old);
        if (auto_cal_reag_table_move(NEEDLE_TYPE_R2, target_idx) < 0) {
            LOG("%s reag table move failed.\n", auto_cal_idx_string(target_idx));
            return AT_EMOVE;
        }
    }
    /* Y就位 */
    ret = auto_cal_r2_y_move(std.y);
    if (ret < 0)  {
        return ret;
    }
    /* 自动查找工装(可能)并探测Z */
    LOG("%s stand_pos = %d.\n", auto_cal_idx_string(target_idx), std.y);
    z = auto_cal_obj_detect(target_idx, needle, &std);
    if (z <= 0) {
        LOG("%s.z = %d cal failed, maybe std.z out_of_range.\n", auto_cal_idx_string(target_idx), z);
        return z;
    }
    LOG("%s.z = %d, new stand_pos = %d.\n", auto_cal_idx_string(target_idx), z, std.y);

    if (target_idx == POS_R2_MIX || target_idx == POS_R2_MAG_1 || target_idx == POS_R2_MAG_4 || target_idx == POS_R2_CLEAN) {
        memset(r2_data, 0, sizeof(r2_data));
        cal_status = AUTO_CAL_INIT;
        y_err = 0;
        for (idx = 0; idx < AUTO_CAL_COUNT_MAX; idx++) {
            memset(&cal_pos, 0, sizeof(cal_pos));
            ret = auto_cal_r2_reset(0);
            if (ret < 0) {
                return ret;
            }

            target_pos.z = z;
            if (cal_status == AUTO_CAL_INIT) {
                target_pos.y = std.y;
            } else {
                for (idy = idx - 1; idy >= 0; idy--) {
                    if (r2_data[idy].y_mid) {
                        target_pos.y = r2_data[idy].y_mid;
                        break;
                    }
                }
            }
            ret = auto_cal_r2_y_move(target_pos.y);
            if (ret < 0) {
                return ret;
            }

            ret = auto_cal_r2_y_pos(target_idx, target_pos, &y0, &y1, 0);
            if (ret < 0) {
                if (ret == AT_EMAXSTEP) {
                    if (y_err++ >= 1) {
                        return AT_ENOTMATCH;
                    } else {
                        continue;
                    }
                } else {
                    return ret;
                }
            }
            r2_data[idx].y0 = y0;
            r2_data[idx].y1 = y1;
            r2_data[idx].y_mid = (y0 + y1) / 2;
            r2_data[idx].y_diff = y1 - y0;
            if (cal_status == AUTO_CAL_INIT) {
                cal_status = AUTO_CAL_PASS;
            } else {
                cal_status = AUTO_CAL_DONE;
            }

            if (target_idx == POS_R2_CLEAN) {
                /* 洗针位工装特殊，且Y不一定在直径上，因此无法比较直径;使用两次探测出来的差值比较，如果相等则认为探测成功 */
                if (idx > 0) {
                    for (idy = idx - 1; idy >= 0; idy--) {
                        if (abs(r2_data[idx].y0 - r2_data[idy].y0) <= 3 && abs(r2_data[idx].y1 - r2_data[idy].y1) <= 3) {
                            cal_pos.y = (r2_data[idx].y0 + r2_data[idx].y1) / 2;
                            LOG("%s.y-%d = %d Ok.\n", auto_cal_idx_string(target_idx), idx, cal_pos.y);
                            break;
                        }
                    }
                }
            } else {
                if (target_idx == POS_R2_MIX) {
                    limit = AUTO_CAL_R2_MIX_Y_D;
                } else {
                    limit = AUTO_CAL_R2_MAG_Y_D;
                }
                /* 外部探测，向内挤压 */
                if (r2_data[idx].y_diff >= limit - (int)(0.2 / S_Y_STEP_MM_RATIO) &&
                    r2_data[idx].y_diff <= limit + (int)(0.1 / S_Y_STEP_MM_RATIO)) {
                    cal_pos.y = r2_data[idx].y_mid;
                    LOG("%s.y-%d = %d(%d - %d) Ok(y.d = %d).\n", auto_cal_idx_string(target_idx), idx, y1 - y0, y1, y0, limit);
                } else {
                    if (idx >= AUTO_CAL_COUNT_MAX / 2) {
                        for (idy = idx - 1; idy >= 0; idy--) {
                            if (abs(r2_data[idx].y0 - r2_data[idy].y0) <= 3 && abs(r2_data[idx].y1 - r2_data[idy].y1) <= 3) {
                                if (r2_data[idx].y_diff >= limit - (int)(0.5 / S_Y_STEP_MM_RATIO) &&
                                    r2_data[idx].y_diff <= limit + (int)(0.4 / S_Y_STEP_MM_RATIO)) {
                                    cal_pos.y = (r2_data[idx].y_mid + r2_data[idy].y_mid) / 2;
                                    LOG("%s.y-%d = %d(%d - %d) Ok-1(y.d = %d, match with %d).\n",
                                        auto_cal_idx_string(target_idx), idx, y1 - y0, y1, y0, limit, idy);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            if (cal_status == AUTO_CAL_DONE && cal_pos.y != 0) {
                break;
            } else {
                if (cal_pos.y == 0 && target_idx != POS_R2_CLEAN) {
                    LOG("%s.y-%d = %d(%d - %d) mismatch(y.d = %d).\n", auto_cal_idx_string(target_idx), idx, y1 - y0, y1, y0, limit);
                }
            }
        }
        if (idx == AUTO_CAL_COUNT_MAX) {
            LOG("%s cal failed.\n", auto_cal_idx_string(target_idx));
            return AT_ENOTMATCH;
        } else {
            /* 重新复位 */
            ret = auto_cal_r2_reset(0);
            if (ret < 0) {
                return ret;
            }
            final_pos->pos.y = cal_pos.y;
            final_pos->pos.z = z;
            LOG("%s final_pos: %d - %d(stand_pos: %d - %d).\n",
                auto_cal_idx_string(target_idx), final_pos->pos.y, final_pos->pos.z, ori_pos.y, ori_pos.z);
        }
    } else if (target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) {
        /* 探测试剂仓时，先分别探测中左右3个位置 */
        for (idx = 0; idx < 3; idx++) {
            if (idx == 0) {
                target_pos.y = std.y;
                adjust_step = 0;
            } else {
                if (cal_loc[idx - 1].y_mid) {
                    target_pos.y = cal_loc[idx - 1].y_mid;
                }
                adjust_step = (idx == 1 ? -33 : 66);    /* 先左后右 */
            }
            target_pos.z = z;

            if (adjust_step) {
                if (auto_cal_stop_flag_get()) {
                    return AT_EMSTOP;
                }
                if (auto_cal_z_check(NEEDLE_TYPE_R2) < 0) {
                    LOG("%s.y-%d cal failed, cant be here(step1).\n", auto_cal_idx_string(target_idx), idx);
                    return AT_EIO;
                }
                LOG("reag_table(step1) adjust_step = %d.\n", adjust_step);
                if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE, CMD_MOTOR_MOVE_STEP, adjust_step,
                    h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].speed,
                    h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
                    LOG("reag_table move failed.\n");
                    return AT_EMOVE;
                }
            }

            ret = auto_cal_r2_y_pos_ensure(target_idx, target_pos, &y0, &y1, idx == 0 ? 0 : 1);
            if (ret < 0) {
                if (idx == 0) {
                    /* 第一次探测中间时，不应出现探测不到的结果 */
                    LOG("%s.y-%d detect failed, cant be here(step1).\n", auto_cal_idx_string(target_idx), idx);
                    return AT_ENOTMATCH;
                } else {
                    /* 左右探测，极限情况下仍然存在一面探测不到的可能 */
                    if (ret == AT_EMAXSTEP || ret == AT_ENOTMATCH) {
                        memset(&cal_loc[idx], 0, sizeof(cal_loc[idx]));
                    } else {
                        return ret;
                    }
                }
            } else {
                cal_loc[idx].y0 = y0;
                cal_loc[idx].y1 = y1;
                cal_loc[idx].y_mid = (y0 + y1) / 2;
                cal_loc[idx].y_diff = y1 - y0;
            }
        }

        /* 结合3次的探测值确定方位 */
        for (idx = 0; idx < 3; idx++) {
            LOG("%s.%d, y0 = %d, y1 = %d, mid = %d, diff = %d.\n", auto_cal_idx_string(target_idx),
                idx, cal_loc[idx].y0, cal_loc[idx].y1, cal_loc[idx].y_mid, cal_loc[idx].y_diff);
        }
        if (cal_loc[1].y_mid == 0 && cal_loc[2].y_mid == 0) {
            /* 绝不可能出现左右都脱靶的情况 */
            LOG("%s.y cal failed, cant be here(step1).\n", auto_cal_idx_string(target_idx));
            return AT_ENOTMATCH;
        }
        if (cal_loc[1].y_diff < cal_loc[0].y_diff && cal_loc[2].y_diff >= cal_loc[0].y_diff) {
            LOG("%s init_pos on the left, confirmed.\n", auto_cal_idx_string(target_idx));
            location = LEFT_SIDE;
        } else if (cal_loc[2].y_diff < cal_loc[0].y_diff && cal_loc[1].y_diff >= cal_loc[0].y_diff) {
            LOG("%s init_pos on the right, confirmed.\n", auto_cal_idx_string(target_idx));
            location = RIGHT_SIDE;
        } else {
            LOG("%s init_pos in the middle, confirmed.\n", auto_cal_idx_string(target_idx));
            location = MIDDLE;
        }

        /* 试剂仓复位 */
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        if (auto_cal_z_check(NEEDLE_TYPE_R2) < 0) {
            LOG("%s.y-%d cal failed, cant be here(step1).\n", auto_cal_idx_string(target_idx), idx);
            return AT_EIO;
        }
        ret = auto_cal_r2_reset(1);
        if (ret < 0) {
            return ret;
        }
        if (auto_cal_stop_flag_get()) {
            return AT_EMSTOP;
        }
        if (auto_cal_reag_table_move(NEEDLE_TYPE_R2, target_idx) < 0) {
            LOG("%s reag table move failed.\n", auto_cal_idx_string(target_idx));
            return AT_EMOVE;
        }
        reag_pos_new = reag_pos_old;

        /* 确定方位后，从AUTO_CAL_R2_REAG_Y_D - 10开始探测，保证后续抛物线完整 */
        if (cal_loc[0].y_diff <= AUTO_CAL_R2_REAG_Y_D - 10) {
            /* nothing todo */
            y_mid = cal_loc[0].y_mid;
        } else {
            memset(r2_data, 0, sizeof(r2_data));
            for (idx = 0; idx < AUTO_CAL_COUNT_MAX; idx++) {
                /* 确定移动位置 */
                if (idx == 0) {
                    target_pos.y = cal_loc[0].y_mid;
                    adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? -50 : 50);
                } else {
                    if (r2_data[idx - 1].y_mid) {
                        target_pos.y = r2_data[idx - 1].y_mid;
                    }
                    adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? -15 : 15);
                }
                target_pos.z = z;

                /* 试剂仓旋转 */
                if (auto_cal_stop_flag_get()) {
                    return AT_EMSTOP;
                }
                if (auto_cal_z_check(NEEDLE_TYPE_R2) < 0) {
                    LOG("%s.y cal failed, cant be here(step2).\n", auto_cal_idx_string(target_idx));
                    return AT_EIO;
                }
                LOG("reag_table(step2) cur = %d, adjust_step = %d.\n", reag_pos_new, adjust_step);
                if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE, CMD_MOTOR_MOVE_STEP, adjust_step,
                    h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].speed,
                    h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
                    LOG("reag_table move failed.\n");
                    return AT_EMOVE;
                }
                reag_pos_new += adjust_step;
                LOG("reag_table(step2) new = %d.\n", reag_pos_new);

                ret = auto_cal_r2_y_pos_ensure(target_idx, target_pos, &y0, &y1, 1);
                if (ret < 0) {
                    LOG("%s.y-%d cal failed(step2).\n", auto_cal_idx_string(target_idx), idx);
                    return ret;
                } else {
                    r2_data[idx].y0 = y0;
                    r2_data[idx].y1 = y1;
                    r2_data[idx].y_mid = (y0 + y1) / 2;
                    r2_data[idx].y_diff = y1 - y0;
                    LOG("%s.y-%d find start_pos cur_diff = %d.\n", auto_cal_idx_string(target_idx), idx, r2_data[idx].y_diff);
                    if (r2_data[idx].y_diff <= AUTO_CAL_R2_REAG_Y_D - 10) {
                        y_mid = (y0 + y1) / 2;
                        break;
                    } else {
                        continue;
                    }
                }
            }
        }

        /* 探测以得到抛物线数据 */
        for (idx = 0; idx < R2_RETRY_MAX; idx++) {
            /* 确定每次Y需要移动的位置 */
            if (idx == 0) {
                target_pos.y = y_mid;
            } else {
                target_pos.y = rcd_pos[idx - 1].y_mid;
            }
            target_pos.z = z;

            ret = auto_cal_r2_y_pos_ensure(target_idx, target_pos, &y0, &y1, 1);
            if (ret < 0) {
                LOG("%s.y-%d cal failed(step3).\n", auto_cal_idx_string(target_idx), idx);
                return ret;
            }

            /* 记录 */
            rcd_pos[idx].y0 = y0;
            rcd_pos[idx].y1 = y1;
            rcd_pos[idx].y_mid = (y0 + y1) / 2;
            rcd_pos[idx].y_diff = y1 - y0;
            rcd_pos[idx].reag = reag_pos_new;
            if (rcd_pos[idx].y_diff >= max_diff) {
                max_idx = idx;
                max_diff = rcd_pos[idx].y_diff;
                LOG("max_idx = %d, max_diff = %d.\n", max_idx, max_diff);
            }
            LOG("%s.%d y0 = %d, y1 = %d, y_mid = %d, cur_diff = %d, cur_table = %d, max_id = %d, max_diff = %d.\n",
                auto_cal_idx_string(target_idx), idx, rcd_pos[idx].y0, rcd_pos[idx].y1,
                rcd_pos[idx].y_mid, rcd_pos[idx].y_diff, rcd_pos[idx].reag, max_idx, max_diff);

            if (idx != 0) {
                /* 跳点处理 */
                if ((abs(adjust_step) <= min_step && rcd_pos[idx - 1].y_diff && rcd_pos[idx].y_diff - rcd_pos[idx - 1].y_diff > 4) ||
                    (abs(adjust_step) <= min_step && rcd_pos[idx - 1].y_diff && rcd_pos[idx - 1].y_diff - rcd_pos[idx].y_diff > 4)) {
                    /* 粒度小的情况下理论不应该出现突然差值变大/变小的情况 */
                    LOG("%s.%d %d - %d out of range.\n", auto_cal_idx_string(target_idx), idx, rcd_pos[idx - 1].y_diff, rcd_pos[idx].y_diff);
                    if (abs(adjust_step) <= min_step && rcd_pos[idx - 1].y_diff && rcd_pos[idx].y_diff - rcd_pos[idx - 1].y_diff > 4) {
                        /* 跳大 */
                        max_idx = max_diff = 0;
                        for (idy = 0; idy < idx; idy++) {   /* 在本数据之前找一个最大值记录 */
                            if (rcd_pos[idy].y_diff >= max_diff) {
                                max_idx = idy;
                                max_diff = rcd_pos[idy].y_diff;
                                LOG("find max_idx = %d, max_diff = %d.\n", max_idx, max_diff);
                            }
                        }
                        if (rcd_pos[idx].y_diff > max_diff) {
                            /* 本次值比之前的最大值还大，如本值跳点,防止影响抛物线顶点,处理 */
                            max_idx = idx;
                            max_diff = max_diff + 1;
                            rcd_pos[idx].y_diff = max_diff;
                            LOG("change max_idx = %d, max_diff = %d.\n", max_idx, max_diff);
                        } else if (rcd_pos[idx].y_diff == max_diff) {
                            max_idx = idx;
                            max_diff = rcd_pos[idx].y_diff;
                        } else {
                            /* nothing todo */
                        }
                    } else {
                        /* 跳小，为防止提前导致break0，处理 */
                        rcd_pos[idx].y_diff = (rcd_pos[idx - 1].y_diff + rcd_pos[idx].y_diff) / 2;
                        LOG("change cur_diff = %d.\n", rcd_pos[idx].y_diff);
                        if (idx >= 8 && max_diff > AUTO_CAL_R2_REAG_Y_D - 6 && max_diff - rcd_pos[idx].y_diff >= min_offset) {
                            LOG("%s force break1.\n", auto_cal_idx_string(target_idx));
                            break;
                        }
                    }
                    if (auto_cal_stop_flag_get()) {
                        return AT_EMSTOP;
                    }
                    if (auto_cal_z_check(NEEDLE_TYPE_R2) < 0) {
                        LOG("%s.y cal failed, cant be here(step3).\n", auto_cal_idx_string(target_idx));
                        return AT_EIO;
                    }
                    adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? min_step : -min_step);
                    LOG("reag_table(step3) cur = %d, adjust_step = %d.\n", reag_pos_new, adjust_step);
                    if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE, CMD_MOTOR_MOVE_STEP, adjust_step,
                        h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].speed,
                        h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
                        LOG("reag_table move failed.\n");
                        return AT_EMOVE;
                    }
                    reag_pos_new += adjust_step;
                    LOG("reag_table(step3) new = %d.\n", reag_pos_new);
                    continue;
                }else {
                    /* 跳出条件 */
                    if (idx >= 8 && max_diff > AUTO_CAL_R2_REAG_Y_D - 6 && max_diff - rcd_pos[idx].y_diff >= min_offset) {
                        LOG("%s force break0.\n", auto_cal_idx_string(target_idx));
                        break;
                    }
                }
            }

            if (AUTO_CAL_R2_REAG_Y_D - rcd_pos[idx].y_diff > 20) {
                adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? 33 : -33);
            } else if (AUTO_CAL_R2_REAG_Y_D - rcd_pos[idx].y_diff > 12 &&
                        AUTO_CAL_R2_REAG_Y_D - rcd_pos[idx].y_diff <= 20) {
                adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? 15 : -15);
            } else if (AUTO_CAL_R2_REAG_Y_D - rcd_pos[idx].y_diff > min_offset &&
                        AUTO_CAL_R2_REAG_Y_D - rcd_pos[idx].y_diff <= 12) {
                adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? 10 : -10);
            } else {
                adjust_step = ((location == MIDDLE || location == LEFT_SIDE) ? min_step : -min_step);
            }
            if (auto_cal_stop_flag_get()) {
                return AT_EMSTOP;
            }
            if (auto_cal_z_check(NEEDLE_TYPE_R2) < 0) {
                LOG("%s.y cal failed, cant be here(step3).\n", auto_cal_idx_string(target_idx));
                return AT_EIO;
            }
            LOG("reag_table(step3) cur = %d, adjust_step = %d.\n", reag_pos_new, adjust_step);
            if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE, CMD_MOTOR_MOVE_STEP, adjust_step,
                h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].speed,
                h3600_conf_get()->motor[MOTOR_REAGENT_TABLE].acc, MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("reag_table move failed.\n");
                return AT_EMOVE;
            }
            reag_pos_new += adjust_step;
            LOG("reag_table(step3) new = %d.\n", reag_pos_new);
        }

        /* 从记录的数据中计算 */
        for (idx = 0; idx < R2_RETRY_MAX; idx++) {
            if (rcd_pos[idx].y_mid) {
                LOG("%d: y0 = %d, y1 = %d, y_mid = %d, reag_table = %d, diff = %d.\n",
                    idx, rcd_pos[idx].y0, rcd_pos[idx].y1, rcd_pos[idx].y_mid, rcd_pos[idx].reag, rcd_pos[idx].y_diff);
            }
        }
        LOG("max_idx = %d, max_diff = %d.\n", max_idx, max_diff);
        if (rcd_pos[max_idx].y_mid == 0 || rcd_pos[max_idx].reag == 0 || max_diff < AUTO_CAL_R2_REAG_Y_D - 6) {
            LOG("%s cal failed, cant be here(step3).\n", auto_cal_idx_string(target_idx));
            return AT_ENOTMATCH;
        }
        /* 在最高点附近找2个值求算术平均值作为最终值 */
        avg_pos[0].y_mid = avg_pos[1].y_mid = rcd_pos[max_idx].y_mid;
        avg_pos[0].reag = avg_pos[1].reag = rcd_pos[max_idx].reag;
        /* 先在最高点左侧找出一个值 */
        for (idx = 0; idx < max_idx; idx++) {
            if (max_diff - rcd_pos[idx].y_diff <= min_offset - 2) {
                avg_pos[0].y_mid = rcd_pos[idx].y_mid;
                avg_pos[0].reag = rcd_pos[idx].reag;
                LOG("%s find idx = %d(y = %d, reag = %d).\n",
                    auto_cal_idx_string(target_idx), idx, rcd_pos[idx].y_mid, rcd_pos[idx].reag);
                break;
            }
        }
        /* 在最高点右侧找出一个值 */
        for (idx = R2_RETRY_MAX - 1; idx > max_idx; idx--) {
            if (rcd_pos[idx].y_diff && max_diff - rcd_pos[idx].y_diff <= min_offset - 2) {
                avg_pos[1].y_mid = rcd_pos[idx].y_mid;
                avg_pos[1].reag = rcd_pos[idx].reag;
                LOG("%s find idx = %d(y = %d, reag = %d).\n",
                    auto_cal_idx_string(target_idx), idx, rcd_pos[idx].y_mid, rcd_pos[idx].reag);
                break;
            }
        }
        final_pos->pos.y = (avg_pos[0].y_mid + avg_pos[1].y_mid) / 2;
        final_pos->pos.z = z;
        final_pos->reag_table = (avg_pos[0].reag + avg_pos[1].reag) / 2;
        ret = auto_cal_r2_reset(1);
        if (ret < 0) {
            return ret;
        }
        LOG("%s final_pos: y = %d, z = %d, table = %d(stand_pos: %d - %d - <%d>).\n",
            auto_cal_idx_string(target_idx), final_pos->pos.y, final_pos->pos.z, final_pos->reag_table,
            ori_pos.y, ori_pos.z, reag_pos_old);
    } else {
        LOG("TBD.\n");
        return AT_EARG;
    }

    return 0;
}

static int auto_cal_start(auto_cal_idx_t target_idx, auto_cal_rcd_t *old_pos, auto_cal_rcd_t *new_pos)
{
    int motor_x = 0, motor_y = 0, motor_z = 0, idx = 0, pos_idx = 0, z_diff = 0;
    int needle = NEEDLE_TYPE_S, ret = 0, lock_ret = 0;
    move_pos_t move_pos = 0;
    pos_t std = {0};
    motor_time_sync_attr_t attr_x = {0}, attr_y = {0}, attr_z = {0};

    switch (target_idx) {
        case POS_SAMPLE_1:
            move_pos = MOVE_S_SAMPLE_NOR;
            pos_idx = POS_0;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_SAMPLE_10:
            move_pos = MOVE_S_SAMPLE_NOR;
            pos_idx = POS_9;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_SAMPLE_60:
            move_pos = MOVE_S_SAMPLE_NOR;
            pos_idx = POS_59;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_ADD_SAMPLE:
            move_pos = MOVE_S_ADD_CUP_PRE;
            pos_idx = POS_0;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_SAMPLE_MIX_1:
            move_pos = MOVE_S_ADD_CUP_MIX;
            pos_idx = POS_PRE_PROCESSOR_MIX1;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_SAMPLE_MIX_2:
            move_pos = MOVE_S_ADD_CUP_MIX;
            pos_idx = POS_PRE_PROCESSOR_MIX2;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_REAG_INSIDE:
            move_pos = MOVE_S_ADD_REAGENT;
            pos_idx = POS_REAGENT_TABLE_S_IN;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_REAG_OUTSIDE:
            move_pos = MOVE_S_ADD_REAGENT;
            pos_idx = POS_REAGENT_TABLE_S_OUT;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_SAMPLE_TEMP:
            move_pos = MOVE_S_TEMP;
            pos_idx = POS_0;
            needle = NEEDLE_TYPE_S;
            break;
        case POS_R2_INSIDE:
            move_pos = MOVE_R2_REAGENT;
            pos_idx = POS_REAGENT_TABLE_R2_IN;
            needle = NEEDLE_TYPE_R2;
            break;
        case POS_R2_OUTSIDE:
            move_pos = MOVE_R2_REAGENT;
            pos_idx = POS_REAGENT_TABLE_R2_OUT;
            needle = NEEDLE_TYPE_R2;
            break;
        case POS_R2_MIX:
            move_pos = MOVE_R2_MIX;
            pos_idx = POS_0;
            needle = NEEDLE_TYPE_R2;
            break;
        case POS_R2_MAG_1:
            move_pos = MOVE_R2_MAGNETIC;
            pos_idx = POS_0;
            needle = NEEDLE_TYPE_R2;
            break;
        case POS_R2_MAG_4:
            move_pos = MOVE_R2_MAGNETIC;
            pos_idx = POS_3;
            needle = NEEDLE_TYPE_R2;
            break;
        case POS_R2_CLEAN:
            move_pos = MOVE_R2_CLEAN;
            pos_idx = POS_0;
            needle = NEEDLE_TYPE_R2;
            break;
        default:
            LOG("auto_cal: id = %d invalid.\n", target_idx);
            return AT_EARG;
    }

    if (needle == NEEDLE_TYPE_S) {
        motor_x = MOTOR_NEEDLE_S_X;
        motor_y = MOTOR_NEEDLE_S_Y;
        motor_z = MOTOR_NEEDLE_S_Z;
    } else {
        motor_y = MOTOR_NEEDLE_R2_Y;
        motor_z = MOTOR_NEEDLE_R2_Z;
    }

    liquid_detect_motor_init(motor_x, motor_y, motor_z, &attr_x, &attr_y, &attr_z);
    if (target_idx == POS_SAMPLE_10 || target_idx == POS_SAMPLE_60 || target_idx == POS_SAMPLE_TEMP) {
        if (target_idx == POS_SAMPLE_10) {
            idx = H3600_CONF_POS_S_SAMPLE_NOR_10;
        } else if (target_idx == POS_SAMPLE_60) {
            idx = H3600_CONF_POS_S_SAMPLE_NOR_60;
        } else if (target_idx == POS_SAMPLE_TEMP) {
            idx = H3600_CONF_POS_S_TEMP;
        }
        std.x = h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][idx];
        std.y = h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][idx];
        std.z = h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Z][idx];
    } else if (target_idx == POS_R2_MAG_1 || target_idx == POS_R2_MAG_4) {
        idx = (target_idx == POS_R2_MAG_1 ? H3600_CONF_POS_R2_MAG_1 : H3600_CONF_POS_R2_MAG_4);
        std.y = h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][idx];
        std.z = h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][idx];
    } else {
        get_special_pos(move_pos, pos_idx, &std, FLAG_POS_UNLOCK);
    }
    z_diff = -g_diff[target_idx].pos.z;  /* 读出保存的原值，需要结合差值使用 */
    LOG("auto_cal: %s stand_pos x = %d, y = %d, z = %d(diff = %d).\n", auto_cal_idx_string(target_idx),
        needle == NEEDLE_TYPE_S ? std.y : std.x, needle == NEEDLE_TYPE_S ? std.x : std.y, std.z, z_diff);
    old_pos->pos.x = std.x;
    old_pos->pos.y = std.y;
    old_pos->pos.z = std.z;

    std.z += z_diff;
    if (needle == NEEDLE_TYPE_S) {
        if (auto_cal_sampler_ctrl(target_idx, ATTR_ENABLE) < 0) {
            ret = AT_ESAMPLER;
        } else {
            ret = auto_cal_s(target_idx, std, attr_x, attr_y, new_pos);
            lock_ret = auto_cal_sampler_ctrl(target_idx, ATTR_DISABLE);
            if (lock_ret < 0) {
                ret = AT_ESAMPLER;
            }
        }
    } else if (needle == NEEDLE_TYPE_R2) {
        ret = auto_cal_r2(target_idx, std, attr_x, attr_y, new_pos);
    } else {
        LOG("auto_cal: needle Not support.\n", needle);
        ret = AT_EARG;
    }
    if (target_idx == POS_REAG_INSIDE || target_idx == POS_REAG_OUTSIDE ||
        target_idx == POS_R2_INSIDE || target_idx == POS_R2_OUTSIDE) {
        reag_table_occupy_flag_set(0);
    }

    return ret;
}

/* 工程师模式自动标定需按照手动调整位置的格式上报，将自动标定的顺序转换为手动调整时规定的顺序 */
static void auto_cal_pos_to_eng_pos(auto_cal_idx_t idx, int *module_idx, int *pos_idx)
{
    if (idx <= POS_SAMPLE_TEMP) {
        *module_idx = 10;
    } else {
        *module_idx = 11;
    }

    if (idx == POS_REAG_INSIDE) {
        *pos_idx = 7;
    } else if (idx == POS_REAG_OUTSIDE) {
        *pos_idx = 8;
    } else if (idx == POS_SAMPLE_1) {
        *pos_idx = 1;
    } else if (idx == POS_SAMPLE_10) {
        *pos_idx = 2;
    } else if (idx == POS_SAMPLE_60) {
        *pos_idx = 3;
    } else if (idx == POS_ADD_SAMPLE) {
        *pos_idx = 4;
    } else if (idx == POS_SAMPLE_MIX_1) {
        *pos_idx = 5;
    } else if (idx == POS_SAMPLE_MIX_2) {
        *pos_idx = 6;
    } else if (idx == POS_SAMPLE_TEMP) {
        *pos_idx = 10;
    } else if (idx == POS_R2_MIX) {
        *pos_idx = 3;
    } else if (idx == POS_R2_MAG_1) {
        *pos_idx = 4;
    } else if (idx == POS_R2_MAG_4) {
        *pos_idx = 5;
    } else if (idx == POS_R2_CLEAN) {
        *pos_idx = 6;
    } else if (idx == POS_R2_INSIDE) {
        *pos_idx = 1;
    } else if (idx == POS_R2_OUTSIDE) {
        *pos_idx = 2;
    }
}

static int auto_cal_pos_report(auto_cal_idx_t idx, auto_cal_mode_t mode)
{
    int i = 0, pos_idx = 0, old_pos = 0, new_pos = 0, diff = 0;
    int eng_module_idx = 0, eng_pos_idx = 0;

    auto_cal_pos_to_eng_pos(idx, &eng_module_idx, &eng_pos_idx);
    LOG("auto_cal: report idx = %d, module_idx = %d, pos_idx = %d.\n", idx, eng_module_idx, eng_pos_idx);
    if (idx <= POS_SAMPLE_TEMP) {
        for (i = 0; i < 3; i++) {
            pos_idx = idx * 3 + i;
            if (i == 0) {
                old_pos = g_old_pos[idx].pos.y;
                new_pos = g_new_pos[idx].pos.x;
                diff = g_diff[idx].pos.x;
            } else if (i == 1) {
                old_pos = g_old_pos[idx].pos.x;
                new_pos = g_new_pos[idx].pos.y;
                diff = g_diff[idx].pos.y;
            } else {
                /* 只需调整Z的补偿(自动标定工装和手动标定工装之间的差异) */
                old_pos = g_old_pos[idx].pos.z;
                new_pos = g_new_pos[idx].pos.z;
                diff = g_diff[idx].pos.z;
            }
            LOG("auto_cal: report-%d, old = %d, new = %d, diff = %d.\n", pos_idx, old_pos, new_pos, diff);
            if (mode == SERVICE_MODE) {
                report_position_calibration(ENG_DEBUG_NEEDLE, pos_idx, old_pos, new_pos + diff);
            }
        }
    } else if (idx == POS_R2_INSIDE || idx == POS_R2_OUTSIDE) {
        for (i = 0; i < 3; i++) {
            if (idx == POS_R2_INSIDE) {
                pos_idx = POS_R2_INSIDE_Y + i;
            } else {
                pos_idx = POS_R2_OUTSIDE_Y + i;
            }
            if (i == 0) {
                old_pos = g_old_pos[idx].pos.y;
                new_pos = g_new_pos[idx].pos.y;
                diff = g_diff[idx].pos.y;
            } else if (i == 1) {
                /* 只需调整Z的补偿(自动标定工装和手动标定工装之间的差异) */
                old_pos = g_old_pos[idx].pos.z;
                new_pos = g_new_pos[idx].pos.z;
                diff = g_diff[idx].pos.z;
            } else {
                old_pos = g_old_pos[idx].reag_table;
                new_pos = g_new_pos[idx].reag_table;
                diff = g_diff[idx].reag_table;
            }
            LOG("auto_cal: report-%d, old = %d, new = %d, diff = %d.\n", pos_idx, old_pos, new_pos, diff);
            if (mode == SERVICE_MODE) {
                report_position_calibration(ENG_DEBUG_NEEDLE, pos_idx, old_pos, new_pos + diff);
            }
        }
    } else {
        for (i = 0; i < 2; i++) {
            if (idx == POS_R2_MIX) {
                pos_idx = POS_R2_MIX_Y + i;
            } else if (idx == POS_R2_MAG_1) {
                pos_idx = POS_R2_MAG_1_Y + i;
            } else if (idx == POS_R2_MAG_4) {
                pos_idx = POS_R2_MAG_4_Y + i;
            } else if (idx == POS_R2_CLEAN) {
                pos_idx = POS_R2_CLEAN_Y + i;
            }

            if (i == 0) {
                old_pos = g_old_pos[idx].pos.y;
                new_pos = g_new_pos[idx].pos.y;
                diff = g_diff[idx].pos.y;
            } else if (i == 1) {
                /* 只需调整Z的补偿(自动标定工装和手动标定工装之间的差异) */
                old_pos = g_old_pos[idx].pos.z;
                new_pos = g_new_pos[idx].pos.z;
                diff = g_diff[idx].pos.z;
            }
            LOG("auto_cal: report-%d, old = %d, new = %d, diff = %d.\n", pos_idx, old_pos, new_pos, diff);
            if (mode == SERVICE_MODE) {
                report_position_calibration(ENG_DEBUG_NEEDLE, pos_idx, old_pos, new_pos + diff);
            }
        }
    }
    if (mode == ENG_MODE) {
        if (idx == POS_R2_INSIDE || idx == POS_R2_OUTSIDE) {
            engineer_needle_pos_report(eng_module_idx, eng_pos_idx,
                g_new_pos[idx].pos.x + g_diff[idx].pos.x,
                g_new_pos[idx].pos.y + g_diff[idx].pos.y,
                g_new_pos[idx].pos.z + g_diff[idx].pos.z,
                g_new_pos[idx].reag_table + g_diff[idx].reag_table);
        } else {
            engineer_needle_pos_report(eng_module_idx, eng_pos_idx,
                g_new_pos[idx].pos.x + g_diff[idx].pos.x,
                g_new_pos[idx].pos.y + g_diff[idx].pos.y,
                g_new_pos[idx].pos.z + g_diff[idx].pos.z, 0);
            if (idx == POS_SAMPLE_TEMP) {
                /* 上报计算得出的洗针位和稀释液位1&2 */
                engineer_needle_pos_report(eng_module_idx, eng_pos_idx - 1,
                    g_new_pos[idx].pos.x + g_diff[idx].pos.x,
                    g_new_pos[idx].pos.y + g_diff[idx].pos.y,
                    g_new_pos[idx].pos.z + g_diff[idx].pos.z, 0);
                engineer_needle_pos_report(eng_module_idx, eng_pos_idx + 1,
                    g_new_pos[idx].pos.x + S_DILU_1_X_DIFF,
                    g_new_pos[idx].pos.y + S_DILU_1_Y_DIFF,
                    g_new_pos[idx].pos.z + S_DILU_1_Z_DIFF, 0);
                engineer_needle_pos_report(eng_module_idx, eng_pos_idx + 2,
                    g_new_pos[idx].pos.x + S_DILU_2_X_DIFF,
                    g_new_pos[idx].pos.y + S_DILU_2_Y_DIFF,
                    g_new_pos[idx].pos.z + S_DILU_2_Z_DIFF, 0);
            }
        }
    }

    return 0;
}

static void auto_cal_s_task(void *arg)
{
    int idx = 0, ret = 0;
    auto_cal_task_t *para = (auto_cal_task_t *)arg;

    for (idx = POS_REAG_INSIDE; idx <= POS_SAMPLE_TEMP; idx++) {
        if (auto_cal_stop_flag_get() || auto_cal_err_flag) {
            LOG("auto_cal: %s stop|err flag = 1, stop.\n", auto_cal_idx_string(idx));
            para->ret = AT_EMSTOP;
            break;
        }

        if (idx == POS_REAG_INSIDE) {
            pthread_mutex_lock(&auto_cal_mtx);
        }
        ret = auto_cal_start(idx, &g_old_pos[idx], &g_new_pos[idx]);
        if (idx == POS_REAG_OUTSIDE) {
            pthread_mutex_unlock(&auto_cal_mtx);
        }
        if (ret == 0) {
            auto_cal_pos_report(idx, SERVICE_MODE);
            LOG("auto_cal: %s cal successed, switch next.\n", auto_cal_idx_string(idx));
            continue;
        } else {
            para->ret = ret;
            auto_cal_err_flag = 1;
            if (idx == POS_REAG_INSIDE) {
                pthread_mutex_unlock(&auto_cal_mtx);
            }
            LOG("auto_cal: %s cal failed.\n", auto_cal_idx_string(idx));
            break;
        }
    }
    sem_post(&para->auto_cal_sem);
}

static void auto_cal_r2_task(void *arg)
{
    int idx = 0, ret = 0;
    auto_cal_task_t *para = (auto_cal_task_t *)arg;

    for (idx = POS_R2_MIX; idx < POS_AUTO_CAL_MAX; idx++) {
        if (auto_cal_stop_flag_get() || auto_cal_err_flag) {
            LOG("auto_cal: %s stop|err flag = 1, stop.\n", auto_cal_idx_string(idx));
            para->ret = AT_EMSTOP;
            break;
        }

        if (idx == POS_R2_INSIDE) {
            pthread_mutex_lock(&auto_cal_mtx);
        }
        ret = auto_cal_start(idx, &g_old_pos[idx], &g_new_pos[idx]);
        if (idx == POS_R2_OUTSIDE) {
            pthread_mutex_unlock(&auto_cal_mtx);
        }
        if (ret == 0) {
            auto_cal_pos_report(idx, SERVICE_MODE);
            LOG("auto_cal: %s cal successed, switch next.\n", auto_cal_idx_string(idx));
            continue;
        } else {
            para->ret = ret;
            auto_cal_err_flag = 1;
            if (idx == POS_R2_INSIDE) {
                pthread_mutex_unlock(&auto_cal_mtx);
            }
            LOG("auto_cal: %s cal failed.\n", auto_cal_idx_string(idx));
            break;
        }
    }
    sem_post(&para->auto_cal_sem);
}

/* service调用自动标定 */
int thrift_auto_cal_func(void)
{
    int ret = 0;
    auto_cal_idx_t idx = 0;
    long start = sys_uptime_sec();
    auto_cal_task_t cal_task[2] = {0};
    /* R2标定试剂仓时，试剂仓值会被改动，因此记录原始数据 */
    int table_in = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN];
    int table_out = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT];

    ret = auto_cal_motor_reset();
    if (ret < 0) {
        return ret;
    }
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);
    all_magnetic_pwm_ctl(TEMP_CTL_OFF);
    all_temperate_ctl(TEMP_CTL_OFF, TEMP_CTL_OFF);

    auto_cal_err_flag = 0;
    memset(g_old_pos, 0, sizeof(g_old_pos));
    memset(g_new_pos, 0, sizeof(g_new_pos));
    g_old_pos[POS_R2_INSIDE].reag_table = table_in;
    g_old_pos[POS_R2_OUTSIDE].reag_table = table_out;

    memset(cal_task, 0, sizeof(cal_task));
    pthread_mutex_init(&auto_cal_mtx, NULL);
    sem_init(&cal_task[0].auto_cal_sem, 0, 0);
    sem_init(&cal_task[1].auto_cal_sem, 0, 0);
    work_queue_add(auto_cal_s_task, (void *)&cal_task[0]);
    work_queue_add(auto_cal_r2_task, (void *)&cal_task[1]);

    sem_wait(&cal_task[0].auto_cal_sem);
    sem_wait(&cal_task[1].auto_cal_sem);
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    all_magnetic_pwm_ctl(TEMP_CTL_NORMAL_ON);
    all_temperate_ctl(TEMP_CTL_NORMAL_ON, TEMP_CTL_NORMAL_ON);
    LOG("auto_cal: s_ret = %d, r2_ret = %d.\n", cal_task[0].ret, cal_task[1].ret);
    if (cal_task[0].ret == 0 && cal_task[1].ret == 0) {
        ret = auto_cal_motor_reset();
        if (ret < 0) {
            LOG("auto_cal: motor reset failed.\n");
            return ret;
        } else {
            LOG("auto_cal: cost %ds.\n",  sys_uptime_sec() - start);
            for (idx = POS_SAMPLE_1; idx < POS_AUTO_CAL_MAX; idx++) {
                LOG("%s: x = %d, y = %d, z = %d, reag_table = %d(%d - %d - %d - %d).\n", auto_cal_idx_string(idx),
                    g_new_pos[idx].pos.x, g_new_pos[idx].pos.y, g_new_pos[idx].pos.z, g_new_pos[idx].reag_table,
                    g_old_pos[idx].pos.x, g_old_pos[idx].pos.y, g_old_pos[idx].pos.z, g_old_pos[idx].reag_table);
            }
            return 0;
        }
    }

    return AT_ENOTMATCH;
}

int thrift_auto_cal_single_func(auto_cal_idx_t target_idx)
{
    auto_cal_rcd_t old_pos = {0}, new_pos = {0};
    int ret = 0;

    ret = auto_cal_motor_reset();
    if (ret < 0) {
        LOG("auto_cal: motor reset failed.\n");
        return ret;
    }

    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);
    all_magnetic_pwm_ctl(TEMP_CTL_OFF);
    all_temperate_ctl(TEMP_CTL_OFF, TEMP_CTL_OFF);

    LOG("auto_cal: %s start...\n", auto_cal_idx_string(target_idx));
    ret = auto_cal_start(target_idx, &old_pos, &new_pos);

    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    all_magnetic_pwm_ctl(TEMP_CTL_NORMAL_ON);
    all_temperate_ctl(TEMP_CTL_NORMAL_ON, TEMP_CTL_NORMAL_ON);

    return ret;
}

/* 上位机工程师模式调用自动标定 */
int eng_auto_cal_func(int32_t id)
{
    int ret = 0, module_idx = 0;
    auto_cal_idx_t idx = 0, start_idx = 0, end_idx = 0;
    long start = sys_uptime_sec();
    /* R2标定试剂仓时，试剂仓值会被改动，因此记录原始数据 */
    int table_in = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN];
    int table_out = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT];

    ret = auto_cal_motor_reset();
    if (ret < 0) {
        LOG("auto_cal: motor reset failed.\n");
        return ret;
    }
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);
    all_magnetic_pwm_ctl(TEMP_CTL_OFF);
    all_temperate_ctl(TEMP_CTL_OFF, TEMP_CTL_OFF);

    memset(g_old_pos, 0, sizeof(g_old_pos));
    memset(g_new_pos, 0, sizeof(g_new_pos));
    if (id == ENG_DEBUG_NEEDLE_S) {
        start_idx = POS_REAG_INSIDE;
        end_idx = POS_R2_MIX;
    } else {
        g_old_pos[POS_R2_INSIDE].reag_table = table_in;
        g_old_pos[POS_R2_OUTSIDE].reag_table = table_out;
        start_idx = POS_R2_MIX;
        end_idx = POS_AUTO_CAL_MAX;
    }
    for (idx = start_idx; idx < end_idx; idx++) {
        if (auto_cal_stop_flag_get()) {
            LOG("auto_cal: %s stop flag = 1, stop.\n", auto_cal_idx_string(idx));
            return AT_EMSTOP;
        }
        LOG("auto_cal: %s start...\n", auto_cal_idx_string(idx));
        ret = auto_cal_start(idx, &g_old_pos[idx], &g_new_pos[idx]);
        if (ret == 0) {
            auto_cal_pos_report(idx, ENG_MODE);
            LOG("auto_cal: %s cal successed, switch next.\n", auto_cal_idx_string(idx));
            continue;
        } else {
            /* 标定失败时，需要将错误的位置号转换成手动标定的位置号上报上位机 */
            auto_cal_pos_to_eng_pos(idx, &module_idx, &ret);
            if (auto_cal_motor_reset() < 0) {
                LOG("auto_cal: motor reset failed.\n");
                break;
            }
            LOG("auto_cal: %s cal failed.\n", auto_cal_idx_string(idx));
            break;
        }
    }
    LOG("auto_cal: cost %ds.\n", sys_uptime_sec() - start);
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    all_magnetic_pwm_ctl(TEMP_CTL_NORMAL_ON);
    all_temperate_ctl(TEMP_CTL_NORMAL_ON, TEMP_CTL_NORMAL_ON);

    return ret;
}

int auto_cal_func_test(void)
{
    #if 1
    int ret = 0;

    while (1) {
        ret = thrift_auto_cal_func();
        if (ret != 0) {
            //break;
            LOG("auto_cal: cal failed.\n");
            if (auto_cal_motor_reset() < 0) {
                LOG("auto_cal: motor reset failed.\n");
                return -1;
            }
            auto_cal_err_flag = 0;
            sleep(10);
        }
        sleep(5);
    }
    #else
    while (1) {
        //thrift_auto_cal_single_func(7);
        //sleep(1);
        thrift_auto_cal_single_func(13);
        sleep(5);
    }
    #endif

    return 0;
}

