/******************************************************
维护功能的实现文件
******************************************************/
#include <stdint.h>
#include <pthread.h>

#include "thrift_handler.h"
#include "h3600_maintain_utils.h"
#include "h3600_com_maintain_utils.h"
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

static com_maintain_t s_com_maintain = {0};
static sem_t sem_com_needle_s_avoid;
static sem_t sem_com_pipeline_done;
static int s_com_maintain_bit = 0;
static com_maintain_cnt_t s_com_maintain_cnt = {0};

int get_com_maintain_size()
{
    return s_com_maintain.size;
}

void set_com_maintain_size(int size)
{
    s_com_maintain.size = size;
}

void set_com_maintain_param(int i, int item_id, int param)
{
    s_com_maintain.com_maintains[i].item_id = item_id;
    s_com_maintain.com_maintains[i].param = param;
}

static void reagent_gate_check()
{
    int report_flag = 0, alarm_flag = 0, count = 0;
    char alarm_message[FAULT_CODE_LEN] = {0};

    /* 等待上位机断开连接 */
    sleep(10);
    slip_button_reag_led_to_sampler(REAG_BUTTON_LED_OPEN);
    indicator_led_set(LED_MACHINE_ID, LED_COLOR_GREEN, LED_BLINK);

    while (thrift_salve_heartbeat_flag_get() == 0) {
        if (ins_io_get().reag_monitor != 0 && ins_io_get().reag_time != 0) {
            if (gpio_get(PE_REGENT_TABLE_GATE) == 1 && ins_io_get().reag_io) {
                count++;
            } else {
                count = 0;
                if (alarm_flag == 1) {
                    set_alarm_mode(SOUND_OFF, SOUND_TYPE_0);
                    alarm_flag = 0;
                }
            }
            if (alarm_flag == 0 && count >= ins_io_get().reag_time) {
                LOG("regent gate open timeout\n");
                report_flag = 1;
                alarm_flag = 1;
                indicator_led_set(LED_MACHINE_ID, LED_COLOR_YELLOW, LED_BLINK);
                set_alarm_mode(SOUND_ON, SOUND_TYPE_3);
            }
        }
        sleep(1);
    }

    if (report_flag == 1) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_REAGENT_TABLE, MODULE_FAULT_REAGENT_GATE_MONITOR);
        report_alarm_message(0, alarm_message);
    }
}

