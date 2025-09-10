/******************************************************
工程师模式的相关流程实现文件（实现 老化功能）
******************************************************/
#include <log.h>
#include <stdint.h>
#include <movement_config.h>
#include <work_queue.h>
#include <h3600_cup_param.h>
#include <module_common.h>
#include <h3600_maintain_utils.h>
#include <module_monitor.h>
#include <module_cup_monitor.h>
#include <module_liquid_detect.h>
#include <module_reagent_table.h>
#include <module_magnetic_bead.h>
#include <module_needle_r2.h>
#include <module_catcher_rs485.h>
#include <module_engineer_debug_aging.h>

static int pos_cnt = 0;
static int engineer_aging_run_flag = 0; /* 运行标志：运行1停止0 */

static void motor_attr_init(motor_time_sync_attr_t *motor_x, motor_time_sync_attr_t *motor_z)
{
    motor_x->v0_speed = 100;
    motor_x->vmax_speed = h3600_conf_get()->motor[MOTOR_CATCHER_X].speed;
    motor_x->speed = h3600_conf_get()->motor[MOTOR_CATCHER_X].speed;
    motor_x->max_acc = h3600_conf_get()->motor[MOTOR_CATCHER_X].acc;
    motor_x->acc = h3600_conf_get()->motor[MOTOR_CATCHER_X].acc;

    motor_z->v0_speed = 100;
    motor_z->vmax_speed = h3600_conf_get()->motor[MOTOR_CATCHER_Z].speed;
    motor_z->speed = h3600_conf_get()->motor[MOTOR_CATCHER_Z].speed;
    motor_z->max_acc = h3600_conf_get()->motor[MOTOR_CATCHER_Z].acc;
    motor_z->acc = h3600_conf_get()->motor[MOTOR_CATCHER_Z].acc;
}

void engineer_aging_run_set(int flag)
{
    engineer_aging_run_flag = flag;
}

