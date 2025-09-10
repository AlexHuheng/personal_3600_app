/******************************************************
工程师模式的相关流程实现文件（实现 调试命令功能）
******************************************************/
#include <log.h>
#include <module_common.h>
#include <module_engineer_debug_cmd.h>
#include <module_monitor.h>
#include <module_sampler_ctl.h>
#include <module_cuvette_supply.h>
#include <module_liquied_circuit.h>
#include <module_liquid_detect.h>
#include <module_engineer_debug_position.h>
#include <module_catcher_rs485.h>

static int engineer_debug_cmd_s_pump_reset(void *arg)
{
    return thrift_motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
}

static int engineer_debug_cmd_s_pipe_fill(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_x_para_back =  {0};
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_x_para_temp =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    thrift_motor_para_get(MOTOR_NEEDLE_S_Y, &motor_y_para_back);
    motor_x_para_temp.speed = motor_x_para_back.speed / 3;
    motor_x_para_temp.acc = motor_x_para_back.acc / 3;
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_temp);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* XYZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_X, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_TEMP]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_TEMP]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        valve_set(DIAPHRAGM_PUMP_F1, ON);
        valve_set(DIAPHRAGM_PUMP_F4, ON);
        usleep(50*1000);
        valve_set(VALVE_SV1, ON);
        usleep(50*1000);
        valve_set(DIAPHRAGM_PUMP_Q1, ON);
        usleep(5000*1000);
        valve_set(VALVE_SV1, OFF);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_Q1, OFF);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_F1, OFF);
        valve_set(DIAPHRAGM_PUMP_F4, OFF);
        FAULT_CHECK_END();
    }

    /* XYZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Z, 0);
        FAULT_CHECK_END();
        if (ret == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                ret = 1;
            }
            FAULT_CHECK_END();
        }
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Y, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_X, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_s_pipe_clear(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_x_para_back =  {0};
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_x_para_temp =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    thrift_motor_para_get(MOTOR_NEEDLE_S_Y, &motor_y_para_back);
    motor_x_para_temp.speed = motor_x_para_back.speed / 3;
    motor_x_para_temp.acc = motor_x_para_back.acc / 3;
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_temp);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* XYZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_X, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_TEMP]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_TEMP]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        valve_set(DIAPHRAGM_PUMP_F1, ON);
        valve_set(DIAPHRAGM_PUMP_F4, ON);
        usleep(50*1000);
        valve_set(VALVE_SV1, ON);
        usleep(50*1000);
        valve_set(DIAPHRAGM_PUMP_Q1, ON);
        usleep(5000*1000);
        valve_set(VALVE_SV1, OFF);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_Q1, OFF);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_F1, OFF);
        valve_set(DIAPHRAGM_PUMP_F4, OFF);
        FAULT_CHECK_END();
    }

    /* XYZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Z, 0);
        FAULT_CHECK_END();
        if (ret == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                ret = 1;
            }
            FAULT_CHECK_END();
        }
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Y, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_X, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_s_clean_normal(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_x_para_back =  {0};
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_x_para_temp =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    thrift_motor_para_get(MOTOR_NEEDLE_S_Y, &motor_y_para_back);
    motor_x_para_temp.speed = motor_x_para_back.speed / 3;
    motor_x_para_temp.acc = motor_x_para_back.acc / 3;
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_temp);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* XYZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_X, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        s_normal_inside_clean();
        FAULT_CHECK_END();
        sleep(2);
    }

    /* XYZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Z, 0);
        FAULT_CHECK_END();
        if (ret == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                ret = 1;
            }
            FAULT_CHECK_END();
        }
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Y, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_X, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_s_clean_special(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_x_para_back =  {0};
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_x_para_temp =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    thrift_motor_para_get(MOTOR_NEEDLE_S_Y, &motor_y_para_back);
    motor_x_para_temp.speed = motor_x_para_back.speed / 3;
    motor_x_para_temp.acc = motor_x_para_back.acc / 3;
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_temp);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* XYZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_X, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        s_special_clean(0.4);
        FAULT_CHECK_END();
        sleep(2);
    }

    /* XYZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Z, 0);
        FAULT_CHECK_END();
        if (ret == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                ret = 1;
            }
            FAULT_CHECK_END();
        }
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Y, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_X, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_s_liq_detect(void *arg)
{
    struct ENGINEER_DEBUG_RUN_RESULT_TT *reuslt = (struct ENGINEER_DEBUG_RUN_RESULT_TT *)arg;
    int ret = 0;
    thrift_motor_para_t motor_x_para_back =  {0};
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_x_para_temp =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};
    liquid_detect_arg_t needle_s_liq_detect_arg = {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    thrift_motor_para_get(MOTOR_NEEDLE_S_Y, &motor_y_para_back);
    motor_x_para_temp.speed = motor_x_para_back.speed / 3;
    motor_x_para_temp.acc = motor_x_para_back.acc / 3;
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_temp);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* XYZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_X, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_1]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_1]);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_DILU;
        needle_s_liq_detect_arg.order_no = 0;
        needle_s_liq_detect_arg.s_cur_step = NEEDLE_S_CLEAN_POS;
        needle_s_liq_detect_arg.take_ul = 100 + NEEDLE_S_DILU_MORE;
        needle_s_liq_detect_arg.reag_idx = POS_REAGENT_DILU_1;
        needle_s_liq_detect_arg.mode = DEBUG_DETECT_MODE;
        ret = liquid_detect_start(needle_s_liq_detect_arg);
        if (ret < EMAX) {
            LOG("s liquid detect error!\n");
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("needle_s_calc_mode reset z failed!\n");
            }
        }
        reuslt->iRunResult = ret;
        FAULT_CHECK_END();
    }
    sleep(2);
    /* XYZ复位还原 */
    /* 无论成功失败，Z轴需尝试复位 */
    ret = thrift_motor_reset(MOTOR_NEEDLE_S_Z, 0);
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            ret = 1;
        }
        FAULT_CHECK_END();
    }
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_Y, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_S_X, 0);
        FAULT_CHECK_END();
    }

    if (reuslt->iRunResult < EMAX) {
        ret = -1;
    } else {
        ret = 0;
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_S_X, &motor_x_para_back);
    motor_para_set_temp(MOTOR_NEEDLE_S_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_r2_pump_reset(void *arg)
{
    return thrift_motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
}

static int engineer_debug_cmd_r2_pipe_fill(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* YZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Z, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN]+NEEDLE_R2C_COMP_STEP-NEEDLE_R2_NOR_CLEAN_STEP);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        valve_set(DIAPHRAGM_PUMP_F3, ON);
        usleep(200*1000);
        valve_set(VALVE_SV3, ON);
        usleep(50*1000);
        valve_set(DIAPHRAGM_PUMP_Q2, ON);
        usleep(5000*1000);
        valve_set(DIAPHRAGM_PUMP_Q2, OFF);
        usleep(200*1000);
        valve_set(VALVE_SV3, OFF);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_F3, OFF);
        FAULT_CHECK_END();
    }

    /* YZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Z, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Y, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_r2_pipe_clear(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* YZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Z, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN]+NEEDLE_R2C_COMP_STEP-NEEDLE_R2_NOR_CLEAN_STEP);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        valve_set(DIAPHRAGM_PUMP_F3, ON);
        usleep(200*1000);
        valve_set(VALVE_SV3, ON);
        usleep(50*1000);
        valve_set(DIAPHRAGM_PUMP_Q2, ON);
        usleep(5000*1000);
        valve_set(DIAPHRAGM_PUMP_Q2, OFF);
        usleep(200*1000);
        valve_set(VALVE_SV3, OFF);
        usleep(200*1000);
        valve_set(DIAPHRAGM_PUMP_F3, OFF);
        FAULT_CHECK_END();
    }

    /* YZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Z, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Y, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_r2_clean_normal(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* YZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        thrift_motor_move_to(MOTOR_NEEDLE_R2_Z, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN]+NEEDLE_R2C_COMP_STEP-NEEDLE_R2_NOR_CLEAN_STEP);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        r2_normal_clean();
        FAULT_CHECK_END();
    }

    /* XYZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Z, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Y, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_r2_clean_special(void *arg)
{
    int ret = 0;
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* YZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        thrift_motor_move_to(MOTOR_NEEDLE_R2_Z, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN]);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        r2_special_clean();
        FAULT_CHECK_END();
    }

    /* XYZ复位还原 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Z, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Y, 0);
        FAULT_CHECK_END();
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_r2_liq_detect(void *arg)
{
    struct ENGINEER_DEBUG_RUN_RESULT_TT *reuslt = (struct ENGINEER_DEBUG_RUN_RESULT_TT *)arg;
    int ret = 0;
    thrift_motor_para_t motor_y_para_back =  {0};
    thrift_motor_para_t motor_y_para_temp =  {0};
    liquid_detect_arg_t needle_r2_liq_detect_arg = {0};

    /* 临时改变电机速度，加速度 */
    thrift_motor_para_get(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);
    motor_y_para_temp.speed = motor_y_para_back.speed / 3;
    motor_y_para_temp.acc = motor_y_para_back.acc / 3;
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_temp);

    /* 全部电机复位 */
    ret = reset_all_motors();

    /* YZ运动到位 */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_REAGENT_TABLE, h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN]);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_R2_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_IN]);
        FAULT_CHECK_END();
    }

    /* 具体流程 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (ret == 0) {
        slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        needle_r2_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_r2_liq_detect_arg.needle = NEEDLE_TYPE_R2;
        needle_r2_liq_detect_arg.order_no = 0;
        needle_r2_liq_detect_arg.s_cur_step = 0;
        needle_r2_liq_detect_arg.take_ul = 50;
        needle_r2_liq_detect_arg.reag_idx = POS_REAGENT_TABLE_I1;
        needle_r2_liq_detect_arg.mode = DEBUG_DETECT_MODE;
        ret = liquid_detect_start(needle_r2_liq_detect_arg);
        if (ret < EMAX) {
            LOG("r2 liquid detect error!\n");
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].speed,
                h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].acc, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("needle_s_calc_mode reset z failed!\n");
            }
        }
        reuslt->iRunResult = ret;
        FAULT_CHECK_END();
        slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    }
    FAULT_CHECK_END();
    sleep(1);
    /* YZ复位还原 */
    /* 无论成功失败，Z轴需尝试复位 */
    ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Z, 0);
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_NEEDLE_R2_Y, 0);
        FAULT_CHECK_END();
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(MOTOR_REAGENT_TABLE, 1);
        FAULT_CHECK_END();
    }

    if (reuslt->iRunResult < EMAX && ret == 0) {
        ret = -1;
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    /* 还原电机速度，加速度 */
    motor_para_set_temp(MOTOR_NEEDLE_R2_Y, &motor_y_para_back);

    return ret;
}

