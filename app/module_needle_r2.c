#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#include "log.h"
#include "common.h"
#include "movement_config.h"
#include "module_common.h"
#include "module_monitor.h"
#include "module_cup_monitor.h"
#include "module_cup_mix.h"
#include "module_incubation.h"
#include "module_optical.h"
#include "module_magnetic_bead.h"
#include "module_needle_r2.h"
#include "module_reagent_table.h"
#include "module_liquid_detect.h"
#include "module_catcher_rs485.h"
#include "h3600_needle.h"

static void clear_pos_param(module_param_t *param)
{
    memset(&param->t1_src, 0, sizeof(pos_t));
    memset(&param->t1_dst, 0, sizeof(pos_t));
    memset(&param->t2_src, 0, sizeof(pos_t));
    memset(&param->t2_dst, 0, sizeof(pos_t));
    memset(&param->t3_src, 0, sizeof(pos_t));
    memset(&param->t3_dst, 0, sizeof(pos_t));
    memset(&param->t4_src, 0, sizeof(pos_t));
    memset(&param->t4_dst, 0, sizeof(pos_t));
    memset(&param->t5_src, 0, sizeof(pos_t));
    memset(&param->t5_dst, 0, sizeof(pos_t));
    memset(&param->t6_src, 0, sizeof(pos_t));
    memset(&param->t6_dst, 0, sizeof(pos_t));
}

static void clear_param(NEEDLE_R2_CMD_PARAM *param)
{
    clear_pos_param(&param->needle_r2_param);
    memset(&param->mix_stat, 0, sizeof(mix_status_t));
    memset(&param->mag_attr, 0, sizeof(struct magnectic_attr));
}

static void motor_attr_init(motor_time_sync_attr_t *motor_y, motor_time_sync_attr_t *motor_z, motor_time_sync_attr_t *motor_pump)
{
    motor_y->v0_speed = 100;
    motor_y->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].speed;
    motor_y->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].speed;
    motor_y->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].acc;
    motor_y->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Y].acc;

    motor_z->v0_speed = 100;
    motor_z->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].speed;
    motor_z->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].speed;
    motor_z->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].acc;
    motor_z->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_Z].acc;

    motor_pump->v0_speed = 1000;
    motor_pump->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].speed;
    motor_pump->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].speed;
    motor_pump->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].acc;
    motor_pump->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_R2_PUMP].acc;
}

static cup_pos_t needle_r2_get_reagent_pos(needle_pos_t needle_pos)
{
    cup_pos_t res_pos = POS_INVALID;
    if (needle_pos % 2) {
        res_pos = POS_REAGENT_TABLE_R2_IN;
    } else {
        res_pos = POS_REAGENT_TABLE_R2_OUT;
    }
    return res_pos;
}

