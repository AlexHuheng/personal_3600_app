#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ev.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

#include "log.h"
#include "h3600_cup_param.h"
#include "h3600_needle.h"
#include "module_common.h"
#include "module_monitor.h"

#include "common.h"
#include "movement_config.h"
#include "thrift_service_software_interface.h"

#include "slip/slip_msg.h"
#include "slip/slip_node.h"
#include "slip_cmd_table.h"
#include "module_liquied_circuit.h"
#include "device_status_count.h"
#include "module_temperate_ctl.h"

#define DEFAULT_K_VALUE     64
#define DEFAULT_R2_K_VALUE  128
#define DEFAULT_B_VALUE     200
#define DEFAULT_R3_B_VALUE  (-50)

/* r0: 5 10 15 25 50 100 150 200 */
static thrift_liquid_kbs_t needle_s_r0_kbs[8] = {0};
/* r2: 20 50 100 150 200 */
static thrift_liquid_kbs_t needle_r2_kbs[5] = {0};
/* r3: 5 10 15 25 50 */
static thrift_liquid_kbs_t needle_s_r3_kbs[5] = {0};
/* r4: 15 20 50 100 150 200 */
static thrift_liquid_kbs_t needle_s_r4_kbs[6] = {0};

static liquid_pump_ctl_t pump_para_for_q1 = {DIAPHRAGM_PUMP_Q1, 0, 1}; /* 用于隔膜泵Q1为样本针外壁清洗时使用 */
static r2_clean_flag_t r2_clean_flag = R2_CLEAN_NONE;

void usleep_frag(int32_t utime)
{
    int i = utime/1000000, j = utime%1000000, k = 0;

    for (k=0; k<i; k++) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        usleep(1000*1000);
        FAULT_CHECK_END();
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    usleep(j);
    FAULT_CHECK_END();
}

static void show_calc_kb_value(thrift_liquid_amount_para_t *param)
{
    LOG("k = %lf, b = %lf\n", param->k_ratio, param->b_ratio);
}

static void calc_kb_value(int default_k_value, int default_b_value,
                            double low_default_ul, double low_true_ul,
                            double high_default_ul, double high_true_ul, thrift_liquid_amount_para_t *liq_param)
{
    int low_true_steps = 0, high_true_steps = 0;

    low_true_steps = default_k_value * low_default_ul + default_b_value;
    high_true_steps = default_k_value * high_default_ul + default_b_value;

    liq_param->k_ratio = (high_true_steps - low_true_steps) / (high_true_ul - low_true_ul);
    liq_param->b_ratio = (high_true_steps) - (liq_param->k_ratio * high_true_ul);
}