static int engineer_debug_cmd_c_grap_init(void *arg)
{
    return catcher_rs485_init();
}

static int engineer_debug_cmd_c_grap_on(void *arg)
{
    return catcher_ctl(CATCHER_OPEN);
}

static int engineer_debug_cmd_c_grap_off(void *arg)
{
    return catcher_ctl(CATCHER_CLOSE);
}

static int engineer_debug_cmd_c_mix1_start(void *arg)
{
    return motor_move_sync(MOTOR_MIX_1, CMD_MOTOR_MOVE_SPEED, 0, 13056, 10000);
}

static int engineer_debug_cmd_c_mix1_stop(void *arg)
{
    return motor_move_sync(MOTOR_MIX_1, CMD_MOTOR_STOP, 0, 0, 10000);
}

static int engineer_debug_cmd_c_mix2_start(void *arg)
{
    return motor_move_sync(MOTOR_MIX_2, CMD_MOTOR_MOVE_SPEED, 0, 13056, 10000);
}

static int engineer_debug_cmd_c_mix2_stop(void *arg)
{
    return motor_move_sync(MOTOR_MIX_2, CMD_MOTOR_STOP, 0, 0, 10000);
}

static int engineer_debug_cmd_c_mix3_start(void *arg)
{
    return motor_move_sync(MOTOR_MIX_3, CMD_MOTOR_MOVE_SPEED, 0, 13056, 10000);
}