static void r2_call_reagent_table_move(void *arg)
{
    time_fragment_t *time_frag = reagent_time_frag_table_get();
    reag_table_cotl_t reag_table_cotl = {0};

    reag_table_cotl.table_move_type = TABLE_COMMON_MOVE;
    reag_table_cotl.req_pos_type = NEEDLE_R2;
    reag_table_cotl.table_dest_pos_idx = (needle_pos_t)arg;
    reag_table_cotl.move_time = time_frag[FRAG3].cost_time;

    if (SAMPLE_QC == get_needle_s_qc_type() && NEEDLE_S_R1_DILU_SAMPLE == get_needle_s_cmd()) {
        /* FDP项目在试剂仓质控时占用的时间比常规时间要长 */
        module_sync_time(get_module_base_time(), time_frag[FRAG4].start_time);
    } else {
        module_sync_time(get_module_base_time(), time_frag[FRAG3].start_time);
    }
    PRINT_FRAG_TIME("FRAG3");

    if (reag_table_occupy_flag_get()) {
        LOG("reagent occupy get failed!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_GET_REAGENT);
    }
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    reag_table_occupy_flag_set(1);
    reagent_table_move_interface(&reag_table_cotl);
    FAULT_CHECK_END();
}

static void r2_call_mag_start(void *arg)
{
    NEEDLE_R2_CMD_PARAM *param = (NEEDLE_R2_CMD_PARAM *)arg;

    magnetic_detect_data_set(param->mag_index, &param->mag_attr, param->mix_stat.order_no, param->cuvette_serialno, param->cuvette_strno);
    magnetic_detect_start(param->mag_index);
}

static void *needle_r2_work_task(void *arg)
{
    struct list_head *needle_r2_cup_list = NULL;
    struct react_cup *pos = NULL, *n = NULL;
    int res = 0, r2_reuse_flag = 0;
    motor_time_sync_attr_t motor_y = {0}, motor_z = {0}, motor_pump = {0};
    unsigned char motor_needle_pump_y[2] = {MOTOR_NEEDLE_R2_Y, MOTOR_NEEDLE_R2_PUMP};
    NEEDLE_R2_CMD_PARAM param = {0};
    liquid_detect_arg_t needle_r2_liq_detect_arg = {0};
    time_fragment_t *time_frag = NULL;
    slip_temperate_ctl_t temperate_ctl = {0};
    float env_temp = 0.0;
    clean_type_t last_clean_type = 0;
    int last_clean_time = 0;

    motor_attr_init(&motor_y, &motor_z, &motor_pump);
    memset(&param.needle_r2_param.cur, 0, sizeof(pos_t));

    /*
        R2温控策略：
        1. 平时控温到TEMP_R2_NORMAL_GOAL摄氏度
        2. 液面探测前，关闭加热
        3. 液面探测后，开启加热
    */
    slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2, 100);
    slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL);
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);

    while (1) {
        module_monitor_wait();   /* 阻塞等待生产模块同步 */
        if (module_fault_stat_get() & MODULE_FAULT_LEVEL2) {
            module_response_cups(MODULE_NEEDLE_R2, 0);  /* 举手，且本模块停止工作 */
            continue;
        }
        LOG("needle r2 do something...\n");
        time_frag = r2_normal_time_frag_table_get();
        r2_reuse_flag = 0;
        clear_param(&param);
        reset_cup_list(NEEDLE_R2_CUP_LIST);
        res = module_request_cups(MODULE_NEEDLE_R2, &needle_r2_cup_list);
        needle_r2_cup_list_show();
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! needle s get cup info failed\n");
            module_response_cups(MODULE_NEEDLE_R2, 0);
            continue;
        }
        list_for_each_entry_safe(pos, n, needle_r2_cup_list, needle_r2_sibling) {
            if (pos->cup_pos >= POS_MAGNECTIC_WORK_1 && pos->cup_pos <= POS_OPTICAL_MIX) {
                if (pos->cup_test_attr.needle_r2.r_x_add_stat == CUP_STAT_UNUSED) {
                    r2_reuse_flag = 1;
                    pos->cup_test_attr.needle_r2.r_x_add_stat = CUP_STAT_USED;
                    param.mix_stat.order_no = pos->order_no;
                    param.r2_reagent_pos = pos->cup_test_attr.needle_r2.needle_pos;
                    param.cuvette_serialno = pos->cuvette_serialno;
                    param.take_ul = pos->cup_test_attr.needle_r2.take_ul;
                    param.curr_ul = pos->cup_test_attr.curr_ul + param.take_ul;
                    param.clean_type = pos->cup_test_attr.needle_r2.post_clean.type;
                    strncpy(param.cuvette_strno, pos->cuvette_strno, strlen(pos->cuvette_strno));
                    if (pos->cup_pos == POS_OPTICAL_MIX) {
                        param.mix_stat.enable = 1;
                        param.mix_stat.time = pos->cup_test_attr.test_cup_optical.optical_mix_time;
                        param.mix_stat.rate = pos->cup_test_attr.test_cup_optical.optical_mix_rate;
                        get_special_pos(MOVE_R2_MIX, pos->cup_pos, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
                    } else {
                        param.mag_attr.magnectic_enable = 1;
                        param.mag_attr.mag_beed_clot_percent = pos->cup_test_attr.test_cup_magnectic.mag_beed_clot_percent;
                        param.mag_attr.magnectic_power = pos->cup_test_attr.test_cup_magnectic.magnectic_power;
                        param.mag_attr.mag_beed_max_detect_seconds = pos->cup_test_attr.test_cup_magnectic.mag_beed_max_detect_seconds;
                        param.mag_attr.mag_beed_min_detect_seconds = pos->cup_test_attr.test_cup_magnectic.mag_beed_min_detect_seconds;
                        param.mag_index = MAGNETIC_POS0 + pos->cup_pos - POS_MAGNECTIC_WORK_1;
                        get_special_pos(MOVE_R2_MAGNETIC, pos->cup_pos, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
                    }
                    get_special_pos(MOVE_R2_CLEAN, pos->cup_pos, &param.needle_r2_param.t1_dst, FLAG_POS_NONE);
                    get_special_pos(MOVE_R2_REAGENT, POS_REAGENT_TABLE_R2_OUT, &param.needle_r2_param.t2_src, FLAG_POS_NONE);
                    break;
                }
            }
        }
        if (r2_reuse_flag == 1) {
            /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_GET_REAGENT);
            }

            slip_temperate_ctl_get(&temperate_ctl);
            env_temp = temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL4]/1000.0;
            LOG("temperate: env:%f, reag:%f, mag:%f, r2:%f\n", env_temp, temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL3]/1000.0, 
                temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL10]/1000.0, temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL11]/1000.0);
            slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);

            /* R2液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_r2_liq_detect_arg.hat_enable = ATTR_DISABLE;
            needle_r2_liq_detect_arg.needle = NEEDLE_TYPE_R2;
            needle_r2_liq_detect_arg.order_no = param.mix_stat.order_no;
            needle_r2_liq_detect_arg.s_cur_step = 0;
            needle_r2_liq_detect_arg.take_ul = param.take_ul + NEEDLE_R2_MORE;
            needle_r2_liq_detect_arg.reag_idx = param.r2_reagent_pos;
            param.needle_r2_param.cur.z = liquid_detect_start(needle_r2_liq_detect_arg);
            if (param.needle_r2_param.cur.z < EMAX) {
                LOG("liquid detect error! errno = %d\n", param.mix_stat.order_no);
                if (param.needle_r2_param.cur.z == ESWITCH) {
                    LOG("R2 switch pos!\n");
                    report_reagent_remain(param.r2_reagent_pos, 1, param.mix_stat.order_no);
                } else {
                    liq_det_r1_set_cup_detach(param.mix_stat.order_no);
                    /* 设置磁珠检测快速丢杯 */
                    param.mag_attr.mag_beed_min_detect_seconds = 3;
                    param.mag_attr.mag_beed_max_detect_seconds = 5;
                    LOG("report R2 add failed, R2 add go on.\n");
                    if (param.needle_r2_param.cur.z == EMAXSTEP) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_MAXSTEP);
                    } else if (param.needle_r2_param.cur.z == ENOTHING) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_DETECT);
                    } else if (param.needle_r2_param.cur.z == EARG) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_Z_EARG);
                    }
                }
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG0");
            module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);

            /* 若离上次洗针超过60s，则控温临时降低0.8度 */
            if (sys_uptime_sec() - last_clean_time > 60) {
                last_clean_type = NORMAL_CLEAN;
                LOG("change to lower r2 goal\n");
                slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL - 8);
            }

            /* 若离上次洗针为特殊清洗，且环境温度<15度时，则控温临时提高2.5度 */
            if (thrift_temp_get(THRIFT_ENVIRONMENTAREA) < 150) {
                if (last_clean_type == SPECIAL_CLEAN) {
                     slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL + 25);
                }
            }

            /* y = -0.004x2 + 1.1x + 30 */
            slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2,
                (uint8_t)(-0.004*param.take_ul*param.take_ul+1.1*param.take_ul + 30));
            slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_SEC_FULL_ON, 20000);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param.take_ul >= R2_ADD_MORE_FRAG) {
                needle_absorb_ul(NEEDLE_TYPE_R2, param.take_ul + NEEDLE_R2_MORE);
                report_reagent_supply_consume(REAGENT, param.r2_reagent_pos, (int)(param.take_ul + NEEDLE_R2_MORE));
            } else {
                needle_absorb_ul(NEEDLE_TYPE_R2, param.take_ul + NEEDLE_R2_LESS);
                report_reagent_supply_consume(REAGENT, param.r2_reagent_pos, (int)(param.take_ul + NEEDLE_R2_LESS));
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG1");
            module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
            motor_z.step = abs(param.needle_r2_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG2].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            } else {
                /* Z复位完成，检测是否有出液面信号 */
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                if (param.needle_r2_param.cur.z > EMAX && liquid_detect_result_get(NEEDLE_TYPE_R2) != LIQ_LEAVE_OUT) {
                    LOG("report R2 detect failed, R2 detach cup.\n");
                    liq_det_r1_set_cup_detach(param.mix_stat.order_no);
                    param.mag_attr.mag_beed_min_detect_seconds = 3;
                    param.mag_attr.mag_beed_max_detect_seconds = 5;
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_DETECT);
                }
                FAULT_CHECK_END();
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            slip_liquid_detect_rcd_set(NEEDLE_TYPE_R2, ATTR_DISABLE);
            reag_table_occupy_flag_set(0);
            PRINT_FRAG_TIME("FRAG2");
            module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 20 * 128, motor_pump.vmax_speed, motor_pump.max_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
            }
            FAULT_CHECK_END();
            /* R2加热 */
            PRINT_FRAG_TIME("FRAG3");
            module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG4].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_src.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG4");
            module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_calc_add_pos(NEEDLE_TYPE_R2, (int)param.curr_ul, &param.calc_pos);
            motor_z.step = abs(param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG5");
            module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param.mag_attr.magnectic_enable == 1) {
                needle_release_ul(NEEDLE_TYPE_R2, param.take_ul, 20 * 128);
            } else {
                needle_release_ul_ctl(NEEDLE_TYPE_R2, param.take_ul, time_frag[FRAG6].cost_time, 20 * 128);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG6");
            /* 启动磁珠检测 */
            if (param.mag_attr.magnectic_enable == 1) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                work_queue_add(r2_call_mag_start, (void *)&param);
                FAULT_CHECK_END();
            }
            module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);

            slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2, 100);
            slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL);
            slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param.needle_r2_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            FAULT_CHECK_END();
            /* 启动光学混匀 */
            if (param.mix_stat.enable == 1) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                /* 启动混匀 */
                cup_mix_data_set(MIX_POS_OPTICAL1, param.mix_stat.order_no, param.mix_stat.rate, param.mix_stat.time);
                cup_mix_start(MIX_POS_OPTICAL1);
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG7");
            module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG8].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_dst.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            leave_singal_send(LEAVE_R2_FRAG8);
            leave_singal_send(LEAVE_C_FRAG26);
            leave_singal_send(LEAVE_C_FRAG35);
            PRINT_FRAG_TIME("FRAG8");
            module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
            if (param.clean_type == SPECIAL_CLEAN) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z);
                FAULT_CHECK_END();
                PRINT_FRAG_TIME("FRAG9");
                module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                set_r2_clean_flag(R2_SPECIAL_CLEAN);
                r2_special_clean();
                set_r2_clean_flag(R2_CLEAN_NONE);
                FAULT_CHECK_END();
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
            } else {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
                FAULT_CHECK_END();
                PRINT_FRAG_TIME("FRAG9");
                module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                set_r2_clean_flag(R2_NORMAL_CLEAN);
                r2_normal_clean();
                set_r2_clean_flag(R2_CLEAN_NONE);
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG10");
            module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
            last_clean_type = param.clean_type;
            last_clean_time = sys_uptime_sec();

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param.needle_r2_param.cur.z) {
                motor_z.step = abs(param.needle_r2_param.cur.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG11");
            module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG12].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG12");
            module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t2_src.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG13].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_src.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG13");
            module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 5 * 128, motor_pump.vmax_speed, motor_pump.max_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
            }
            FAULT_CHECK_END();
        } else {
            leave_singal_send(LEAVE_R2_FRAG8);
            leave_singal_send(LEAVE_C_FRAG26);
            leave_singal_send(LEAVE_C_FRAG35);
        }
        
        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        leave_singal_wait(LEAVE_C_MIX_DET_POS_READY);
        reset_cup_list(NEEDLE_R2_CUP_LIST);
        res = module_request_cups(MODULE_NEEDLE_R2, &needle_r2_cup_list);
        needle_r2_cup_list_show();
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! needle s get cup info failed\n");
            module_response_cups(MODULE_NEEDLE_S, 0);
            continue;
        }
        r2_reuse_flag = 0;
        list_for_each_entry_safe(pos, n, needle_r2_cup_list, needle_r2_sibling) {
            if (pos->cup_pos >= POS_MAGNECTIC_WORK_1 && pos->cup_pos <= POS_OPTICAL_MIX) {
                if (pos->cup_test_attr.needle_r2.r_x_add_stat == CUP_STAT_UNUSED) {
                    r2_reuse_flag = 1;
                    param.r2_reagent_pos = pos->cup_test_attr.needle_r2.needle_pos;
                    get_special_pos(MOVE_R2_REAGENT, needle_r2_get_reagent_pos(param.r2_reagent_pos), &param.needle_r2_param.t2_dst, FLAG_POS_NONE);
                    break;
                }
            }
        }
        if (r2_reuse_flag == 1) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
            motor_pump.step = abs(R2_PUMP_AIR_STEP);
            motor_pump.acc = calc_motor_move_in_time(&motor_pump, time_frag[FRAG15].cost_time);
            motor_move_ctl_async(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc);
            motor_move_ctl_async(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, R2_PUMP_AIR_STEP, motor_pump.speed, motor_pump.acc);
            if (motors_move_timewait(motor_needle_pump_y, ARRAY_SIZE(motor_needle_pump_y), MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_dst.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG15");
            module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            r2_call_reagent_table_move((void*)param.r2_reagent_pos);
            FAULT_CHECK_END();
        } else {
            if (param.needle_r2_param.cur.y != 0) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_y.step = abs(param.needle_r2_param.cur.y);
                motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
                }
                set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
                FAULT_CHECK_END();
            }
        }

        LOG("needle r2 done...\n");
        module_response_cups(MODULE_NEEDLE_R2, 1);  /* 举手，表示模块已完成本周期工作 */
    }
    return NULL;
}

