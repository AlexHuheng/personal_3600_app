/******************************************************
维护功能的实现文件
******************************************************/
#include <stdint.h>
#include <pthread.h>

#include "thrift_handler.h"
#include "h3600_maintain_utils.h"
#include "module_catcher_rs485.h"
#include "module_common.h"
#include "module_cuvette_supply.h"
#include "module_liquied_circuit.h"
#include "module_reagent_table.h"
#include "module_magnetic_bead.h"
#include "magnetic_algorithm.h"
#include "module_temperate_ctl.h"
#include "module_liquid_detect.h"
#include "module_engineer_debug_position.h"
#include "soft_power.h"

static sem_t sem_needle_s_avoid;
static maintain_needle_clean_flag_t needle_clean_flag = MAINTAIN_NORMAL_CLEAN;
static int shutdown_temp_regent_flag = 0; /* 表示 关闭主机时，是否保持试剂仓制冷。0：否 1：是 */

int get_shutdown_temp_regent_flag(void)
{
    return shutdown_temp_regent_flag;
}

static void motor_attr_init(motor_time_sync_attr_t *motor_x, motor_time_sync_attr_t *motor_z)
{
    motor_x->v0_speed = 100;
    motor_x->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].speed;
    motor_x->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].speed;
    motor_x->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].acc;
    motor_x->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].acc;

    motor_z->v0_speed = 100;
    motor_z->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed;
    motor_z->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed;
    motor_z->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc;
    motor_z->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc;
}

static void *needle_s_maintain_task(void *arg)
{
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};

    motor_attr_init(&motor_x, &motor_z);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    get_special_pos(MOVE_S_TEMP, 0, &pos_param.t1_src, FLAG_POS_NONE);

    motor_x.step = abs(pos_param.t1_src.x);
    motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_S_X);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, pos_param.t1_src.x,
                                pos_param.t1_src.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();
    sem_post(&sem_needle_s_avoid);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (needle_clean_flag == MAINTAIN_PIPE_LINE_PREFILL) {
        motor_z.step = abs(pos_param.t1_src.z + NEEDLE_S_SPECIAL_COMP_STEP);
        motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z + NEEDLE_S_SPECIAL_COMP_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            *(int *)arg = 1;
        }
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needle S.pump timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (needle_clean_flag == MAINTAIN_PIPE_LINE_PREFILL) {
        /* 灌注or洗针 */
        sleep(3);
    } else if (needle_clean_flag == MAINTAIN_NORMAL_CLEAN) {
        s_normal_inside_clean();
    }
    FAULT_CHECK_END();

    return NULL;
}

static void *needle_r2_maintain_task(void *arg)
{
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_y = {0}, motor_z = {0};

    motor_attr_init(&motor_y, &motor_z);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    get_special_pos(MOVE_R2_CLEAN, 0, &pos_param.t1_src, FLAG_POS_NONE);

    motor_y.step = abs(pos_param.t1_src.x);
    motor_y.acc = calc_motor_move_in_time(&motor_y, STARTUP_TIMES_S_X);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(pos_param.t1_src.z);
    motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needle r2.pump timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (needle_clean_flag == MAINTAIN_PIPE_LINE_PREFILL) {
        /* 灌注or洗针 */
        *(int *)arg = liquid_self_maintence_interface(0);
        sleep(3);
        s_normal_inside_clean();
        sleep(1);
        r2_normal_clean();
    } else if (needle_clean_flag == MAINTAIN_NORMAL_CLEAN) {
        r2_normal_clean();
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 5 * 128, 45000, 180000, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    motor_y.step = abs(pos_param.t1_src.x);
    motor_y.acc = calc_motor_move_in_time(&motor_y, STARTUP_TIMES_S_X);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    return NULL;
}

static int module_pos_init(void)
{
    int res = 0;
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};

    motor_attr_init(&motor_x, &motor_z);
    get_special_pos(MOVE_S_TEMP, 0, &pos_param.t1_src, FLAG_POS_NONE);
    get_special_pos(MOVE_S_CLEAN, 0, &pos_param.t1_dst, FLAG_POS_NONE);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(pos_param.t1_src.z);
    motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        res = 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_x.step = abs(pos_param.t1_src.y);
    motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_S_X);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, -pos_param.t1_src.x, -pos_param.t1_src.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        res = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_x.step = abs(pos_param.t1_dst.x);
    motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_S_X);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, pos_param.t1_dst.x,
                                pos_param.t1_dst.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        res = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    get_special_pos(MOVE_C_PRE, 0, &pos_param.t1_src, FLAG_POS_UNLOCK);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, pos_param.t1_src.x, pos_param.t1_src.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(MOTOR_CATCHER_X, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        res = 1;
    }
    FAULT_CHECK_END();

    return res;
}