static int catcher_aging_quality_test()
{
    module_param_t catcher_param = {0};

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, offset_x = 0, offset_y = 0, offset_z = 0;

    catcher_rs485_init();
    motor_attr_init(&motor_x, &motor_z);

    LOG("catcher do aging...\n");
    time_frag = catcher_time_frag_table_get();

    if (pos_cnt == 30) {
        pos_cnt = 0;
    }
    offset_x = 0;
    offset_y = 0;
    offset_z = 0;
    /* 执行抓手从孵育位到进杯位的动作 */
    get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+pos_cnt, &catcher_param.t1_src, FLAG_POS_UNLOCK);
    get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
    if (catcher_param.t1_src.x != catcher_param.cur.x || catcher_param.t1_src.y != catcher_param.cur.y) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(catcher_param.t1_src.x - catcher_param.cur.x) > abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t1_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG0].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_src.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t1_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG0].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t1_src.x + offset_x, catcher_param.t1_src.y + offset_y, 0);
        FAULT_CHECK_END();
    }
    PRINT_FRAG_TIME("FRAG0");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t1_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG1].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG1");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG2");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t1_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG3].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG3");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t1_dst.x - catcher_param.cur.x) > abs((catcher_param.t1_dst.y - catcher_param.cur.y) - (catcher_param.t1_dst.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t1_dst.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t1_dst.y - catcher_param.cur.y) - (catcher_param.t1_dst.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG4].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_dst.x - catcher_param.cur.x + offset_x,
                                catcher_param.t1_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG4].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t1_dst.x + offset_x, catcher_param.t1_dst.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG4");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t1_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG5");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG6");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t1_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG7");

    LOG("catcher time 2 do [cup_init -> pre]!\n");
    /* 执行抓手从进杯盘抓杯到前处理的动作 */
    get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
    switch (pos_cnt%3) {
    case 0:
        get_special_pos(MOVE_C_PRE, 0, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
        break;
    case 1:
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
        break;
    case 2:
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX2, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
        break;
    default:
        break;
    }
    /* 抓手在本时间片复位一次 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.cur.x) > abs(catcher_param.cur.y)) {
        motor_x.step = abs(catcher_param.cur.x);
    } else {
        motor_x.step = abs(catcher_param.cur.y);
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, catcher_param.cur.x, catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, 0, 0, catcher_param.cur.z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG9");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t2_src.x - catcher_param.cur.x) > abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t2_src.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_src.x - catcher_param.cur.x + offset_x,
                                catcher_param.t2_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t2_src.x + offset_x, catcher_param.t2_src.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG10");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t2_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG11");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CUVETTE_SUPPLY_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG12");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t2_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG13].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG13");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t2_dst.x - catcher_param.cur.x) > abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t2_dst.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_dst.x - catcher_param.cur.x + offset_x,
                                catcher_param.t2_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t2_dst.x + offset_x, catcher_param.t2_dst.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t2_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t2_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从前处理到孵育的动作 */
    get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+pos_cnt, &catcher_param.t3_src, FLAG_POS_UNLOCK);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t2_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z + offset_z);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t2_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t3_src.x - catcher_param.cur.x) > abs((catcher_param.t3_src.y - catcher_param.cur.y) - (catcher_param.t3_src.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t3_src.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t3_src.y - catcher_param.cur.y) - (catcher_param.t3_src.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_src.x - catcher_param.cur.x + offset_x,
                                catcher_param.t3_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t3_src.x + offset_x, catcher_param.t3_src.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t3_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t3_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从孵育到光学混匀的动作 */
    get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t3_dst, FLAG_POS_UNLOCK);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t3_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t3_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t3_dst.x - catcher_param.cur.x) > abs((catcher_param.t3_dst.y - catcher_param.cur.y) - (catcher_param.t3_dst.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t3_dst.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t3_dst.y - catcher_param.cur.y) - (catcher_param.t3_dst.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_dst.x - catcher_param.cur.x + offset_x,
                                catcher_param.t3_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t3_dst.x + offset_x, catcher_param.t3_dst.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t3_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t3_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从光学混匀到光学检测的动作 */
    get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1 + (pos_cnt%8), &catcher_param.t4_src, FLAG_POS_UNLOCK);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t3_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t3_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    LOG("t4_src.x = %d, t4_src.y = %d, cur.x = %d, cur.y = %d\n", catcher_param.t4_src.x, catcher_param.t4_src.y,
        catcher_param.cur.x, catcher_param.cur.y);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t4_src.x - catcher_param.cur.x) > abs((catcher_param.t4_src.y - catcher_param.cur.y) - (catcher_param.t4_src.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t4_src.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t4_src.y - catcher_param.cur.y) - (catcher_param.t4_src.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_src.x - catcher_param.cur.x + offset_x,
                                catcher_param.t4_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t4_src.x + offset_x, catcher_param.t4_src.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t4_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t4_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从光学检测到丢杯的动作 */
    get_special_pos(MOVE_C_DETACH, 0, &catcher_param.t4_dst, FLAG_POS_UNLOCK);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t4_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t4_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t4_dst.x - catcher_param.cur.x) > abs((catcher_param.t4_dst.y - catcher_param.cur.y) - (catcher_param.t4_dst.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t4_dst.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t4_dst.y - catcher_param.cur.y) - (catcher_param.t4_dst.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_dst.x - catcher_param.cur.x + offset_x,
                                catcher_param.t4_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t4_dst.x + offset_x, catcher_param.t4_dst.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t4_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");

    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t4_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();

    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从丢杯到磁珠检测位的动作 */
    get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1 + (pos_cnt%4), &catcher_param.t5_src, FLAG_POS_UNLOCK);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t4_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");

    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t4_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();

    PRINT_FRAG_TIME("FRAG17");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t5_src.x - catcher_param.cur.x) > abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t5_src.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_src.x - catcher_param.cur.x + offset_x,
                                catcher_param.t5_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t5_src.x + offset_x, catcher_param.t5_src.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t5_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t5_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从磁珠检测位到前处理的动作 */
    get_special_pos(MOVE_C_PRE, 0, &catcher_param.t5_dst, FLAG_POS_UNLOCK);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t5_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t5_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t5_dst.x - catcher_param.cur.x) > abs((catcher_param.t5_dst.y - catcher_param.cur.y) - (catcher_param.t5_dst.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t5_dst.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t5_dst.y - catcher_param.cur.y) - (catcher_param.t5_dst.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_dst.x - catcher_param.cur.x + offset_x,
                                catcher_param.t5_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t5_dst.x + offset_x, catcher_param.t5_dst.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t5_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t5_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    /* 执行抓手从前处理到孵育的动作 */
    get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+pos_cnt, &catcher_param.t6_src, FLAG_POS_UNLOCK);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t5_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_CLOSE)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t5_dst.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (!check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.t6_src.x - catcher_param.cur.x) > abs((catcher_param.t6_src.y - catcher_param.cur.y) - (catcher_param.t6_src.x - catcher_param.cur.x))) {
        motor_x.step = abs(catcher_param.t6_src.x - catcher_param.cur.x);
    } else {
        motor_x.step = abs((catcher_param.t6_src.y - catcher_param.cur.y) - (catcher_param.t6_src.x - catcher_param.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t6_src.x - catcher_param.cur.x + offset_x,
                                catcher_param.t6_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.t6_src.x + offset_x, catcher_param.t6_src.y + offset_y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG14");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(catcher_param.t6_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t6_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t6_src.z + offset_z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (catcher_ctl(CATCHER_OPEN)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
        return 1;
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG16");
    FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
    motor_z.step = abs(catcher_param.t6_src.z);
    calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
        return 1;
    }
    set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (check_catcher_status()) {
        
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG17");
    pos_cnt++;
    LOG("catcher done...\n");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.cur.x) > abs(catcher_param.cur.y)) {
        motor_x.step = abs(catcher_param.cur.x);
    } else {
        motor_x.step = abs(catcher_param.cur.y);
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, catcher_param.cur.x, catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, 0, 0, catcher_param.cur.z);
    FAULT_CHECK_END();
    LOG("catcher test finish!\n");
    return 0;
}