/* 单次称量模式 */
static int needle_r2_sig_add_test(int add_ul)
{
    liquid_detect_arg_t needle_r2_liq_detect_arg = {0};
    NEEDLE_R2_CMD_PARAM param = {0};
    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_y = {0}, motor_z = {0}, motor_pump = {0};
    unsigned char motor_needle_pump_y[2] = {MOTOR_NEEDLE_R2_Y, MOTOR_NEEDLE_R2_PUMP};

    motor_attr_init(&motor_y, &motor_z, &motor_pump);
    memset(&param.needle_r2_param.cur, 0, sizeof(pos_t));
    time_frag = r2_normal_time_frag_table_get();

    param.take_ul = (double)add_ul;
    if (add_ul < 100) {
        param.curr_ul = param.take_ul+150;
    } else {
        param.curr_ul = param.take_ul;
    }
    param.r2_reagent_pos = POS_REAGENT_TABLE_I2;
    reag_table_occupy_flag_set(0);

    motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_PUMP);
        return -1;
    }

    get_special_pos(MOVE_R2_REAGENT, needle_r2_get_reagent_pos(POS_REAGENT_TABLE_I2), &param.needle_r2_param.t2_dst, FLAG_POS_NONE);
    get_special_pos(MOVE_R2_MIX, 0, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
    get_special_pos(MOVE_R2_CLEAN, 0, &param.needle_r2_param.t1_dst, FLAG_POS_NONE);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_y.step = abs(param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y);
    motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
    motor_pump.step = abs(R2_PUMP_AIR_STEP);
    motor_pump.acc = calc_motor_move_in_time(&motor_pump, time_frag[FRAG15].cost_time);
    motor_move_ctl_async(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc);
    motor_move_ctl_async(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, R2_PUMP_AIR_STEP, motor_pump.speed, motor_pump.acc);
    if (motors_move_timewait(motor_needle_pump_y, ARRAY_SIZE(motor_needle_pump_y), MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
    }
    set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_dst.y, param.needle_r2_param.cur.z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG15");
    r2_call_reagent_table_move((void*)param.r2_reagent_pos);

    /* 检查试剂仓状态 */
    if (TABLE_IDLE != reag_table_stage_check()) {
        LOG("reagent move failed!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_GET_REAGENT);
    }
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);
    /* R2液面探测 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    needle_r2_liq_detect_arg.hat_enable = ATTR_DISABLE;
    needle_r2_liq_detect_arg.needle = NEEDLE_TYPE_R2;
    needle_r2_liq_detect_arg.order_no = param.mix_stat.order_no;
    needle_r2_liq_detect_arg.s_cur_step = 0;
    needle_r2_liq_detect_arg.take_ul = param.take_ul + NEEDLE_R2_MORE;
    needle_r2_liq_detect_arg.reag_idx = 1;
    needle_r2_liq_detect_arg.mode = DEBUG_DETECT_MODE;
    param.needle_r2_param.cur.z = liquid_detect_start(needle_r2_liq_detect_arg);
    if (param.needle_r2_param.cur.z < EMAX) {
        LOG("liquid detect error!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_MAXSTEP);
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG0");

    /* y = -0.004x2 + 1.1x + 30 */
    slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2,
    (uint8_t)(-0.004*param.take_ul*param.take_ul+1.1*param.take_ul + 30));
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_SEC_FULL_ON, 20000);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (param.take_ul >= R2_ADD_MORE_FRAG) {
        needle_absorb_ul(NEEDLE_TYPE_R2, param.take_ul + NEEDLE_R2_MORE);
    } else {
        needle_absorb_ul(NEEDLE_TYPE_R2, param.take_ul + NEEDLE_R2_LESS);
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG1");


    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(param.needle_r2_param.cur.z);
    motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG2].cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("liquid detect error!\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
    } else {
        /* Z复位完成，检测是否有出液面信号 */
        if (param.needle_r2_param.cur.z > EMAX && liquid_detect_result_get(NEEDLE_TYPE_R2) != LIQ_LEAVE_OUT) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_DETECT);
        }
    }
    set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
    FAULT_CHECK_END();
    slip_liquid_detect_rcd_set(NEEDLE_TYPE_R2, ATTR_DISABLE);
    reag_table_occupy_flag_set(0);
    PRINT_FRAG_TIME("FRAG2");
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 20 * 128, motor_pump.vmax_speed, motor_pump.max_acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    sleep(1);
    /* R2加热 */
    PRINT_FRAG_TIME("FRAG3");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_y.step = abs(param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y);
    motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG4].cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
    }
    set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_src.y, param.needle_r2_param.cur.z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG4");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    needle_calc_add_pos(NEEDLE_TYPE_R2, (int)param.curr_ul, &param.calc_pos);
    motor_z.step = abs(param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
    motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
    }
    set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG5");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    needle_release_ul(NEEDLE_TYPE_R2, param.take_ul, 20 * 128);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG6");
    slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2, 100);
    slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL);
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_z.step = abs(param.needle_r2_param.cur.z);
    motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
    }
    set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG7");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_y.step = abs(param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y);
    motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG8].cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
    }
    set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_dst.y, param.needle_r2_param.cur.z);
    FAULT_CHECK_END();
    leave_singal_send(LEAVE_R2_FRAG8);
    PRINT_FRAG_TIME("FRAG8");
    if (param.clean_type == SPECIAL_CLEAN) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        r2_special_clean();
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
    } else {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        r2_normal_clean();
    }
    PRINT_FRAG_TIME("FRAG10");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (param.needle_r2_param.cur.z) {
        motor_z.step = abs(param.needle_r2_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
    }
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG11");
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_y.step = abs(param.needle_r2_param.cur.y);
    motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG12].cost_time);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
    }
    set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
    FAULT_CHECK_END();
    PRINT_FRAG_TIME("FRAG12");

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    reag_table_cotl_t reag_table_cotl = {0};
    reag_table_cotl.table_move_type = TABLE_COMMON_RESET;
    reag_table_cotl.req_pos_type = NEEDLE_R2;
    reag_table_cotl.table_dest_pos_idx = 1;
    reag_table_cotl.move_time = 0.6;
    reagent_table_move_interface(&reag_table_cotl);
    FAULT_CHECK_END();

    return 0;
}