static void check_com_maintain()
{
    int i = 0, com_remain_time = 5;
    LOG("list show:[\n");
    for (i=0; i<s_com_maintain.size; i++) {
        switch (s_com_maintain.com_maintains[i].item_id) {
        case MAINTAIN_CMD_RESET:
            s_com_maintain_bit |= S_COM_RESET;
            s_com_maintain_cnt.reset_motors = s_com_maintain.com_maintains[i].param;
            com_remain_time += 10;
            LOG("reset\n");
            break;
        case MAINTAIN_CMD_REAGENT_SCAN:
            s_com_maintain_bit |= S_COM_REAGENT_SCAN;
            s_com_maintain_cnt.reag_scan = s_com_maintain.com_maintains[i].param;
            com_remain_time += 30;
            LOG("reagent_scan\n");
            break;
        case MAINTAIN_CMD_REAGENT_MIX:
            s_com_maintain_bit |= S_COM_REAGENT_MIX;
            s_com_maintain_cnt.reag_mix = s_com_maintain.com_maintains[i].param;
            com_remain_time += 5;
            LOG("reagent_mix\n");
            break;
        case MAINTAIN_CMD_DROP_CUPS:
            s_com_maintain_bit |= S_COM_DROP_CUPS;
            s_com_maintain_cnt.drop_cups = s_com_maintain.com_maintains[i].param;
            com_remain_time += 45;
            LOG("drop_cups\n");
            break;
        case MAINTAIN_CMD_POWEROFF:
            s_com_maintain_bit |= S_COM_POWEROFF;
            s_com_maintain_cnt.poweroff = s_com_maintain.com_maintains[i].param;
            LOG("poweroff[%d]\n", s_com_maintain_cnt.poweroff);
            break;
        case MAINTAIN_CMD_PIPELINE_FILL:
            s_com_maintain_bit |= S_COM_PIPELINE_FILL;
            s_com_maintain_cnt.pipeline_fill = s_com_maintain.com_maintains[i].param;
            com_remain_time += 80;
            LOG("pipeline_fill[%d]\n", s_com_maintain_cnt.pipeline_fill);
            break;
        case MAINTAIN_CMD_PIPELINE_CLEAN:
            s_com_maintain_bit |= S_COM_PIPELINE_CLEAN;
            s_com_maintain_cnt.pipeline_clean = s_com_maintain.com_maintains[i].param;
            com_remain_time += 80;
            LOG("pileline_clean[%d]\n", s_com_maintain_cnt.pipeline_clean);
            break;
        case MAINTAIN_CMD_S_NOR_CLEAN:
            s_com_maintain_bit |= S_COM_S_NOR_CLEAN;
            s_com_maintain_cnt.s_nor_clean = s_com_maintain.com_maintains[i].param;
            com_remain_time += 5*s_com_maintain_cnt.s_nor_clean;
            LOG("s_nor_clean[%d]\n", s_com_maintain_cnt.s_nor_clean);
            break;
        case MAINTAIN_CMD_R2_NOR_CLEAN:
            s_com_maintain_bit |= S_COM_R2_NOR_CLEAN;
            s_com_maintain_cnt.r2_nor_clean = s_com_maintain.com_maintains[i].param;
            com_remain_time += 5*s_com_maintain_cnt.r2_nor_clean;
            LOG("r2_nor_clean[%d]\n", s_com_maintain_cnt.r2_nor_clean);
            break;
        case MAINTAIN_CMD_S_SPEC_CLEAN:
            s_com_maintain_bit |= S_COM_S_SPEC_CLEAN;
            s_com_maintain_cnt.s_spec_clean = s_com_maintain.com_maintains[i].param;
            com_remain_time += 20*s_com_maintain_cnt.s_spec_clean;
            LOG("s_spec_clean[%d]\n", s_com_maintain_cnt.s_spec_clean);
            break;
        case MAINTAIN_CMD_R2_SPEC_CLEAN:
            s_com_maintain_bit |= S_COM_R2_SPEC_CLEAN;
            s_com_maintain_cnt.r2_spec_clean = s_com_maintain.com_maintains[i].param;
            com_remain_time += 5*s_com_maintain_cnt.r2_spec_clean;
            LOG("r2_spec_clean[%d]\n", s_com_maintain_cnt.r2_spec_clean);
            break;
        case MAINTAIN_CMD_S_SOAK:
            s_com_maintain_bit |= S_COM_S_SOAK;
            s_com_maintain_cnt.s_soak = s_com_maintain.com_maintains[i].param;
            com_remain_time += 60*s_com_maintain_cnt.s_soak;
            LOG("s_soak[%d]\n", s_com_maintain_cnt.s_soak);
            break;
        case MAINTAIN_CMD_R2_SOAK:
            s_com_maintain_bit |= S_COM_R2_SOAK;
            s_com_maintain_cnt.r2_soak = s_com_maintain.com_maintains[i].param;
            com_remain_time += 60*s_com_maintain_cnt.r2_soak;
            LOG("r2_soak[%d]\n", s_com_maintain_cnt.r2_soak);
            break;
        case MAINTAIN_CMD_CHECKIO:
            s_com_maintain_bit |= S_COM_CHECKIO;
            s_com_maintain_cnt.check_io = s_com_maintain.com_maintains[i].param;
            LOG("check_io\n");
            break;
        default:
            break;
        }
    }
    LOG("]\n");
    if (TBIT(s_com_maintain_bit, S_COM_R2_SOAK) && TBIT(s_com_maintain_bit, S_COM_S_SOAK)) {
        if (s_com_maintain_cnt.r2_soak > s_com_maintain_cnt.s_soak) {
            com_remain_time -= 60*s_com_maintain_cnt.s_soak;
        } else {
            com_remain_time -= 60*s_com_maintain_cnt.r2_soak;
        }
    }
    if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) && TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) {
        com_remain_time -= 80;
    }
    if (TBIT(s_com_maintain_bit, S_COM_S_NOR_CLEAN) && TBIT(s_com_maintain_bit, S_COM_R2_NOR_CLEAN)) {
        if (s_com_maintain_cnt.s_nor_clean > s_com_maintain_cnt.r2_nor_clean) {
            com_remain_time -= 5*s_com_maintain_cnt.r2_nor_clean;
        } else {
            com_remain_time -= 5*s_com_maintain_cnt.s_nor_clean;
        }
    }
    if (TBIT(s_com_maintain_bit, S_COM_S_SPEC_CLEAN) && TBIT(s_com_maintain_bit, S_COM_R2_SPEC_CLEAN)) {
        if (s_com_maintain_cnt.s_spec_clean > (s_com_maintain_cnt.r2_spec_clean*4)) {
            com_remain_time -= 5*s_com_maintain_cnt.r2_spec_clean;
        } else {
            com_remain_time -= 5*s_com_maintain_cnt.s_spec_clean;
        }
    }
    if ((TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) || TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) && TBIT(s_com_maintain_bit, S_COM_DROP_CUPS)) {
        com_remain_time -= 45;
    }
    report_maintenance_remain_time(com_remain_time);
}