static int module_pos_init_after_reset_all()
{
    int res = 0;
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};

    motor_attr_init(&motor_x, &motor_z);
    get_special_pos(MOVE_S_CLEAN, 0, &pos_param.t1_dst, FLAG_POS_NONE);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_x.step = abs(pos_param.t1_dst.x);
    motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_S_X);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, pos_param.t1_dst.x,
                                pos_param.t1_dst.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        res = 1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    get_special_pos(MOVE_C_PRE, 0, &pos_param.t1_src, FLAG_POS_UNLOCK);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, pos_param.t1_src.x, pos_param.t1_src.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(MOTOR_CATCHER_X, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        res = 1;
    }
    FAULT_CHECK_END();

    return res;
}


int catcher_drop_one_cup(module_param_t *pos_param)
{
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, res = 0;
    double move_time = 0;

    motor_attr_init(&motor_x, &motor_z);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (abs(pos_param->t1_src.x - pos_param->cur.x) > abs((pos_param->t1_src.y - pos_param->cur.y) - (pos_param->t1_src.x - pos_param->cur.x))) {
        motor_x.step = abs(pos_param->t1_src.x - pos_param->cur.x);
    } else {
        motor_x.step = abs((pos_param->t1_src.y - pos_param->cur.y) -(pos_param->t1_src.x - pos_param->cur.x));
    }
    if (motor_x.step > CATCHER_LONG_DISTANCE) {
        move_time = STARTUP_TIMES_C_X;
    } else {
        move_time = STARTUP_TIMES_C_Z;
    }
    calc_acc = calc_motor_move_in_time(&motor_x, move_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, pos_param->t1_src.x - pos_param->cur.x,
                                pos_param->t1_src.y - pos_param->cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, move_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
    }
    set_pos(&pos_param->cur, pos_param->t1_src.x, pos_param->t1_src.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(pos_param->t1_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, pos_param->t1_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
    }
    set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, pos_param->t1_src.z);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(pos_param->t1_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
    }
    set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (check_catcher_status()) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (abs(pos_param->t1_dst.x - pos_param->cur.x) > abs((pos_param->t1_dst.y - pos_param->cur.y) - (pos_param->t1_dst.x - pos_param->cur.x))) {
            motor_x.step = abs(pos_param->t1_dst.x - pos_param->cur.x);
        } else {
            motor_x.step = abs((pos_param->t1_dst.y - pos_param->cur.y) - (pos_param->t1_dst.x - pos_param->cur.x));
        }
        if (motor_x.step > CATCHER_LONG_DISTANCE) {
            move_time = STARTUP_TIMES_C_X;
        } else {
            move_time = STARTUP_TIMES_C_Z;
        }
        calc_acc = calc_motor_move_in_time(&motor_x, move_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, pos_param->t1_dst.x - pos_param->cur.x,
                                    pos_param->t1_dst.y - pos_param->cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, move_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        }
        set_pos(&pos_param->cur, pos_param->t1_dst.x, pos_param->t1_dst.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param->t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, pos_param->t1_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        }
        set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, pos_param->t1_dst.z);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (catcher_ctl(CATCHER_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
        }
        report_reagent_supply_consume(WASTE_CUP, 1, 1);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_async(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, -200, 25000, 160000)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        }
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                    -1400, 50000, 150000, MOTOR_DEFAULT_TIMEOUT, 0.01)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        }
        if (motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] z reset timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        }
        set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y-1400, pos_param->cur.z-200);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param->t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        }
        set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, 0);
        if (check_catcher_status()) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
        }
        FAULT_CHECK_END();
    }else {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (catcher_ctl(CATCHER_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
        }
        FAULT_CHECK_END();
    }
    FAULT_CHECK_END();
    return res;
}

