#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#include "log.h"
#include "common.h"
#include "movement_config.h"
#include "module_common.h"
#include "module_catcher.h"
#include "module_monitor.h"
#include "module_cup_monitor.h"
#include "module_cup_mix.h"
#include "module_magnetic_bead.h"
#include "module_optical.h"
#include "module_incubation.h"
#include "module_needle_s.h"
#include "module_catcher_rs485.h"
#include "module_catcher_motor.h"
#include "module_cuvette_supply.h"

static uint32_t dilu_cup_num = 0;
static pre_use_t catcher_pre_use_stat = NOT_USE;
static uint8_t clear_catcher_pos = 0;

static int get_exist_dilu_cup(uint32_t num)
{
    if (num == dilu_cup_num || dilu_cup_num == 0) {
        return 0;
    } else {
        return 1;
    }
}

static void set_exist_dilu_cup(uint32_t num)
{
    dilu_cup_num = num;
}

static void set_pre_use_stat(pre_use_t stat)
{
    catcher_pre_use_stat = stat;
}

/* 判定有无测试杯未完成加样的，如果有则此周期不进杯 */
static pre_use_t get_pre_use_stat(void)
{
    return catcher_pre_use_stat;
}

void clear_exist_dilu_cup(void)
{
    set_exist_dilu_cup(0);
}

static struct react_cup *find_cup_by_module_pos(int mod_in_pos, module_pos_t mod_pos)
{
    struct list_head *module_pos_cup_list = NULL;
    struct react_cup *pos = NULL, *n = NULL;
    cup_pos_t cup_pos = POS_INVALID;

    reset_cup_list(CATCHER_CUP_LIST);
    if (0 != module_request_cups(MODULE_CATCHER, &module_pos_cup_list)) {
        LOG("ERROR! catcher get cup info failed\n");
        return NULL;
    }

    switch (mod_pos) {
    case MAGNETIC_CUP_POS:
        cup_pos = POS_MAGNECTIC_WORK_1;
        break;
    case OPTICAL_CUP_POS:
        cup_pos = POS_OPTICAL_WORK_1;
        break;
    case INCUBATION_CUP_POS:
        cup_pos = POS_INCUBATION_WORK_1;
        break;
    default:
        cup_pos = POS_INVALID;
        break;
    }
    list_for_each_entry_safe(pos, n, module_pos_cup_list, catcher_sibling) {
        if (pos->cup_pos == (cup_pos + mod_in_pos)) {
            return pos;
        }
    }

    return NULL;
}

static void clear_pos_param(module_param_t *param)
{
    pos_t init_pos = {0};

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

    if (clear_catcher_pos == 1) {
        get_special_pos(MOVE_C_PRE, 0, &init_pos, FLAG_POS_UNLOCK);
        set_pos(&param->cur, init_pos.x, init_pos.y, 0);
        clear_catcher_pos = 0;
    }
}

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