static int com_io_check()
{
    if (TBIT(s_com_maintain_bit, S_COM_CHECKIO)) {
        report_maintenance_item_result(MAINTAIN_CMD_CHECKIO, s_com_maintain_cnt.check_io, (bool)1, (bool)1);
        report_maintenance_item_result(MAINTAIN_CMD_CHECKIO, s_com_maintain_cnt.check_io, (bool)0, (bool)1);
    }
    return 0;
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

static void *com_s_maintain_task(void *arg)
{
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int i = 0;

    motor_attr_init(&motor_x, &motor_z);
    set_pos(&pos_param.cur, 0, 0, 0);
    get_special_pos(MOVE_S_TEMP, 0, &pos_param.t1_src, FLAG_POS_NONE);

    if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) || TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN) ||
        TBIT(s_com_maintain_bit, S_COM_DROP_CUPS) || TBIT(s_com_maintain_bit, S_COM_S_SOAK) ||
        TBIT(s_com_maintain_bit, S_COM_S_SPEC_CLEAN) || TBIT(s_com_maintain_bit, S_COM_S_NOR_CLEAN)) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_x.step = abs(pos_param.t1_src.x);
        motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_S_X);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, pos_param.t1_src.x,
                                    pos_param.t1_src.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            *(int *)arg = 1;
        }
        set_pos(&pos_param.cur, pos_param.t1_src.x, pos_param.t1_src.y, 0);
        FAULT_CHECK_END();
    }
    sem_post(&sem_com_needle_s_avoid);
    if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) || TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param.t1_src.z + NEEDLE_S_SPECIAL_COMP_STEP);
        motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z + NEEDLE_S_SPECIAL_COMP_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            *(int *)arg = 1;
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
        /* 灌注or洗针 */
        sleep(3);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        sem_wait(&sem_com_pipeline_done);
        FAULT_CHECK_END();
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_S_SOAK)) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        report_maintenance_item_result(MAINTAIN_CMD_S_SOAK, s_com_maintain_cnt.s_soak, (bool)1, (bool)1);
        motor_z.step = abs(14000 - NEEDLE_S_CLEAN_POS);
        motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, 14000 - NEEDLE_S_CLEAN_POS, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        for (i=0; i<s_com_maintain_cnt.s_soak*12; i++) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            sleep(5);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param.t1_src.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_S_SOAK, s_com_maintain_cnt.s_soak, (bool)0, (bool)0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_maintenance_item_result(MAINTAIN_CMD_S_SOAK, s_com_maintain_cnt.s_soak, (bool)0, (bool)1);
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_S_SPEC_CLEAN)) {
        report_maintenance_item_result(MAINTAIN_CMD_S_SPEC_CLEAN, s_com_maintain_cnt.s_spec_clean, (bool)1, (bool)1);
        get_special_pos(MOVE_S_CLEAN, 0, &pos_param.t1_dst, FLAG_POS_NONE);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_x.step = abs(pos_param.t1_dst.x - pos_param.cur.x);
        motor_x.acc = calc_motor_move_in_time(&motor_x, STARTUP_TIMES_S_X);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, pos_param.t1_dst.x - pos_param.cur.x,
                                    pos_param.t1_dst.y - pos_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            *(int *)arg = 1;
        }
        set_pos(&pos_param.cur, pos_param.t1_dst.x, pos_param.t1_dst.y, 0);
        FAULT_CHECK_END();
        for (i=0; i<s_com_maintain_cnt.s_spec_clean; i++) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            s_special_clean(0.4);
            FAULT_CHECK_END();
        }
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_S_SPEC_CLEAN, s_com_maintain_cnt.s_spec_clean, (bool)0, (bool)0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_maintenance_item_result(MAINTAIN_CMD_S_SPEC_CLEAN, s_com_maintain_cnt.s_spec_clean, (bool)0, (bool)1);
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_S_NOR_CLEAN)) {
        report_maintenance_item_result(MAINTAIN_CMD_S_NOR_CLEAN, s_com_maintain_cnt.s_nor_clean, (bool)1, (bool)1);
        for (i=0; i<s_com_maintain_cnt.s_nor_clean; i++) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_S_NOR_CLEAN, s_com_maintain_cnt.s_nor_clean, (bool)0, (bool)0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_maintenance_item_result(MAINTAIN_CMD_S_NOR_CLEAN, s_com_maintain_cnt.s_nor_clean, (bool)0, (bool)1);
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();

    return NULL;
}