static void *all_drop_cup_maintain_task(void *arg)
{
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int i = 0;

    motor_attr_init(&motor_x, &motor_z);
    set_pos(&pos_param.cur, 0, 0, 0);
    sem_wait(&sem_needle_s_avoid);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_src, FLAG_POS_UNLOCK);
    motor_x.step = abs(pos_param.t1_src.x - pos_param.cur.x);
    motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_C_X);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, pos_param.t1_src.x, pos_param.t1_src.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_C_X)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        *(int *)arg = 1;
    }
    set_pos(&pos_param.cur, pos_param.t1_src.x, pos_param.t1_src.y, pos_param.t1_src.z);
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    catcher_rs485_init();
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                -500, 50000, 150000, MOTOR_DEFAULT_TIMEOUT, 0.01)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
    }
    set_pos(&pos_param.cur, pos_param.cur.x, pos_param.cur.y-500, pos_param.cur.z);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        *(int *)arg = 1;
    }
    set_pos(&pos_param.cur, pos_param.cur.x, pos_param.cur.y, 0);
    FAULT_CHECK_END();

    /* 常规加样位 */
    get_special_pos(MOVE_C_PRE, 0, &pos_param.t1_src, FLAG_POS_UNLOCK);
    get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
    catcher_drop_one_cup(&pos_param);
    /* 常规混匀位 */
    for (i=0; i<2; i++) {
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        catcher_drop_one_cup(&pos_param);
    }
    /* 孵育位 */
    for (i=0; i<30; i++) {
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        catcher_drop_one_cup(&pos_param);
    }
    /* 光学检测位 */
    for (i=0; i<8; i++) {
        get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        catcher_drop_one_cup(&pos_param);
    }
    /* 光学混匀位 */
    get_special_pos(MOVE_C_OPTICAL_MIX, 0, &pos_param.t1_src, FLAG_POS_UNLOCK);
    get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
    catcher_drop_one_cup(&pos_param);
    /* 磁珠检测位 */
    for (i=0; i<4; i++) {
        LOG("magnetic\n");
        get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        catcher_drop_one_cup(&pos_param);
    }

    /* 复位 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_x.step = abs(pos_param.cur.x);
    motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_C_X);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, pos_param.cur.x, pos_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_C_X)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        *(int *)arg = 1;
    }
    FAULT_CHECK_END();

    return NULL;
}

int remain_detect_prepare(void)
{
    int res_needle_s = 0, res_needle_r2 = 0;
    pthread_t startup_needle_s_thread, startup_needle_r2_thread;
    reag_table_cotl_t reag_table_cotl = {0};
    sem_init(&sem_needle_s_avoid, 0, 0);

    if (0 != pthread_create(&startup_needle_s_thread, NULL, needle_s_maintain_task, &res_needle_s)) {
        LOG("startup needle_s thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_needle_r2_thread, NULL, needle_r2_maintain_task, &res_needle_r2)) {
        LOG("startup needle_r2 thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    reag_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
    reag_table_cotl.req_pos_type = NEEDLE_S;
    reag_table_cotl.table_dest_pos_idx = POS_REAGENT_TABLE_NONE;
    reag_table_cotl.move_time = 1;
    reagent_table_move_interface(&reag_table_cotl);

    pthread_join(startup_needle_s_thread, NULL);
    pthread_join(startup_needle_r2_thread, NULL);
    if (res_needle_s || res_needle_r2) {
        return 1;
    }
    /* 检查试剂仓状态 */
    if (TABLE_IDLE != reag_table_stage_check()) {
        LOG("reagent move failed!\n");
        return 1;
    }
    reag_table_occupy_flag_set(0);

    return 0;
}