/* 称量模式 */
int needle_r2_add_test(int add_ul)
{
    if (reset_all_motors()) {
        return 1;
    }
    return needle_r2_sig_add_test(add_ul);
}

int needle_r2_muti_add_test(int add_ul)
{
    motor_time_sync_attr_t motor_x = {0}, motor_y = {0}, motor_z = {0};
    int calc_acc = 0, i = 0;
    time_fragment_t *time_frag = NULL;
    module_param_t catcher_param = {0};
    NEEDLE_R2_CMD_PARAM param = {0};

    catcher_rs485_init();
    motor_attr_init(&motor_x, &motor_y, &motor_z);

    if (reset_all_motors()) {
        return 1;
    }

    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return 1;
    }

    /* 样本针避让抓手 */
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_r2_param.t1_src.x, param.needle_r2_param.t1_src.y, 12000, 50000, MOTOR_DEFAULT_TIMEOUT, 0.0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        return 1;
    }
    FAULT_CHECK_END();

    time_frag = catcher_time_frag_table_get();

    for (i=0;i<10;i++) {
        if (i < 8) {
            get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
            get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
        } else {
            get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1+i-8, &catcher_param.t1_src, FLAG_POS_UNLOCK);
            get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
        }

        if (catcher_param.t1_src.x != catcher_param.cur.x || catcher_param.t1_src.y != catcher_param.cur.y) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t1_src.x - catcher_param.cur.x) > abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t1_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG0].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_src.x - catcher_param.cur.x,
                                        catcher_param.t1_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG0].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.t1_src.x, catcher_param.t1_src.y, 0);
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t1_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG1].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_src.z);
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_dst.x - catcher_param.cur.x,
                                    catcher_param.t1_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG4].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t1_dst.x, catcher_param.t1_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_dst.z);
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        /* 抓手避让 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_x.step = abs(8000);
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, -8000, 0, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x-8000, catcher_param.cur.y, catcher_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");

        needle_r2_sig_add_test(add_ul);


        if (i < 8) {
            get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1+i, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
            get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
        } else {
            get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1+i-8, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
            get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
        }

        if (catcher_param.t2_src.x != catcher_param.cur.x || catcher_param.t2_src.y != catcher_param.cur.y) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t2_src.x - catcher_param.cur.x) > abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t2_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG0].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_src.x - catcher_param.cur.x,
                                        catcher_param.t2_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG0].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.t2_src.x, catcher_param.t2_src.y, 0);
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t2_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG1].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_src.z);
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
        motor_z.step = abs(catcher_param.t2_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG3].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_status()) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(catcher_param.t2_dst.x - catcher_param.cur.x) > abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t2_dst.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG4].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_dst.x - catcher_param.cur.x,
                                    catcher_param.t2_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG4].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t2_dst.x, catcher_param.t2_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z);
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
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_status()) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (abs(catcher_param.cur.x) > abs(catcher_param.cur.y)) {
        motor_x.step = abs(catcher_param.cur.x);
    } else {
        motor_x.step = abs(catcher_param.cur.y);
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, catcher_param.cur.x, catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
        LOG("[%s:%d] x-y reset timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
        return 1;
    }
    set_pos(&catcher_param.cur, 0, 0, catcher_param.cur.z);
    FAULT_CHECK_END();

    return 0;
}

int needle_r2_poweron_check(int times, r2_aging_speed_mode_t mode, r2_aging_clean_mode_t clean_mode)
{
    NEEDLE_R2_CMD_PARAM param = {0};
    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_y = {0}, motor_z = {0}, motor_pump = {0};
    unsigned char motor_needle_pump_y[1] = {MOTOR_NEEDLE_R2_Y};
    time_fragment_t time_fast[20] = {0};

    motor_attr_init(&motor_y, &motor_z, &motor_pump);
    memset(&param.needle_r2_param.cur, 0, sizeof(pos_t));
    if (mode == R2_NORMAL_MODE) {
        time_frag = r2_normal_time_frag_table_get();
    } else {
        time_frag = time_fast;
    }

    param.take_ul = (double)50;
    param.curr_ul = param.take_ul+150;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_NEEDLE_R2_Z, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
        return 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_NEEDLE_R2_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Y);
        return 1;
    }
    FAULT_CHECK_END();
    int cnt = 0;
    while (1) {
        if (times != 0) {
            if (cnt >= times) {
                break;
            }
        }
        LOG("==================cnt = %d===================\n", cnt);
        get_special_pos(MOVE_R2_REAGENT, POS_REAGENT_TABLE_R2_IN, &param.needle_r2_param.t2_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_R2_MIX, 0, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_R2_CLEAN, 0, &param.needle_r2_param.t1_dst, FLAG_POS_NONE);

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_y.step = abs(param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y);
        motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
        motor_pump.step = abs(R2_PUMP_AIR_STEP);
        motor_pump.acc = calc_motor_move_in_time(&motor_pump, time_frag[FRAG15].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc);
        if (motors_move_timewait(motor_needle_pump_y, ARRAY_SIZE(motor_needle_pump_y), MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_dst.y, param.needle_r2_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        /* R2液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_dst.z-param.needle_r2_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t2_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG0");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        sleep(1);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_r2_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG2].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        sleep(1);
        /* R2加热 */
        PRINT_FRAG_TIME("FRAG3");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_y.step = abs(param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y);
        motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG4].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_src.y, param.needle_r2_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_R2, (int)param.curr_ul, &param.calc_pos);
        motor_z.step = abs(param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        sleep(1);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_r2_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_y.step = abs(param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y);
        motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_dst.y, param.needle_r2_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        if (clean_mode == R2_NORMAL_CLEAN_MODE) {
            r2_normal_clean();
        } else {
            sleep(2);
        }
        PRINT_FRAG_TIME("FRAG10");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param.needle_r2_param.cur.z) {
            motor_z.step = abs(param.needle_r2_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                return 1;
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_y.step = abs(param.needle_r2_param.cur.y);
        motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG12].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            return 1;
        }
        set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        sleep(1);
        cnt++;
    }

    return 0;
}