static void *catcher_work_task(void *arg)
{
    struct list_head *catcher_cup_list = NULL;
    struct react_cup *pos = NULL, *n = NULL, *init_pos = NULL;
    int res = 0, catcher_reuse_flag = 0, catcher_initcup_flag = 0, catcher_mix_flag = 0;
    module_param_t catcher_param = {0};
    cup_pos_t tmp_cup_pos = POS_INVALID;
    magnetic_pos_t mag_pos = MAGNETIC_POS_INVALID;
    optical_pos_t optical_pos = OPTICAL_POS_INVALID;
    incubation_pos_t incubation_pos = INCUBATION_POS_INVALID;

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, i = 0;

    motor_attr_init(&motor_x, &motor_z);
    get_special_pos(MOVE_C_PRE, 0, &catcher_param.t1_src, FLAG_POS_UNLOCK);
    set_pos(&catcher_param.cur, catcher_param.t1_src.x, catcher_param.t1_src.y, 0);

    while (1) {
        module_monitor_wait();   /* 阻塞等待生产模块同步 */
        if (module_fault_stat_get() & MODULE_FAULT_LEVEL2) {
            module_response_cups(MODULE_CATCHER, 0);  /* 举手，且本模块停止工作 */
            continue;
        }
        LOG("catcher do something...\n");
        time_frag = catcher_time_frag_table_get();
        clear_pos_param(&catcher_param);
        set_pre_use_stat(NOT_USE);
        reset_cup_list(CATCHER_CUP_LIST);
        res = module_request_cups(MODULE_CATCHER, &catcher_cup_list);
        catcher_cup_list_show();
        catcher_reuse_flag = 0;
        catcher_mix_flag = 0;
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! catcher get cup info failed\n");
            module_response_cups(MODULE_CATCHER, 0);
            continue;
        }
        list_for_each_entry_safe(pos, n, catcher_cup_list, catcher_sibling) {
            if (pos->cup_pos == POS_PRE_PROCESSOR) {
                if (catcher_reuse_flag == 0) {
                    if (pos->cup_active == CUP_INACTIVE) {
                        /* 加样失败需要丢弃的反应杯（液面探测失败） */
                        tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t1_dst, FLAG_POS_LOCK);
                        if (POS_INVALID != tmp_cup_pos) {
                            get_special_pos(MOVE_C_PRE, 0, &catcher_param.t1_src, FLAG_POS_UNLOCK);
                            LOG("catcher time 1 do [pre -> incubation] (cup inactive)!\n");
                            catcher_reuse_flag = 1;
                            pos->cup_pos = tmp_cup_pos;
                            pos->cup_type = DILU_CUP;
                        } else {
                            set_pre_use_stat(IN_USE);
                            LOG("catcher time 1 do nothing, because incubation is full!\n");
                        }
                        break;
                    } else if (pos->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_USED &&
                        pos->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat == CUP_STAT_USED &&
                        pos->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat == CUP_STAT_USED) {
                        tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t1_dst, FLAG_POS_LOCK);
                        if (POS_INVALID != tmp_cup_pos) {
                            get_special_pos(MOVE_C_PRE, 0, &catcher_param.t1_src, FLAG_POS_UNLOCK);
                            LOG("catcher time 1 do [pre -> incubation]!\n");
                            catcher_reuse_flag = 1;
                            catcher_mix_flag = 1;
                            pos->cup_pos = tmp_cup_pos;
                        } else {
                            set_pre_use_stat(IN_USE);
                            LOG("catcher time 1 do nothing, because incubation is full!\n");
                        }
                        break;
                    } else {
                        set_pre_use_stat(IN_USE);
                        LOG("catcher time 1 do nothing, because sample or r1 not done!\n");
                    }
                }
            } else if (pos->cup_pos >= POS_PRE_PROCESSOR_MIX1 && pos->cup_pos <= POS_PRE_PROCESSOR_MIX2) {
                if (catcher_reuse_flag == 0) {
                    if (pos->cup_active == CUP_INACTIVE) {
                        /* 加样失败需要丢弃的反应杯（液面探测失败） */
                        tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t1_dst, FLAG_POS_LOCK);
                        if (POS_INVALID != tmp_cup_pos) {
                            get_special_pos(MOVE_C_MIX, pos->cup_pos, &catcher_param.t1_src, FLAG_POS_UNLOCK);
                            LOG("catcher time 1 do [pre_mix -> incubation] (cup inactive)!\n");
                            catcher_reuse_flag = 1;
                            cup_mix_stop(pos_cup_trans_mix(pos->cup_pos));
                            while (CUP_MIX_FINISH != cup_mix_state_get(pos_cup_trans_mix(pos->cup_pos))) {
                                usleep(10*1000);
                                if (module_fault_stat_get() & MODULE_FAULT_LEVEL2) {
                                    LOG("detect fault, wait mix force break.\n");
                                    break;
                                }
                            }
                            clear_one_cup_mix_data(pos_cup_trans_mix(pos->cup_pos));
                            if (pos->cup_type == DILU_CUP) {
                                set_exist_dilu_cup(0);
                            }
                            pos->cup_pos = tmp_cup_pos;
                            pos->cup_type = DILU_CUP;
                        } else {
                            set_pre_use_stat(IN_USE);
                            LOG("catcher time 1 do nothing, because incubation is full!\n");
                        }
                        break;
                    } else if (pos->cup_type == TEST_CUP && pos->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_USED &&
                        pos->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat == CUP_STAT_USED &&
                        pos->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat == CUP_STAT_USED) {
                        if (CUP_MIX_FINISH == cup_mix_state_get(pos_cup_trans_mix(pos->cup_pos))) {
                            tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t1_dst, FLAG_POS_LOCK);
                            if (POS_INVALID != tmp_cup_pos) {
                                get_special_pos(MOVE_C_MIX, pos->cup_pos, &catcher_param.t1_src, FLAG_POS_UNLOCK);
                                LOG("catcher time 1 do [pre_mix -> incubation]!\n");
                                catcher_reuse_flag = 1;
                                clear_one_cup_mix_data(pos_cup_trans_mix(pos->cup_pos));
                                pos->cup_pos = tmp_cup_pos;
                            } else {
                                set_pre_use_stat(IN_USE);
                                LOG("catcher time 1 do nothing, because incubation is full!\n");
                            }
                            break;
                        }
                    } else if (pos->cup_type == DILU_CUP) {
                        if (pos->cup_dilu_attr.trans_state == CUP_STAT_USED) {
                            tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t1_dst, FLAG_POS_LOCK);
                            if (POS_INVALID != tmp_cup_pos) {
                                get_special_pos(MOVE_C_MIX, pos->cup_pos, &catcher_param.t1_src, FLAG_POS_UNLOCK);
                                LOG("catcher time 1 do [pre_mix -> incubation] (dilu cup)!\n");
                                catcher_reuse_flag = 1;
                                clear_one_cup_mix_data(pos_cup_trans_mix(pos->cup_pos));
                                pos->cup_pos = tmp_cup_pos;
                                set_exist_dilu_cup(0);
                            } else {
                                set_pre_use_stat(IN_USE);
                                LOG("catcher time 1 do nothing, because incubation is full!\n");
                            }
                            break;
                        } else {
                            LOG("catcher time 1 do nothing, because dilu cup not done!\n");
                        }
                    } else {
                        LOG("catcher time 1 do nothing, because sample or r1 not done!\n");
                    }
                }
            }
        }
        /* 提前判定第二周期，因为样本针需要提前获取杯子信息 */
        set_slot_del_lock();    /* 与删除槽订单互斥，以免进杯与删除槽订单冲突 */
        reset_cup_list(CATCHER_CUP_LIST);
        res = module_request_cups(MODULE_CATCHER, &catcher_cup_list);
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! catcher get cup info failed\n");
            set_slot_del_unlock();
            continue;
        }
        catcher_initcup_flag = 0;
        list_for_each_entry_safe(init_pos, n, catcher_cup_list, catcher_sibling) {
            /* 前处理只能有一对稀释分杯存在 && 磁珠加样位不能有反应杯 && 样本针本周期不能为穿刺暂存周期 */
            if (init_pos->cup_pos == POS_CUVETTE_SUPPLY_INIT && 0 == get_exist_dilu_cup(init_pos->order_no) &&
                NOT_USE == get_pre_use_stat() && SAMPLER_ADD_START == module_sampler_add_get()) {
                if (REACTION_CUP_NONE != cuvette_supply_get(NORMAL_GET_MODE)) {
                    get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
                    if (init_pos->cup_test_attr.test_cup_magnectic.magnectic_enable == 1 && init_pos->cup_type == TEST_CUP) {
                        get_special_pos(MOVE_C_PRE, 0, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
                        init_pos->cup_pos = POS_PRE_PROCESSOR;
                    } else {
                        tmp_cup_pos = get_valid_pos(MOVE_C_MIX, &catcher_param.t2_dst, FLAG_POS_LOCK);
                        if (tmp_cup_pos != POS_INVALID) {
                            init_pos->cup_pos = tmp_cup_pos;
                            if (init_pos->cup_type == DILU_CUP) {
                                set_exist_dilu_cup(init_pos->order_no);
                            }
                        } else {
                            LOG("get pre mix failed, pre mix is full!\n");
                            break;
                        }
                    }
                    catcher_initcup_flag = 1;
                } else {
                    LOG("get cuvette supply failed!\n");
                }
                break;
            }
        }
        catcher_cup_list_show();
        set_slot_del_unlock();  /* 解锁 */
        leave_singal_send(LEAVE_C_GET_INIT_POS_READY);
        if (catcher_reuse_flag == 1) {
            /* 执行抓手从前处理抓杯到孵育位的动作 */
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
                }
                set_pos(&catcher_param.cur, catcher_param.t1_src.x, catcher_param.t1_src.y, 0);
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG0");
            module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t1_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG1].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_src.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG1");
            module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_CLOSE)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG2");
            module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t1_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG3].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (!check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG3");
            module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
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
            }
            set_pos(&catcher_param.cur, catcher_param.t1_dst.x, catcher_param.t1_dst.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG4");
            module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);

            if (catcher_mix_flag == 1 && 0 == get_throughput_mode()) {
                /* 抓手辅助磁珠混匀 */
                for (i=0;i<4;i++) {
                    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, -1000, 0, 48000, 238000, MOTOR_DEFAULT_TIMEOUT, 0.1)) {
                        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                    }
                    FAULT_CHECK_END();
                    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 1000, 0, 48000, 238000, MOTOR_DEFAULT_TIMEOUT, 0.1)) {
                        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                    }
                    FAULT_CHECK_END();
                }
            }

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t1_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_dst.z + CATCHER_Z_MOVE_COMP_STEP, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG5");
            module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_OPEN)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG6");
            module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t1_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG7");
            module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);

            incubation_data_set(INCUBATION_POS0 + (pos->cup_pos - POS_INCUBATION_WORK_1), pos->order_no,
                pos->cup_test_attr.test_cup_incubation.incubation_time, pos->cup_type);
            incubation_start(INCUBATION_POS0 + (pos->cup_pos - POS_INCUBATION_WORK_1));
            if (pos->cup_active == CUP_INACTIVE || pos->cup_delay_active == CUP_INACTIVE) {
                incubation_inactive_by_order(pos->order_no);
            }
        } else {
            LOG("catcher time 1 do nothing, because there is no cup!\n");
        }
        
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        /* 进杯在第一周期已判定,这里判定一下本周期是不是穿刺周期,如果是穿刺周期则不进杯并回退杯状态 */
        if (NEEDLE_S_SP == get_needle_s_cmd() && catcher_initcup_flag == 1) {
            LOG("cancel time2 [cup_init -> pre], needle s in sp time!\n");
            get_special_pos(MOVE_C_MIX, init_pos->cup_pos, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
            init_pos->cup_pos = POS_CUVETTE_SUPPLY_INIT;
            catcher_initcup_flag = 0;
            if (init_pos->cup_type == DILU_CUP) {
                set_exist_dilu_cup(0);
            }
        } else {
            /* 判定前处理上有两个测试杯（一个常规位一个混匀位）且都未完成加样，则这一周期不能进杯，这种情况一般是双R1试剂项目 */
            reset_cup_list(CATCHER_CUP_LIST);
            res = module_request_cups(MODULE_CATCHER, &catcher_cup_list);
            if (res == 1) {
                /* 获取反应杯信息错误 */
                LOG("ERROR! catcher get cup info failed\n");
                continue;
            }
            catcher_reuse_flag = 0;
            i = 0;
            list_for_each_entry_safe(pos, n, catcher_cup_list, catcher_sibling) {
                if (pos->cup_pos == POS_PRE_PROCESSOR && pos->cup_type == TEST_CUP) {
                    if (pos->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED) {
                        catcher_reuse_flag = 1;
                    }
                } else if (pos->cup_pos == POS_PRE_PROCESSOR_MIX1 || pos->cup_pos == POS_PRE_PROCESSOR_MIX2) {
                    if (pos->cup_type == TEST_CUP) {
                        if (pos->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED) {
                            i++;
                        }
                    }
                }
                if ((catcher_reuse_flag + i) >= 2) {
                    /* 回退反应杯状态 */
                    LOG("cancel time2 [cup_init -> pre], there are 2 pre & pre_mix test cups not trans!\n");
                    get_special_pos(MOVE_C_MIX, init_pos->cup_pos, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
                    init_pos->cup_pos = POS_CUVETTE_SUPPLY_INIT;
                    catcher_initcup_flag = 0;
                }
            }
        }
        if (catcher_initcup_flag == 1) {
            LOG("catcher time 2 do [cup_init -> pre]!\n");
            if (get_throughput_mode() == 0) { /* !PT360 */
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
                }
                set_pos(&catcher_param.cur, 0, 0, catcher_param.cur.z);
                FAULT_CHECK_END();
            }
            module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
            PRINT_FRAG_TIME("FRAG9");
            /* 执行抓手从进杯盘抓杯到前处理的动作 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t2_src.x - catcher_param.cur.x) > abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t2_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_src.x - catcher_param.cur.x,
                                        catcher_param.t2_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t2_src.x, catcher_param.t2_src.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG10");
            module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t2_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_src.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG11");
            module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_CLOSE)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CUVETTE_SUPPLY_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG12");
            module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
            FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
            motor_z.step = abs(catcher_param.t2_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG13].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            report_reagent_supply_consume(CUP, 1, 1);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (!check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CUVETTE_SUPPLY_PE);
            }
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            cuvette_supply_notify();
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG13");
            module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t2_dst.x - catcher_param.cur.x) > abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t2_dst.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_dst.x - catcher_param.cur.x,
                                        catcher_param.t2_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t2_dst.x, catcher_param.t2_dst.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG14");
            module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t2_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z + CATCHER_Z_MOVE_COMP_STEP, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG15");
            module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_OPEN)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG16");
            module_sync_time(get_module_base_time(), time_frag[FRAG16].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t2_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG17");
            module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        } else {
            LOG("s_cmd = %d\n", get_needle_s_cmd());
            cuvette_supply_notify();
            if (NEEDLE_S_DILU3_MIX == get_needle_s_cmd() || NEEDLE_S_DILU3_DILU_MIX == get_needle_s_cmd()) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_x.step = abs(catcher_param.cur.y);
                if (catcher_param.cur.y != 0) {
                    calc_acc = calc_motor_move_in_time(&motor_x, 0.4);
                    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -catcher_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, 0.4)) {
                        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                    }
                }
                set_pos(&catcher_param.cur, catcher_param.cur.x, 0, catcher_param.cur.z);
                FAULT_CHECK_END();
                leave_singal_send(LEAVE_C_FRAG8);
            } else {
                LOG("catcher time 2 do nothing, because there is no new cup!\n");
            }
            module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        }

        mag_pos = magnetic_detect_output_get();
        optical_pos = optical_detect_output_get();
        incubation_pos = incubation_timeout_output_get();
        if (MAGNETIC_POS_INVALID != mag_pos) {
            pos = find_cup_by_module_pos(mag_pos, MAGNETIC_CUP_POS);
        } else if (OPTICAL_POS_INVALID != optical_pos) {
            pos = find_cup_by_module_pos(optical_pos, OPTICAL_CUP_POS);
        } else if (INCUBATION_POS_INVALID != incubation_pos) {
            pos = find_cup_by_module_pos(incubation_pos, INCUBATION_CUP_POS);
        } else {
            pos = NULL;
        }

        if (pos != NULL) {
            /* 执行抓手从磁珠/光学/孵育丢杯的动作 */
            LOG("catcher time 3 do [mag/opti/incubation -> detach]!\n");
            if (pos->cup_pos >= POS_MAGNECTIC_WORK_1 && pos->cup_pos <= POS_MAGNECTIC_WORK_4) {
                get_special_pos(MOVE_C_MAGNETIC, pos->cup_pos, &catcher_param.t3_src, FLAG_POS_UNLOCK);
                clear_one_magnetic_data(mag_pos);
            } else if (pos->cup_pos >= POS_OPTICAL_WORK_1 && pos->cup_pos <= POS_OPTICAL_WORK_8) {
                get_special_pos(MOVE_C_OPTICAL, pos->cup_pos, &catcher_param.t3_src, FLAG_POS_UNLOCK);
                clear_one_optical_data(optical_pos);
            } else if (pos->cup_pos >= POS_INCUBATION_WORK_1 && pos->cup_pos <= POS_INCUBATION_WORK_30) {
                get_special_pos(MOVE_C_INCUBATION, pos->cup_pos, &catcher_param.t3_src, FLAG_POS_UNLOCK);
                clear_one_incubation_data(incubation_pos);
            }
            get_special_pos(MOVE_C_DETACH, 0, &catcher_param.t3_dst, FLAG_POS_UNLOCK);
            pos->cup_pos = POS_REACT_CUP_DETACH;

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t3_src.x - catcher_param.cur.x) > abs((catcher_param.t3_src.y - catcher_param.cur.y) - (catcher_param.t3_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t3_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t3_src.y - catcher_param.cur.y) - (catcher_param.t3_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG18].cost_time);
            motor_move_dual_ctl_async(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_src.x - catcher_param.cur.x,
                                    catcher_param.t3_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, time_frag[FRAG18].cost_time);
            FAULT_CHECK_END();
            leave_singal_send(LEAVE_C_FRAG18);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t3_src.x, catcher_param.t3_src.y, 0);
            FAULT_CHECK_END();
            leave_singal_wait(LEAVE_R2_FRAG8);
            PRINT_FRAG_TIME("FRAG18");
            module_sync_time(get_module_base_time(), time_frag[FRAG18].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t3_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG19].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG19");
            module_sync_time(get_module_base_time(), time_frag[FRAG19].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_CLOSE)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_TRASH_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG20");
            module_sync_time(get_module_base_time(), time_frag[FRAG20].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t3_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG21].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (!check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_TRASH_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG21");
            module_sync_time(get_module_base_time(), time_frag[FRAG21].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t3_dst.x - catcher_param.cur.x) > abs((catcher_param.t3_dst.y - catcher_param.cur.y) - (catcher_param.t3_dst.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t3_dst.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t3_dst.y - catcher_param.cur.y) - (catcher_param.t3_dst.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG22].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_dst.x - catcher_param.cur.x,
                                        catcher_param.t3_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG22].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t3_dst.x, catcher_param.t3_dst.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG22");
            module_sync_time(get_module_base_time(), time_frag[FRAG22].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t3_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG23].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG23");
            module_sync_time(get_module_base_time(), time_frag[FRAG23].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_OPEN)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_TRASH_PE);
            }
            /* 上报 丢杯计数 */
            report_reagent_supply_consume(WASTE_CUP, 1, 1);
            FAULT_CHECK_END();
            if (get_throughput_mode() == 0) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
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
                set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y-1400, catcher_param.t3_dst.z-200);
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG24");
            module_sync_time(get_module_base_time(), time_frag[FRAG24].end_time);
            if (get_throughput_mode() == 0) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(catcher_param.t3_dst.z);
                calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG25].cost_time);
                if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
                }
                set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
                FAULT_CHECK_END();
            } else {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_z.step = abs(catcher_param.t3_dst.z);
                calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG25].cost_time);
                motor_move_ctl_async(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc);
                motor_move_dual_ctl_async(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -1400, 50000, 200000, 0.01);
                if (motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
                    LOG("[%s:%d] z reset timeout\n", __func__, __LINE__);
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
                }
                if (motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT)) {
                    LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                }
                set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y-1400, 0);
                FAULT_CHECK_END();
            }
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_TRASH_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG25");
            module_sync_time(get_module_base_time(), time_frag[FRAG25].end_time);
        } else {
            if (NEEDLE_S_NONE != get_needle_s_cmd()) {
                /* 抓手让位给样本针 */
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                motor_x.step = abs(catcher_param.cur.y);
                calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG18].cost_time);
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG18].cost_time)) {
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Y);
                }
                set_pos(&catcher_param.cur, catcher_param.cur.x, 0, catcher_param.cur.z);
                FAULT_CHECK_END();
                LOG("catcher time 3 do avoid, because needle s will move!\n");
            } else {
                LOG("catcher time 3 do nothing, because there is no cup to detach!\n");
            }
            leave_singal_send(LEAVE_C_FRAG18);
            leave_singal_wait(LEAVE_R2_FRAG8);
            module_sync_time(get_module_base_time(), time_frag[FRAG25].end_time);
        }

        reset_cup_list(CATCHER_CUP_LIST);
        res = module_request_cups(MODULE_CATCHER, &catcher_cup_list);
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! catcher get cup info failed\n");
            continue;
        }
        catcher_cup_list_show();
        catcher_reuse_flag = 0;
        list_for_each_entry_safe(pos, n, catcher_cup_list, catcher_sibling) {
            if (pos->cup_pos == POS_OPTICAL_MIX) {
                if (CUP_MIX_FINISH == cup_mix_state_get(MIX_POS_OPTICAL1)) {
                    get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t4_src, FLAG_POS_UNLOCK);
                    /* 光学检测固定通道的项目AT-III */
                    if (pos->cup_test_attr.test_cup_optical.main_wave == 405 && pos->cup_test_attr.test_cup_optical.optical_main_seconds <= 60) {
                        tmp_cup_pos = get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_8, &catcher_param.t4_dst, FLAG_POS_LOCK);
                        if (tmp_cup_pos == POS_INVALID) {
                            LOG("AT-III pos in use, check another one!\n");
                            tmp_cup_pos = get_valid_pos(MOVE_C_OPTICAL, &catcher_param.t4_dst, FLAG_POS_LOCK);
                        }
                    } else {
                        tmp_cup_pos = get_valid_pos(MOVE_C_OPTICAL, &catcher_param.t4_dst, FLAG_POS_LOCK);
                    }
                    if (tmp_cup_pos != POS_INVALID) {
                        pos->cup_pos = tmp_cup_pos;
                        catcher_reuse_flag = 1;
                        clear_one_cup_mix_data(MIX_POS_OPTICAL1);
                    } else {
                        LOG("get optical pos failed, optical work pos is full!\n");
                    }
                    break;
                }
            }
        }
        if (catcher_reuse_flag == 1) {
            LOG("catcher time 4 do [opti mix -> opti detect]!\n");
            leave_singal_wait(LEAVE_C_FRAG26);
            /* 执行抓手从光学混匀位到光学检测位的动作 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t4_src.x - catcher_param.cur.x) > abs((catcher_param.t4_src.y - catcher_param.cur.y) - (catcher_param.t4_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t4_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t4_src.y - catcher_param.cur.y) - (catcher_param.t4_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG26].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_src.x - catcher_param.cur.x,
                                        catcher_param.t4_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG26].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t4_src.x, catcher_param.t4_src.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG26");
            module_sync_time(get_module_base_time(), time_frag[FRAG26].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t4_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG27].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG27");
            module_sync_time(get_module_base_time(), time_frag[FRAG27].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_CLOSE)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_OPTI_MIX_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG28");
            module_sync_time(get_module_base_time(), time_frag[FRAG28].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t4_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG29].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (!check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_OPTI_MIX_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG29");
            module_sync_time(get_module_base_time(), time_frag[FRAG29].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t4_dst.x - catcher_param.cur.x) > abs((catcher_param.t4_dst.y - catcher_param.cur.y) - (catcher_param.t4_dst.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t4_dst.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t4_dst.y - catcher_param.cur.y) - (catcher_param.t4_dst.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG30].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_dst.x - catcher_param.cur.x,
                                        catcher_param.t4_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG30].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t4_dst.x, catcher_param.t4_dst.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG30");
            module_sync_time(get_module_base_time(), time_frag[FRAG30].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t4_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG31].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG31");
            module_sync_time(get_module_base_time(), time_frag[FRAG31].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_OPEN)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_OPTI_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG32");
            module_sync_time(get_module_base_time(), time_frag[FRAG32].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t4_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG33].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_OPTI_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG33");
            module_sync_time(get_module_base_time(), time_frag[FRAG33].end_time);
            if (pos->cup_active == CUP_INACTIVE) {
                /* 设置光学检测位快速丢杯 */
                pos->cup_test_attr.test_cup_optical.optical_main_seconds = 5;
                pos->cup_test_attr.test_cup_optical.optical_vice_seconds = 5;
            }
            optical_detect_data_set(OPTICAL_POS0 + (pos->cup_pos - POS_OPTICAL_WORK_1), &pos->cup_test_attr.test_cup_optical, 
                pos->order_no, pos->cuvette_serialno, pos->cuvette_strno);
            optical_detect_start(OPTICAL_POS0 + (pos->cup_pos - POS_OPTICAL_WORK_1));
        } else {
            LOG("catcher time 4 do nothing, because there is no cup to optical!\n");
            leave_singal_wait(LEAVE_C_FRAG26);
        }
        PRINT_FRAG_TIME("FRAG34");
        module_sync_time(get_module_base_time(), time_frag[FRAG34].end_time);

        incubation_pos = incubation_finish_output_get();
        if (INCUBATION_POS_INVALID != incubation_pos) {
            pos = find_cup_by_module_pos(incubation_pos, INCUBATION_CUP_POS);
        } else {
            pos = NULL;
        }
        catcher_reuse_flag = 0;
        if (pos != NULL) {
            /* 执行抓手从孵育到光学混匀/磁珠的动作 */
            get_special_pos(MOVE_C_INCUBATION, pos->cup_pos, &catcher_param.t5_src, FLAG_POS_UNLOCK);
            if (pos->cup_test_attr.test_cup_optical.optical_enable == ATTR_ENABLE && CUP_MIX_UNUSED == cup_mix_state_get(MIX_POS_OPTICAL1)) {
                /* 光学检测固定通道的项目AT-III */
                if (pos->cup_test_attr.test_cup_optical.main_wave == 405 && pos->cup_test_attr.test_cup_optical.optical_main_seconds <= 60) {
                    if (POS_INVALID != get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_8, &catcher_param.t5_dst, FLAG_POS_NONE)) {
                        get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t5_dst, FLAG_POS_LOCK);
                        pos->cup_pos = POS_OPTICAL_MIX;
                        catcher_reuse_flag = 1;
                        clear_one_incubation_data(incubation_pos);
                    } else {
                        LOG("AT-III pos in use! check another one!\n");
                        if (POS_INVALID != get_valid_pos(MOVE_C_OPTICAL, &catcher_param.t5_dst, FLAG_POS_NONE)) {
                            get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t5_dst, FLAG_POS_LOCK);
                            pos->cup_pos = POS_OPTICAL_MIX;
                            catcher_reuse_flag = 1;
                            clear_one_incubation_data(incubation_pos);
                        } else {
                            /* 回退孵育池的占用状态 */
                            get_special_pos(MOVE_C_INCUBATION, pos->cup_pos, &catcher_param.t5_src, FLAG_POS_LOCK);
                            LOG("get optical mix pos failed, optical pos is full!\n");
                        }
                    }
                } else {
                    if (POS_INVALID != get_valid_pos(MOVE_C_OPTICAL, &catcher_param.t5_dst, FLAG_POS_NONE)) {
                        get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t5_dst, FLAG_POS_LOCK);
                        pos->cup_pos = POS_OPTICAL_MIX;
                        catcher_reuse_flag = 1;
                        clear_one_incubation_data(incubation_pos);
                    } else {
                        /* 回退孵育池的占用状态 */
                        get_special_pos(MOVE_C_INCUBATION, pos->cup_pos, &catcher_param.t5_src, FLAG_POS_LOCK);
                        LOG("get optical mix pos failed, at3 pos is full!\n");
                    }
                }
            } else if (pos->cup_test_attr.test_cup_magnectic.magnectic_enable == ATTR_ENABLE) {
                tmp_cup_pos = get_valid_pos(MOVE_C_MAGNETIC, &catcher_param.t5_dst, FLAG_POS_LOCK);
                if (tmp_cup_pos != POS_INVALID) {
                    pos->cup_pos = tmp_cup_pos;
                    catcher_reuse_flag = 1;
                    clear_one_incubation_data(incubation_pos);
                } else {
                    /* 回退孵育池的占用状态 */
                    get_special_pos(MOVE_C_INCUBATION, pos->cup_pos, &catcher_param.t5_src, FLAG_POS_LOCK);
                    LOG("get magnetic pos failed, magnetic pos is full!\n");
                }
            } else {
                /* 回退孵育池的占用状态 */
                get_special_pos(MOVE_C_INCUBATION, pos->cup_pos, &catcher_param.t5_src, FLAG_POS_LOCK);
                LOG("Error! not optical or magnetic !\n");
            }
        }
        leave_singal_send(LEAVE_C_MIX_DET_POS_READY);
        if (catcher_reuse_flag == 1) {
            LOG("catcher time 5 do [incubation -> mag detect/opti mix]!\n");
            leave_singal_wait(LEAVE_C_FRAG35);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t5_src.x - catcher_param.cur.x) > abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t5_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG35].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_src.x - catcher_param.cur.x,
                                        catcher_param.t5_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG35].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t5_src.x, catcher_param.t5_src.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG35");
            module_sync_time(get_module_base_time(), time_frag[FRAG35].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t5_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG36].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG36");
            module_sync_time(get_module_base_time(), time_frag[FRAG36].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_CLOSE)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG37");
            module_sync_time(get_module_base_time(), time_frag[FRAG37].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t5_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG38].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (!check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG38");
            module_sync_time(get_module_base_time(), time_frag[FRAG38].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t5_dst.x - catcher_param.cur.x) > abs((catcher_param.t5_dst.y - catcher_param.cur.y) - (catcher_param.t5_dst.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t5_dst.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t5_dst.y - catcher_param.cur.y) - (catcher_param.t5_dst.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG39].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_dst.x - catcher_param.cur.x,
                                        catcher_param.t5_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG39].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t5_dst.x, catcher_param.t5_dst.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG39");
            module_sync_time(get_module_base_time(), time_frag[FRAG39].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t5_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG40].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z + CATCHER_Z_MOVE_COMP_STEP, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG40");
            module_sync_time(get_module_base_time(), time_frag[FRAG40].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_OPEN)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_OPTI_MIX_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG41");
            module_sync_time(get_module_base_time(), time_frag[FRAG41].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t5_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG42].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_OPTI_MIX_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG42");
            module_sync_time(get_module_base_time(), time_frag[FRAG42].end_time);
        } else {
            LOG("catcher time 5 do nothing, because there is no incubation cup ready!\n");
            leave_singal_wait(LEAVE_C_FRAG35);
        }

        leave_singal_wait(LEAVE_S_CLEAN);
        get_special_pos(MOVE_C_PRE, 0, &catcher_param.t1_src, FLAG_POS_UNLOCK);
        if (catcher_param.t1_src.x == catcher_param.cur.x && catcher_param.t1_src.y == catcher_param.cur.y) {
            LOG("catcher now not move!\n");
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t1_src.x - catcher_param.cur.x) > abs(abs(catcher_param.t1_src.y - catcher_param.cur.y) - abs(catcher_param.t1_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t1_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG43].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_src.x - catcher_param.cur.x,
                                        catcher_param.t1_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG43].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t1_src.x, catcher_param.t1_src.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG43");
            module_sync_time(get_module_base_time(), time_frag[FRAG43].end_time);
        }

        leave_singal_wait(LEAVE_S_DILU_TRANS_READY);
        if (get_throughput_mode() == 0) {/* 防止判定过快未识别到混匀完成的反应杯 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            module_sync_time(get_module_base_time(), 20000-200);
            FAULT_CHECK_END();
        }
        /* 光学磁珠切换MIX1/MIX2→孵育 */
        reset_cup_list(CATCHER_CUP_LIST);
        res = module_request_cups(MODULE_CATCHER, &catcher_cup_list);
        catcher_cup_list_show();
        catcher_reuse_flag = 0;
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! catcher get cup info failed\n");
            module_response_cups(MODULE_CATCHER, 0);
            continue;
        }
        list_for_each_entry_safe(pos, n, catcher_cup_list, catcher_sibling) {
        /* 本时间片只能进前处理有两个测试杯都ready的情况下的混匀杯或丢稀释杯 */
            if (pos->cup_pos >= POS_PRE_PROCESSOR_MIX1 && pos->cup_pos <= POS_PRE_PROCESSOR_MIX2) {
                if (pos->cup_type == TEST_CUP && pos->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_USED &&
                    pos->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat == CUP_STAT_USED &&
                    pos->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat == CUP_STAT_USED) {
                    if (CUP_MIX_FINISH == cup_mix_state_get(pos_cup_trans_mix(pos->cup_pos))) {
                        init_pos = pos;
                        catcher_reuse_flag++;
                    }
                } else if (pos->cup_type == DILU_CUP && pos->cup_dilu_attr.trans_state == CUP_STAT_USED) {
                    tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t6_dst, FLAG_POS_LOCK);
                    if (POS_INVALID != tmp_cup_pos) {
                        get_special_pos(MOVE_C_MIX, pos->cup_pos, &catcher_param.t6_src, FLAG_POS_UNLOCK);
                        LOG("catcher time 6 do [pre_mix[dilu] -> incubation]!\n");
                        clear_one_cup_mix_data(pos_cup_trans_mix(pos->cup_pos));
                        pos->cup_pos = tmp_cup_pos;
                        set_exist_dilu_cup(0);
                        catcher_reuse_flag = 3;
                    } else {
                        LOG("catcher time 6 do nothing, because incubation is full!\n");
                    }
                    break;
                } else {
                    LOG("catcher time 6 do nothing, because sample or r1 not done!\n");
                }
            } else if (pos->cup_pos == POS_PRE_PROCESSOR) {
                catcher_reuse_flag++;
            }
        }
        LOG("for extend time, catcher_reuse_flag = %d\n", catcher_reuse_flag);
        if (catcher_reuse_flag == 2) {
            pos = init_pos;
            tmp_cup_pos = get_valid_pos(MOVE_C_INCUBATION, &catcher_param.t6_dst, FLAG_POS_LOCK);
            if (POS_INVALID != tmp_cup_pos) {
                get_special_pos(MOVE_C_MIX, pos->cup_pos, &catcher_param.t6_src, FLAG_POS_UNLOCK);
                LOG("catcher time 6 do [pre_mix -> incubation]!\n");
                clear_one_cup_mix_data(pos_cup_trans_mix(pos->cup_pos));
                pos->cup_pos = tmp_cup_pos;
                catcher_reuse_flag = 3;
            } else {
                LOG("catcher time 6 do nothing, because incubation is full or mix not ready!\n");
            }
        }
        if (catcher_reuse_flag == 3) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t6_src.x - catcher_param.cur.x) > abs(abs(catcher_param.t6_src.y - catcher_param.cur.y) - abs(catcher_param.t6_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t6_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t6_src.y - catcher_param.cur.y) - (catcher_param.t6_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG44].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t6_src.x - catcher_param.cur.x,
                                        catcher_param.t6_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG44].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }

            set_pos(&catcher_param.cur, catcher_param.t6_src.x, catcher_param.t6_src.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG44");
            module_sync_time(get_module_base_time(), time_frag[FRAG44].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t6_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG45].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t6_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t6_src.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG45");
            module_sync_time(get_module_base_time(), time_frag[FRAG45].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_CLOSE)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_MIX_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG46");
            module_sync_time(get_module_base_time(), time_frag[FRAG46].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t6_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG47].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (!check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_MIX_PE);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG47");
            module_sync_time(get_module_base_time(), time_frag[FRAG47].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t6_dst.x - catcher_param.cur.x) > abs((catcher_param.t6_dst.y - catcher_param.cur.y) - (catcher_param.t6_dst.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t6_dst.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t6_dst.y - catcher_param.cur.y) - (catcher_param.t6_dst.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG48].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t6_dst.x - catcher_param.cur.x,
                                        catcher_param.t6_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG48].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.t6_dst.x, catcher_param.t6_dst.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG48");
            module_sync_time(get_module_base_time(), time_frag[FRAG48].end_time);

            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(catcher_param.t6_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG49].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t6_dst.z + CATCHER_Z_MOVE_COMP_STEP, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t6_dst.z + CATCHER_Z_MOVE_COMP_STEP);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG49");
            module_sync_time(get_module_base_time(), time_frag[FRAG49].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (catcher_ctl(CATCHER_OPEN)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG50");
            module_sync_time(get_module_base_time(), time_frag[FRAG50].end_time);
            FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
            motor_z.step = abs(catcher_param.t6_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG51].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (check_catcher_status()) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE_R);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG51");
            module_sync_time(get_module_base_time(), time_frag[FRAG51].end_time);

            incubation_data_set(INCUBATION_POS0 + (pos->cup_pos - POS_INCUBATION_WORK_1), pos->order_no,
                pos->cup_test_attr.test_cup_incubation.incubation_time, pos->cup_type);
            incubation_start(INCUBATION_POS0 + (pos->cup_pos - POS_INCUBATION_WORK_1));
            if (pos->cup_active == CUP_INACTIVE || pos->cup_delay_active == CUP_INACTIVE) {
                incubation_inactive_by_order(pos->order_no);
            }

            get_special_pos(MOVE_C_PRE, 0, &catcher_param.t1_src, FLAG_POS_UNLOCK);
            if (catcher_param.t1_src.x == catcher_param.cur.x && catcher_param.t1_src.y == catcher_param.cur.y) {
                LOG("catcher now not move!\n");
            } else {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                if (abs(catcher_param.t1_src.x - catcher_param.cur.x) > abs(abs(catcher_param.t1_src.y - catcher_param.cur.y) - abs(catcher_param.t1_src.x - catcher_param.cur.x))) {
                    motor_x.step = abs(catcher_param.t1_src.x - catcher_param.cur.x);
                } else {
                    motor_x.step = abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x));
                }
                calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG52].cost_time);
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_src.x - catcher_param.cur.x,
                                            catcher_param.t1_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG52].cost_time)) {
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                }
                set_pos(&catcher_param.cur, catcher_param.t1_src.x, catcher_param.t1_src.y, 0);
                FAULT_CHECK_END();
                PRINT_FRAG_TIME("FRAG52");
                module_sync_time(get_module_base_time(), time_frag[FRAG52].end_time);
            }
        } else {
            LOG("catcher time 6 do nothing, because there is no pre mix cup ready!\n");
        }

        if (get_throughput_mode() == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            module_sync_time(get_module_base_time(), 20000-200);
            FAULT_CHECK_END();
        }
        LOG("catcher done...\n");
        module_response_cups(MODULE_CATCHER, 1);  /* 举手，表示模块已完成本周期工作 */
    }
    return NULL;
}