static int engineer_debug_cmd_c_mix3_stop(void *arg)
{
    return motor_move_sync(MOTOR_MIX_3, CMD_MOTOR_STOP, 0, 0, 10000);
}

static int engineer_debug_cmd_c_cuvette(void *arg)
{
    int ret = 0;
    reaction_cup_state_t state_back = 0;
    cuvette_supply_para_t *csp = cuvette_supply_para_get();

    state_back = csp[REACTION_CUP_INSIDE].state;


    csp[REACTION_CUP_INSIDE].state = CUP_INIT;
    csp[REACTION_CUP_INSIDE].available = 1;

    ret = cuvette_supply_reset(POWER_ON);
    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }
    csp[REACTION_CUP_INSIDE].state = state_back;

    return ret;
}

static int engineer_debug_cmd_reagent_scan_reagent(void *arg)
{
    struct ENGINEER_DEBUG_RUN_RESULT_TT *reuslt = (struct ENGINEER_DEBUG_RUN_RESULT_TT *)arg;
    int ret = 0;
    char barcode[SCANNER_BARCODE_LENGTH] = {0};

    ret = thrift_read_barcode(SCANNER_REAGENT, barcode, SCANNER_BARCODE_LENGTH);
    memcpy(reuslt->strBarcode, barcode, SCANNER_BARCODE_LENGTH);

    if (strlen(barcode) == 0) {
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}

static int engineer_debug_cmd_reagent_mix_start(void *arg)
{
    int ret = 0;

    ret = slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_REVERSE);
    ret = slip_bldc_ctl_set(BLDC_MAX, REAGENT_MIX_SPEED_NORMAL);
    return ret;
}

static int engineer_debug_cmd_reagent_mix_stop(void *arg)
{
    int ret = 0;

    ret = slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_STOP);
    return ret;
}