int remain_detect_done(void)
{
    if (module_pos_init()) {
        return 1;
    }

    return 0;
}

int instrument_self_check(uint8_t cup_clear_flag, uint8_t liquid_detect_remain_flag)
{
    int res_catcher = 0, res_needle_s = 0, res_needle_r2 = 0;
    pthread_t startup_drop_cup_thread, startup_needle_s_thread, startup_needle_r2_thread;
    reag_table_cotl_t reag_table_cotl = {0};
    static int first_check = 0;
    sem_init(&sem_needle_s_avoid, 0, 0);

    if (0 != pthread_create(&startup_needle_s_thread, NULL, needle_s_maintain_task, &res_needle_s)) {
        LOG("startup needle_s thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_needle_r2_thread, NULL, needle_r2_maintain_task, &res_needle_r2)) {
        LOG("startup needle_r2 thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (cup_clear_flag == 1 || first_check == 0) {
        if (0 != pthread_create(&startup_drop_cup_thread, NULL, all_drop_cup_maintain_task, &res_catcher)) {
            LOG("startup drop_cup thread create failed!, %s\n", strerror(errno));
            return -1;
        }
    }
    if (REACTION_CUP_NONE == cuvette_supply_reset(POWER_ON)) {
        LOG("cuvette supply failed!\n");
    }

    reag_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
    reag_table_cotl.req_pos_type = NEEDLE_S;
    reag_table_cotl.table_dest_pos_idx = POS_REAGENT_TABLE_NONE;
    reag_table_cotl.move_time = 1;
    reagent_table_move_interface(&reag_table_cotl);

    pthread_join(startup_needle_s_thread, NULL);
    pthread_join(startup_needle_r2_thread, NULL);
    /* 特殊清洗液柱塞泵初始化状态 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    liquid_onpower_5ml_pump_manage();
    FAULT_CHECK_END();
    if (cup_clear_flag == 1 || first_check == 0) {
        first_check = 1;
        pthread_join(startup_drop_cup_thread, NULL);
    }
    if (res_catcher || res_needle_s || res_needle_r2) {
        return 1;
    }
    /* 检查试剂仓状态 */
    if (TABLE_IDLE != reag_table_stage_check()) {
        LOG("reagent move failed!\n");
        return 1;
    }
    reag_table_occupy_flag_set(0);

    if (liquid_detect_remain_flag == 1) {
        /* 余量探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (liquid_detect_remain_func() != 0) {
            LOG("reag_remain_detect: in instrument_self_check failed!\n");
            return 1;
        }
        FAULT_CHECK_END();
        //在余量探测后进行试剂混匀
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (reagent_mix_interface() < 0) {
            LOG("reagnet_table: do reagent mix faild.\n");
            return 1;
        }
        FAULT_CHECK_END();
    }

    if (module_pos_init()) {
        return 1;
    }

    /* 检查 光学子板信号情况（需在清杯之后）*/
    optical_ad_data_check();

    return 0;
}

static int power_on_check()
{
    int res_catcher = 0, res_needle_s = 0, res_needle_r2 = 0;
    pthread_t startup_drop_cup_thread, startup_needle_s_thread, startup_needle_r2_thread;
    reag_table_cotl_t reag_table_cotl = {0};
    sem_init(&sem_needle_s_avoid, 0, 0);

    if (0 != pthread_create(&startup_needle_s_thread, NULL, needle_s_maintain_task, &res_needle_s)) {
        LOG("startup needle_s thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_needle_r2_thread, NULL, needle_r2_maintain_task, &res_needle_r2)) {
        LOG("startup needle_r2 thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_drop_cup_thread, NULL, all_drop_cup_maintain_task, &res_catcher)) {
        LOG("startup drop_cup thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (REACTION_CUP_NONE == cuvette_supply_reset(POWER_ON)) {
        LOG("cuvette supply failed!\n");
    }

    reag_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
    reag_table_cotl.req_pos_type = NEEDLE_S;
    reag_table_cotl.table_dest_pos_idx = POS_REAGENT_TABLE_NONE;
    reag_table_cotl.move_time = 1;
    reagent_table_move_interface(&reag_table_cotl);

    pthread_join(startup_needle_s_thread, NULL);
    pthread_join(startup_needle_r2_thread, NULL);
    pthread_join(startup_drop_cup_thread, NULL);
    if (res_catcher || res_needle_s || res_needle_r2) {
        return 1;
    }
    /* 检查试剂仓状态 */
    if (TABLE_IDLE != reag_table_stage_check()) {
        LOG("reagent move failed!\n");
        return 1;
    }
    if (module_pos_init()) {
        return 1;
    }

    /* 检查 光学子板信号情况（需在清杯之后）*/
    optical_ad_data_check();

    return 0;
}

static int power_off_check()
{
    int res_needle_s = 0, res_needle_r2 = 0, res_catcher = 0;
    pthread_t startup_needle_s_thread, startup_needle_r2_thread, startup_drop_cup_thread;
    sem_init(&sem_needle_s_avoid, 0, 0);

    if (0 != pthread_create(&startup_needle_s_thread, NULL, needle_s_maintain_task, &res_needle_s)) {
        LOG("startup needle_s thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_needle_r2_thread, NULL, needle_r2_maintain_task, &res_needle_r2)) {
        LOG("startup needle_r2 thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_drop_cup_thread, NULL, all_drop_cup_maintain_task, &res_catcher)) {
        LOG("startup drop_cup thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    pthread_join(startup_needle_s_thread, NULL);
    pthread_join(startup_needle_r2_thread, NULL);
    pthread_join(startup_drop_cup_thread, NULL);

    if (res_catcher || res_needle_s || res_needle_r2) {
        return 1;
    }

    if (module_pos_init()) {
        return 1;
    }

    return 0;
}

static int pipefill_start()
{
    int res_needle_s = 0, res_needle_r2 = 0;
    pthread_t startup_needle_s_thread, startup_needle_r2_thread;
    sem_init(&sem_needle_s_avoid, 0, 0);

    LOG("\n");
    if (0 != pthread_create(&startup_needle_s_thread, NULL, needle_s_maintain_task, &res_needle_s)) {
        LOG("startup needle_s thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    if (0 != pthread_create(&startup_needle_r2_thread, NULL, needle_r2_maintain_task, &res_needle_r2)) {
        LOG("startup needle_r2 thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    pthread_join(startup_needle_s_thread, NULL);
    pthread_join(startup_needle_r2_thread, NULL);
    if (res_needle_s || res_needle_r2) {
        return 1;
    }
    if (module_pos_init()) {
        return 1;
    }

    return 0;
}


int emergency_stop(void)
{
    int idx = 0, i = 0;
    unsigned char ret[MAX_MOTOR_NUM] = {0};

    /* 停止 电机运动 */
    memset(ret, 0, sizeof(ret));
    motor_stop_all_timedwait(0, ret, 3000);
    for (idx = 0; idx < MAX_MOTOR_NUM; idx++) {
        if (ret[idx] != CONTROL_CMD_RESULT_SUCCESSS) {
            LOG("A9 motor(%d) ret:%d,stop failed!\n", idx+1, ret[idx]);
        }
    }

    /* 关闭 所有阀 emergency_stop只能使用gpio_set，不能使用受value_set_control控制的valve_set */
    /* 防止溅水， 需先关闭 打液阀， 再关闭 排废等其它阀 */

    /* 关闭 排废等其它阀 */
    for (i=DIAPHRAGM_PUMP_Q1; i<=DIAPHRAGM_PUMP_F4; i++) {
        gpio_set(i, OFF);
    }
    for (i=VALVE_SV1; i<=VALVE_SV12; i++) {
        gpio_set(i, OFF);
    }

    /* 停止   杯盘进杯     */

    /* 磁珠驱动力还原为 正常 */
    for (idx=0; idx<MAGNETIC_CH_NUMBER; idx++) {
        slip_magnetic_pwm_duty_set(idx, 0);
    }

    return 0;
}

/* 需等待维护过程中的残余动作完成，再应答上位机 */
static int wait_residue_finish()
{
    int count = 0;

    while (1) {
        /* 30s超时 */
        if (count++ > 30) {
            LOG("wait timeout\n");
            return -1;
        }

        if (machine_maintence_state_get() == 0) {
            LOG("wait ok\n");
            return 0;
        }
        sleep(1);
    }

    return 0;
}

void manual_stop_async_handler(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    static int emerg_stop_flag = 1;

    async_return.return_type = RETURN_INT;
    async_return.return_int = 0;

    FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);

    /* 停机之后，释放清架信号,但不执行电机动作 */
    if (get_machine_stat() == MACHINE_STAT_STOP) {
        
    } else {
        
    }

    /* 待机状态下，即使触发紧急停机按钮，也不设置停机标志 */
    if (get_machine_stat() != MACHINE_STAT_STANDBY) {
        set_machine_stat(MACHINE_STAT_STOP);
        module_start_control(MODULE_CMD_STOP);
        cuvette_supply_led_ctrl(REACTION_CUP_MAX, LED_NONE_BLINK);
        set_is_test_finished(1);
    }

    /* 关阀、停止电机运动 */
    if (emerg_stop_flag == 1) {
        emerg_stop_flag = 0;
        usleep(400*1000);
        emergency_stop();
        emerg_stop_flag = 1;
    }

    clear_module_base_time();
    /* 需等待维护过程中的残余动作完成，再应答上位机 */
    LOG("wait_residue_finish\n");
    wait_residue_finish();
    LOG("ensure_residue_finish\n");

    LOG("wait_detect_period_finish\n");
    wait_detect_period_finish();
    set_detect_period_flag(0);
    LOG("ensure_detect_period_finish\n");
    /* 抓手释放，避免闭合状态使抓手过热 */
    catcher_release();
    catcher_set_low_curr();

    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);

    free(arg);
}

/* 维护流程、余量探测、自检流程 需要流程互斥执行 */
static int machine_is_maintence = 0; /* 0:维护流程完成 1：维护流程中 */

void machine_maintence_state_set(int sta)
{
    machine_is_maintence = sta;
}

int machine_maintence_state_get()
{
    return machine_is_maintence;
}
/* 停机维护 */
int emergency_stop_maintain(void)
{
    if (reset_all_motors()) {
        return -1;
    }
    if (normal_clearer_bubble_stage_notify()) {
        needle_clean_flag = MAINTAIN_PIPE_LINE_PREFILL;
    } else {
        needle_clean_flag = MAINTAIN_NORMAL_CLEAN;
    }
    if (instrument_self_check(1, 0)) {
        return -1;
    }
    /* 防止异常中断流程无法解除撞针屏蔽 */
    slip_liquid_detect_collsion_barrier_set(NEEDLE_TYPE_S, ATTR_DISABLE);
    liquid_detect_err_count_all_clear();

    return 0;
}
/* 开机维护 */
int power_on_maintain(void)
{
    /* 更新开机状态 */
    set_power_off_stat(0);

    if (reset_all_motors()) {
        return -1;
    }
    needle_clean_flag = MAINTAIN_PIPE_LINE_PREFILL;
    if (power_on_check()) {
        return -1;
    }
    needle_clean_flag = MAINTAIN_NORMAL_CLEAN;
    /* 开机维护时，解锁进样器电磁铁,并复位LED */
    if (-1 == ele_unlock_by_status()) {
        LOG("eletro unlock fail!\n");
        return -1;
    }
    /* 防止异常中断流程无法解除撞针屏蔽 */
    slip_liquid_detect_collsion_barrier_set(NEEDLE_TYPE_S, ATTR_DISABLE);
    liquid_detect_err_count_all_clear();

    return 0;
}

/* 关机维护 */
int power_off_maintain()
{
    if (reset_all_motors()) {
        LOG("reset all motors faild\n");
        return -1;
    }
    if (power_off_check()) {
        LOG("power_off_check failed\n");
        return -1;
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault, break\n");
        return -1;
    } else {
        /* 更新关机状态 */
        set_power_off_stat(1);
    }
    /* 防止异常中断流程无法解除撞针屏蔽 */
    slip_liquid_detect_collsion_barrier_set(NEEDLE_TYPE_S, ATTR_DISABLE);
    liquid_detect_err_count_all_clear();

    return 0;
}

static void thrift_disconnect_delay_task(void *arg)
{
    int *temp_regent_flag = (int *)arg;
    int count = 0;
    int temp_regent_timeout = reagent_gate_timeout_get();
    int set_flag = 0;
    char alarm_message[FAULT_CODE_LEN] = {0};

    /* （20241203版本后由上位机管控，下位机一直开启监听及连接） */
    /* sleep(8); 延迟关闭下位机连接(为确保ACk能回传至上位机，需延迟关闭下位机thrift连接) */
    /* thrift_slave_client_connect_ctl(0); */
    /* thrift_slave_server_connect_ctl(0); */
    /* sleep(1); */

    /* 关闭 所有温控（试剂仓单独控制） */
    all_temperate_ctl(0, *temp_regent_flag);

    /* 关闭 磁珠驱动力 */
    all_magnetic_pwm_ctl(0);

    /* 关闭 光学检测位led灯 */
    all_optical_led_ctl(0);

    /* 下电 所有电机 */
    all_motor_power_clt(0);

    LOG("all thrift disconnect, temp_regent_flag:%d, temp_regent_timeout:%d\n", *temp_regent_flag, temp_regent_timeout);
    temp_regent_timeout = temp_regent_timeout<0 ? 0 : temp_regent_timeout;

    indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_GREEN, LED_OFF);
    indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_YELLOW, LED_OFF);
    indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_RED, LED_OFF);
    slip_button_reag_led_to_sampler(REAG_BUTTON_LED_CLOSE);
    set_alarm_mode(SOUND_OFF, SOUND_TYPE_0);
    engineer_is_run_set(ENGINEER_IS_RUN); /* 借助工程师模式的操作标志，迫使下次开机时进行清杯 */

    if (*temp_regent_flag == 1) {
        indicator_led_set(LED_MACHINE_ID, LED_COLOR_GREEN, LED_BLINK);
        slip_button_reag_led_to_sampler(REAG_BUTTON_LED_OPEN);
        shutdown_temp_regent_flag = 1;
        while (thrift_slave_client_connect_ctl_get() == 0 || thrift_salve_heartbeat_flag_get() == 0) {
            if (temp_regent_timeout > 0) {
                if (gpio_get(PE_REGENT_TABLE_GATE) == 1) {
                    count++;
                } else {
                    count = 0;
                    if (set_flag == 1) {
                        set_alarm_mode(SOUND_OFF, SOUND_TYPE_0);
                    }
                }

                if (set_flag==0 && count>=temp_regent_timeout) {
                    LOG("regent gate open timeout\n");
                    set_flag = 1;
                    indicator_led_set(LED_MACHINE_ID, LED_COLOR_YELLOW, LED_BLINK);
                    set_alarm_mode(SOUND_ON, SOUND_TYPE_3);
                }
            }

            sleep(1);
        }

        if (set_flag == 1) {
            fault_code_generate(alarm_message, MODULE_CLASS_FAULT_REAGENT_TABLE, MODULE_FAULT_REAGENT_GATE_OPENED);
            report_alarm_message(0, alarm_message);
        }
        shutdown_temp_regent_flag = 0;
        LOG("done\n");
    } else {
        indicator_led_set(LED_MACHINE_ID, LED_COLOR_GREEN, LED_OFF);
        indicator_led_set(LED_MACHINE_ID, LED_COLOR_YELLOW, LED_OFF);
        indicator_led_set(LED_MACHINE_ID, LED_COLOR_RED, LED_OFF);
    }

    free(temp_regent_flag);
}

/**
 * @brief: 上位机重启激活仪器。
 * @param: 暂无参数输入。
 * @return:激活结果。
 */
void upper_start_active_machine(void *arg)
{
    int ret = 0;
    int32_t *user_data = (int32_t *)arg;
    async_return_t async_return;

    /* 开启 thrift服务（20241203版本后由上位机管控，下位机一直开启监听及连接） */
    /* thrift_slave_client_connect_ctl(1); */
    /* thrift_slave_server_connect_ctl(1); */
    /* 使能 磁珠驱动力 */
    if (all_magnetic_pwm_ctl(1) == -1) {
        //TODO? 错误处理
    }
    /* 使能 光学检测位led灯 */
    if (all_optical_led_ctl(1) == -1) {
        //TODO? 错误处理
    }
    /* 使能 所有温控 */
    if (all_temperate_ctl(1, 0) == -1) {
        //TODO? 错误处理
    }
    /* 上电 所有电机 */
    if (all_motor_power_clt(1) == -1) {
        //TODO? 错误处理
    }
    slip_button_reag_led_to_sampler(REAG_BUTTON_LED_OPEN);
    ret = ele_unlock_by_status();
    set_power_off_stat(0);

    LOG("active machine done. return value: %d\n",ret);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, !!ret, &async_return);

    free(user_data);
}


/* 电机下电，关闭温控 */
int power_down_maintain(int temp_regent_table)
{
    int *data = NULL;

    set_power_off_stat(1);

    /* 关闭 thrift服务(为确保ACk能回传至上位机，需延迟关闭下位机thrift连接) */
    data = (int *)calloc(1, sizeof(int));
    *data = temp_regent_table;
    work_queue_add(thrift_disconnect_delay_task, data);

    return 0;
}

/* 打包关机后程序退出 */
void exit_program(void *arg)
{
    sleep(5);
    exit(0);
}

/* 管路填充 */
int pipeline_fill_maintain()
{
    if (reset_all_motors()) {
        LOG("reset all motors failed\n");
        return -1;
    }
    needle_clean_flag = MAINTAIN_PIPE_LINE_PREFILL;
    if (pipefill_start()) {
        LOG("reset all motors failed\n");
        return -1;
    }
    needle_clean_flag = MAINTAIN_NORMAL_CLEAN;
    return 0;
}
/* 仪器复位 */
int reset_all_motors_maintain()
{
    if (reset_all_motors()) {
        LOG("reset all motors failed\n");
        return -1;
    }

    if (module_pos_init_after_reset_all()) {
        LOG("module pos init failed\n");
        return 1;
    }
    return 0;
}

/* 检查 与X子板连接情况 */
int check_all_board_connect()
{
    slip_magnetic_bead_t magnetic_bead = {0};
    slip_temperate_ctl_t temperate_ctl = {0};

    LOG("run check board maintain\n");

    /* 检查 与M0的通信 */
    /* 磁珠板 */
    if (slip_magnetic_bead_get(&magnetic_bead) == -1) {
        LOG("check M0.magnetic connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_MAG_CONNECT);
        return -1;
    }

    /* 温控板 */
    if (slip_temperate_ctl_get(&temperate_ctl) == -1) {
        LOG("check M0.temperate connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_TEMP_CONNECT);
        return -1;
    }

    /* 液面探测板（样本针） */
    if (liquid_detect_connect_check(NEEDLE_TYPE_S) == -1) {
        LOG("check M0.liq detect S connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_LQ_S_CONNECT);
        return -1;
    }

    /* 液面探测板（试剂针2） */
    if (liquid_detect_connect_check(NEEDLE_TYPE_R2) == -1) {
        LOG("check M0.liq detect R2 connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_LQ_R2_CONNECT);
        return -1;
    }

    /* 光学检测板1 */
    if (slip_optical_set(OPTICAL_HEART_BEAT) == -1) {
        LOG("check M0.optical1 connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_OPTICAL_CONNECT);
        return -1;
    }

    /* 检查 与M7的通信 */
    if (gpio_get(TEMP_SAMPLE_TUBE_HAT_CHECK) == -1) {
        LOG("check M7 connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_SAMPLER_CONNECT);
        return -1;
    }

    /* 检查 与A9.RTOS的通信 */
    if (gpio_get(PE_UP_CAP) == -1) {
        LOG("check A9 connect fail\n");
        FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_A9_RTOS_CONNECT);
        return -1;
    }

    return 0;
}