/* 标识完成测试后是否回到复位位置 */
void set_clear_catcher_pos(uint8_t num)
{
    clear_catcher_pos = num;
}

/* 待机时回到复位位置 */
int catcher_reset_standby(void)
{
    if (get_power_off_stat() == 1) {
        return 0;
    }
    motor_time_sync_attr_t motor_x = {0};
    pos_t cur_pos = {0};
    int calc_acc = 0;

    motor_x.v0_speed = 100;
    motor_x.vmax_speed = h3600_conf_get()->motor[MOTOR_CATCHER_X].speed;
    motor_x.speed = h3600_conf_get()->motor[MOTOR_CATCHER_X].speed;
    motor_x.max_acc = h3600_conf_get()->motor[MOTOR_CATCHER_X].acc;
    motor_x.acc = h3600_conf_get()->motor[MOTOR_CATCHER_X].acc;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    get_special_pos(MOVE_C_PRE, 0, &cur_pos, FLAG_POS_UNLOCK);
    if (abs(cur_pos.x) > abs(cur_pos.y - cur_pos.x)) {
        motor_x.step = abs(cur_pos.x);
    } else {
        motor_x.step = abs(cur_pos.y - cur_pos.x);
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.6);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, cur_pos.x, cur_pos.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.6)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
    }
    FAULT_CHECK_END();
    return 0;
}