#if 1 /* 用于加热针吐液温度的测试 */
#define R2_HEAT_DEBUG_FILE "/tmp/r2_heat_debug"
static FILE *fd_r2 = NULL;

static void show_r2_temperate(const char* str)
{
    slip_temperate_ctl_t temperate_ctl = {0};

    slip_temperate_ctl_get(&temperate_ctl);
    printf("%s:%f\n",str, temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL8] / 1000.0);
}

static int get_r2_heat_last()
{
    int last_time = 0;
    char line_buff[128] = {0};

    if (fd_r2 == NULL) {
        if ((access(R2_HEAT_DEBUG_FILE, F_OK)) != -1) {
            fd_r2 = fopen(R2_HEAT_DEBUG_FILE, "r+");
        } else {
            fd_r2 = fopen(R2_HEAT_DEBUG_FILE, "w+");
        }
    }

    fseek(fd_r2, 0, SEEK_SET);
    fgets(line_buff, sizeof(line_buff) - 1, fd_r2);
    last_time = atoi(line_buff);
    printf("r2_last_time: %s, %d\n", line_buff, last_time);

    return last_time;
}

static void set_r2_heat_last()
{
    int last_time = 0;

    if (fd_r2 == NULL) {
        if ((access(R2_HEAT_DEBUG_FILE, F_OK)) != -1) {
            fd_r2 = fopen(R2_HEAT_DEBUG_FILE, "r+");
        } else {
            fd_r2 = fopen(R2_HEAT_DEBUG_FILE, "w+");
        }
    }

    last_time = sys_uptime_sec();
    fseek(fd_r2, 0, SEEK_SET);
    fprintf(fd_r2, "%d", last_time);
}