static void *com_r2_maintain_task(void *arg)
{
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_y = {0}, motor_z = {0};
    int i = 0;

    motor_attr_init(&motor_y, &motor_z);
    get_special_pos(MOVE_R2_CLEAN, 0, &pos_param.t1_src, FLAG_POS_NONE);

    if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) || TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN) ||
        TBIT(s_com_maintain_bit, S_COM_R2_SOAK) || TBIT(s_com_maintain_bit, S_COM_R2_SPEC_CLEAN) ||
        TBIT(s_com_maintain_bit, S_COM_R2_NOR_CLEAN)) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_y.step = abs(pos_param.t1_src.x);
        motor_y.acc = calc_motor_move_in_time(&motor_y, STARTUP_TIMES_S_X);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
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
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) || TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) {
        if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) {
            report_maintenance_item_result(MAINTAIN_CMD_PIPELINE_CLEAN, s_com_maintain_cnt.pipeline_clean, (bool)1, (bool)0);
        }
        if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL)) {
            report_maintenance_item_result(MAINTAIN_CMD_PIPELINE_FILL, s_com_maintain_cnt.pipeline_fill, (bool)1, (bool)0);
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param.t1_src.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        /* 灌注or洗针 */
        *(int *)arg = liquid_self_maintence_interface(0);
        sleep(3);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        s_normal_inside_clean();
        sleep(1);
        FAULT_CHECK_END();
        sem_post(&sem_com_pipeline_done);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        r2_normal_clean();
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) {
                report_maintenance_item_result(MAINTAIN_CMD_PIPELINE_CLEAN, s_com_maintain_cnt.pipeline_clean, (bool)0, (bool)0);
            }
            if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL)) {
                report_maintenance_item_result(MAINTAIN_CMD_PIPELINE_FILL, s_com_maintain_cnt.pipeline_fill, (bool)0, (bool)0);
            }
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN)) {
                report_maintenance_item_result(MAINTAIN_CMD_PIPELINE_CLEAN, s_com_maintain_cnt.pipeline_clean, (bool)0, (bool)1);
            }
            if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL)) {
                report_maintenance_item_result(MAINTAIN_CMD_PIPELINE_FILL, s_com_maintain_cnt.pipeline_fill, (bool)0, (bool)1);
            }
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_R2_SOAK)) {
        report_maintenance_item_result(MAINTAIN_CMD_R2_SOAK, s_com_maintain_cnt.s_soak, (bool)1, (bool)0);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param.t1_src.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z + NEEDLE_R2C_COMP_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        for (i=0; i<s_com_maintain_cnt.r2_soak*12; i++) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            sleep(5);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_R2_SOAK, s_com_maintain_cnt.s_soak, (bool)0, (bool)0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_maintenance_item_result(MAINTAIN_CMD_R2_SOAK, s_com_maintain_cnt.s_soak, (bool)0, (bool)1);
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_R2_SPEC_CLEAN)) {
        report_maintenance_item_result(MAINTAIN_CMD_R2_SPEC_CLEAN, s_com_maintain_cnt.r2_spec_clean, (bool)1, (bool)1);
        for (i=0; i<s_com_maintain_cnt.r2_spec_clean; i++) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            motor_z.step = abs(pos_param.t1_src.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                *(int *)arg = 1;
            }
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            r2_special_clean();
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                *(int *)arg = 1;
            }
            FAULT_CHECK_END();
        }
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_R2_SPEC_CLEAN, s_com_maintain_cnt.r2_spec_clean, (bool)0, (bool)0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_maintenance_item_result(MAINTAIN_CMD_R2_SPEC_CLEAN, s_com_maintain_cnt.r2_spec_clean, (bool)0, (bool)1);
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (TBIT(s_com_maintain_bit, S_COM_R2_NOR_CLEAN)) {
        report_maintenance_item_result(MAINTAIN_CMD_R2_NOR_CLEAN, s_com_maintain_cnt.r2_nor_clean, (bool)1, (bool)1);
        for (i=0; i<s_com_maintain_cnt.r2_nor_clean; i++) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            motor_z.step = abs(pos_param.t1_src.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_S_Z);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, pos_param.t1_src.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                *(int *)arg = 1;
            }
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            r2_normal_clean();
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                *(int *)arg = 1;
            }
            FAULT_CHECK_END();
        }
        if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_R2_NOR_CLEAN, s_com_maintain_cnt.r2_nor_clean, (bool)0, (bool)0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            report_maintenance_item_result(MAINTAIN_CMD_R2_NOR_CLEAN, s_com_maintain_cnt.r2_nor_clean, (bool)0, (bool)1);
            FAULT_CHECK_END();
        }
    }
    FAULT_CHECK_END();

    if (TBIT(s_com_maintain_bit, S_COM_PIPELINE_FILL) || TBIT(s_com_maintain_bit, S_COM_PIPELINE_CLEAN) ||
        TBIT(s_com_maintain_bit, S_COM_R2_SOAK) || TBIT(s_com_maintain_bit, S_COM_R2_SPEC_CLEAN) ||
        TBIT(s_com_maintain_bit, S_COM_R2_NOR_CLEAN)) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 5 * 128, 45000, 180000, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_y.step = abs(pos_param.t1_src.x);
        motor_y.acc = calc_motor_move_in_time(&motor_y, STARTUP_TIMES_S_X);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            *(int *)arg = 1;
        }
        FAULT_CHECK_END();
    }

    return NULL;
}