/* 根据真实加样量计算区间吸吐样KB值 */
void kb_ul_init(void)
{
    int i = 0;
    /* R0称量值与默认值 */
    needle_s_r0_kbs[0].liq_default_value = 5;
    needle_s_r0_kbs[0].liq_true_value = h3600_conf_get()->r0_liq_true_value[0];
    needle_s_r0_kbs[1].liq_default_value = 10;
    needle_s_r0_kbs[1].liq_true_value = h3600_conf_get()->r0_liq_true_value[1];
    needle_s_r0_kbs[2].liq_default_value = 15;
    needle_s_r0_kbs[2].liq_true_value = h3600_conf_get()->r0_liq_true_value[2];
    needle_s_r0_kbs[3].liq_default_value = 25;
    needle_s_r0_kbs[3].liq_true_value = h3600_conf_get()->r0_liq_true_value[3];
    needle_s_r0_kbs[4].liq_default_value = 50;
    needle_s_r0_kbs[4].liq_true_value = h3600_conf_get()->r0_liq_true_value[4];
    needle_s_r0_kbs[5].liq_default_value = 100;
    needle_s_r0_kbs[5].liq_true_value = h3600_conf_get()->r0_liq_true_value[5];
    needle_s_r0_kbs[6].liq_default_value = 150;
    needle_s_r0_kbs[6].liq_true_value = h3600_conf_get()->r0_liq_true_value[6];
    needle_s_r0_kbs[7].liq_default_value = 200;
    needle_s_r0_kbs[7].liq_true_value = h3600_conf_get()->r0_liq_true_value[7];
    LOG("===============r0 kb value============\n");
    for (i=0; i<7; i++) {
        LOG("   %lf < true_value <= %lf\n", needle_s_r0_kbs[i].liq_true_value, needle_s_r0_kbs[i+1].liq_true_value);
        calc_kb_value(DEFAULT_K_VALUE, DEFAULT_B_VALUE,
                    needle_s_r0_kbs[i].liq_default_value, needle_s_r0_kbs[i].liq_true_value,
                    needle_s_r0_kbs[i+1].liq_default_value, needle_s_r0_kbs[i+1].liq_true_value,
                    &needle_s_r0_kbs[i].liq_kb_param);
        show_calc_kb_value(&needle_s_r0_kbs[i].liq_kb_param);
    }

    /* R2称量值与默认值 */
    needle_r2_kbs[0].liq_default_value = 20;
    needle_r2_kbs[0].liq_true_value = h3600_conf_get()->r2_liq_true_value[0];
    needle_r2_kbs[1].liq_default_value = 50;
    needle_r2_kbs[1].liq_true_value = h3600_conf_get()->r2_liq_true_value[1];
    needle_r2_kbs[2].liq_default_value = 100;
    needle_r2_kbs[2].liq_true_value = h3600_conf_get()->r2_liq_true_value[2];
    needle_r2_kbs[3].liq_default_value = 150;
    needle_r2_kbs[3].liq_true_value = h3600_conf_get()->r2_liq_true_value[3];
    needle_r2_kbs[4].liq_default_value = 200;
    needle_r2_kbs[4].liq_true_value = h3600_conf_get()->r2_liq_true_value[4];
    LOG("===============r2 kb value============\n");
    for (i=0; i<4; i++) {
        LOG("   %lf < true_value <= %lf\n", needle_r2_kbs[i].liq_true_value, needle_r2_kbs[i+1].liq_true_value);
        calc_kb_value(DEFAULT_R2_K_VALUE, DEFAULT_B_VALUE,
                    needle_r2_kbs[i].liq_default_value, needle_r2_kbs[i].liq_true_value,
                    needle_r2_kbs[i+1].liq_default_value, needle_r2_kbs[i+1].liq_true_value,
                    &needle_r2_kbs[i].liq_kb_param);
        show_calc_kb_value(&needle_r2_kbs[i].liq_kb_param);
    }

    /* R3称量值与默认值 */
    needle_s_r3_kbs[0].liq_default_value = 5;
    needle_s_r3_kbs[0].liq_true_value = h3600_conf_get()->r3_liq_true_value[0];
    needle_s_r3_kbs[1].liq_default_value = 10;
    needle_s_r3_kbs[1].liq_true_value = h3600_conf_get()->r3_liq_true_value[1];
    needle_s_r3_kbs[2].liq_default_value = 15;
    needle_s_r3_kbs[2].liq_true_value = h3600_conf_get()->r3_liq_true_value[2];
    needle_s_r3_kbs[3].liq_default_value = 25;
    needle_s_r3_kbs[3].liq_true_value = h3600_conf_get()->r3_liq_true_value[3];
    needle_s_r3_kbs[4].liq_default_value = 50;
    needle_s_r3_kbs[4].liq_true_value = h3600_conf_get()->r3_liq_true_value[4];
    LOG("===============r3 kb value============\n");
    for (i=0; i<4; i++) {
        LOG("   %lf < true_value <= %lf\n", needle_s_r3_kbs[i].liq_true_value, needle_s_r3_kbs[i+1].liq_true_value);
        calc_kb_value(DEFAULT_K_VALUE, DEFAULT_R3_B_VALUE,
                    needle_s_r3_kbs[i].liq_default_value, needle_s_r3_kbs[i].liq_true_value,
                    needle_s_r3_kbs[i+1].liq_default_value, needle_s_r3_kbs[i+1].liq_true_value,
                    &needle_s_r3_kbs[i].liq_kb_param);
        show_calc_kb_value(&needle_s_r3_kbs[i].liq_kb_param);
    }

    /* R4称量值与默认值 */
    needle_s_r4_kbs[0].liq_default_value = 15;
    needle_s_r4_kbs[0].liq_true_value = h3600_conf_get()->r4_liq_true_value[0];
    needle_s_r4_kbs[1].liq_default_value = 20;
    needle_s_r4_kbs[1].liq_true_value = h3600_conf_get()->r4_liq_true_value[1];
    needle_s_r4_kbs[2].liq_default_value = 50;
    needle_s_r4_kbs[2].liq_true_value = h3600_conf_get()->r4_liq_true_value[2];
    needle_s_r4_kbs[3].liq_default_value = 100;
    needle_s_r4_kbs[3].liq_true_value = h3600_conf_get()->r4_liq_true_value[3];
    needle_s_r4_kbs[4].liq_default_value = 150;
    needle_s_r4_kbs[4].liq_true_value = h3600_conf_get()->r4_liq_true_value[4];
    needle_s_r4_kbs[5].liq_default_value = 200;
    needle_s_r4_kbs[5].liq_true_value = h3600_conf_get()->r4_liq_true_value[5];
    LOG("===============r4 kb value============\n");
    for (i=0; i<5; i++) {
        LOG("   %lf < true_value <= %lf\n", needle_s_r4_kbs[i].liq_true_value, needle_s_r4_kbs[i+1].liq_true_value);
        calc_kb_value(DEFAULT_K_VALUE, DEFAULT_B_VALUE,
                    needle_s_r4_kbs[i].liq_default_value, needle_s_r4_kbs[i].liq_true_value,
                    needle_s_r4_kbs[i+1].liq_default_value, needle_s_r4_kbs[i+1].liq_true_value,
                    &needle_s_r4_kbs[i].liq_kb_param);
        show_calc_kb_value(&needle_s_r4_kbs[i].liq_kb_param);
    }
}