static int reagent_table_aging_move_test()
{
    time_fragment_t *time_frag = reagent_time_frag_table_get();
    reag_table_cotl_t reag_table_cotl = {0};

    reag_table_cotl.table_move_type = TABLE_COMMON_MOVE;
    reag_table_cotl.req_pos_type = NEEDLE_S;
    reag_table_cotl.table_dest_pos_idx = POS_REAGENT_TABLE_I1 + pos_cnt;
    reag_table_cotl.move_time = time_frag[FRAG1].cost_time - 0.2;

    module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
    PRINT_FRAG_TIME("FRAG0");
    LOG("needle = %d, pos = %d\n", reag_table_cotl.req_pos_type, reag_table_cotl.table_dest_pos_idx);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    reagent_table_move_interface(&reag_table_cotl);
    FAULT_CHECK_END();
    usleep(200*1000);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    reag_table_cotl.table_move_type = TABLE_COMMON_RESET;
    reag_table_cotl.req_pos_type = NEEDLE_S;
    reag_table_cotl.table_dest_pos_idx = 1;
    reag_table_cotl.move_time = 0.6;
    reagent_table_move_interface(&reag_table_cotl);
    FAULT_CHECK_END();
    return 0;
}

static int mix_aging_move_test()
{
    cup_mix_data_set(MIX_POS_INCUBATION1, 0, 10000, 4000);
    cup_mix_data_set(MIX_POS_INCUBATION2, 0, 10000, 4000);
    cup_mix_data_set(MIX_POS_OPTICAL1, 0, 10000, 4000);
    cup_mix_start(MIX_POS_INCUBATION1);
    cup_mix_start(MIX_POS_INCUBATION2);
    cup_mix_start(MIX_POS_OPTICAL1);
    usleep(4500*1000);
    clear_one_cup_mix_data(MIX_POS_INCUBATION1);
    clear_one_cup_mix_data(MIX_POS_INCUBATION2);
    clear_one_cup_mix_data(MIX_POS_OPTICAL1);
    return 0;
}

int engineer_aging_test(engineer_debug_aging_t *aging_param)
{
    int cnt = 0;

    LOG("%d %d %d %d %d\n", aging_param->loop_cnt, aging_param->needle_s_enable, aging_param->needle_r2_enable, aging_param->catcher_enable, aging_param->reag_enable);
    if (reset_all_motors()) {
        return 1;
    }

    if (!aging_param->needle_s_enable && aging_param->catcher_enable) {
        if (needle_s_avoid_catcher()) {
            LOG("needle s avoid error!\n");
            return 1;
        }
    }
    pos_cnt = 0;
    while (1) {
        if (aging_param->loop_cnt != 0 && cnt >= aging_param->loop_cnt) {
            LOG("engineer aging test finish!\n");
            break;
        }
        if (engineer_aging_run_flag == 0) {
            LOG("engineer aging test stop!\n");
            break;
        }

        if (aging_param->needle_s_enable) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (needle_s_sp_aging_test(1, 1)) {
                LOG("needle s aging test error!\n");
                return 1;
            }
            FAULT_CHECK_END();
        }
        if (aging_param->needle_r2_enable) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (needle_r2_poweron_check(1, R2_NORMAL_MODE, R2_NORMAL_CLEAN_MODE)) {
                LOG("needle r2 aging test error!\n");
                return 1;
            }
            FAULT_CHECK_END();
        }
        if (aging_param->catcher_enable) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_aging_quality_test()) {
                LOG("catcher aging test error!\n");
                return 1;
            }
            FAULT_CHECK_END();
        }
        if (aging_param->reag_enable) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (reagent_table_aging_move_test()) {
                LOG("reagent table aging test error!\n");
                return 1;
            }
            FAULT_CHECK_END();
            if (!aging_param->catcher_enable) {
                pos_cnt++;
                if (pos_cnt > 35) {
                    pos_cnt = 0;
                }
            }
        }
        if (aging_param->mix_enable) {
            if (mix_aging_move_test()) {
                LOG("mix aging test error!\n");
                return 1;
            }
        }
        if (module_fault_stat_get() != MODULE_FAULT_NONE) {
            LOG("aging test detect error, break!\n");
            return 1;
        }
        LOG("=========cnt = %d============\n", ++cnt);
    }
    return 0;
}