static int com_catcher_drop_one_cup(module_param_t *pos_param)
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
        res = 1;
    }
    set_pos(&pos_param->cur, pos_param->t1_src.x, pos_param->t1_src.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(pos_param->t1_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, pos_param->t1_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        res = 1;
    }
    set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, pos_param->t1_src.z);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        res = 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(pos_param->t1_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        res = 1;
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
            res = 1;
        }
        set_pos(&pos_param->cur, pos_param->t1_dst.x, pos_param->t1_dst.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param->t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, pos_param->t1_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            res = 1;
        }
        set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, pos_param->t1_dst.z);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (catcher_ctl(CATCHER_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            res = 1;
        }
        report_reagent_supply_consume(WASTE_CUP, 1, 1);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_async(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, -200, 25000, 160000)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            res = 1;
        }
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                    -1400, 50000, 150000, MOTOR_DEFAULT_TIMEOUT, 0.01)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            res = 1;
        }
        if (motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] z reset timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            res = 1;
        }
        set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y-1400, pos_param->cur.z-200);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(pos_param->t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, STARTUP_TIMES_C_Z);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            res = 1;
        }
        set_pos(&pos_param->cur, pos_param->cur.x, pos_param->cur.y, 0);
        if (check_catcher_status()) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            res = 1;
        }
        FAULT_CHECK_END();
    }else {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (catcher_ctl(CATCHER_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            res = 1;
        }
        FAULT_CHECK_END();
    }
    FAULT_CHECK_END();
    return res;
}