int catcher_aging_test(int times, catcher_aging_speed_mode_t mode, int drop_cup_flag, catcher_offset_t offset)
{
    module_param_t catcher_param = {0};

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, i = 0, cnt = 0, offset_x = 0, offset_y = 0, offset_z = 0;
    time_fragment_t time_fast[20] = {0};

    catcher_rs485_init();
    motor_attr_init(&motor_x, &motor_z);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_CATCHER_Z, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Z);
        return 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_CATCHER_Y, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.y timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Y);
        return 1;
    }
    motor_reset(MOTOR_CATCHER_X, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT*2)) {
        LOG("reset catcher.x timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Y);
        return 1;
    }
    FAULT_CHECK_END();

    LOG("catcher do aging...\n");
    if (mode == SPEED_NORMAL_MODE) {
        time_frag = catcher_time_frag_table_get();
    } else {
        time_frag = time_fast;
    }
    clear_pos_param(&catcher_param);
    while (1) {
        if (times != 0) {
            if (cnt >= times) {
                break;
            }
        }
        if (i == 30) {
            i = 0;
        }
        if (offset == OF_OFFSET_MODE) {
            if (i>=0 && i<=9) {
                offset_x = CATCHER_AGING_TEST_OFFSET_X;
                offset_y = 0;
                offset_z = 0;
            } else if (i>=10 && i<=19) {
                offset_x = 0;
                offset_y = CATCHER_AGING_TEST_OFFSET_Y;
                offset_z = 0;
            } else if (i>=20 && i<=29) {
                offset_x = 0;
                offset_y = 0;
                offset_z = CATCHER_AGING_TEST_OFFSET_Z;
            } else {
                offset_x = 0;
                offset_y = 0;
                offset_z = 0;
            }
        } else {
            offset_x = 0;
            offset_y = 0;
            offset_z = 0;
        }
        /* 执行抓手从孵育位到进杯位的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");

        LOG("catcher time 2 do [cup_init -> pre]!\n");
        /* 执行抓手从进杯盘抓杯到前处理的动作 */
        get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
        switch (i%3) {
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CUVETTE_SUPPLY_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从前处理到孵育的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t3_src, FLAG_POS_UNLOCK);

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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从光学混匀到光学检测的动作 */
        get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1 + (i%8), &catcher_param.t4_src, FLAG_POS_UNLOCK);
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (catcher_ctl(CATCHER_OPEN)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
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
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (check_catcher_status()) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从丢杯到磁珠检测位的动作 */
        get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1 + (i%4), &catcher_param.t5_src, FLAG_POS_UNLOCK);
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
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (catcher_ctl(CATCHER_CLOSE)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
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
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (!check_catcher_status()) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从前处理到孵育的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t6_src, FLAG_POS_UNLOCK);
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
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
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return 1;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");
        i++;
        cnt++;
        LOG("catcher done...\n");
        LOG("============cnt = %d ==============\n", cnt);
    }
    if (drop_cup_flag == 1) {
        if (times > 30) {
            times = 30;
        }
        for (i=0; i<times; i++) {
            get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
            get_special_pos(MOVE_C_DETACH, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
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
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                        -1400, 50000, 150000, MOTOR_DEFAULT_TIMEOUT, 0.01)) {
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y-1400, 0);
            FAULT_CHECK_END();
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
        }
    }
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