/* 加液量转换函数 */
#if 0
int ul_to_step(needle_type_t needle_x, double amount_ul)
{
    int step = 0;

    step = (int)(h3600_conf_get()->liquid_amount[needle_x].k_ratio*amount_ul + h3600_conf_get()->liquid_amount[needle_x].b_ratio);
    LOG("amount_ul = %lf, step=%d\n", amount_ul, step);
    return step;

}
#else
int ul_to_step(needle_type_t needle_x, double amount_ul)
{
    int step = 0, temp = 0;

    temp = thrift_temp_get(THRIFT_ENVIRONMENTAREA);

    switch (needle_x) {
    case NEEDLE_TYPE_S:
    case NEEDLE_TYPE_S_R1:
        if (temp <= 200) {  /* 小于20°C温度的补偿 */
            if (amount_ul <= 5.0001) {
                amount_ul = amount_ul / 1.130739 * 1.0135;
            } else if (amount_ul > 5.0001 && amount_ul <= 10.0001) {
                amount_ul = amount_ul / 1.063508 * 1.0135;
            } else if (amount_ul > 10.0001 && amount_ul <= 15.0001) {
                amount_ul = amount_ul / 1.054 *1.0174;
            } else if (amount_ul > 15.0001 && amount_ul <= 25.0001) {
                amount_ul = amount_ul / 1.036 * 1.0163;
            } else if (amount_ul > 25.0001 && amount_ul <= 50.0001) {
                amount_ul = amount_ul / 1.027135 * 1.0108;
            } else if (amount_ul > 50.0001 && amount_ul <= 100.0001) {
                amount_ul = amount_ul / 1.02 * 1.0123;
            } else if (amount_ul > 100.0001 && amount_ul <= 150.0001) {
                amount_ul = amount_ul / 1.01208 * 1.0060;
            } else {
                amount_ul = amount_ul / 1.006 * 1.0060;
            }
        }
        /* r0: 5 10 15 25 50 100 150 200 */
        if (amount_ul <= needle_s_r0_kbs[1].liq_true_value) {
            step = (needle_s_r0_kbs[0].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[0].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r0_kbs[1].liq_true_value && amount_ul <= needle_s_r0_kbs[2].liq_true_value) {
            step = (needle_s_r0_kbs[1].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[1].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r0_kbs[2].liq_true_value && amount_ul <= needle_s_r0_kbs[3].liq_true_value) {
            step = (needle_s_r0_kbs[2].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[2].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r0_kbs[3].liq_true_value && amount_ul <= needle_s_r0_kbs[4].liq_true_value) {
            step = (needle_s_r0_kbs[3].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[3].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r0_kbs[4].liq_true_value && amount_ul <= needle_s_r0_kbs[5].liq_true_value) {
            step = (needle_s_r0_kbs[4].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[4].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r0_kbs[5].liq_true_value && amount_ul <= needle_s_r0_kbs[6].liq_true_value) {
            step = (needle_s_r0_kbs[5].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[5].liq_kb_param.b_ratio;
        } else {
            step = (needle_s_r0_kbs[6].liq_kb_param.k_ratio * amount_ul) + needle_s_r0_kbs[6].liq_kb_param.b_ratio;
        }
        break;
    case NEEDLE_TYPE_R2:
        /* r2: 20 50 100 150 200 */
        if (amount_ul <= needle_r2_kbs[1].liq_true_value) {
            step = (needle_r2_kbs[0].liq_kb_param.k_ratio * amount_ul) + needle_r2_kbs[0].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_r2_kbs[1].liq_true_value && amount_ul <= needle_r2_kbs[2].liq_true_value) {
            step = (needle_r2_kbs[1].liq_kb_param.k_ratio * amount_ul) + needle_r2_kbs[1].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_r2_kbs[2].liq_true_value && amount_ul <= needle_r2_kbs[3].liq_true_value) {
            step = (needle_r2_kbs[2].liq_kb_param.k_ratio * amount_ul) + needle_r2_kbs[2].liq_kb_param.b_ratio;
        } else {
            step = (needle_r2_kbs[3].liq_kb_param.k_ratio * amount_ul) + needle_r2_kbs[3].liq_kb_param.b_ratio;
        }
        break;
    case NEEDLE_TYPE_S_DILU:
        if (temp <= 200) {  /* 小于20°C温度的补偿 */
            if (amount_ul <= 5.0001) {
                amount_ul = amount_ul * 0.9615;
            } else if (amount_ul > 5.0001 && amount_ul <= 10.0001) {
                amount_ul = amount_ul * 0.9615;
            } else if (amount_ul > 10.0001 && amount_ul <= 15.0001) {
                amount_ul = amount_ul * 0.9740;
            } else if (amount_ul > 15.0001 && amount_ul <= 25.0001) {
                amount_ul = amount_ul * 0.9215;
            } else if (amount_ul > 25.0001 && amount_ul <= 50.0001) {
                amount_ul = amount_ul * 0.9921;
            } else {
                amount_ul = amount_ul * 1;
            }
        }
        /* r3: 5 10 15 25 50 */
        if (amount_ul <= needle_s_r3_kbs[1].liq_true_value) {
            step = (needle_s_r3_kbs[0].liq_kb_param.k_ratio * amount_ul) + needle_s_r3_kbs[0].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r3_kbs[1].liq_true_value && amount_ul <= needle_s_r3_kbs[2].liq_true_value) {
            step = (needle_s_r3_kbs[1].liq_kb_param.k_ratio * amount_ul) + needle_s_r3_kbs[1].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r3_kbs[2].liq_true_value && amount_ul <= needle_s_r3_kbs[3].liq_true_value) {
            step = (needle_s_r3_kbs[2].liq_kb_param.k_ratio * amount_ul) + needle_s_r3_kbs[2].liq_kb_param.b_ratio;
        } else {
            step = (needle_s_r3_kbs[3].liq_kb_param.k_ratio * amount_ul) + needle_s_r3_kbs[3].liq_kb_param.b_ratio;
        }
        break;
    case NEEDLE_TYPE_S_BOTH:
        if (temp <= 200) {  /* 小于20°C温度的补偿 */
            if (amount_ul <= 15.0001) {
                amount_ul = amount_ul / 1.061828 * 1.0241;
            } else if (amount_ul > 15.0001 && amount_ul <= 20.0001) {
                amount_ul = amount_ul / 1.035 * 1.0128;
            } else if (amount_ul > 20.0001 && amount_ul <= 50.0001) {
                amount_ul = amount_ul / 1.013543;
            } else if (amount_ul > 50.0001 && amount_ul <= 100.0001) {
                amount_ul = amount_ul / 1.012 * 1.0035;
            } else if (amount_ul > 100.0001 && amount_ul <= 150.0001) {
                amount_ul = amount_ul / 1.011845 * 1.0076;
            } else {
                amount_ul = amount_ul / 1.01 * 1.0056;
            }
        }
        /* r4: 15 20 50 100 150 200 */
        if (amount_ul <= needle_s_r4_kbs[1].liq_true_value) {
            step = (needle_s_r4_kbs[0].liq_kb_param.k_ratio * amount_ul) + needle_s_r4_kbs[0].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r4_kbs[1].liq_true_value && amount_ul <= needle_s_r4_kbs[2].liq_true_value) {
            step = (needle_s_r4_kbs[1].liq_kb_param.k_ratio * amount_ul) + needle_s_r4_kbs[1].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r4_kbs[2].liq_true_value && amount_ul <= needle_s_r4_kbs[3].liq_true_value) {
            step = (needle_s_r4_kbs[2].liq_kb_param.k_ratio * amount_ul) + needle_s_r4_kbs[2].liq_kb_param.b_ratio;
        } else if (amount_ul > needle_s_r4_kbs[3].liq_true_value && amount_ul <= needle_s_r4_kbs[4].liq_true_value) {
            step = (needle_s_r4_kbs[3].liq_kb_param.k_ratio * amount_ul) + needle_s_r4_kbs[3].liq_kb_param.b_ratio;
        } else {
            step = (needle_s_r4_kbs[4].liq_kb_param.k_ratio * amount_ul) + needle_s_r4_kbs[4].liq_kb_param.b_ratio;
        }
        break;
    default:
        break;
    }

    LOG("amount_ul = %lf, step = %d, needle = %d, temp = %d\n", amount_ul, step, needle_x, temp);
    return step;

}
#endif

/* 针S R1 R2 吸取 */
int needle_absorb_ul(needle_type_t needle_x, double amount_ul)
{
    int amount_step;
    int pump_speed, pump_acc;
    h3600_conf_t *h3600_conf;

    h3600_conf = h3600_conf_get();
    amount_step = ul_to_step(needle_x, amount_ul);

    switch (needle_x) {
    case NEEDLE_TYPE_S:
         liquid_clot_check_interface(amount_ul, 1);/* 在此准备凝块检测 */
    case NEEDLE_TYPE_S_DILU:
        if (amount_ul <= 100) {
            pump_speed = h3600_conf->pierce_support ? PUMP_SPEED_FAST : PUMP_SPEED_SLOW;
            pump_acc = h3600_conf->pierce_support ? PUMP_ACC_FAST : PUMP_ACC_SLOW;
        } else {
            if (amount_ul > 120 && needle_x == NEEDLE_TYPE_S) { /* 吸取大量样本至暂存池 */
                pump_speed = PUMP_SPEED_FASTEST;
                pump_acc = PUMP_ACC_FASTEST;
            } else {
                pump_speed = h3600_conf->pierce_support ? PUMP_SPEED_FAST : PUMP_SPEED_FAST;
                pump_acc = h3600_conf->pierce_support ? PUMP_ACC_FAST : PUMP_ACC_MIDDLE;
            }
        }
        break;
    case NEEDLE_TYPE_R2:
        if (1 == get_throughput_mode()) {
            pump_speed = 45000;
            pump_acc = 250000;
        } else {
            if (amount_ul <= 50) {
                pump_speed = PUMP_SPEED_SLOW;
                pump_acc = PUMP_ACC_SLOW;
            } else if (amount_ul > 50 && amount_ul < 100) {
                pump_speed = 45000;
                pump_acc = 45000;
            } else if (amount_ul >= 100) {
                pump_speed = 45000;
                pump_acc = 45000;
            }
        }
        break;
    default:
        break;
    }

    switch (needle_x) {
    case NEEDLE_TYPE_S:
    case NEEDLE_TYPE_S_DILU:
        motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, amount_step, pump_speed, pump_acc, MOTOR_DEFAULT_TIMEOUT);
        break;
    case NEEDLE_TYPE_R2:
        motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, amount_step, pump_speed, pump_acc, MOTOR_DEFAULT_TIMEOUT);
        break;
    default:
        break;
    }

    switch (needle_x) {
    case NEEDLE_TYPE_S:
         liquid_clot_check_interface(amount_ul, 0);//在次关闭凝块检测
    case NEEDLE_TYPE_S_DILU:
        usleep(100*1000);
        break;
    case NEEDLE_TYPE_R2:
        usleep(100*1000);
        break;
    default:
        break;
    }

    return 0;
}

/* 针S R1 R2 吐出 */
int needle_release_ul(needle_type_t needle_x, double amount_ul, int comps_step)
{
    int amount_step = 0;
    amount_step = ul_to_step(needle_x, amount_ul);    /* 加液量转换 */

    switch (needle_x) {
    case NEEDLE_TYPE_S:
    case NEEDLE_TYPE_S_DILU:
    case NEEDLE_TYPE_S_BOTH:
        if (comps_step != -1) {
            amount_step += comps_step;
        }
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -amount_step, PUMP_SPEED_NORMAL, PUMP_ACC_NORMAL);
        if (needle_x == NEEDLE_TYPE_S_BOTH && comps_step == -1) {
            motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, 0, -400, 5000, 5000, 0.5);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (needle_x == NEEDLE_TYPE_S_BOTH && comps_step == -1) {
            if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            }
        }
        device_status_count_add(DS_PUMP_S_USED_COUNT, 1);
        device_status_count_add(DS_S_PUSH_USED_COUNT, 1);
        break;
    case NEEDLE_TYPE_R2:
        amount_step += comps_step;
        motor_move_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, -amount_step, h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].speed, MOTOR_DEFAULT_TIMEOUT);
        device_status_count_add(DS_PUMP_R2_USED_COUNT, 1);
        device_status_count_add(DS_R2_USED_COUNT, 1);
        break;
    default:
        break;
    }

    if (get_throughput_mode() == 0) { /* !PT360 */
        usleep(200*1000);
    }

    return 0;

}
/* 针S R1 R2 吐出 */
int needle_release_ul_ctl(needle_type_t needle_x, double amount_ul, double cost_time, int comps_step)
{
    int amount_step = 0;
    motor_time_sync_attr_t release_attr = {0};
    int release_move_acc = 0;
    int speed = 0;
    int acc = 0;

    amount_step = ul_to_step(needle_x, amount_ul);    /* 加液量转换 */
    amount_step += comps_step;
    release_attr.v0_speed = 1000;
    release_attr.step = abs(amount_step);

    switch (needle_x) {
    case NEEDLE_TYPE_S:
    case NEEDLE_TYPE_S_DILU:
    case NEEDLE_TYPE_S_BOTH:
        release_attr.speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed;
        release_attr.vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed;
        release_attr.acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc;
        release_attr.max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc;
        release_move_acc = calc_motor_move_in_time(&release_attr, cost_time);
        if (amount_ul > 120 && needle_x == NEEDLE_TYPE_S) { /* 大量样本吐至暂存池 */
            speed = PUMP_SPEED_FASTEST;
            acc = PUMP_ACC_FASTEST;
        } else {
            /* 穿刺针去洗针池吐样本时，采用慢速，防止产生过多气泡 */
            speed = PUMP_SPEED_FAST;
            acc   = PUMP_ACC_FAST;
        }
        LOG("release total step = %d(t = %d, 5%_step + nb_step = %d).\n", amount_step, amount_step, comps_step);
        motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -amount_step, speed, acc, MOTOR_DEFAULT_TIMEOUT);
        device_status_count_add(DS_PUMP_S_USED_COUNT, 1);
        device_status_count_add(DS_S_PUSH_USED_COUNT, 1);
        break;
    case NEEDLE_TYPE_S_R1:
        release_attr.speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed;
        release_attr.vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed;
        release_attr.acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc;
        release_attr.max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc;
        release_move_acc = calc_motor_move_in_time(&release_attr, cost_time);
        motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -amount_step, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, release_move_acc, MOTOR_DEFAULT_TIMEOUT);
        device_status_count_add(DS_PUMP_S_USED_COUNT, 1);
        device_status_count_add(DS_S_PUSH_USED_COUNT, 1);
        break;
    case NEEDLE_TYPE_R2:
        release_attr.speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].speed;
        release_attr.vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].speed;
        release_attr.acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].acc;
        release_attr.max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].acc;
        release_move_acc = calc_motor_move_in_time(&release_attr, cost_time);
        motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, -amount_step, h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].speed, release_move_acc, MOTOR_DEFAULT_TIMEOUT);
        device_status_count_add(DS_PUMP_R2_USED_COUNT, 1);
        device_status_count_add(DS_R2_USED_COUNT, 1);
        break;
    default:
        break;
    }

    usleep(100*1000);

    return 0;

}