static void *com_drop_cup_maintain_task(void *arg)
{
    module_param_t pos_param = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int i = 0;

    motor_attr_init(&motor_x, &motor_z);
    set_pos(&pos_param.cur, 0, 0, 0);
    sem_wait(&sem_com_needle_s_avoid);

    if (!TBIT(s_com_maintain_bit, S_COM_DROP_CUPS)) {
        return NULL;
    }
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    report_maintenance_item_result(MAINTAIN_CMD_DROP_CUPS, s_com_maintain_cnt.drop_cups, (bool)1, (bool)1);
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
    if (catcher_rs485_init()) {
        *(int *)arg = 1;
    }
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
    if (com_catcher_drop_one_cup(&pos_param)) {
        *(int *)arg = 1;
    }
    /* 常规混匀位 */
    for (i=0; i<2; i++) {
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        if (com_catcher_drop_one_cup(&pos_param)) {
            *(int *)arg = 1;
        }
    }
    /* 孵育位 */
    for (i=0; i<30; i++) {
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        if (com_catcher_drop_one_cup(&pos_param)) {
            *(int *)arg = 1;
        }
    }
    /* 光学检测位 */
    for (i=0; i<8; i++) {
        get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        if (com_catcher_drop_one_cup(&pos_param)) {
            *(int *)arg = 1;
        }
    }
    /* 光学混匀位 */
    get_special_pos(MOVE_C_OPTICAL_MIX, 0, &pos_param.t1_src, FLAG_POS_UNLOCK);
    get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
    if (com_catcher_drop_one_cup(&pos_param)) {
        *(int *)arg = 1;
    }
    /* 磁珠检测位 */
    for (i=0; i<4; i++) {
        LOG("magnetic\n");
        get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1+i, &pos_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_DETACH, 0, &pos_param.t1_dst, FLAG_POS_UNLOCK);
        if (com_catcher_drop_one_cup(&pos_param)) {
            *(int *)arg = 1;
        }
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

    if (*(int *)arg == 1 || module_fault_stat_get() != MODULE_FAULT_NONE) {
        report_maintenance_item_result(MAINTAIN_CMD_DROP_CUPS, s_com_maintain_cnt.drop_cups, (bool)0, (bool)0);
    } else {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        report_maintenance_item_result(MAINTAIN_CMD_DROP_CUPS, s_com_maintain_cnt.drop_cups, (bool)0, (bool)1);
        FAULT_CHECK_END();
    }
    FAULT_CHECK_END();

    return NULL;
}

static int com_reagent_scan(int32_t scan_idx)
{
    LOG("recv scan table idx is %d.\n", scan_idx);
    if (TBIT(s_com_maintain_bit, S_COM_REAGENT_SCAN)) {
        report_maintenance_item_result(MAINTAIN_CMD_REAGENT_SCAN, s_com_maintain_cnt.reag_scan, (bool)1, (bool)1);
        sleep(2);
        if (scan_idx == 0) {
            reagent_scan_interface();
        } else {
            reagent_scan_engineer_mode(scan_idx);
        }
        if (module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_REAGENT_SCAN, s_com_maintain_cnt.reag_scan, (bool)0, (bool)0);
        } else {
            report_maintenance_item_result(MAINTAIN_CMD_REAGENT_SCAN, s_com_maintain_cnt.reag_scan, (bool)0, (bool)1);
        }
    }
    return 0;
}

static int com_reagent_mix()
{
    if (TBIT(s_com_maintain_bit, S_COM_REAGENT_MIX)) {
        report_maintenance_item_result(MAINTAIN_CMD_REAGENT_MIX, s_com_maintain_cnt.reag_mix, (bool)1, (bool)1);
        if (reagent_mix_interface() < 0) {
            LOG("reagnet_table: do reagent mix faild.\n");
            report_maintenance_item_result(MAINTAIN_CMD_REAGENT_MIX, s_com_maintain_cnt.reag_mix, (bool)0, (bool)0);
            return 1;
        }
        if (module_fault_stat_get() != MODULE_FAULT_NONE) {
            report_maintenance_item_result(MAINTAIN_CMD_REAGENT_MIX, s_com_maintain_cnt.reag_mix, (bool)0, (bool)0);
        } else {
            report_maintenance_item_result(MAINTAIN_CMD_REAGENT_MIX, s_com_maintain_cnt.reag_mix, (bool)0, (bool)1);
        }
    }
    return 0;
}