static int engineer_debug_cmd_sampler_scan_normal(void *arg)
{
    struct ENGINEER_DEBUG_RUN_RESULT_TT *reuslt = (struct ENGINEER_DEBUG_RUN_RESULT_TT *)arg;
    int ret = 0;
    char barcode[SCANNER_BARCODE_LENGTH] = {0};

    ret = thrift_read_barcode(SCANNER_RACKS, barcode, SCANNER_BARCODE_LENGTH);
    memcpy(reuslt->strBarcode, barcode, SCANNER_BARCODE_LENGTH);

    if (strlen(barcode) == 0) {
        ret = -1;
    } else {
        ret = 0;
    }

    return ret;
}


/* 工程师调试模式的调试命令表 */
const engineer_debug_debug_cmd_t engineer_debug_debug_cmd_tbl[] = 
{
    {10, "样本针",         1, "柱塞泵复位", engineer_debug_cmd_s_pump_reset},
    {10, "样本针",         2, "管路填充", engineer_debug_cmd_s_pipe_fill},
    {10, "样本针",         3, "管路排空", engineer_debug_cmd_s_pipe_clear},
    {10, "样本针",         4, "普通清洗", engineer_debug_cmd_s_clean_normal},
    {10, "样本针",         5, "特殊清洗", engineer_debug_cmd_s_clean_special},
    {10, "样本针",         6, "液面探测", engineer_debug_cmd_s_liq_detect},
    {10, "样本针",         7, "孵育试剂混匀1启动", engineer_debug_cmd_c_mix1_start},
    {10, "样本针",         8, "孵育试剂混匀1停止", engineer_debug_cmd_c_mix1_stop},
    {10, "样本针",         9, "孵育试剂混匀2启动", engineer_debug_cmd_c_mix2_start},
    {10, "样本针",        10, "孵育试剂混匀2停止", engineer_debug_cmd_c_mix2_stop},

    {11, "R2试剂针",       1, "柱塞泵复位", engineer_debug_cmd_r2_pump_reset},
    {11, "R2试剂针",       2, "管路填充", engineer_debug_cmd_r2_pipe_fill},
    {11, "R2试剂针",       3, "管路排空", engineer_debug_cmd_r2_pipe_clear},
    {11, "R2试剂针",       4, "普通清洗", engineer_debug_cmd_r2_clean_normal},
    {11, "R2试剂针",       5, "特殊清洗", engineer_debug_cmd_r2_clean_special},
    {11, "R2试剂针",       6, "液面探测", engineer_debug_cmd_r2_liq_detect},
    {11, "R2试剂针",       7, "启动光学混匀", engineer_debug_cmd_c_mix3_start},
    {11, "R2试剂针",       8, "停止光学混匀", engineer_debug_cmd_c_mix3_stop},

    {12, "反应杯抓手",       1, "初始化电爪", engineer_debug_cmd_c_grap_init},
    {12, "反应杯抓手",       2, "开启电爪", engineer_debug_cmd_c_grap_on},
    {12, "反应杯抓手",       3, "关闭电爪", engineer_debug_cmd_c_grap_off},
    {12, "反应杯抓手",       4, "进杯盘进杯", engineer_debug_cmd_c_cuvette},

    {13, "试剂存储",        1, "试剂仓混匀启动", engineer_debug_cmd_reagent_mix_start},
    {13, "试剂存储",        2, "试剂仓混匀停止", engineer_debug_cmd_reagent_mix_stop},

    {14, "进样器",         1, "常规位扫码", engineer_debug_cmd_sampler_scan_normal},
    {14, "进样器",         2, "试剂仓扫码", engineer_debug_cmd_reagent_scan_reagent},
};


/* 执行 调试指令 */
int engineer_debug_cmd_run(struct ENGINEER_DEBUG_CMD_TT *engineer_cmd_param, struct ENGINEER_DEBUG_RUN_RESULT_TT *reuslt)
{
    int i = 0;
    int ret = 0;

    for (i=0; i<sizeof(engineer_debug_debug_cmd_tbl)/sizeof(engineer_debug_debug_cmd_tbl[0]); i++) {
        if (engineer_cmd_param->iModuleIndex==engineer_debug_debug_cmd_tbl[i].module_idx && engineer_cmd_param->iCmd==engineer_debug_debug_cmd_tbl[i].cmd_idx) {
            if (engineer_debug_debug_cmd_tbl[i].hander) {
                ret = engineer_debug_debug_cmd_tbl[i].hander((void*)reuslt);
                break;
            }
        }
    }

    if (i >= sizeof(engineer_debug_debug_cmd_tbl)/sizeof(engineer_debug_debug_cmd_tbl[0])) {
        LOG("not find debug cmd:%d,%d\n", engineer_cmd_param->iModuleIndex, engineer_cmd_param->iCmd);
        ret = -1;
    }

    return ret;
}