/*
int xitu: 0:正常吸吐  1:仅从杯子里 吸走试剂
float take_ul：吸取试剂量
int clean_type：0:普通清洗 1:特殊清洗
int loop_cnt：循环次数（最大6次）
int mode_flag：0：正常模式 1：普通-特殊交替 2:50-100UL交替 3:手动功率

int pow_xi：吸试剂的加热功率
int pow_nor：普通清洗的加热功率
int pow_spec：特殊清洗的加热功率

int predict_clean: R2是否预洗针
*/
void* needle_r2_heat_test(int xitu, float take_ul1, int clean_type, int loop_cnt, int mode_flag, int pow_xi, int pow_nor ,int pow_spec, int predict_clean)
{
    struct list_head *needle_r2_cup_list = NULL;
    struct react_cup *pos = NULL, *n = NULL;
    int res = 0, r2_reuse_flag = 0;
    motor_time_sync_attr_t motor_y = {0}, motor_z = {0}, motor_pump = {0};
    unsigned char motor_needle_pump_y[2] = {MOTOR_NEEDLE_R2_Y, MOTOR_NEEDLE_R2_PUMP};
//    unsigned char motor_needle_pump_z[1] = {MOTOR_NEEDLE_R2_Z};
    NEEDLE_R2_CMD_PARAM param = {0};
    liquid_detect_arg_t needle_r2_liq_detect_arg = {0};
    time_fragment_t *time_frag = NULL;
    slip_temperate_ctl_t temperate_ctl = {0};
    float env_temp = 0.0;
    clean_type_t last_clean_type = 0;
    int last_clean_time = 0;

    last_clean_time = last_clean_time;
    motor_attr_init(&motor_y, &motor_z, &motor_pump);
    memset(&param.needle_r2_param.cur, 0, sizeof(pos_t));

    /*
        R2温控策略：
        1. 平时控温到TEMP_R2_NORMAL_GOAL摄氏度
        2. 液面探测前，关闭加热
        3. 液面探测后，开启加热
    */
    slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2, 100);
    slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL);
    slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    slip_temperate_ctl_sensor_type_set(1);

    reset_all_motors();
    reinit_reagent_table_data();
    reag_table_occupy_flag_set(0);

    cup_pos_t cup_pos_xitu = POS_MAGNECTIC_WORK_1;
    int cnt = loop_cnt>MAGNETIC_CH_NUMBER ? MAGNETIC_CH_NUMBER : loop_cnt;
    param.r2_reagent_pos = POS_REAGENT_TABLE_I2;

    if (predict_clean == 1) {
        cnt++;
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_R2_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needle r2.pump timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
    }
    FAULT_CHECK_END();

    pump_cur_steps_set(0);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    valve_set(VALVE_SV4, ON);
    usleep(1000*500);
    motor_reset(MOTOR_CLEARER_PUMP, 1);
    if (motor_timedwait(MOTOR_CLEARER_PUMP, MOTOR_DEFAULT_TIMEOUT) != 0) {
       LOG("liquid_circuit: pump motor wait timeout!\n");
       FAULT_CHECK_DEAL(FAULT_LIQ_CIRCUIT_MODULE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_CLEARER_PUMP_TIMEOUT);
    }
    pump_5ml_absorb_clearer(MOTOR_CLEARER_PUMP, LIQ_S_PERF_VARIABLE, 1);
    usleep(1000*1000);
    valve_set(VALVE_SV4, OFF);
    FAULT_CHECK_END();

    if (xitu == 0 && predict_clean == 0) {
        time_frag = r2_normal_time_frag_table_get();
        if (mode_flag == 1) {
            if (cnt%2 == 0) {
                clean_type = NORMAL_CLEAN;
            } else {
                clean_type = SPECIAL_CLEAN;
            }
        } else if (mode_flag == 2) {
            if (cnt%2 == 0) {
                take_ul1 = 50.0;
            } else {
                take_ul1 = 100.0;
            }
        }

        react_cup_list_test(1, 1, 5007, POS_REAGENT_TABLE_I1, POS_REAGENT_TABLE_I2, clean_type, take_ul1, cup_pos_xitu);
        get_special_pos(MOVE_R2_REAGENT, needle_r2_get_reagent_pos(param.r2_reagent_pos), &param.needle_r2_param.t2_dst, FLAG_POS_NONE);

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_y.step = abs(param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y);
        motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
        motor_pump.step = abs(R2_PUMP_AIR_STEP);
        motor_pump.acc = calc_motor_move_in_time(&motor_pump, time_frag[FRAG15].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc);
        motor_move_ctl_async(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, R2_PUMP_AIR_STEP, motor_pump.speed, motor_pump.acc);
        if (motors_move_timewait(motor_needle_pump_y, ARRAY_SIZE(motor_needle_pump_y), MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
        }
        set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_dst.y, param.needle_r2_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
//        module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
        r2_call_reagent_table_move((void*)param.r2_reagent_pos);
    }

    sleep(5);

    while (cnt-- > 0) {
        if (module_fault_stat_get() != MODULE_FAULT_NONE) {
            LOG("detect fault, force break\n");
            return NULL;
        }

        /******************吸干试剂模式************************/
        /* 仅从杯子里 吸取试剂 */
        if (xitu == 1) {
            time_frag = r2_normal_time_frag_table_get();
            /* 到磁珠池 */
            get_special_pos(MOVE_R2_MAGNETIC, cup_pos_xitu, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
            get_special_pos(MOVE_R2_CLEAN, cup_pos_xitu, &param.needle_r2_param.t1_dst, FLAG_POS_NONE);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG4].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_src.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG4");
//            module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_calc_add_pos(NEEDLE_TYPE_R2, (int)param.curr_ul, &param.calc_pos);
            param.calc_pos.z += 330;//ken1
            motor_z.step = abs(param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG5");
//            module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_absorb_ul(NEEDLE_TYPE_R2, 200);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG6");
//            module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param.needle_r2_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG8].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_dst.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG8");
//            module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
            #if 0
            if (cnt == 0) { /* 最后一次才洗针 */
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
                FAULT_CHECK_END();
                PRINT_FRAG_TIME("FRAG9");
//                module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
                needle_release_ul(NEEDLE_TYPE_R2, 200, 0);
                r2_normal_clean();
            } else {
                needle_release_ul(NEEDLE_TYPE_R2, 200, 0);
                motor_move_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_RST, 0, 0, MOTOR_DEFAULT_TIMEOUT);
            }
            #else
            /* 每次洗针 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG9");
//                module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
            needle_release_ul(NEEDLE_TYPE_R2, 200, 0);
            r2_normal_clean();
            #endif

            PRINT_FRAG_TIME("FRAG10");
//            module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param.needle_r2_param.cur.z) {
                motor_z.step = abs(param.needle_r2_param.cur.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG11");
//            module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG12].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG12");
//            module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
            printf("xi shiji end\n");
            cup_pos_xitu++;
            set_r2_heat_last();
            last_clean_time = sys_uptime_sec();
            continue;
        }

        /******************正常模式************************/
        module_start_control(MODULE_CMD_START);
        module_monitor_start(NULL);
        leave_singal_send(LEAVE_C_MIX_DET_POS_READY);

        /******start*****/
//        module_monitor_wait();   /* 阻塞等待生产模块同步 */
//        if (module_fault_stat_get() & MODULE_FAULT_LEVEL2) {
//            module_response_cups(MODULE_NEEDLE_R2, 0);  /* 举手，且本模块停止工作 */
//            continue;
//        }

        LOG("needle r2 do something...\n");
        time_frag = r2_normal_time_frag_table_get();
        r2_reuse_flag = 0;
        clear_param(&param);
        reset_cup_list(NEEDLE_R2_CUP_LIST);
        res = module_request_cups(MODULE_NEEDLE_R2, &needle_r2_cup_list);
        needle_r2_cup_list_show();
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! needle s get cup info failed\n");
            module_response_cups(MODULE_NEEDLE_R2, 0);
            continue;
        }
        list_for_each_entry_safe(pos, n, needle_r2_cup_list, needle_r2_sibling) {
            if (pos->cup_pos >= POS_MAGNECTIC_WORK_1 && pos->cup_pos <= POS_OPTICAL_MIX) {
                if (pos->cup_test_attr.needle_r2.r_x_add_stat == CUP_STAT_UNUSED) {
                    r2_reuse_flag = 1;
                    pos->cup_test_attr.needle_r2.r_x_add_stat = CUP_STAT_USED;
                    param.mix_stat.order_no = pos->order_no;
                    param.r2_reagent_pos = pos->cup_test_attr.needle_r2.needle_pos;
                    param.cuvette_serialno = pos->cuvette_serialno;
                    param.take_ul = pos->cup_test_attr.needle_r2.take_ul;
                    param.curr_ul = pos->cup_test_attr.curr_ul + param.take_ul;
                    param.clean_type = pos->cup_test_attr.needle_r2.post_clean.type;
                    strncpy(param.cuvette_strno, pos->cuvette_strno, strlen(pos->cuvette_strno));
                    if (pos->cup_pos == POS_OPTICAL_MIX) {
                        param.mix_stat.enable = 1;
                        param.mix_stat.time = pos->cup_test_attr.test_cup_optical.optical_mix_time;
                        param.mix_stat.rate = pos->cup_test_attr.test_cup_optical.optical_mix_rate;
                        get_special_pos(MOVE_R2_MIX, pos->cup_pos, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
                    } else {
                        param.mag_attr.magnectic_enable = 1;
                        param.mag_attr.mag_beed_clot_percent = pos->cup_test_attr.test_cup_magnectic.mag_beed_clot_percent;
                        param.mag_attr.magnectic_power = pos->cup_test_attr.test_cup_magnectic.magnectic_power;
                        param.mag_attr.mag_beed_max_detect_seconds = pos->cup_test_attr.test_cup_magnectic.mag_beed_max_detect_seconds;
                        param.mag_attr.mag_beed_min_detect_seconds = pos->cup_test_attr.test_cup_magnectic.mag_beed_min_detect_seconds;
                        param.mag_index = MAGNETIC_POS0 + pos->cup_pos - POS_MAGNECTIC_WORK_1;
                        get_special_pos(MOVE_R2_MAGNETIC, pos->cup_pos, &param.needle_r2_param.t1_src, FLAG_POS_NONE);
                    }
                    get_special_pos(MOVE_R2_CLEAN, pos->cup_pos, &param.needle_r2_param.t1_dst, FLAG_POS_NONE);
                    get_special_pos(MOVE_R2_REAGENT, POS_REAGENT_TABLE_R2_OUT, &param.needle_r2_param.t2_src, FLAG_POS_NONE);
                    break;
                }
            }
        }
        if (r2_reuse_flag == 1) {
            /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_GET_REAGENT);
            }

            slip_temperate_ctl_get(&temperate_ctl);
            env_temp = temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL4]/1000.0;
            LOG("temperate: env:%f, reag:%f, mag:%f, r2:%f\n", env_temp, temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL3]/1000.0, 
                temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL10]/1000.0, temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL11]/1000.0);
            slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);

            /* R2液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
#if 1 //ken1
            needle_r2_liq_detect_arg.mode = DEBUG_DETECT_MODE;
            needle_r2_liq_detect_arg.hat_enable = ATTR_DISABLE;
            needle_r2_liq_detect_arg.needle = NEEDLE_TYPE_R2;
            needle_r2_liq_detect_arg.order_no = param.mix_stat.order_no;
            needle_r2_liq_detect_arg.s_cur_step = 0;
            needle_r2_liq_detect_arg.take_ul = param.take_ul + NEEDLE_R2_MORE;
            needle_r2_liq_detect_arg.reag_idx = param.r2_reagent_pos;
            param.needle_r2_param.cur.z = liquid_detect_start(needle_r2_liq_detect_arg);
            if (param.needle_r2_param.cur.z < EMAX) {
                LOG("liquid detect error! errno = %d\n", param.mix_stat.order_no);
                if (param.needle_r2_param.cur.z == ESWITCH) {
                    LOG("R2 switch pos!\n");
                    report_reagent_remain(param.r2_reagent_pos, 1, param.mix_stat.order_no);
                } else {
                    liq_det_r1_set_cup_detach(param.mix_stat.order_no);
                    /* 设置磁珠检测快速丢杯 */
                    param.mag_attr.mag_beed_min_detect_seconds = 3;
                    param.mag_attr.mag_beed_max_detect_seconds = 5;
                    LOG("report R2 add failed, R2 add go on.\n");
                    if (param.needle_r2_param.cur.z == EMAXSTEP) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_MAXSTEP);
                    } else if (param.needle_r2_param.cur.z == ENOTHING) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_DETECT);
                    } else if (param.needle_r2_param.cur.z == EARG) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_Z_EARG);
                    }
                }
            }