static int com_poweroff_task()
{
    if (TBIT(s_com_maintain_bit, S_COM_POWEROFF)) {
        report_maintenance_item_result(MAINTAIN_CMD_POWEROFF, s_com_maintain_cnt.poweroff, (bool)1, (bool)1);
        /* 关闭 所有温控（试剂仓单独控制） */
        all_temperate_ctl(0, s_com_maintain_cnt.poweroff);

        /* 关闭 磁珠驱动力 */
        all_magnetic_pwm_ctl(0);

        /* 关闭 光学检测位led灯 */
        all_optical_led_ctl(0);

        /* 下电 所有电机 */
        all_motor_power_clt(0);

        indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_GREEN, LED_OFF);
        indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_YELLOW, LED_OFF);
        indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_RED, LED_OFF);
        slip_button_reag_led_to_sampler(REAG_BUTTON_LED_CLOSE);
        set_alarm_mode(SOUND_OFF, SOUND_TYPE_0);
        engineer_is_run_set(ENGINEER_IS_RUN); /* 借助工程师模式的操作标志，迫使下次开机时进行清杯 */

        if (s_com_maintain_cnt.poweroff) {
            work_queue_add(reagent_gate_check, NULL);
        }
        set_power_off_stat(1);

        report_maintenance_item_result(MAINTAIN_CMD_POWEROFF, s_com_maintain_cnt.poweroff, (bool)0, (bool)1);
    }

    return 0;
}

void com_maintain_task(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0, res_catcher = 0, res_needle_s = 0, res_needle_r2 = 0;
    async_return_t async_return;
    pthread_t com_drop_cup_thread, com_needle_s_thread, com_needle_r2_thread;

    s_com_maintain_bit = 0;
    check_com_maintain();
    sem_init(&sem_com_needle_s_avoid, 0, 0);
    sem_init(&sem_com_pipeline_done, 0, 0);

    if (com_io_check()) {
        LOG("io check failed!\n");
        ret = 1;
        goto end;
    }

    if (TBIT(s_com_maintain_bit, S_COM_RESET)) {
        report_maintenance_item_result(MAINTAIN_CMD_RESET, s_com_maintain_cnt.reset_motors, (bool)1, (bool)1);
    }
    if (reset_all_motors()) {
        LOG("reset all motors failed!\n");
        ret = 1;
    }
    if (ret == 1) {
        if (TBIT(s_com_maintain_bit, S_COM_RESET)) {
            report_maintenance_item_result(MAINTAIN_CMD_RESET, s_com_maintain_cnt.reset_motors, (bool)0, (bool)0);
        } else {
            report_maintenance_item_result(s_com_maintain.com_maintains[0].item_id, s_com_maintain.com_maintains[0].param, (bool)0, (bool)0);
        }
        goto end;
    } else {
        if (TBIT(s_com_maintain_bit, S_COM_RESET)) {
            report_maintenance_item_result(MAINTAIN_CMD_RESET, s_com_maintain_cnt.reset_motors, (bool)0, (bool)1);
        }
    }

    if (0 != pthread_create(&com_needle_s_thread, NULL, com_s_maintain_task, &res_needle_s)) {
        LOG("com_maintain needle_s thread create failed!, %s\n", strerror(errno));
        ret = 1;
        goto end;
    }
    if (0 != pthread_create(&com_needle_r2_thread, NULL, com_r2_maintain_task, &res_needle_r2)) {
        LOG("com_maintain needle_r2 thread create failed!, %s\n", strerror(errno));
        ret = 1;
        goto end;
    }
    if (0 != pthread_create(&com_drop_cup_thread, NULL, com_drop_cup_maintain_task, &res_catcher)) {
        LOG("com_maintain drop_cup thread create failed!, %s\n", strerror(errno));
        ret = 1;
        goto end;
    }

    pthread_join(com_needle_s_thread, NULL);
    pthread_join(com_needle_r2_thread, NULL);
    pthread_join(com_drop_cup_thread, NULL);
    LOG("com_maintain_task 3 thread end.\n");
    if (res_needle_s || res_needle_r2 || res_catcher) {
        LOG("com_maintain_task detect fault! %d %d %d\n", res_needle_s, res_needle_r2, res_catcher);
        ret = 1;
        goto end;
    }
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (com_reagent_scan((int32_t)s_com_maintain_cnt.reag_scan)) {
        ret = 1;
        goto end;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (com_reagent_mix()) {
        ret = 1;
        goto end;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (com_poweroff_task()) {
        ret = 1;
        goto end;
    }
    FAULT_CHECK_END();

    if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = 1;
    }

end:
    machine_maintence_state_set(0);

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    set_com_maintain_size(0);
}