int catcher_poweron_check(int times, int drop_cup_flag)
{
    module_param_t catcher_param = {0};

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, i = 0, cnt = 0, offset_x = 0, offset_y = 0, offset_z = 0;
    time_fragment_t time_fast[20] = {0};

    catcher_rs485_init();
    motor_attr_init(&motor_x, &motor_z);

    motor_reset(MOTOR_CATCHER_Z, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.z timeout.\n");
        return 1;
    }
    motor_reset(MOTOR_CATCHER_Y, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.y timeout.\n");
        return 1;
    }
    motor_reset(MOTOR_CATCHER_X, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT*2)) {
        LOG("reset catcher.x timeout.\n");
        return 1;
    }

    LOG("catcher do poweron check...\n");

    clear_pos_param(&catcher_param);
    while (1) {
        if (times != 0) {
            if (cnt >= 30) {
                break;
            }
        }
        if (i == 30) {
            i = 0;
        }
        if (i >= 6 && i <= 11) {
            time_frag = time_fast;
        } else {
            time_frag = catcher_time_frag_table_get();
        }
        if (i >= 12 && i <= 17) {
            offset_x = CATCHER_AGING_TEST_OFFSET_X;
            offset_y = 0;
            offset_z = 0;
        } else if (i >= 18 && i<= 23) {
            offset_x = 0;
            offset_y = CATCHER_AGING_TEST_OFFSET_Y;
            offset_z = 0;
        } else if (i >= 24 && i<= 29) {
            offset_x = 0;
            offset_y = 0;
            offset_z = CATCHER_AGING_TEST_OFFSET_Z;
        } else {
            offset_x = 0;
            offset_y = 0;
            offset_z = 0;
        }
        /* 执行抓手从孵育位到进杯位的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
        if (catcher_param.t1_src.x != catcher_param.cur.x || catcher_param.t1_src.y != catcher_param.cur.y) {
            if (abs(catcher_param.t1_src.x - catcher_param.cur.x) > abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t1_src.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG0].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_src.x - catcher_param.cur.x + offset_x,
                                        catcher_param.t1_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG0].cost_time)) {
                LOG("move catcher xy failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.t1_src.x + offset_x, catcher_param.t1_src.y + offset_y, 0);
        }
        PRINT_FRAG_TIME("FRAG0");
        motor_z.step = abs(catcher_param.t1_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG1].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG1");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG2");

        motor_z.step = abs(catcher_param.t1_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG3].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG3");
        if (abs(catcher_param.t1_dst.x - catcher_param.cur.x) > abs((catcher_param.t1_dst.y - catcher_param.cur.y) - (catcher_param.t1_dst.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t1_dst.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t1_dst.y - catcher_param.cur.y) - (catcher_param.t1_dst.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG4].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_dst.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t1_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG4].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t1_dst.x + offset_x, catcher_param.t1_dst.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG4");

        motor_z.step = abs(catcher_param.t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG5");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG6");
        motor_z.step = abs(catcher_param.t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG7");

        LOG("catcher time 2 do [cup_init -> pre]!\n");
        /* 执行抓手从进杯盘抓杯到前处理的动作 */
        get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
        switch (i%3) {
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
        if (abs(catcher_param.cur.x) > abs(catcher_param.cur.y)) {
            motor_x.step = abs(catcher_param.cur.x);
        } else {
            motor_x.step = abs(catcher_param.cur.y);
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, catcher_param.cur.x, catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            return 1;
        }
        set_pos(&catcher_param.cur, 0, 0, catcher_param.cur.z);
        PRINT_FRAG_TIME("FRAG9");
        if (abs(catcher_param.t2_src.x - catcher_param.cur.x) > abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t2_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_src.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t2_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t2_src.x + offset_x, catcher_param.t2_src.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG10");

        motor_z.step = abs(catcher_param.t2_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG11");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG12");
        motor_z.step = abs(catcher_param.t2_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG13].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG13");
        if (abs(catcher_param.t2_dst.x - catcher_param.cur.x) > abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t2_dst.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t2_dst.y - catcher_param.cur.y) - (catcher_param.t2_dst.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_dst.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t2_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t2_dst.x + offset_x, catcher_param.t2_dst.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("move catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从前处理到孵育的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t3_src, FLAG_POS_UNLOCK);

        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z + offset_z);
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        if (abs(catcher_param.t3_src.x - catcher_param.cur.x) > abs((catcher_param.t3_src.y - catcher_param.cur.y) - (catcher_param.t3_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t3_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t3_src.y - catcher_param.cur.y) - (catcher_param.t3_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_src.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t3_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t3_src.x + offset_x, catcher_param.t3_src.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从孵育到光学混匀的动作 */
        get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t3_dst, FLAG_POS_UNLOCK);

        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        if (abs(catcher_param.t3_dst.x - catcher_param.cur.x) > abs((catcher_param.t3_dst.y - catcher_param.cur.y) - (catcher_param.t3_dst.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t3_dst.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t3_dst.y - catcher_param.cur.y) - (catcher_param.t3_dst.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_dst.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t3_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t3_dst.x + offset_x, catcher_param.t3_dst.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从光学混匀到光学检测的动作 */
        get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1 + (i%8), &catcher_param.t4_src, FLAG_POS_UNLOCK);
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        LOG("t4_src.x = %d, t4_src.y = %d, cur.x = %d, cur.y = %d\n", catcher_param.t4_src.x, catcher_param.t4_src.y,
            catcher_param.cur.x, catcher_param.cur.y);

        if (abs(catcher_param.t4_src.x - catcher_param.cur.x) > abs((catcher_param.t4_src.y - catcher_param.cur.y) - (catcher_param.t4_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t4_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t4_src.y - catcher_param.cur.y) - (catcher_param.t4_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_src.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t4_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t4_src.x + offset_x, catcher_param.t4_src.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从光学检测到丢杯的动作 */
        get_special_pos(MOVE_C_DETACH, 0, &catcher_param.t4_dst, FLAG_POS_UNLOCK);
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        if (abs(catcher_param.t4_dst.x - catcher_param.cur.x) > abs((catcher_param.t4_dst.y - catcher_param.cur.y) - (catcher_param.t4_dst.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t4_dst.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t4_dst.y - catcher_param.cur.y) - (catcher_param.t4_dst.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_dst.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t4_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t4_dst.x + offset_x, catcher_param.t4_dst.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
//        if (catcher_ctl(CATCHER_OPEN)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
//        if (check_catcher_status()) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从丢杯到磁珠检测位的动作 */
        get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1 + (i%4), &catcher_param.t5_src, FLAG_POS_UNLOCK);
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
//        if (catcher_ctl(CATCHER_CLOSE)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
//        if (!check_catcher_status()) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        PRINT_FRAG_TIME("FRAG17");

        if (abs(catcher_param.t5_src.x - catcher_param.cur.x) > abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t5_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_src.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t5_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t5_src.x + offset_x, catcher_param.t5_src.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从磁珠检测位到前处理的动作 */
        get_special_pos(MOVE_C_PRE, 0, &catcher_param.t5_dst, FLAG_POS_UNLOCK);
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        if (abs(catcher_param.t5_dst.x - catcher_param.cur.x) > abs((catcher_param.t5_dst.y - catcher_param.cur.y) - (catcher_param.t5_dst.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t5_dst.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t5_dst.y - catcher_param.cur.y) - (catcher_param.t5_dst.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_dst.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t5_dst.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t5_dst.x + offset_x, catcher_param.t5_dst.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从前处理到孵育的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t6_src, FLAG_POS_UNLOCK);
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_CLOSE)) {
            LOG("catcher close failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (!check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");

        if (abs(catcher_param.t6_src.x - catcher_param.cur.x) > abs((catcher_param.t6_src.y - catcher_param.cur.y) - (catcher_param.t6_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t6_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t6_src.y - catcher_param.cur.y) - (catcher_param.t6_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t6_src.x - catcher_param.cur.x + offset_x,
                                    catcher_param.t6_src.y - catcher_param.cur.y + offset_y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            LOG("move catcher xy failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.t6_src.x + offset_x, catcher_param.t6_src.y + offset_y, 0);
        PRINT_FRAG_TIME("FRAG14");

        motor_z.step = abs(catcher_param.t6_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t6_src.z + offset_z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("move catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t6_src.z + offset_z);
        PRINT_FRAG_TIME("FRAG15");
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher open failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG16");
        motor_z.step = abs(catcher_param.t6_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("rst catcher z failed\n");
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        if (check_catcher_status()) {
            LOG("catcher pe failed\n");
            return 1;
        }
        PRINT_FRAG_TIME("FRAG17");
        i++;
        cnt++;
        LOG("catcher done...\n");
        LOG("============cnt = %d ==============\n", cnt);
    }
    if (drop_cup_flag == 1) {
        if (times > 30) {
            times = 30;
        }
        for (i=0; i<times; i++) {
            get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
            get_special_pos(MOVE_C_DETACH, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
            if (catcher_param.t1_src.x != catcher_param.cur.x || catcher_param.t1_src.y != catcher_param.cur.y) {
                if (abs(catcher_param.t1_src.x - catcher_param.cur.x) > abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x))) {
                    motor_x.step = abs(catcher_param.t1_src.x - catcher_param.cur.x);
                } else {
                    motor_x.step = abs((catcher_param.t1_src.y - catcher_param.cur.y) - (catcher_param.t1_src.x - catcher_param.cur.x));
                }
                calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG0].cost_time);
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_src.x - catcher_param.cur.x,
                                            catcher_param.t1_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG0].cost_time)) {
                    LOG("move catcher xy failed\n");
                    return 1;
                }
                set_pos(&catcher_param.cur, catcher_param.t1_src.x, catcher_param.t1_src.y, 0);
            }
            PRINT_FRAG_TIME("FRAG0");
            motor_z.step = abs(catcher_param.t1_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG1].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("move catcher z failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_src.z);
            PRINT_FRAG_TIME("FRAG1");
            if (catcher_ctl(CATCHER_CLOSE)) {
                LOG("catcher close failed\n");
                return 1;
            }
            PRINT_FRAG_TIME("FRAG2");
            motor_z.step = abs(catcher_param.t1_src.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG3].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("rst catcher z failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            if (!check_catcher_status()) {
                LOG("catcher pe failed\n");
                return 1;
            }
            PRINT_FRAG_TIME("FRAG3");
            if (abs(catcher_param.t1_dst.x - catcher_param.cur.x) > abs((catcher_param.t1_dst.y - catcher_param.cur.y) - (catcher_param.t1_dst.x - catcher_param.cur.x))) {
                motor_x.step = abs(catcher_param.t1_dst.x - catcher_param.cur.x);
            } else {
                motor_x.step = abs((catcher_param.t1_dst.y - catcher_param.cur.y) - (catcher_param.t1_dst.x - catcher_param.cur.x));
            }
            calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG4].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t1_dst.x - catcher_param.cur.x,
                                        catcher_param.t1_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG4].cost_time)) {
                LOG("move catcher xy failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.t1_dst.x, catcher_param.t1_dst.y, 0);
            PRINT_FRAG_TIME("FRAG4");

            motor_z.step = abs(catcher_param.t1_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("move catcher z failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_dst.z);
            PRINT_FRAG_TIME("FRAG5");
            if (catcher_ctl(CATCHER_OPEN)) {
                LOG("catcher open failed\n");
                return 1;
            }
            PRINT_FRAG_TIME("FRAG6");
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                        -1400, 50000, 150000, MOTOR_DEFAULT_TIMEOUT, 0.01)) {
                LOG("move catcher xy failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y-1400, 0);
            motor_z.step = abs(catcher_param.t1_dst.z);
            calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("rst catcher z failed\n");
                return 1;
            }
            set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
            if (check_catcher_status()) {
                LOG("catcher pe failed\n");
                return 1;
            }
            PRINT_FRAG_TIME("FRAG7");
        }
    }
    if (abs(catcher_param.cur.x) > abs(catcher_param.cur.y)) {
        motor_x.step = abs(catcher_param.cur.x);
    } else {
        motor_x.step = abs(catcher_param.cur.y);
    }
    calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, catcher_param.cur.x, catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        return 1;
    }
    set_pos(&catcher_param.cur, 0, 0, catcher_param.cur.z);
    LOG("catcher test finish!\n");
    return 0;
}


void catcher_motor_aging_test(void)
{
    module_param_t catcher_param = {0};

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, i = 0, cnt = 0;

    catcher_motor_init();
    motor_attr_init(&motor_x, &motor_z);

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_CATCHER_Z, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Z);
        return;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_CATCHER_Y, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.y timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Y);
        return;
    }
    motor_reset(MOTOR_CATCHER_X, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT*2)) {
        LOG("reset catcher.x timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Y);
        return;
    }
    FAULT_CHECK_END();

    LOG("catcher do aging...\n");
    time_frag = catcher_time_frag_table_get();
    clear_pos_param(&catcher_param);
    while (1) {
        if (i == 30) {
            i = 0;
        }
        /* 执行抓手从孵育位到进杯位的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t1_dst, FLAG_POS_UNLOCK);
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
                return;
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
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t1_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG3].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t1_dst.x, catcher_param.t1_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG5].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t1_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t1_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t1_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG7].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_INCU_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");

        LOG("catcher time 2 do [cup_init -> pre]!\n");
        /* 执行抓手从进杯盘抓杯到前处理的动作 */
        get_special_pos(MOVE_C_NEW_CUP, 0, &catcher_param.t2_src, FLAG_POS_UNLOCK);
        switch (i%3) {
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
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_src.x - catcher_param.cur.x,
                                    catcher_param.t2_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t2_src.x, catcher_param.t2_src.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t2_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CUVETTE_SUPPLY_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t2_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG13].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CUVETTE_SUPPLY_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t2_dst.x - catcher_param.cur.x,
                                    catcher_param.t2_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t2_dst.x, catcher_param.t2_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从前处理到孵育的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t3_src, FLAG_POS_UNLOCK);

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t2_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t2_dst.z);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t2_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_src.x - catcher_param.cur.x,
                                    catcher_param.t3_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t3_src.x, catcher_param.t3_src.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从孵育到光学混匀的动作 */
        get_special_pos(MOVE_C_OPTICAL_MIX, 0, &catcher_param.t3_dst, FLAG_POS_UNLOCK);

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t3_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t3_dst.x - catcher_param.cur.x,
                                    catcher_param.t3_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t3_dst.x, catcher_param.t3_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从光学混匀到光学检测的动作 */
        get_special_pos(MOVE_C_OPTICAL, POS_OPTICAL_WORK_1 + (i%8), &catcher_param.t4_src, FLAG_POS_UNLOCK);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t3_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t3_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t3_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_src.x - catcher_param.cur.x,
                                    catcher_param.t4_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t4_src.x, catcher_param.t4_src.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从光学检测到丢杯的动作 */
        get_special_pos(MOVE_C_DETACH, 0, &catcher_param.t4_dst, FLAG_POS_UNLOCK);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t4_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t4_dst.x - catcher_param.cur.x,
                                    catcher_param.t4_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t4_dst.x, catcher_param.t4_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从丢杯到磁珠检测位的动作 */
        get_special_pos(MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1 + (i%4), &catcher_param.t5_src, FLAG_POS_UNLOCK);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t4_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t4_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t4_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
//        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
//            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
//        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(catcher_param.t5_src.x - catcher_param.cur.x) > abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x))) {
            motor_x.step = abs(catcher_param.t5_src.x - catcher_param.cur.x);
        } else {
            motor_x.step = abs((catcher_param.t5_src.y - catcher_param.cur.y) - (catcher_param.t5_src.x - catcher_param.cur.x));
        }
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_src.x - catcher_param.cur.x,
                                    catcher_param.t5_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t5_src.x, catcher_param.t5_src.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从磁珠检测位到前处理的动作 */
        get_special_pos(MOVE_C_PRE, 0, &catcher_param.t5_dst, FLAG_POS_UNLOCK);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t5_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t5_dst.x - catcher_param.cur.x,
                                    catcher_param.t5_dst.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t5_dst.x, catcher_param.t5_dst.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");

        /* 执行抓手从前处理到孵育的动作 */
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t6_src, FLAG_POS_UNLOCK);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t5_dst.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t5_dst.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t5_dst.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (!check_catcher_motor_status(CATCHER_MOTOR_CLOSE)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
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
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, catcher_param.t6_src.x - catcher_param.cur.x,
                                    catcher_param.t6_src.y - catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.t6_src.x, catcher_param.t6_src.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(catcher_param.t6_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_param.t6_src.z, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, catcher_param.t6_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (catcher_motor_ctl(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_STOP_ALL);
        motor_z.step = abs(catcher_param.t6_src.z);
        calc_acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_Z);
            return;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, catcher_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (check_catcher_motor_status(CATCHER_MOTOR_OPEN)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_PRE_PE);
            return;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");
        i++;
        cnt++;
        LOG("catcher done...\n");
        LOG("============cnt = %d ==============\n", cnt);
    }
    LOG("catcher test finish!\n");
}


/* 初始化抓手模块 */
int catcher_init(void)
{
    pthread_t catcher_main_thread;

    if (0 != pthread_create(&catcher_main_thread, NULL, catcher_work_task, NULL)) {
        LOG("catcher_work_task thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