#else
            //10600
            motor_move_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, 8000, motor_z.speed, MOTOR_DEFAULT_TIMEOUT);
            motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, 2630, 3000, 10000, MOTOR_DEFAULT_TIMEOUT);
            set_pos(&param.needle_r2_param.cur, param.needle_r2_param.cur.x, param.needle_r2_param.cur.y, 10600);
#endif

            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG0");
            module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);

            show_r2_temperate("R2 xi start temp");
            /* 若离上次洗针超过60s，则控温临时降低0.8度 */
            if (sys_uptime_sec() - get_r2_heat_last() > 60) {
//            if (sys_uptime_sec() - last_clean_time > 60) {
                last_clean_type = NORMAL_CLEAN;
                LOG("change to lower r2 goal\n");
                slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL - 8);
            }

            /* 若离上次洗针为特殊清洗，且环境温度<15度时，则控温临时提高2.5度 */
            if (thrift_temp_get(THRIFT_ENVIRONMENTAREA) < 150) {
                if (last_clean_type == SPECIAL_CLEAN) {
                    slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL + 25);
                }
            }

            /* y = -0.004x2 + 1.1x + 30 */
            slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2,
                (uint8_t)(-0.004*param.take_ul*param.take_ul+1.1*param.take_ul + 30));
            slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_SEC_FULL_ON, 20000);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_absorb_ul(NEEDLE_TYPE_R2, param.take_ul + NEEDLE_R2_MORE);
            report_reagent_supply_consume(REAGENT, param.r2_reagent_pos, (int)(param.take_ul + NEEDLE_R2_MORE));
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG1");
            module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
            motor_z.step = abs(param.needle_r2_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG2].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            } else {
                /* Z复位完成，检测是否有出液面信号 */
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                if (param.needle_r2_param.cur.z > EMAX && liquid_detect_result_get(NEEDLE_TYPE_R2) != LIQ_LEAVE_OUT) {
                    LOG("report R2 detect failed, R2 detach cup.\n");
                    liq_det_r1_set_cup_detach(param.mix_stat.order_no);
                    param.mag_attr.mag_beed_min_detect_seconds = 3;
                    param.mag_attr.mag_beed_max_detect_seconds = 5;
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_R2_DETECT);
                }
                FAULT_CHECK_END();
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            slip_liquid_detect_rcd_set(NEEDLE_TYPE_R2, ATTR_DISABLE);
            reag_table_occupy_flag_set(0);
            PRINT_FRAG_TIME("FRAG2");
            module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 20 * 128, motor_pump.vmax_speed, motor_pump.max_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
            }
            FAULT_CHECK_END();
            /* R2加热 */
            PRINT_FRAG_TIME("FRAG3");
            module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG4].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_src.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG4");
            module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_calc_add_pos(NEEDLE_TYPE_R2, (int)param.curr_ul, &param.calc_pos);
            motor_z.step = abs(param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_src.z-param.needle_r2_param.cur.z+param.calc_pos.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG5");
            module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
            show_r2_temperate("R2 tu start temp");

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param.mag_attr.magnectic_enable == 1) {
                needle_release_ul(NEEDLE_TYPE_R2, param.take_ul, 20 * 128);
            } else {
                needle_release_ul_ctl(NEEDLE_TYPE_R2, param.take_ul, time_frag[FRAG6].cost_time, 20 * 128);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG6");
            /* 启动磁珠检测 */
            if (param.mag_attr.magnectic_enable == 1) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                work_queue_add(r2_call_mag_start, (void *)&param);
                FAULT_CHECK_END();
            }
            module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);

            slip_temperate_ctl_maxpower_set(TEMP_NEEDLE_R2, 100);
            slip_temperate_ctl_goal_set(TEMP_NEEDLE_R2, TEMP_R2_NORMAL_GOAL);
            slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param.needle_r2_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            FAULT_CHECK_END();
            /* 启动光学混匀 */
            if (param.mix_stat.enable == 1) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                /* 启动混匀 */
                cup_mix_data_set(MIX_POS_OPTICAL1, param.mix_stat.order_no, param.mix_stat.rate, param.mix_stat.time);
                cup_mix_start(MIX_POS_OPTICAL1);
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG7");
            module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG8].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t1_dst.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            leave_singal_send(LEAVE_R2_FRAG8);
            leave_singal_send(LEAVE_C_FRAG26);
            leave_singal_send(LEAVE_C_FRAG35);
            PRINT_FRAG_TIME("FRAG8");
            module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);

            show_r2_temperate("R2 clean start temp");

            if (param.clean_type == SPECIAL_CLEAN) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z);
                FAULT_CHECK_END();
                PRINT_FRAG_TIME("FRAG9");
                module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                set_r2_clean_flag(R2_SPECIAL_CLEAN);
                r2_special_clean();
                set_r2_clean_flag(R2_CLEAN_NONE);
                FAULT_CHECK_END();
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
            } else {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG9].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t1_dst.z - param.needle_r2_param.cur.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, param.needle_r2_param.t1_dst.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP);
                FAULT_CHECK_END();
                PRINT_FRAG_TIME("FRAG9");
                module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                set_r2_clean_flag(R2_NORMAL_CLEAN);
                r2_normal_clean();
                set_r2_clean_flag(R2_CLEAN_NONE);
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG10");
            module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
            last_clean_type = param.clean_type;
            last_clean_time = sys_uptime_sec();

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param.needle_r2_param.cur.z) {
                motor_z.step = abs(param.needle_r2_param.cur.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Z);
                }
                set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.cur.y, 0);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG11");
            module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG12].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG12");
            module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t2_src.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG13].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_src.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_src.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG13");
            module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, 5 * 128, motor_pump.vmax_speed, motor_pump.max_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_PUMP);
            }
            FAULT_CHECK_END();
        } else {
            leave_singal_send(LEAVE_R2_FRAG8);
            leave_singal_send(LEAVE_C_FRAG26);
            leave_singal_send(LEAVE_C_FRAG35);
        }

        #if 1
        if (mode_flag == 1) {
            if (cnt%2 == 0) {
                clean_type = NORMAL_CLEAN;
            } else {
                clean_type = SPECIAL_CLEAN;
            }
        } else if (mode_flag == 2) {
            if (cnt%2 == 0) {
                take_ul1 = 50.0;
            } else {
                take_ul1 = 100.0;
            }
        }

        if (predict_clean == 0) {
            cup_pos_xitu++;
            react_cup_list_test(1, 1, 5007, POS_REAGENT_TABLE_I1, POS_REAGENT_TABLE_I2, clean_type, take_ul1, cup_pos_xitu);
        } else if (predict_clean == 1){
            react_cup_list_test(1, 1, 5007, POS_REAGENT_TABLE_I1, POS_REAGENT_TABLE_I2, clean_type, take_ul1, cup_pos_xitu);
            cup_pos_xitu++;
        }
        #endif

        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        leave_singal_wait(LEAVE_C_MIX_DET_POS_READY);
        reset_cup_list(NEEDLE_R2_CUP_LIST);
        res = module_request_cups(MODULE_NEEDLE_R2, &needle_r2_cup_list);
        needle_r2_cup_list_show();
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! needle s get cup info failed\n");
            module_response_cups(MODULE_NEEDLE_S, 0);
            continue;
        }
        r2_reuse_flag = 0;
        list_for_each_entry_safe(pos, n, needle_r2_cup_list, needle_r2_sibling) {
            if (pos->cup_pos >= POS_MAGNECTIC_WORK_1 && pos->cup_pos <= POS_OPTICAL_MIX) {
                if (pos->cup_test_attr.needle_r2.r_x_add_stat == CUP_STAT_UNUSED) {
                    r2_reuse_flag = 1;
                    param.r2_reagent_pos = pos->cup_test_attr.needle_r2.needle_pos;
                    get_special_pos(MOVE_R2_REAGENT, needle_r2_get_reagent_pos(param.r2_reagent_pos), &param.needle_r2_param.t2_dst, FLAG_POS_NONE);
                    break;
                }
            }
        }
        if (r2_reuse_flag == 1) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_y.step = abs(param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y);
            motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
            motor_pump.step = abs(R2_PUMP_AIR_STEP);
            motor_pump.acc = calc_motor_move_in_time(&motor_pump, time_frag[FRAG15].cost_time);
            motor_move_ctl_async(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_MOVE_STEP, param.needle_r2_param.t2_dst.y - param.needle_r2_param.cur.y, motor_y.speed, motor_y.acc);
            motor_move_ctl_async(MOTOR_NEEDLE_R2_PUMP, CMD_MOTOR_MOVE_STEP, R2_PUMP_AIR_STEP, motor_pump.speed, motor_pump.acc);
            if (motors_move_timewait(motor_needle_pump_y, ARRAY_SIZE(motor_needle_pump_y), MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
            }
            set_pos(&param.needle_r2_param.cur, 0, param.needle_r2_param.t2_dst.y, param.needle_r2_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG15");
            module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            r2_call_reagent_table_move((void*)param.r2_reagent_pos);
            FAULT_CHECK_END();
        } else {
            if (param.needle_r2_param.cur.y != 0) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_y.step = abs(param.needle_r2_param.cur.y);
                motor_y.acc = calc_motor_move_in_time(&motor_y, time_frag[FRAG15].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_R2_Y, CMD_MOTOR_RST, 0, motor_y.speed, motor_y.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
                }
                set_pos(&param.needle_r2_param.cur, 0, 0, param.needle_r2_param.cur.z);
                FAULT_CHECK_END();
            }
        }

        LOG("needle r2 done...\n");
        set_r2_heat_last();
        module_response_cups(MODULE_NEEDLE_R2, 1);  /* 举手，表示模块已完成本周期工作 */
        module_sync_time(get_module_base_time(), 20000-50);
        show_r2_temperate("=======circle end temp");
    }

    /* 等待试剂盘慢速复位完成 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_REAGENT_TABLE, 1);    /* 试剂盘上电复位耗时最长大约35s */
    if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT*2)) {/* 试剂盘上电复位耗时最长大约35s */
        LOG("reset reagent table timeout.\n");
        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_REAGENT_TABLE);
        return NULL;
    }
    FAULT_CHECK_END();

    return NULL;
}
#endif

/* 初始化试剂针R2模块 */
int needle_r2_init(void)
{
    pthread_t needle_r2_main_thread;

    if (0 != pthread_create(&needle_r2_main_thread, NULL, needle_r2_work_task, NULL)) {
        LOG("needle r2 work thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