/* 样本针S内壁普通清洗流程 */
void s_normal_inside_clean(void)
{
    clean_liquid_type_t type = 0;

    LOG("liquid_circuit: sampler needle inside normal clear start\n");
    valve_set(DIAPHRAGM_PUMP_F1, ON);
    valve_set(DIAPHRAGM_PUMP_F4, ON);
    usleep(50*1000);
    valve_set(VALVE_SV1, ON);
    normal_bubble_status_check(&type);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_RST, 0, 20000, 50000) < 0) {
        LOG("liquid_circuit: S_pump move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
    }
    FAULT_CHECK_END();
    if (get_throughput_mode() == 1) { /* PT360 */
        usleep(400*1000);
    } else {
        usleep(800*1000);
    }
    valve_set(VALVE_SV1, OFF);
    valve_set(VALVE_SV2, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: S pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
    }
    FAULT_CHECK_END();
    usleep(400*1000);
    valve_set(VALVE_SV2, OFF);
    usleep(50*1000);
    valve_set(VALVE_SV7, ON);
    if (motor_move_ctl_async(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, 400, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC) < 0) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    usleep(50*1000);
    liquid_pump_pwm_open(&pump_para_for_q1);
    usleep(200*1000);
    if (motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: S pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    if (motor_move_ctl_async(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, -400, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC) < 0) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    liquid_pump_close(DIAPHRAGM_PUMP_Q1);
    valve_set(VALVE_SV7, OFF);
    usleep(200*1000);
    normal_bubble_check_end();
    if (motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: S pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    valve_set(DIAPHRAGM_PUMP_F1, OFF);
    valve_set(DIAPHRAGM_PUMP_F4, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    report_reagent_supply_consume(WASH_B, 0, WASH_S_NORMAL_PER_TIME);
    FAULT_CHECK_END();
    LOG("liquid_circuit: sampler needle inside normal clear end\n");

}

/* 样本针S外壁普通清洗流程  flag为1 表示开泵 0表示关闭 */
void s_normal_outside_clean(int flag)
{
    clean_liquid_type_t type = 0;

    if (flag) {
        LOG("liquid_circuit: sampler needle outside normal clear start\n");
        valve_set(DIAPHRAGM_PUMP_F1, ON);
        valve_set(DIAPHRAGM_PUMP_F4, ON);
        usleep(200*1000);
        valve_set(VALVE_SV7, ON);
        usleep(50*1000);
        normal_bubble_status_check(&type);
        liquid_pump_pwm_open(&pump_para_for_q1);
    } else {
        liquid_pump_close(DIAPHRAGM_PUMP_Q1);
        usleep(200*1000);
        normal_bubble_check_end();
        valve_set(VALVE_SV7, OFF);
        usleep(200*1000);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        report_reagent_supply_consume(WASH_B, 0, WASH_S_OUT_PER_TIME);
        FAULT_CHECK_END();
        valve_set(DIAPHRAGM_PUMP_F1, OFF);
        valve_set(DIAPHRAGM_PUMP_F4, OFF);
        LOG("liquid_circuit: sampler needle outside normal clear end\n");
    }

}

/* 试剂针普通清洗流程 */
void r2_normal_clean(void)
{
    LOG("liquid_circuit: R2 needle normal clear start\n");
    r2_clean_mutex_lock(1);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    usleep(200*1000);
    valve_set(VALVE_SV3, ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_async(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_RST, 0, 20000, 50000) < 0) {
        LOG("liquid_circuit: S_pump move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    if (get_throughput_mode() == 1) { /* PT360 */
        usleep(500*1000);
    } else {
        usleep(3200*1000);
    }
    valve_set(DIAPHRAGM_PUMP_Q2, OFF);
    usleep(200*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_timedwait(MOTOR_NEEDLE_R2_PUMP, MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: R2 pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    valve_set(VALVE_SV3, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    report_reagent_supply_consume(WASH_B, 0, WASH_R2_NORMAL_PER_TIME);
    FAULT_CHECK_END();
    r2_clean_mutex_lock(0);
    LOG("liquid_circuit: R2 needle normal clear end\n");
}

/* 试剂针特殊清洗流程 */
void r2_special_clean(void)
{
    thrift_motor_para_t liq_r2_z_motor_para = {0};

    LOG("liquid_circuit: R2 needle special clear start\n");
    pump_5ml_inuse_manage(1);
    r2_clean_mutex_lock(1);
    thrift_motor_para_get(MOTOR_NEEDLE_R2_Z, &liq_r2_z_motor_para);
    /* 洗针池普通清洗 */
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    valve_set(VALVE_SV12, ON);
    valve_set(VALVE_SV3, ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, ON);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_async(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_RST, 0, 20000, 50000) < 0) {
        LOG("liquid_circuit: R2 pump move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z,
                            CMD_MOTOR_MOVE_STEP,
                            NEEDLE_R2C_COMP_STEP,
                            liq_r2_z_motor_para.speed,
                            liq_r2_z_motor_para.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: R2 z move wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_timedwait(MOTOR_NEEDLE_R2_PUMP, MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: R2 pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    valve_set(DIAPHRAGM_PUMP_Q2, OFF);
    valve_set(VALVE_SV3, OFF);
    usleep(500*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    usleep(50*1000);
    valve_set(VALVE_SV12, OFF);
    usleep(150*1000);
    valve_set(VALVE_SV10, ON);
    usleep(200*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 100, 0);
    usleep(200*1000);
    valve_set(VALVE_SV10, OFF);
    usleep(100*1000);
    pump_absorb_sample(NEEDLE_TYPE_R2, 120, 1);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    report_reagent_supply_consume(WASH_A, 0, WASH_R2_SPEC_B_PER_TIME);
    FAULT_CHECK_END();
    usleep(200*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: R2_pump move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();
    valve_set(VALVE_SV12, ON);
    valve_set(DIAPHRAGM_PUMP_F3, ON);
    valve_set(VALVE_SV3, ON);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, ON);
    usleep(300*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, OFF);
    valve_set(VALVE_SV3, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z,
                            CMD_MOTOR_MOVE_STEP,
                            -NEEDLE_R2_NOR_CLEAN_STEP,
                            liq_r2_z_motor_para.speed,
                            liq_r2_z_motor_para.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: R2_Z motor wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
    }
    FAULT_CHECK_END();

    valve_set(VALVE_SV12, OFF);
    usleep(100*1000);
    valve_set(VALVE_SV3, ON);
    usleep(100*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, ON);
    usleep(300*1000);
    usleep(2400*1000);
    valve_set(DIAPHRAGM_PUMP_Q2, OFF);
    valve_set(VALVE_SV3, OFF);
    usleep(400*1000);
    valve_set(DIAPHRAGM_PUMP_F3, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    report_reagent_supply_consume(WASH_B, 0, WASH_R2_SPEC_A_PER_TIME);
    FAULT_CHECK_END();
    r2_clean_mutex_lock(0);
    pump_5ml_inuse_manage(0);
    LOG("liquid_circuit: R2 needle special clear end\n");

}

/* 样本针特殊清洗流程s需移动至暂存池特殊清洗液供给上方 */
void s_special_clean(double cost_time)
{
    motor_time_sync_attr_t motor_z = {0};
    pos_t rt_para = {0};
    clean_liquid_type_t type = 0;

    motor_z.v0_speed = 100;
    motor_z.vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed;
    motor_z.speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed;
    motor_z.max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc;
    motor_z.acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc;

    LOG("liquid_circuit: sampler needle special clear start\n");
    pump_5ml_inuse_manage(1);
    valve_set(DIAPHRAGM_PUMP_F1, ON);
    valve_set(DIAPHRAGM_PUMP_F4, ON);
    valve_set(VALVE_SV2, ON);
    normal_bubble_status_check(&type);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    usleep(800*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    usleep(200*1000);
    valve_set(VALVE_SV2, OFF);
    valve_set(DIAPHRAGM_PUMP_F1, OFF);
    valve_set(DIAPHRAGM_PUMP_F4, OFF);
    valve_set(VALVE_SV9, ON);
    usleep(50*1000);
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 140, 0);
    usleep(200*1000);
    valve_set(VALVE_SV9, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    get_special_pos(MOVE_S_TEMP, 0, &rt_para, FLAG_POS_UNLOCK);
    motor_z.step = abs(rt_para.z + NEEDLE_S_SPECIAL_COMP_STEP - NEEDLE_S_CLEAN_POS);
    motor_z.acc = calc_motor_move_in_time(&motor_z, cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z,
                            CMD_MOTOR_MOVE_STEP,
                            rt_para.z + NEEDLE_S_SPECIAL_COMP_STEP - NEEDLE_S_CLEAN_POS,
                            motor_z.speed, motor_z.acc,
                            MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: s_z reset wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();
    pump_absorb_sample(NEEDLE_TYPE_S, 200, 1);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: S_pump move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
    }
    FAULT_CHECK_END();
    pump_absorb_sample(NEEDLE_TYPE_S, 200, 1);
    usleep(100*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    report_reagent_supply_consume(WASH_A, 0, WASH_S_SPEC_B_PER_TIME);
    FAULT_CHECK_END();
    s_normal_outside_clean(ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z,
                             CMD_MOTOR_RST,
                             0,
                             motor_z.speed,
                             motor_z.acc,
                             MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: s_z reset wait timeout!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();
    usleep(200*1000);
    s_normal_outside_clean(OFF);
    valve_set(DIAPHRAGM_PUMP_F1, ON);
    valve_set(DIAPHRAGM_PUMP_F4, ON);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_RST, 0, 20000, 50000, MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("liquid_circuit: S_pump move faild\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
    }
    FAULT_CHECK_END();
//    valve_set(VALVE_SV9, ON);
////    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, 30, 1);
//    usleep(200*1000);
//    valve_set(VALVE_SV9, OFF);
    valve_set(VALVE_SV2, ON);
    usleep(50*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    usleep(2000*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    usleep(500*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    usleep(2000*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    usleep(500*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, ON);
    usleep(2000*1000);
    valve_set(DIAPHRAGM_PUMP_Q1, OFF);
    normal_bubble_check_end();
    valve_set(VALVE_SV2, OFF);
    usleep(200*1000);
    valve_set(DIAPHRAGM_PUMP_F1, OFF);
    valve_set(DIAPHRAGM_PUMP_F4, OFF);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    report_reagent_supply_consume(WASH_B, 0, WASH_S_SPEC_A_PER_TIME);
    FAULT_CHECK_END();
    pump_5ml_inuse_manage(0);
    LOG("liquid_circuit: sampler needle special clear end.\n");
}

void needle_s_calc_pos(uint32_t curr_ul, pos_t * pos)
{
    if (curr_ul <= 20) {
        pos->x = 0;
        pos->y = 230;
        pos->z = 6000;
    } else if ((curr_ul > 20) && (curr_ul <= 60)) {
        pos->x = 0;
        pos->y = 230;
        pos->z = 6000;
    } else if ((curr_ul > 60) && (curr_ul <= 100)) {
        pos->x = 0;
        pos->y = 230;
        pos->z = 5600;
    } else if ((curr_ul > 100) && (curr_ul <= 300)) {
        pos->x = 0;
        pos->y = 0;
        pos->z = 5300 - (int)(((double)curr_ul-100) * 10);
    } else {
        pos->x = 0;
        pos->y = 0;
        pos->z = 0;
    }
}

void needle_r2_calc_pos(uint32_t curr_ul, pos_t *pos)
{
   if (curr_ul <= 100) {
        pos->x = 0;
        pos->y = 0;
        pos->z = 1150;
    } else if ((curr_ul > 100) && (curr_ul <= 300)) {
        pos->x = 0;
        pos->y = 0;
        pos->z = 1150-(int)(((double)curr_ul-100)*2.75);
    } else {
        pos->x = 0;
        pos->y = 0;
        pos->z = 0;
    }
}

int needle_calc_add_pos(needle_type_t needle_x, uint32_t curr_ul, pos_t *pos)
{
    LOG("needle = %d, curr_ul = %d\n", needle_x, curr_ul);
    switch (needle_x) {
    case NEEDLE_TYPE_S:
    case NEEDLE_TYPE_S_R1:
        needle_s_calc_pos(curr_ul, pos);
        break;
    case NEEDLE_TYPE_R2:
        needle_r2_calc_pos(curr_ul, pos);
        break;
    default:
        return -1;
    }

    return 0;
}

int needle_s_calc_stemp_pos(uint32_t curr_ul, pos_t *pos)
{
    if (curr_ul < 100) {
        pos->x = 0;
        pos->y = 0;
        pos->z = NEEDLE_SPC_COMP_STEP - (int)(curr_ul * NEEDLE_SP_TEMP_RATIO);
    } else {
        pos->x = 0;
        pos->y = 0;
        pos->z = NEEDLE_SPC_COMP_STEP - (int)(curr_ul * NEEDLE_SP_TEMP_RATIO) + NEEDLS_SPC_ADD_STEP;
    }

    return 0;
}

r2_clean_flag_t get_r2_clean_flag()
{
    return r2_clean_flag;
}

void set_r2_clean_flag(r2_clean_flag_t flag)
{
    r2_clean_flag = flag;
}


