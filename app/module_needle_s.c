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
#include "module_needle_s.h"
#include "module_reagent_table.h"
#include "module_liquid_detect.h"
#include "h3600_needle.h"
#include "module_catcher_rs485.h"
#include "module_liquied_circuit.h"
#include "device_status_count.h"

static sample_stamp_stat_t stmp_stat = {0};
static needle_s_cmd_t needle_s_cmd = 0;
static sample_type_t needle_s_qc = 0;
static uint8_t clear_needle_s_pos = 0;
static uint8_t s_spec_clean_flag = 0;   /* 标识样本针是否在特殊清洗流程中 */
static uint8_t needle_s_calc_mode = 0;  /* 标识样本针是否处于称量流程（只用于液面探测）0:常规 1:称量 */
struct list_head sq_list;   /* 记录样本质量检测相关信息 */

void sq_add_tail(sq_info_t * info)
{
    list_add_tail(&info->sqsibling, &sq_list);
}

int sq_clot_check_flag_get(uint32_t orderno, int check_result)
{
    sq_info_t * pos = NULL;
    sq_info_t * n = NULL;
    sq_info_t * tmp_pos = NULL;
    uint32_t check_flag = 0;
    uint32_t tmp_tube_id = 0;
    int ar_report_flag = 0;

    list_for_each_entry_safe(pos, n, &sq_list, sqsibling) {
        if (orderno == pos->order_no) {
            tmp_tube_id = pos->tube_id;
            LOG("handle cur tube id = %d.\n", tmp_tube_id);
            if (pos->sq_report & CHECK_CLOT) {
                LOG("ar check get.\n"); 
                ar_report_flag = 1;/* 凝块功能开启后才进行ar状态上报 */
                if (check_result) {
                   LOG("get clot check flag is %d.\n", pos->sq.clot_handle_mode);
                   check_flag = pos->sq.clot_handle_mode;
                   pos->sq.check_clot = 1;
                   pos->sq.clot_error = 1;
                   tmp_pos = pos;
                   break;
                }
            }
        }
    }

    if (tmp_tube_id != 0 && ar_report_flag) {
        list_for_each_entry_safe(pos, n, &sq_list, sqsibling) {
            if (tmp_tube_id == pos->tube_id) {
                if (pos->sq.ar_error == 1) {
                    LOG("report ar err stage.\n");
                    if (tmp_pos != NULL) {
                        tmp_pos->sq.ar_error = 1;
                        tmp_pos->sq.check_anticoagulation_ratio = pos->sq.check_anticoagulation_ratio;
                    } else {
                        tmp_pos = pos;
                    }
                }
                break;
            }
        }
    }

    if (tmp_pos != NULL) {
       report_sample_quality(tmp_tube_id, &tmp_pos->sq);
    }

    return check_flag;
}

void sq_del_node_all(int rack_idx, int tube_id)
{
    sq_info_t *             pos         = NULL;
    sq_info_t *             n           = NULL;

    list_for_each_entry_safe(pos, n, &sq_list, sqsibling) {
        if (rack_idx == pos->rack_idx && tube_id == pos->tube_id) {
            list_del(&pos->sqsibling);
            free(pos);
            pos = NULL;
        }
    }
}

void sq_del_node(sq_info_t * info)
{
    list_del(&info->sqsibling);
    free(info);
    info = NULL;
}

void report_list_show(void)
{
    sq_info_t *             pos         = NULL;
    sq_info_t *             n           = NULL;

    LOG("------------------------sq_list start-----------------------------------\n");
    list_for_each_entry_safe(pos, n, &sq_list, sqsibling) {
        LOG("rack = %d, pos = %d, tubeid = %d, order = %d, report = %d.\n",
            pos->rack_idx, pos->pos_idx, pos->tube_id, pos->order_no, pos->sq_report);
    }
    LOG("------------------------sq_list end-----------------------------------\n");
}

void sq_list_clear(void)
{
    sq_info_t *             pos         = NULL;
    sq_info_t *             n           = NULL;

    if (sq_list.next) {
        list_for_each_entry_safe(pos, n, &sq_list, sqsibling) {
            sq_del_node(pos);
        }
    }
    LOG("sample_quality: All node deleted.\n");
}

static void sq_list_init(void)
{
    INIT_LIST_HEAD(&sq_list);
}

void ar_range_cal(int *min, int *max, sq_info_t *info)
{
    double s = 0, v_min = 0, v_max = 0, h_min = 0, h_max = 0;

    if (info->d == 0 || info->v == 0 || info->f == 0) {
        info->d = 10.35;
        info->v = 3;
        info->f = 10;
        LOG("sample_quality: use default arg.\n");
    }

    s = M_PI * pow(info->d / 2, 2);
    v_min = info->v * (1 - info->f / 100);
    v_max = info->v * (1 + info->f / 100);
    h_min = (v_min * 1000) / s;
    h_max = (v_max * 1000) / s;
    *min = h_min * S_1MM_STEPS;
    *max = h_max * S_1MM_STEPS;

    LOG("sample_quality: ar step_min = %d(%fmm), step_max = %d(%fmm).\n", *min, h_min, *max, h_max);
}

/*
    抗凝比例筛查：每管仅检测一次。
*/
static int needle_s_check_ar(NEEDLE_S_CMD_PARAM *para)
{
    int min = 0, max = 0, step_left = 0;
    sq_info_t *n = NULL, *pos = NULL;
    pos_t pos_info = {0};

    LOG("sample_quality: ready check_ar, order = %d.\n", para->orderno);

    list_for_each_entry_safe(pos, n, &sq_list, sqsibling) {
        if (para->orderno == pos->order_no && TBIT(pos->sq_report, CHECK_AR)) {
            pos->sq.check_anticoagulation_ratio = 1;
            pos->sq.ar_error = 0;
            if (para->tube_type != PP_1_8 && para->tube_type != PP_2_7) {
                LOG("sample_quality: tube_type = %d, order = %d, Cant be here.\n", para->tube_type, para->orderno);
                break;
            } else {
                get_special_pos(MOVE_S_SAMPLE_NOR, POS_1, &pos_info, FLAG_POS_UNLOCK);

                /* 探测的反馈值是针尖深入液面2mm的高度 */
                step_left = pos_info.z + S_TO_BOTTOM - (para->needle_s_param.cur.z - 2 * S_1MM_STEPS);
                ar_range_cal(&min, &max, pos);
                if (step_left < min || step_left > max) {
                    LOG("sample_quality: check_ar tube_type = %d, order = %d, step left = %d(%fmm), ar_error.\n",
                        para->tube_type, para->orderno, step_left, step_left / S_1MM_STEPS);
                    pos->sq.ar_error = 1;
                }
            }
            LOG("sample_quality: check_ar tube_type = %d, order = %d, ar_error = %d, hand_mode = %d, report = %d.\n",
                para->tube_type, para->orderno, pos->sq.ar_error, pos->sq.ar_handle_mode, pos->sq_report);
            if (pos->sq.ar_error && pos->sq.ar_handle_mode == HANDLING_MODE_DO_NOT_DETECT_THIS_SAMPLE) {
                /* 抗凝比例异常，本管所有订单不需要再测试，放在周期后统一处理 */
                report_sample_quality(pos->tube_id, &pos->sq);
                /* TBD delete order */
                work_queue_add(delete_tube_order_by_ar, (void *)para->orderno);
                if (pos->sq_report == CHECK_AR) {
                    /* 后续没有其他样本质量检测项目，则删除该节点 */
                    LOG("sample_quality: check_ar rack = %d, tube_id = %d del_node by tube_id.\n", pos->rack_idx, pos->tube_id);
                    sq_del_node_all(pos->rack_idx, pos->tube_id);
                }
                break;
            }

            if (pos->sq_report == CHECK_AR) {
                LOG("sample_quality: check_ar rack = %d, tube_id = %d, order = %d, report.\n",
                    pos->rack_idx, pos->tube_id, para->orderno);
                report_sample_quality(pos->tube_id, &pos->sq);
                sq_del_node(pos);
            } else {
                LOG("sample_quality: check_ar rack = %d, tube_id = %d, order = %d, report ignore.\n",
                    pos->rack_idx, pos->tube_id, para->orderno);
            }
            break;
        }
    }

    return 0;
}


/* 标识完成测试后是否回到复位位置 */
void set_clear_needle_s_pos(uint8_t num)
{
    clear_needle_s_pos = num;
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

    if (clear_needle_s_pos == 1) {
        get_special_pos(MOVE_S_CLEAN, 0, &init_pos, FLAG_POS_UNLOCK);
        set_pos(&param->cur, init_pos.x, init_pos.y, NEEDLE_S_CLEAN_POS);
        stmp_stat.sample_left_ul = 0;
        stmp_stat.stemp_left_ul = 0;
        clear_needle_s_pos = 0;
    }
}

static void motor_attr_init(motor_time_sync_attr_t *motor_x, motor_time_sync_attr_t *motor_z)
{
    motor_x->v0_speed = 100;
    motor_x->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].speed;
    motor_x->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].speed;
    motor_x->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].acc;
    motor_x->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_X].acc;

    motor_z->v0_speed = 1000;
    motor_z->vmax_speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed;
    motor_z->speed = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed;
    motor_z->max_acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc;
    motor_z->acc = h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc;
}

static needle_s_time_t needle_s_cmd_trans_time_frag(needle_s_cmd_t cmd)
{
    needle_s_time_t s_time = NEEDLE_S_NORMAL_TIME;

    switch (cmd) {
    case NEEDLE_S_NONE:
    case NEEDLE_S_NORMAL_SAMPLE:
        s_time = NEEDLE_S_NORMAL_TIME;
        break;
    case NEEDLE_S_R1_SAMPLE:
        s_time = NEEDLE_S_R1_TIME;
        break;
    case NEEDLE_S_DILU_SAMPLE:
        s_time = NEEDLE_S_DILU_TIME;
        break;
    case NEEDLE_S_R1_DILU_SAMPLE:
        s_time = NEEDLE_S_DILU_R1_TIME;
        break;
    case NEEDLE_S_R1_ONLY:
        s_time = NEEDLE_S_R1_ONLY_TIME;
        break;
    case NEEDLE_S_SP:
        s_time = NEEDLE_S_P_TIME;
        break;
    case NEEDLE_S_DILU1_SAMPLE:
        s_time = NEEDLE_S_DILU1_TIME;
        break;
    case NEEDLE_S_DILU2_R1:
        s_time = NEEDLE_S_DILU2_TIME;
        break;
    case NEEDLE_S_DILU3_MIX:
        s_time = NEEDLE_S_DILU3_WITHOUT_DILU_TIME;
        break;
    case NEEDLE_S_DILU3_DILU_MIX:
        s_time = NEEDLE_S_DILU3_WITH_DILU_TIME;
        break;
    default:
        break;
    }
    return s_time;
}

needle_s_cmd_t get_needle_s_cmd(void)
{
    return needle_s_cmd;
}

static void set_needle_s_cmd(needle_s_cmd_t stat)
{
    needle_s_cmd = stat;
}

/* 常规项目(包括常规项目的试剂仓质控)为QC_NORMAL，仅当稀释R1试剂仓质控时(例如FDP项目的试剂仓质控)为SAMPLE_QC */
sample_type_t get_needle_s_qc_type(void)
{
    return needle_s_qc;
}

static void set_needle_s_qc_type(sample_type_t qc_type)
{
    needle_s_qc = qc_type;
}

static void s_call_special_clean(void *arg)
{
    int i = 0;
    time_fragment_t *time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_DILU2_R1));
    s_spec_clean_flag = 1;
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    s_special_clean(time_frag[FRAG13].cost_time);
    FAULT_CHECK_END();
    for (i=0; i<4; i++) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_inside_clean();
        usleep(200 * 1000);
        FAULT_CHECK_END();
    }
    s_spec_clean_flag = 0;
}

static void s_call_stemp_post_clean(void *arg)
{
    liq_slave_numb_t clean_type = (liq_slave_numb_t)arg;
    stage_pool_self_clear(clean_type);
}

static void s_call_stemp_pre_clean(void *arg)
{
    liq_slave_numb_t clean_type = (liq_slave_numb_t)arg;
    int i = 0;
    for (i=0; i<3; i++) {
        if (i != 0) {
            usleep(500*1000);
        }
        stage_pool_self_clear(clean_type);
    }
}

static void s_call_reagent_table_move(void *arg)
{
    time_fragment_t *time_frag = reagent_time_frag_table_get();
    reag_table_cotl_t reag_table_cotl = {0};
    int cnt = 0;

    reag_table_cotl.table_move_type = TABLE_COMMON_MOVE;
    reag_table_cotl.req_pos_type = NEEDLE_S;
    reag_table_cotl.table_dest_pos_idx = (needle_pos_t)arg;
    reag_table_cotl.move_time = time_frag[FRAG1].cost_time - 0.2;

    module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
    PRINT_FRAG_TIME("FRAG0");
    LOG("needle = %d, pos = %d\n", reag_table_cotl.req_pos_type, reag_table_cotl.table_dest_pos_idx);
    while (1) {
        if (0 == reag_table_occupy_flag_get()) {
            break;
        } else {
            if (cnt >= 20) {
                LOG("reagent occupy get failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
                break;
            }
        }
        cnt++;
        LOG("delay 10ms.\n");
        usleep(10*1000);
    }
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    reag_table_occupy_flag_set(1);
    reagent_table_move_interface(&reag_table_cotl);
    FAULT_CHECK_END();
}

static cup_pos_t needle_s_get_reagent_pos(needle_pos_t needle_pos)
{
    cup_pos_t res_pos = POS_INVALID;
    if (needle_pos % 2) {
        res_pos = POS_REAGENT_TABLE_S_IN;
    } else {
        res_pos = POS_REAGENT_TABLE_S_OUT;
    }
    return res_pos;
}

static int needle_s_cmd_work(NEEDLE_S_CMD_PARAM *param)
{
    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    liquid_detect_arg_t needle_s_liq_detect_arg = {0};
    int needle_s_dilu_curr_zero = 0, needle_s_sp_liq_flag = 0;/* 标识本周期液面探测失败 */

    motor_attr_init(&motor_x, &motor_z);
    set_cur_sampler_ordno_for_clot_check(param->orderno, 1, param->cmd);
    LOG("orderno = [%d], cmd = [%d]\n", param->orderno, param->cmd);
    needle_s_liq_detect_arg.mode = NORMAL_DETECT_MODE;
    switch (param->cmd) {
    case NEEDLE_S_NONE:
        LOG("NEEDLE S do nothing...\n");
        if (param->needle_s_param.cur.z != NEEDLE_S_CLEAN_POS) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param->needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, 0.6);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, 0.4);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
            FAULT_CHECK_END();
        }
        if (param->needle_s_param.t1_src.x == param->needle_s_param.cur.x && param->needle_s_param.t1_src.y == param->needle_s_param.cur.y) {
            LOG("needle s now not move!\n");
        } else {
            /* 当进杯盘出现错误第一次运行时避让抓手 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
                motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
            } else {
                motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
            }
            motor_x.acc = calc_motor_move_in_time(&motor_x, 0.4);
            if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                        param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, 0.4)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
            FAULT_CHECK_END();
        }
        if (param->pre_clean_enable == ATTR_ENABLE) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        leave_singal_wait(LEAVE_C_FRAG18);
        leave_singal_send(LEAVE_S_CLEAN);
        break;
    case NEEDLE_S_NORMAL_SAMPLE:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_NORMAL_SAMPLE));
        if (param->sample_type == SAMPLE_QC) {
            work_queue_add(s_call_reagent_table_move, (void*)param->qc_reagent_pos);
        }
        if (param->pre_clean_enable == ATTR_ENABLE || (get_throughput_mode() == 1)) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        liq_s_handle_sampler(1);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        liq_s_handle_sampler(0);
        if (param->sample_type == SAMPLE_QC) {
            if (TABLE_IDLE != reag_table_stage_check()) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
            }
        }
        if (param->sample_type == SAMPLE_TEMP) {
            /* 取缓存样本 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_s_calc_stemp_pos((int)(stmp_stat.stemp_left_ul - param->take_ul - NEEDLE_S_SAMPLE_MORE), &param->calc_pos);
            motor_z.step = abs(param->needle_s_param.t1_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG2].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_src.z + param->calc_pos.z);
            FAULT_CHECK_END();
        } else {
            /* 样本液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
            if (param->sample_type == SAMPLE_QC) {
                needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
                needle_s_liq_detect_arg.reag_idx = param->qc_reagent_pos;
            } else {
                needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
                needle_s_liq_detect_arg.reag_idx = 1;
            }
            needle_s_liq_detect_arg.tube = param->tube_type;
            needle_s_liq_detect_arg.order_no = param->orderno;
            needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
            needle_s_liq_detect_arg.take_ul = param->take_ul + NEEDLE_S_SAMPLE_MORE;
            param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
            if (param->needle_s_param.cur.z < EMAX) {
                if (needle_s_calc_mode) {
                    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                        LOG("needle_s_calc_mode reset z failed!\n");
                    }
                    return 1;
                } else {
                    LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                    if (param->needle_s_param.cur.z == ESWITCH) {
                        LOG("QC switch pos!\n");
                        report_reagent_remain(param->qc_reagent_pos, 1, param->orderno);
                    } else {
                        liq_det_set_cup_detach(param->orderno);
                        if (param->needle_s_param.cur.z == EMAXSTEP) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                        } else if (param->needle_s_param.cur.z == ENOTHING) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                        } else if (param->needle_s_param.cur.z == EARG) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                        }
                        needle_s_sp_liq_flag = 1;
                    }
                }
            } else {
                if (!needle_s_calc_mode && needle_s_liq_detect_arg.needle == NEEDLE_TYPE_S) {
                    needle_s_check_ar(param);
                }
            }
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        clot_write_log("sampler order_no: %d\t", param->orderno);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_absorb_ul(NEEDLE_TYPE_S, (param->take_ul*1.1) + NEEDLE_S_SAMPLE_MORE);
            } else {
                if (param->tube_type < PP_1_8) {
                    needle_absorb_ul(NEEDLE_TYPE_S, param->take_ul + NEEDLE_S_SAMPLE_LESS);
                } else {
                    needle_absorb_ul(NEEDLE_TYPE_S, param->take_ul + NEEDLE_S_SAMPLE_MORE);
                }
            }
        }
        if (param->sample_type == SAMPLE_QC) {
            report_reagent_supply_consume(REAGENT, param->qc_reagent_pos, (int)(param->take_ul + NEEDLE_S_SAMPLE_MORE));
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->sample_type == SAMPLE_QC) {
            reag_table_occupy_flag_set(0);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        if (param->stemp_post_clean_enable == ATTR_ENABLE) {
            LOG("stemp post clean!\n");
            work_queue_add(s_call_stemp_post_clean, (void *)STEGE_POOL_LAST_CLEAR);
        }
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG5].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x + param->calc_pos.x, param->needle_s_param.t1_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_release_ul(NEEDLE_TYPE_S, param->take_ul * 1.1, 0);
            } else {
                needle_release_ul(NEEDLE_TYPE_S, param->take_ul, 0);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x, param->needle_s_param.t2_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        if (get_throughput_mode() == 0) { /* !PT360 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);

        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        break;
    case NEEDLE_S_R1_SAMPLE:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_R1_SAMPLE));
        work_queue_add(s_call_reagent_table_move, (void*)param->r1_reagent_pos);
        if (param->pre_clean_enable == ATTR_ENABLE) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        liq_s_handle_sampler(1);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        liq_s_handle_sampler(0);
        /* 检查试剂仓状态 */
        if (TABLE_IDLE != reag_table_stage_check()) {
            LOG("reagent move failed!\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
        }
        /* R1液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param->take_r1_ul + NEEDLE_S_R1_MORE;
        needle_s_liq_detect_arg.reag_idx = param->r1_reagent_pos;
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
            if (param->needle_s_param.cur.z == ESWITCH) {
                LOG("R1 switch pos!\n");
                report_reagent_remain(param->r1_reagent_pos, 1, param->orderno);
            } else {
                liq_det_r1_set_cup_detach(param->orderno);
                LOG("report R1 add failed, R1 add go on.\n");
                if (param->needle_s_param.cur.z == EMAXSTEP) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                } else if (param->needle_s_param.cur.z == ENOTHING) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                } else if (param->needle_s_param.cur.z == EARG) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                }
                needle_s_sp_liq_flag = 1;
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S, param->take_r1_ul + NEEDLE_S_R1_MORE);
        }
        report_reagent_supply_consume(REAGENT, param->r1_reagent_pos, (int)(param->take_r1_ul + NEEDLE_S_R1_MORE));
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        reag_table_occupy_flag_set(0);
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        if (param->sample_type == SAMPLE_QC) {
            work_queue_add(s_call_reagent_table_move, (void*)param->qc_reagent_pos);
        }
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)(param->curr_ul - param->take_ul), &param->calc_pos);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG5].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x + param->calc_pos.x, param->needle_s_param.t1_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_release_ul(NEEDLE_TYPE_S, param->take_r1_ul, 0);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        /* 洗针 */
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG10].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x, param->needle_s_param.t2_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        if (param->sample_type == SAMPLE_QC) {
        /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
            }
        }
        if (param->sample_type == SAMPLE_TEMP) {
            /* 取缓存样本 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_s_calc_stemp_pos((int)(stmp_stat.stemp_left_ul - param->take_ul - NEEDLE_S_SAMPLE_MORE), &param->calc_pos);
            motor_z.step = abs(param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t2_src.z + param->calc_pos.z);
            FAULT_CHECK_END();
        } else {
            /* 样本液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
            if (param->sample_type == SAMPLE_QC) {
                needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
                needle_s_liq_detect_arg.reag_idx = param->qc_reagent_pos;
            } else {
                needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
                needle_s_liq_detect_arg.reag_idx = 1;
            }
            needle_s_liq_detect_arg.tube = param->tube_type;
            needle_s_liq_detect_arg.order_no = param->orderno;
            needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
            needle_s_liq_detect_arg.take_ul = param->take_ul + NEEDLE_S_SAMPLE_MORE;
            param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
            if (param->needle_s_param.cur.z < EMAX) {
                LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                if (param->needle_s_param.cur.z == ESWITCH) {
                    LOG("QC switch pos!\n");
                    report_reagent_remain(param->qc_reagent_pos, 1, param->orderno);
                } else {
                    liq_det_set_cup_detach(param->orderno);
                    if (param->needle_s_param.cur.z == EMAXSTEP) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                    } else if (param->needle_s_param.cur.z == ENOTHING) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                    } else if (param->needle_s_param.cur.z == EARG) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                    }
                    needle_s_sp_liq_flag = 1;
                }
            } else {
                if (needle_s_liq_detect_arg.needle == NEEDLE_TYPE_S) {
                    needle_s_check_ar(param);
                }
            }
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        clot_write_log("sampler order_no: %d\t", param->orderno);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_absorb_ul(NEEDLE_TYPE_S, (param->take_ul*1.09) + NEEDLE_S_SAMPLE_MORE);
            } else {
                if (param->tube_type < PP_1_8) {
                    needle_absorb_ul(NEEDLE_TYPE_S, param->take_ul + NEEDLE_S_SAMPLE_LESS);
                } else {
                    needle_absorb_ul(NEEDLE_TYPE_S, param->take_ul + NEEDLE_S_SAMPLE_MORE);
                }
            }
        }
        if (param->sample_type == SAMPLE_QC) {
            report_reagent_supply_consume(REAGENT, param->qc_reagent_pos, (int)(param->take_ul + NEEDLE_S_SAMPLE_MORE));
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->sample_type == SAMPLE_QC) {
            reag_table_occupy_flag_set(0);
        }
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        if (param->stemp_post_clean_enable == ATTR_ENABLE) {
            LOG("stemp post clean!\n");
            work_queue_add(s_call_stemp_post_clean, (void *)STEGE_POOL_LAST_CLEAR);
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_dst.x + param->calc_pos.x, param->needle_s_param.t2_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t2_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t2_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t2_dst.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_release_ul(NEEDLE_TYPE_S, param->take_ul * 1.09, 0);
            } else {
                needle_release_ul(NEEDLE_TYPE_S, param->take_ul, 0);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        module_sync_time(get_module_base_time(), time_frag[FRAG16].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG17].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");
        module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        if (param->mix_pos != MIX_POS_INVALID) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            /* 启动混匀 */
            cup_mix_data_set(param->mix_pos, param->mix_stat.order_no, param->mix_stat.rate, param->mix_stat.time);
            cup_mix_start(param->mix_pos);
            FAULT_CHECK_END();
        }
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG18].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG18].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG18");
        module_sync_time(get_module_base_time(), time_frag[FRAG18].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t3_src.x - param->needle_s_param.cur.x) > abs(param->needle_s_param.t3_src.y - param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.t3_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t3_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t3_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG19].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t3_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t3_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG19].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t3_src.x, param->needle_s_param.t3_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG19");
        module_sync_time(get_module_base_time(), time_frag[FRAG19].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG20].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG20");
        module_sync_time(get_module_base_time(), time_frag[FRAG20].end_time);
        break;
    case NEEDLE_S_DILU_SAMPLE:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_DILU_SAMPLE));
        if (param->sample_type == SAMPLE_QC) {
            work_queue_add(s_call_reagent_table_move, (void*)param->qc_reagent_pos);
        }
        if (param->pre_clean_enable == ATTR_ENABLE) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        liq_s_handle_sampler(1);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        liq_s_handle_sampler(0);
        /* 稀释液液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_DILU;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param->take_dilu_ul + NEEDLE_S_DILU_MORE;
        needle_s_liq_detect_arg.reag_idx = param->s_dilu_pos;
        if (needle_s_calc_mode == 1) {
            needle_s_liq_detect_arg.mode = DEBUG_DETECT_MODE;
        }
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            if (needle_s_calc_mode == 1) {
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                    LOG("needle_s_calc_mode reset z failed!\n");
                }
                return 1;
            } else {
                LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                if (param->needle_s_param.cur.z == ESWITCH) {
                    LOG("dilu switch pos!\n");
                    report_reagent_remain(param->s_dilu_pos, 1, param->orderno);
                } else {
                    liq_det_r1_set_cup_detach(param->orderno);
                    LOG("report dilu add failed, dilu add go on.\n");
                    if (param->needle_s_param.cur.z == EMAXSTEP) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_MAXSTEP);
                    } else if (param->needle_s_param.cur.z == ENOTHING) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                    } else if (param->needle_s_param.cur.z == EARG) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                    }
                    needle_s_sp_liq_flag = 1;
                }
            }
        }
        if (needle_s_calc_mode == 1) {
            needle_s_liq_detect_arg.mode = NORMAL_DETECT_MODE;
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_dilu_ul + NEEDLE_S_DILU_MORE);
        }
        report_reagent_supply_consume(DILUENT, param->s_dilu_pos, (int)(param->take_dilu_ul + NEEDLE_S_DILU_MORE));
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG4].cost_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 64*2, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG5].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x, param->needle_s_param.t1_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        if (param->sample_type == SAMPLE_QC) {
        /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
            }
        }
        if (param->sample_type == SAMPLE_TEMP) {
            /* 取缓存样本 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->take_ul > 0.000001) {
                needle_s_calc_stemp_pos((int)(stmp_stat.stemp_left_ul - param->take_ul), &param->calc_pos);
                motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                }
                set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
            }
            FAULT_CHECK_END();
        } else {
            /* 样本液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->take_ul > 0.000001) {
                needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
                if (param->sample_type == SAMPLE_QC) {
                    needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
                    needle_s_liq_detect_arg.reag_idx = param->qc_reagent_pos;
                } else {
                    needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
                    needle_s_liq_detect_arg.reag_idx = 1;
                }
                needle_s_liq_detect_arg.tube = param->tube_type;
                needle_s_liq_detect_arg.order_no = param->orderno;
                needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
                needle_s_liq_detect_arg.take_ul = param->take_ul;
                param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
                if (param->needle_s_param.cur.z < EMAX) {
                    if (needle_s_calc_mode == 1) {
                        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                            LOG("needle_s_calc_mode reset z failed!\n");
                        }
                        return 1;
                    } else {
                        LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                        if (param->needle_s_param.cur.z == ESWITCH) {
                            LOG("QC switch pos!\n");
                            report_reagent_remain(param->qc_reagent_pos, 1, param->orderno);
                        } else {
                            liq_det_set_cup_detach(param->orderno);
                            if (param->needle_s_param.cur.z == EMAXSTEP) {
                                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                            } else if (param->needle_s_param.cur.z == ENOTHING) {
                                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                            } else if (param->needle_s_param.cur.z == EARG) {
                                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                            }
                            needle_s_sp_liq_flag = 1;
                        }
                    }
                } else {
                    if (!needle_s_calc_mode && needle_s_liq_detect_arg.needle == NEEDLE_TYPE_S) {
                        needle_s_check_ar(param);
                    }
                }
            }
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->take_ul > 0.000001 && needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_ul * 1.08);
            } else {
                needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_ul);
                if (param->sample_type == SAMPLE_QC) {
                    report_reagent_supply_consume(REAGENT, param->qc_reagent_pos, (int)param->take_ul);
                }
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->sample_type == SAMPLE_QC) {
            reag_table_occupy_flag_set(0);
        }
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        if (param->stemp_post_clean_enable == ATTR_ENABLE) {
            LOG("stemp post clean!\n");
            work_queue_add(s_call_stemp_post_clean, (void *)STEGE_POOL_LAST_CLEAR);
        }
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (((param->take_ul+param->take_dilu_ul) > 99.9999) && ((param->take_ul+param->take_dilu_ul) < 100.0001)
            && ((param->curr_ul-param->take_dilu_ul-param->take_ul) < 0.0001)) {    /* fib类加样 */
            param->calc_pos.y = 200;
            needle_s_dilu_curr_zero = -1;
        }
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x + param->calc_pos.x, param->needle_s_param.t2_src.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG10].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t2_src.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_release_ul(NEEDLE_TYPE_S_BOTH, (param->take_dilu_ul+(param->take_ul*1.05)), needle_s_dilu_curr_zero);
            } else {
                needle_release_ul(NEEDLE_TYPE_S_BOTH, param->take_dilu_ul+param->take_ul, needle_s_dilu_curr_zero);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG12].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        if (needle_s_dilu_curr_zero == -1) {  /* fib类加样 */
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y-400, 0);
        } else {
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        }
        FAULT_CHECK_END();
        needle_s_dilu_curr_zero = 0;
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG13].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG13].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_dst.x, param->needle_s_param.t2_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        module_sync_time(get_module_base_time(), time_frag[FRAG16].end_time);
        PRINT_FRAG_TIME("FRAG17");
        module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        break;
    case NEEDLE_S_R1_DILU_SAMPLE:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_R1_DILU_SAMPLE));
        work_queue_add(s_call_reagent_table_move, (void*)param->r1_reagent_pos);
        if (param->pre_clean_enable == ATTR_ENABLE) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        liq_s_handle_sampler(1);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        liq_s_handle_sampler(0);
        /* 检查试剂仓状态 */
        if (TABLE_IDLE != reag_table_stage_check()) {
            LOG("reagent move failed!\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
        }
        /* R1液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param->take_r1_ul + NEEDLE_S_R1_MORE;
        needle_s_liq_detect_arg.reag_idx = param->r1_reagent_pos;
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
            if (param->needle_s_param.cur.z == ESWITCH) {
                LOG("R1 switch pos!\n");
                report_reagent_remain(param->r1_reagent_pos, 1, param->orderno);
            } else {
                liq_det_r1_set_cup_detach(param->orderno);
                LOG("report R1 add failed, R1 add go on.\n");
                if (param->needle_s_param.cur.z == EMAXSTEP) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                } else if (param->needle_s_param.cur.z == ENOTHING) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                } else if (param->needle_s_param.cur.z == EARG) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                }
                needle_s_sp_liq_flag = 1;
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S, param->take_r1_ul + NEEDLE_S_R1_MORE);
        }
        report_reagent_supply_consume(REAGENT, param->r1_reagent_pos, (int)(param->take_r1_ul + NEEDLE_S_R1_MORE));
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        reag_table_occupy_flag_set(0);
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        if (param->sample_type == SAMPLE_QC) {
            work_queue_add(s_call_reagent_table_move, (void*)param->qc_reagent_pos);
        }
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->take_r1_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG5].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x + param->calc_pos.x, param->needle_s_param.t1_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_release_ul(NEEDLE_TYPE_S, param->take_r1_ul, 0);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        /* 洗针 */
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x) > abs(param->needle_s_param.t2_src.y - param->needle_s_param.cur.y - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.t2_src.y - param->needle_s_param.cur.y - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG10].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x, param->needle_s_param.t2_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        /* 稀释液液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_DILU;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param->take_dilu_ul + NEEDLE_S_DILU_MORE;
        needle_s_liq_detect_arg.reag_idx = param->s_dilu_pos;
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
            if (param->needle_s_param.cur.z == ESWITCH) {
                LOG("dilu switch pos!\n");
                report_reagent_remain(param->s_dilu_pos, 1, param->orderno);
            } else {
                liq_det_r1_set_cup_detach(param->orderno);
                LOG("report dilu add failed, dilu add go on.\n");
                if (param->needle_s_param.cur.z == EMAXSTEP) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_MAXSTEP);
                } else if (param->needle_s_param.cur.z == ENOTHING) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                } else if (param->needle_s_param.cur.z == EARG) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                }
                needle_s_sp_liq_flag = 1;
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_dilu_ul + NEEDLE_S_DILU_MORE);
        }
        report_reagent_supply_consume(DILUENT, param->s_dilu_pos, (int)(param->take_dilu_ul + NEEDLE_S_DILU_MORE));
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG13].cost_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 64*2, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG14].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_dst.x, param->needle_s_param.t2_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        if (param->sample_type == SAMPLE_QC) {
            /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
            }
        }
        if (param->sample_type == SAMPLE_TEMP) {
            /* 取缓存样本 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->take_ul > 0.000001) {
                needle_s_calc_stemp_pos((int)(stmp_stat.stemp_left_ul - param->take_ul), &param->calc_pos);
                motor_z.step = abs(param->needle_s_param.t2_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t2_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                }
                set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t2_dst.z + param->calc_pos.z);
            }
            FAULT_CHECK_END();
        } else {
            /* 样本液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->take_ul > 0.000001) {
                needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
                if (param->sample_type == SAMPLE_QC) {
                    needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
                    needle_s_liq_detect_arg.reag_idx = param->qc_reagent_pos;
                } else {
                    needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
                    needle_s_liq_detect_arg.reag_idx = 1;
                }
                needle_s_liq_detect_arg.tube = param->tube_type;
                needle_s_liq_detect_arg.order_no = param->orderno;
                needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
                needle_s_liq_detect_arg.take_ul = param->take_ul;
                param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
                if (param->needle_s_param.cur.z < EMAX) {
                    LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                    if (param->needle_s_param.cur.z == ESWITCH) {
                        LOG("QC switch pos!\n");
                        report_reagent_remain(param->qc_reagent_pos, 1, param->orderno);
                    } else {
                        liq_det_set_cup_detach(param->orderno);
                        if (param->needle_s_param.cur.z == EMAXSTEP) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                        } else if (param->needle_s_param.cur.z == ENOTHING) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                        } else if (param->needle_s_param.cur.z == EARG) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                        }
                        needle_s_sp_liq_flag = 1;
                    }
                } else {
                    if (needle_s_liq_detect_arg.needle == NEEDLE_TYPE_S) {
                        needle_s_check_ar(param);
                    }
                }
            }
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG15");
        module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->take_ul > 0.000001 && needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_ul * 1.07);
            } else {
                needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_ul);
                if (param->sample_type == SAMPLE_QC) {
                    report_reagent_supply_consume(REAGENT, param->qc_reagent_pos, (int)param->take_ul);
                }
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        module_sync_time(get_module_base_time(), time_frag[FRAG16].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->sample_type == SAMPLE_QC) {
            reag_table_occupy_flag_set(0);
        }
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG17");
        module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        if (param->stemp_post_clean_enable == ATTR_ENABLE) {
            LOG("stemp post clean!\n");
            work_queue_add(s_call_stemp_post_clean, (void *)STEGE_POOL_LAST_CLEAR);
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t3_src.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t3_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t3_src.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t3_src.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t3_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t3_src.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG18].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t3_src.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t3_src.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG18].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t3_src.x + param->calc_pos.x, param->needle_s_param.t3_src.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG18");
        module_sync_time(get_module_base_time(), time_frag[FRAG18].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t3_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG19].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t3_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t3_src.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG19");
        module_sync_time(get_module_base_time(), time_frag[FRAG19].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_release_ul(NEEDLE_TYPE_S_BOTH, (param->take_dilu_ul+(param->take_ul)), 0);
            } else {
                needle_release_ul(NEEDLE_TYPE_S_BOTH, param->take_dilu_ul+param->take_ul, 0);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG20");
        module_sync_time(get_module_base_time(), time_frag[FRAG20].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG21].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG21");
        module_sync_time(get_module_base_time(), time_frag[FRAG21].end_time);
        if (param->mix_pos != MIX_POS_INVALID) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            /* 启动混匀 */
            cup_mix_data_set(param->mix_pos, param->mix_stat.order_no, param->mix_stat.rate, param->mix_stat.time);
            cup_mix_start(param->mix_pos);
            FAULT_CHECK_END();
        }
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG22].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG22].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG22");
        module_sync_time(get_module_base_time(), time_frag[FRAG22].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t3_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t3_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t3_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t3_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t3_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t3_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG23].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t3_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t3_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG23].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t3_dst.x, param->needle_s_param.t3_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG23");
        module_sync_time(get_module_base_time(), time_frag[FRAG23].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG24].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG24");
        module_sync_time(get_module_base_time(), time_frag[FRAG24].end_time);
        break;
    case NEEDLE_S_SP:
    case NEEDLE_S_SINGLE_SP:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_SP));
        if (param->stemp_pre_clean_enable == ATTR_ENABLE) {
            /* 准备暂存池 */
            work_queue_add(s_call_stemp_pre_clean, (void *)STEGE_POOL_PRE_CLEAR);
            LOG("stemp get ready!\n");
        }
        if (param->pre_clean_enable == ATTR_ENABLE || get_throughput_mode() == 1) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        liq_s_handle_sampler(1);
        usleep(200*1000);
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        liq_s_handle_sampler(0);
        /* 压帽液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_ENABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        if (param->cmd == NEEDLE_S_SP) {
            needle_s_liq_detect_arg.take_ul = param->take_ul + NEEDLE_S_SP_MORE;
        } else {
            needle_s_liq_detect_arg.take_ul = param->take_ul + NEEDLE_S_SAMPLE_MORE;
        }
        needle_s_liq_detect_arg.reag_idx = 1;
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            if (needle_s_calc_mode == 1) {
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                    LOG("needle_s_calc_mode reset z failed!\n");
                }
                slip_liquid_detect_collsion_barrier_set(NEEDLE_TYPE_S, ATTR_DISABLE);
                return 1;
            } else {
                LOG("liquid detect error!\n");
                if (param->cmd == NEEDLE_S_SP) {
                    work_queue_add(delete_tube_order, (void *)param->orderno);
                } else {
                    liq_det_set_cup_detach(param->orderno);
                }
                if (param->needle_s_param.cur.z == EMAXSTEP) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                } else if (param->needle_s_param.cur.z == ENOTHING) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                } else if (param->needle_s_param.cur.z == EARG) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                }
                needle_s_sp_liq_flag = 1;
            }
        } else {
            if (!needle_s_calc_mode && needle_s_liq_detect_arg.needle == NEEDLE_TYPE_S) {
                needle_s_check_ar(param);
            }
            device_status_count_add(DS_S_PIERCE_USED_COUNT, 1);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        clot_write_log("sampler order_no: %d\t", param->orderno);
        if (needle_s_sp_liq_flag == 0) {
            if (param->cmd == NEEDLE_S_SP) {
                needle_absorb_ul(NEEDLE_TYPE_S, param->take_ul+NEEDLE_S_SP_MORE); /* 穿刺模式多吸35ul */
            } else {
                needle_absorb_ul(NEEDLE_TYPE_S, param->take_ul+NEEDLE_S_SAMPLE_MORE);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_outside_clean(1);
        if (param->needle_s_param.cur.z < EMAX) {
            motor_z.acc = NEEDLE_S_Z_REMOVE_ACC / 2;
        } else {
            motor_z.step = abs(param->needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG4].cost_time);
        }
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        } else {
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            /* Z复位完成，检测是否有出液面信号 */
            if (param->needle_s_param.cur.z > EMAX && liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                LOG("report S detect failed, s detach cup.\n");
                if (param->cmd == NEEDLE_S_SP) {
                    work_queue_add(delete_tube_order, (void *)param->orderno);
                } else {
                    liq_det_set_cup_detach(param->orderno);
                }
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
            }
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
            FAULT_CHECK_END();
        }
        s_normal_outside_clean(0);
        FAULT_CHECK_END();
        slip_liquid_detect_collsion_barrier_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->cmd == NEEDLE_S_SP) {
            needle_s_calc_stemp_pos((int)(param->take_ul + stmp_stat.stemp_left_ul), &param->calc_pos);
        } else if (param->cmd == NEEDLE_S_SINGLE_SP) {
            needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        }
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        /* XY移动至暂存池时弃液10ul */
        liq_s_handle_sampler(1);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, time_frag[FRAG5].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x + param->calc_pos.x, param->needle_s_param.t1_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        liq_s_handle_sampler(0);
        if (needle_s_sp_liq_flag == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
            FAULT_CHECK_END();
        } else {
            LOG("stemp post clean!\n");
            work_queue_add(s_call_stemp_post_clean, (void *)STEGE_POOL_LAST_CLEAR);
        }
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            if (param->cmd == NEEDLE_S_SP) {
                needle_release_ul(NEEDLE_TYPE_S, param->take_ul+NEEDLE_S_SP_COMP, 0);
            } else {
                needle_release_ul(NEEDLE_TYPE_S, param->take_ul, 0);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        if (needle_s_sp_liq_flag == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param->needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x, param->needle_s_param.t2_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        if (get_throughput_mode() == 0) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        break;
    case NEEDLE_S_DILU1_SAMPLE:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_DILU1_SAMPLE));
        if (param->sample_type == SAMPLE_QC) {
            work_queue_add(s_call_reagent_table_move, (void*)param->qc_reagent_pos);
        }
        if (param->pre_clean_enable == ATTR_ENABLE) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            s_normal_inside_clean();
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        liq_s_handle_sampler(1);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        liq_s_handle_sampler(0);
        /* 稀释液液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_DILU;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param->take_dilu_ul + NEEDLE_S_DILU_MORE;
        needle_s_liq_detect_arg.reag_idx = param->s_dilu_pos;
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
            if (param->needle_s_param.cur.z == ESWITCH) {
                LOG("dilu switch pos!\n");
                report_reagent_remain(param->s_dilu_pos, 1, param->orderno);
            } else {
                liq_det_r1_set_delay_detach(param->orderno);
                LOG("report dilu add failed, dilu add go on.\n");
                if (param->needle_s_param.cur.z == EMAXSTEP) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_MAXSTEP);
                } else if (param->needle_s_param.cur.z == ENOTHING) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                } else if (param->needle_s_param.cur.z == EARG) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                }
                needle_s_sp_liq_flag = 1;
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_dilu_ul + NEEDLE_S_DILU_MORE);
        }
        report_reagent_supply_consume(DILUENT, param->s_dilu_pos, (int)(param->take_dilu_ul + NEEDLE_S_DILU_MORE));
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG4].cost_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_delay_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 64*2, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG5].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x, param->needle_s_param.t1_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        if (param->sample_type == SAMPLE_QC) {
        /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
            }
        }
        if (param->sample_type == SAMPLE_TEMP) {
            /* 取缓存样本 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->take_ul > 0.000001) {
                needle_s_calc_stemp_pos((int)(stmp_stat.stemp_left_ul - param->take_ul), &param->calc_pos);
                motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
                motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                }
                set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
            }
            FAULT_CHECK_END();
        } else {
            /* 样本液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->take_ul > 0.000001) {
                needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
                if (param->sample_type == SAMPLE_QC) {
                    needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
                    needle_s_liq_detect_arg.reag_idx = param->qc_reagent_pos;
                } else {
                    needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
                    needle_s_liq_detect_arg.reag_idx = 1;
                }
                needle_s_liq_detect_arg.tube = param->tube_type;
                needle_s_liq_detect_arg.order_no = param->orderno;
                needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
                needle_s_liq_detect_arg.take_ul = param->take_ul;
                param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
                if (param->needle_s_param.cur.z < EMAX) {
                    LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                    if (param->needle_s_param.cur.z == ESWITCH) {
                        LOG("QC switch pos!\n");
                        report_reagent_remain(param->qc_reagent_pos, 1, param->orderno);
                    } else {
                        liq_det_r1_set_delay_detach(param->orderno);
                        if (param->needle_s_param.cur.z == EMAXSTEP) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                        } else if (param->needle_s_param.cur.z == ENOTHING) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                        } else if (param->needle_s_param.cur.z == EARG) {
                            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                        }
                        needle_s_sp_liq_flag = 1;
                    }
                } else {
                    if (needle_s_liq_detect_arg.needle == NEEDLE_TYPE_S) {
                        needle_s_check_ar(param);
                    }
                }
            }
            FAULT_CHECK_END();
        }
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->take_ul > 0.000001 && needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_ul * 1.07);
            } else {
                needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_ul);
                if (param->sample_type == SAMPLE_QC) {
                    report_reagent_supply_consume(REAGENT, param->qc_reagent_pos, (int)param->take_ul);
                }
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_delay_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (param->sample_type == SAMPLE_QC) {
            reag_table_occupy_flag_set(0);
        }
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        if (param->stemp_post_clean_enable == ATTR_ENABLE) {
            LOG("stemp post clean!\n");
            work_queue_add(s_call_stemp_post_clean, (void *)STEGE_POOL_LAST_CLEAR);
        }
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x + param->calc_pos.x, param->needle_s_param.t2_src.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG10].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t2_src.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            if (param->sample_type == SAMPLE_TEMP) {
                needle_release_ul(NEEDLE_TYPE_S_BOTH, (param->take_dilu_ul+(param->take_ul)), 0);
            } else {
                needle_release_ul(NEEDLE_TYPE_S_BOTH, param->take_dilu_ul+param->take_ul, 0);
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG12].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        /* 启动混匀 */
        cup_mix_data_set(param->mix_pos, param->mix_stat.order_no, param->mix_stat.rate, param->mix_stat.time);
        cup_mix_start(param->mix_pos);
        FAULT_CHECK_END();
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG13].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG13].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x) > abs(( param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(( param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_dst.x, param->needle_s_param.t2_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        module_sync_time(get_module_base_time(), time_frag[FRAG16].end_time);
        PRINT_FRAG_TIME("FRAG17");
        module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        break;
    case NEEDLE_S_R1_ONLY:
    case NEEDLE_S_DILU2_R1:
    case NEEDLE_S_DILU2_R1_TWICE:
        if (param->take_r1_ul >= 0.000001) {
            time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(param->cmd));
            work_queue_add(s_call_reagent_table_move, (void*)param->r1_reagent_pos);
            if (param->pre_clean_enable == ATTR_ENABLE) {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                s_normal_inside_clean();
                FAULT_CHECK_END();
            }
            PRINT_FRAG_TIME("FRAG0");
            module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            liq_s_handle_sampler(1);
            if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
                motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
            } else {
                motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
            }
            motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
            motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
            motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                        param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
            if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
            }
            if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
                LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG1");
            module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
            liq_s_handle_sampler(0);
            /* 检查试剂仓状态 */
            if (TABLE_IDLE != reag_table_stage_check()) {
                LOG("reagent move failed!\n");
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_GET_REAGENT);
            }
            /* R1液面探测 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
            needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_R1;
            needle_s_liq_detect_arg.order_no = param->orderno;
            needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
            needle_s_liq_detect_arg.take_ul = param->take_r1_ul + NEEDLE_S_R1_MORE;
            needle_s_liq_detect_arg.reag_idx = param->r1_reagent_pos;
            param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
            if (param->needle_s_param.cur.z < EMAX) {
                LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
                if (param->needle_s_param.cur.z == ESWITCH) {
                    LOG("R1 switch pos!\n");
                    report_reagent_remain(param->r1_reagent_pos, 1, param->orderno);
                } else {
                    if (param->cmd == NEEDLE_S_R1_ONLY) {
                        liq_det_r1_set_cup_detach(param->orderno);
                    } else {
                        liq_det_r1_set_delay_detach(param->orderno);
                    }
                    LOG("report R1 add failed, R1 add go on.\n");
                    if (param->needle_s_param.cur.z == EMAXSTEP) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_MAXSTEP);
                    } else if (param->needle_s_param.cur.z == ENOTHING) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                    } else if (param->needle_s_param.cur.z == EARG) {
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                    }
                    needle_s_sp_liq_flag = 1;
                }
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG2");
            module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (needle_s_sp_liq_flag == 0) {
                needle_absorb_ul(NEEDLE_TYPE_S, param->take_r1_ul + NEEDLE_S_R1_MORE);
            }
            report_reagent_supply_consume(REAGENT, param->r1_reagent_pos, (int)(param->take_r1_ul + NEEDLE_S_R1_MORE));
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG3");
            module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
            if (param->needle_s_param.cur.z < EMAX) {
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                }
                set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            } else {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
                if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
                } else {
                    /* Z运动完成，检测是否有出液面信号 */
                    if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                        LOG("report S detect failed, s detach cup.\n");
                        if (param->cmd == NEEDLE_S_R1_ONLY) {
                            liq_det_r1_set_cup_detach(param->orderno);
                        } else {
                            liq_det_r1_set_delay_detach(param->orderno);
                        }
                        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DETECT);
                    }
                }
                set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
                FAULT_CHECK_END();
            }
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            reag_table_occupy_flag_set(0);
            FAULT_CHECK_END();
            slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
            PRINT_FRAG_TIME("FRAG4");
            module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
            leave_singal_wait(LEAVE_C_FRAG18);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->take_r1_ul, &param->calc_pos);
            if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
                motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
            } else {
                motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
            }
            motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                        param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG5].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x + param->calc_pos.x, param->needle_s_param.t1_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG5");
            module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG6");
            module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (needle_s_sp_liq_flag == 0) {
                needle_release_ul(NEEDLE_TYPE_S, param->take_r1_ul, 0);
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG7");
            module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(param->needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG8");
            module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
            /* 样本针在本时间片复位一次 */
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
                motor_x.step = abs(param->needle_s_param.cur.x);
            } else {
                motor_x.step = abs(param->needle_s_param.cur.y);
            }
            motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
                LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            }
            set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG9");
            module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x))) {
                motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x);
            } else {
                motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x));
            }
            motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
            if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x,
                                        param->needle_s_param.t2_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x, param->needle_s_param.t2_src.y, param->needle_s_param.cur.z);
            FAULT_CHECK_END();
            leave_singal_send(LEAVE_S_CLEAN);
            PRINT_FRAG_TIME("FRAG10");
            module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG11");
            module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (param->now_clean_type == SPECIAL_CLEAN) {
                work_queue_add(s_call_special_clean, NULL);
            } else {
                s_normal_inside_clean();
            }
            FAULT_CHECK_END();
            PRINT_FRAG_TIME("FRAG12");
            module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
            PRINT_FRAG_TIME("FRAG13");
            module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        } else {
            leave_singal_wait(LEAVE_C_FRAG18);
            LOG("NEEDLE_S_R1_ONLY or NEEDLE_S_DILU2_R1 do nothing!\n");
        }
        break;
    case NEEDLE_S_DILU3_MIX:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_DILU3_MIX));
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        if (s_spec_clean_flag != 0) {
            /* 特殊清洗未完成 */
            LOG("s call special clean failed!\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        leave_singal_wait(LEAVE_C_FRAG8);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        /* 取混合液直接走Z */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)(100), &param->calc_pos);
        motor_z.step = abs(param->needle_s_param.t1_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_src.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        clot_write_log("sampler order_no: %d\t", param->orderno);
        needle_absorb_ul(NEEDLE_TYPE_S, param->take_mix_ul + NEEDLE_S_SAMPLE_MORE);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG4].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG5].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x + param->calc_pos.x, param->needle_s_param.t1_dst.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_release_ul(NEEDLE_TYPE_S, param->take_mix_ul, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        if (param->mix_pos != MIX_POS_INVALID) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            /* 启动混匀 */
            cup_mix_data_set(param->mix_pos, param->mix_stat.order_no, param->mix_stat.rate, param->mix_stat.time);
            cup_mix_start(param->mix_pos);
            FAULT_CHECK_END();
        }
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x, param->needle_s_param.t2_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        break;
    case NEEDLE_S_DILU3_DILU_MIX:
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_DILU3_DILU_MIX));
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        if (s_spec_clean_flag != 0) {
            /* 特殊清洗未完成 */
            LOG("s call special clean failed!\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_src.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_src.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_src.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_src.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_src.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_src.x, param->needle_s_param.t1_src.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        /* 稀释液液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_liq_detect_arg.hat_enable = ATTR_DISABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S_DILU;
        needle_s_liq_detect_arg.order_no = param->orderno;
        needle_s_liq_detect_arg.s_cur_step = param->needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param->take_dilu_ul + NEEDLE_S_DILU_MORE;
        needle_s_liq_detect_arg.reag_idx = param->s_dilu_pos;
        param->needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param->needle_s_param.cur.z < EMAX) {
            LOG("liquid detect error! errno = %d\n", param->needle_s_param.cur.z);
            if (param->needle_s_param.cur.z == ESWITCH) {
                LOG("dilu switch pos!\n");
                report_reagent_remain(param->s_dilu_pos, 1, param->orderno);
            } else {
                liq_det_r1_set_cup_detach(param->orderno);
                LOG("report dilu add failed, dilu add go on.\n");
                if (param->needle_s_param.cur.z == EMAXSTEP) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_MAXSTEP);
                } else if (param->needle_s_param.cur.z == ENOTHING) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                } else if (param->needle_s_param.cur.z == EARG) {
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_Z_EARG);
                }
                needle_s_sp_liq_flag = 1;
            }
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_dilu_ul + NEEDLE_S_DILU_MORE);
        }
        report_reagent_supply_consume(DILUENT, param->s_dilu_pos, (int)(param->take_dilu_ul + NEEDLE_S_DILU_MORE));
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG4].cost_time);
        if (param->needle_s_param.cur.z < EMAX) {
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
                FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            } else {
                /* Z运动完成，检测是否有出液面信号 */
                if (liquid_detect_result_get(NEEDLE_TYPE_S) != LIQ_LEAVE_OUT) {
                    LOG("report S detect failed, s detach cup.\n");
                    liq_det_r1_set_cup_detach(param->orderno);
                    FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_DILU_DETECT);
                }
            }
            set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
            FAULT_CHECK_END();
        }
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        leave_singal_wait(LEAVE_C_FRAG8);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 64*2, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t1_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t1_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG5].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t1_dst.x, param->needle_s_param.t1_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        /* 取混合液直接走Z */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)(50), &param->calc_pos);
        motor_z.step = abs(param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t1_dst.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t1_dst.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_absorb_ul(NEEDLE_TYPE_S_DILU, param->take_mix_ul);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        leave_singal_wait(LEAVE_C_FRAG18);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param->curr_ul, &param->calc_pos);
        if (abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x) > abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x))) {
            motor_x.step = abs(param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y) - (param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_src.x - param->needle_s_param.cur.x + param->calc_pos.x,
                                    param->needle_s_param.t2_src.y - param->needle_s_param.cur.y + param->calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_src.x + param->calc_pos.x, param->needle_s_param.t2_src.y + param->calc_pos.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG10].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param->needle_s_param.t2_src.z - param->needle_s_param.cur.z + param->calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, param->needle_s_param.t2_src.z + param->calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (needle_s_sp_liq_flag == 0) {
            needle_release_ul(NEEDLE_TYPE_S_BOTH, param->take_dilu_ul+param->take_mix_ul, 0);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG12].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        if (param->mix_pos != MIX_POS_INVALID) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            /* 启动混匀 */
            cup_mix_data_set(param->mix_pos, param->mix_stat.order_no, param->mix_stat.rate, param->mix_stat.time);
            cup_mix_start(param->mix_pos);
            FAULT_CHECK_END();
        }
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.cur.x) > abs(param->needle_s_param.cur.y)) {
            motor_x.step = abs(param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param->needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG13].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param->needle_s_param.cur.x, param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG13].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, 0, 0, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x) > abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x))) {
            motor_x.step = abs(param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y) - (param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param->needle_s_param.t2_dst.x - param->needle_s_param.cur.x,
                                    param->needle_s_param.t2_dst.y - param->needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG14].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.t2_dst.x, param->needle_s_param.t2_dst.y, param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        leave_singal_send(LEAVE_S_CLEAN);
        PRINT_FRAG_TIME("FRAG14");
        module_sync_time(get_module_base_time(), time_frag[FRAG14].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG15].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        }
        set_pos(&param->needle_s_param.cur, param->needle_s_param.cur.x, param->needle_s_param.cur.y, NEEDLE_S_CLEAN_POS - param->needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG15");
        module_sync_time(get_module_base_time(), time_frag[FRAG15].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        module_sync_time(get_module_base_time(), time_frag[FRAG16].end_time);
        PRINT_FRAG_TIME("FRAG17");
        module_sync_time(get_module_base_time(), time_frag[FRAG17].end_time);
        break;
    default:
        LOG("ERROR! cmd = %d\n", param->cmd);
        break;
    }
    set_cur_sampler_ordno_for_clot_check(param->orderno, 0, param->cmd);

    return 0;
}

static void needle_s_cmd_deal_with_dilucup(struct react_cup *dilu_cup, struct react_cup *test_cup, NEEDLE_S_CMD_PARAM *param)
{
    if (dilu_cup->cup_dilu_attr.add_state == CUP_STAT_UNUSED) {
        param->cmd = NEEDLE_S_DILU1_SAMPLE;
    } else {
        if (CUP_MIX_FINISH == cup_mix_state_get(pos_cup_trans_mix(dilu_cup->cup_pos))) {
            if (test_cup->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED) {
                if (test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat == CUP_STAT_UNUSED &&
                    test_cup->cup_test_attr.needle_r1[R1_ADD2].take_ul > 0.000001) {
                    param->cmd = NEEDLE_S_DILU2_R1_TWICE;
                } else {
                    if (test_cup->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED && dilu_cup->cup_dilu_attr.trans_state == CUP_STAT_UNUSED &&
                        test_cup->cup_test_attr.needle_s.take_dilu_ul > 0.000001) {
                        param->cmd = NEEDLE_S_DILU3_DILU_MIX;
                    } else {
                        param->cmd = NEEDLE_S_DILU3_MIX;
                    }
                }
            } else {
                param->cmd = NEEDLE_S_NONE;
            }
        } else {
            if (test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat == CUP_STAT_UNUSED &&
                test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul > 0.000001) {
                param->cmd = NEEDLE_S_DILU2_R1;
            } else {
                param->cmd = NEEDLE_S_NONE;
            }
        }
    }
}

static void needle_s_cmd_deal_with_hat(struct react_cup *dilu_cup, struct react_cup *test_cup, NEEDLE_S_CMD_PARAM *param)
{
    double cost_sample_ul = 0.0;
    if (dilu_cup == NULL) {
        cost_sample_ul = test_cup->cup_test_attr.needle_s.take_ul;
    } else {
        cost_sample_ul = dilu_cup->cup_dilu_attr.take_ul;
    }

    if (stmp_stat.sample_tube_id == test_cup->cup_sample_tube.sample_tube_id && ((stmp_stat.stemp_left_ul-STEM_TEMP_DEAD_UL+0.000001) > cost_sample_ul)) {
        param->sample_type = SAMPLE_TEMP;
        if (dilu_cup == NULL) {
            if (test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat == CUP_STAT_UNUSED &&
                test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul > 0.000001) {
                if (test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat == CUP_STAT_UNUSED &&
                    test_cup->cup_test_attr.needle_r1[R1_ADD2].take_ul > 0.000001) {
                    param->cmd = NEEDLE_S_R1_ONLY;
                } else {
                    if (test_cup->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED &&
                        test_cup->cup_test_attr.needle_s.take_dilu_ul > 0.000001) {
                        param->cmd = NEEDLE_S_R1_DILU_SAMPLE;
                    } else {
                        param->cmd = NEEDLE_S_R1_SAMPLE;
                    }
                }
            } else {
                if (test_cup->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED &&
                    test_cup->cup_test_attr.needle_s.take_dilu_ul > 0.000001) {
                    param->cmd = NEEDLE_S_DILU_SAMPLE;
                } else {
                    param->cmd = NEEDLE_S_NORMAL_SAMPLE;
                }
            }
        } else {
            needle_s_cmd_deal_with_dilucup(dilu_cup, test_cup, param);
        }
    } else {
        param->sample_type = SAMPLE_NORMAL;
        if (test_cup->cup_sample_tube.test_cnt == 1 && test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul < 0.000001 &&
            test_cup->cup_test_attr.needle_s.take_dilu_ul < 0.000001) {
            /* 穿刺常规加样 */
            param->cmd = NEEDLE_S_SINGLE_SP;
        } else {
            if (stmp_stat.sample_tube_id != test_cup->cup_sample_tube.sample_tube_id) {
                stmp_stat.sample_left_ul = 0;
                stmp_stat.stemp_left_ul = 0;
            }
            param->cmd = NEEDLE_S_SP;
        }
    }
}

static void needle_s_cmd_deal_without_hat(struct react_cup *dilu_cup, struct react_cup *test_cup, NEEDLE_S_CMD_PARAM *param)
{
    if (test_cup->cup_sample_tube.rack_type == REAGENT_QC_SAMPLE) {
        param->sample_type = SAMPLE_QC;
    } else {
        param->sample_type = SAMPLE_NORMAL;
    }
    if (dilu_cup == NULL) {
        if (test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat == CUP_STAT_UNUSED &&
            test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul > 0.000001) {
            if (test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat == CUP_STAT_UNUSED &&
                test_cup->cup_test_attr.needle_r1[R1_ADD2].take_ul > 0.000001) {
                param->cmd = NEEDLE_S_R1_ONLY;
            } else {
                if (test_cup->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED &&
                    test_cup->cup_test_attr.needle_s.take_dilu_ul > 0.000001) {
                    param->cmd = NEEDLE_S_R1_DILU_SAMPLE;
                } else {
                    param->cmd = NEEDLE_S_R1_SAMPLE;
                }
            }
        } else {
            if (test_cup->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED &&
                test_cup->cup_test_attr.needle_s.take_dilu_ul > 0.000001) {
                param->cmd = NEEDLE_S_DILU_SAMPLE;
            } else {
                param->cmd = NEEDLE_S_NORMAL_SAMPLE;
            }
        }
    } else {
        needle_s_cmd_deal_with_dilucup(dilu_cup, test_cup, param);
    }
}

static void needle_s_cmd_set_pos(struct react_cup *dilu_cup, struct react_cup *test_cup, NEEDLE_S_CMD_PARAM *param)
{
    struct react_cup *tmp_cup = NULL;

    switch (param->cmd) {
    case NEEDLE_S_NONE:
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        break;
    case NEEDLE_S_NORMAL_SAMPLE:
        if (param->sample_type == SAMPLE_NORMAL) {
            get_special_pos(MOVE_S_SAMPLE_NOR, test_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t1_src, FLAG_POS_NONE);
            param->tube_type = test_cup->cup_sample_tube.type;
        } else if (param->sample_type == SAMPLE_TEMP) {
            get_special_pos(MOVE_S_TEMP, 0, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        } else if (param->sample_type == SAMPLE_QC) {
            get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_sample_tube.sample_index), &param->needle_s_param.t1_src, FLAG_POS_NONE);
            param->qc_reagent_pos = test_cup->cup_sample_tube.sample_index;
            param->tube_type = test_cup->cup_sample_tube.type;
        } else {
            LOG("error! no such sample type\n");
        }
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        param->take_ul = test_cup->cup_test_attr.needle_s.take_ul;
        param->curr_ul = param->take_ul;
        param->now_clean_type = test_cup->cup_test_attr.needle_s.post_clean.type;
        break;
    case NEEDLE_S_R1_SAMPLE:
        get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos), &param->needle_s_param.t1_src, FLAG_POS_NONE);
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos;
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        if (param->sample_type == SAMPLE_NORMAL) {
            get_special_pos(MOVE_S_SAMPLE_NOR, test_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t2_src, FLAG_POS_NONE);
            param->tube_type = test_cup->cup_sample_tube.type;
        } else if (param->sample_type == SAMPLE_TEMP) {
            get_special_pos(MOVE_S_TEMP, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        } else if (param->sample_type == SAMPLE_QC) {
            get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_sample_tube.sample_index), &param->needle_s_param.t2_src, FLAG_POS_NONE);
            param->qc_reagent_pos = test_cup->cup_sample_tube.sample_index;
            param->tube_type = test_cup->cup_sample_tube.type;
        } else {
            LOG("error! no such sample type\n");
        }
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t3_src, FLAG_POS_NONE);
        param->take_ul = test_cup->cup_test_attr.needle_s.take_ul;
        param->take_r1_ul = test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul;
        param->curr_ul = test_cup->cup_test_attr.curr_ul + param->take_ul + param->take_r1_ul;
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos;
        if (test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable == ATTR_ENABLE) {
            param->mix_pos = pos_cup_trans_mix(test_cup->cup_pos);
            param->mix_stat.time = test_cup->cup_test_attr.test_cup_incubation.mix_time;
            param->mix_stat.rate = test_cup->cup_test_attr.test_cup_incubation.mix_rate;
            param->mix_stat.order_no = test_cup->order_no;
        } else {
            param->mix_pos = MIX_POS_INVALID;
        }
        break;
    case NEEDLE_S_DILU_SAMPLE:
        get_special_pos(MOVE_S_DILU, test_cup->cup_sample_tube.diluent_pos, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        if (param->sample_type == SAMPLE_NORMAL) {
            get_special_pos(MOVE_S_SAMPLE_NOR, test_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
            param->tube_type = test_cup->cup_sample_tube.type;
        } else if (param->sample_type == SAMPLE_TEMP) {
            get_special_pos(MOVE_S_TEMP, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else if (param->sample_type == SAMPLE_QC) {
            get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_sample_tube.sample_index), &param->needle_s_param.t1_dst, FLAG_POS_NONE);
            param->qc_reagent_pos = test_cup->cup_sample_tube.sample_index;
            param->tube_type = test_cup->cup_sample_tube.type;
        } else {
            LOG("error! no such sample type\n");
        }
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
        param->take_ul = test_cup->cup_test_attr.needle_s.take_ul;
        param->take_dilu_ul = test_cup->cup_test_attr.needle_s.take_dilu_ul;
        param->curr_ul = test_cup->cup_test_attr.curr_ul + param->take_ul + param->take_dilu_ul;
        param->now_clean_type = test_cup->cup_test_attr.needle_s.post_clean.type;
        param->s_dilu_pos = DILU_IDX_START + test_cup->cup_sample_tube.diluent_pos;
        break;
    case NEEDLE_S_R1_DILU_SAMPLE:
        get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos), &param->needle_s_param.t1_src, FLAG_POS_NONE);
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos;
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_DILU, test_cup->cup_sample_tube.diluent_pos, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        if (param->sample_type == SAMPLE_NORMAL) {
            get_special_pos(MOVE_S_SAMPLE_NOR, test_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
            param->tube_type = test_cup->cup_sample_tube.type;
        } else if (param->sample_type == SAMPLE_TEMP) {
            get_special_pos(MOVE_S_TEMP, 0, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
        } else if (param->sample_type == SAMPLE_QC) {
            get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_sample_tube.sample_index), &param->needle_s_param.t2_dst, FLAG_POS_NONE);
            param->qc_reagent_pos = test_cup->cup_sample_tube.sample_index;
            param->tube_type = test_cup->cup_sample_tube.type;
            set_needle_s_qc_type(SAMPLE_QC);
        } else {
            LOG("error! no such sample type\n");
        }
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t3_src, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t3_src, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t3_dst, FLAG_POS_NONE);
        param->take_ul = test_cup->cup_test_attr.needle_s.take_ul;
        param->take_dilu_ul = test_cup->cup_test_attr.needle_s.take_dilu_ul;
        param->take_r1_ul = test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul;
        param->curr_ul = test_cup->cup_test_attr.curr_ul + param->take_ul + param->take_dilu_ul + param->take_r1_ul;
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos;
        param->s_dilu_pos = DILU_IDX_START + test_cup->cup_sample_tube.diluent_pos;
        if (test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable == ATTR_ENABLE) {
            param->mix_pos = pos_cup_trans_mix(test_cup->cup_pos);
            param->mix_stat.time = test_cup->cup_test_attr.test_cup_incubation.mix_time;
            param->mix_stat.rate = test_cup->cup_test_attr.test_cup_incubation.mix_rate;
            param->mix_stat.order_no = test_cup->order_no;
        } else {
            param->mix_pos = MIX_POS_INVALID;
        }
        break;
    case NEEDLE_S_R1_ONLY:
        get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos), &param->needle_s_param.t1_src, FLAG_POS_NONE);
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos;
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        param->take_r1_ul = test_cup->cup_test_attr.needle_r1[R1_ADD2].take_ul;
        param->curr_ul = param->take_r1_ul;
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos;
        param->now_clean_type = test_cup->cup_test_attr.needle_r1[R1_ADD2].post_clean.type;
        break;
    case NEEDLE_S_DILU1_SAMPLE:
        get_special_pos(MOVE_S_DILU, dilu_cup->cup_sample_tube.diluent_pos, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        if (param->sample_type == SAMPLE_NORMAL) {
            get_special_pos(MOVE_S_SAMPLE_NOR, dilu_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
            param->tube_type = dilu_cup->cup_sample_tube.type;
        } else if (param->sample_type == SAMPLE_TEMP) {
            get_special_pos(MOVE_S_TEMP, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else if (param->sample_type == SAMPLE_QC) {
            get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(dilu_cup->cup_sample_tube.sample_index), &param->needle_s_param.t1_dst, FLAG_POS_NONE);
            param->qc_reagent_pos = dilu_cup->cup_sample_tube.sample_index;
            param->tube_type = dilu_cup->cup_sample_tube.type;
        } else {
            LOG("error! no such sample type\n");
        }
        get_special_pos(MOVE_S_ADD_CUP_MIX, dilu_cup->cup_pos, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
        param->take_ul = dilu_cup->cup_dilu_attr.take_ul;
        param->take_dilu_ul = dilu_cup->cup_dilu_attr.dilu_ul;
        param->curr_ul = param->take_ul + param->take_dilu_ul;
        param->now_clean_type = NORMAL_CLEAN;
        param->mix_pos = pos_cup_trans_mix(dilu_cup->cup_pos);
        param->mix_stat.time = 45000;
        param->mix_stat.rate = 10000;
        param->mix_stat.order_no = dilu_cup->order_no;
        param->s_dilu_pos = DILU_IDX_START + dilu_cup->cup_sample_tube.diluent_pos;
        break;
    case NEEDLE_S_DILU2_R1:
        get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos), &param->needle_s_param.t1_src, FLAG_POS_NONE);
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos;
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        param->take_r1_ul = test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul;
        param->curr_ul = param->take_r1_ul;
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos;
        param->now_clean_type = test_cup->cup_test_attr.needle_r1[R1_ADD1].post_clean.type;
        break;
    case NEEDLE_S_DILU2_R1_TWICE:
        get_special_pos(MOVE_S_ADD_REAGENT, needle_s_get_reagent_pos(test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos), &param->needle_s_param.t1_src, FLAG_POS_NONE);
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos;
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        param->take_r1_ul = test_cup->cup_test_attr.needle_r1[R1_ADD2].take_ul;
        param->curr_ul = test_cup->cup_test_attr.curr_ul + param->take_r1_ul;
        param->r1_reagent_pos = test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos;
        param->now_clean_type = test_cup->cup_test_attr.needle_r1[R1_ADD2].post_clean.type;
        break;
    case NEEDLE_S_DILU3_MIX:
        get_special_pos(MOVE_S_ADD_CUP_MIX, dilu_cup->cup_pos, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        param->mix_curr_ul = dilu_cup->cup_dilu_attr.take_ul + dilu_cup->cup_dilu_attr.dilu_ul;
        param->take_mix_ul = dilu_cup->cup_dilu_attr.take_mix_ul;
        param->curr_ul = test_cup->cup_test_attr.curr_ul + param->take_mix_ul;
        param->now_clean_type = test_cup->cup_test_attr.needle_s.post_clean.type;
        if (test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable == ATTR_ENABLE) {
            param->mix_pos = pos_cup_trans_mix(test_cup->cup_pos);
            param->mix_stat.time = test_cup->cup_test_attr.test_cup_incubation.mix_time;
            param->mix_stat.rate = test_cup->cup_test_attr.test_cup_incubation.mix_rate;
            param->mix_stat.order_no = test_cup->order_no;
        } else {
            param->mix_pos = MIX_POS_INVALID;
        }
        break;
    case NEEDLE_S_DILU3_DILU_MIX:
        get_special_pos(MOVE_S_DILU, dilu_cup->cup_sample_tube.diluent_pos, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_ADD_CUP_MIX, dilu_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        }
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_dst, FLAG_POS_NONE);
        param->mix_curr_ul = dilu_cup->cup_dilu_attr.take_ul + dilu_cup->cup_dilu_attr.dilu_ul;
        param->take_mix_ul = dilu_cup->cup_dilu_attr.take_mix_ul;
        param->take_dilu_ul = test_cup->cup_test_attr.needle_s.take_dilu_ul;
        param->curr_ul = test_cup->cup_test_attr.curr_ul + param->take_mix_ul + param->take_dilu_ul;
        param->now_clean_type = test_cup->cup_test_attr.needle_s.post_clean.type;
        param->s_dilu_pos = DILU_IDX_START + dilu_cup->cup_sample_tube.diluent_pos;
        if (test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable == ATTR_ENABLE) {
            param->mix_pos = pos_cup_trans_mix(test_cup->cup_pos);
            param->mix_stat.time = test_cup->cup_test_attr.test_cup_incubation.mix_time;
            param->mix_stat.rate = test_cup->cup_test_attr.test_cup_incubation.mix_rate;
            param->mix_stat.order_no = test_cup->order_no;
        } else {
            param->mix_pos = MIX_POS_INVALID;
        }
        break;
    case NEEDLE_S_SP:
        if (dilu_cup == NULL) {
            tmp_cup = test_cup;
        } else {
            tmp_cup = dilu_cup;
        }
        get_special_pos(MOVE_S_SAMPLE_NOR, tmp_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        param->tube_type = tmp_cup->cup_sample_tube.type;
        get_special_pos(MOVE_S_TEMP, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        LOG("before sp, sample_left_ul = %f, sample_total_volume = %f\n", stmp_stat.sample_left_ul, tmp_cup->cup_sample_tube.sample_volume);
        if (stmp_stat.sample_left_ul <= 0.000001) {
            stmp_stat.sample_left_ul = tmp_cup->cup_sample_tube.sample_volume;
        }
        if (stmp_stat.sample_left_ul > STEM_TEMP_MAX_UL) {
            if (stmp_stat.stemp_left_ul <= 0.000001) {
                /* 切换样本管后的缓存 */
                param->take_ul = STEM_TEMP_MAX_UL + STEM_TEMP_DEAD_UL;
                param->stemp_pre_clean_enable = ATTR_ENABLE;
                stmp_stat.sample_left_ul -= STEM_TEMP_MAX_UL;
            } else {
                /* 补液 */
                param->take_ul = STEM_TEMP_MAX_UL + STEM_TEMP_DEAD_UL - stmp_stat.stemp_left_ul;
                stmp_stat.sample_left_ul -= param->take_ul;
            }
        } else {
            if (stmp_stat.stemp_left_ul <= 0.000001) {
                /* 切换样本管后的缓存 */
                param->take_ul = stmp_stat.sample_left_ul + STEM_TEMP_DEAD_UL;
                param->stemp_pre_clean_enable = ATTR_ENABLE;
            } else {
                /* 补液 */
                param->take_ul = stmp_stat.sample_left_ul;
            }
            stmp_stat.sample_left_ul = 0;
        }
        stmp_stat.sample_tube_id = tmp_cup->cup_sample_tube.sample_tube_id;
        LOG("after sp, sample_left_ul = %f\n", stmp_stat.sample_left_ul);
        param->curr_ul = param->take_ul;
        param->now_clean_type = tmp_cup->cup_test_attr.needle_s.post_clean.type;
        break;
    case NEEDLE_S_SINGLE_SP:
        get_special_pos(MOVE_S_SAMPLE_NOR, test_cup->cup_sample_tube.rack_idx-1, &param->needle_s_param.t1_src, FLAG_POS_NONE);
        if (test_cup->cup_pos == POS_PRE_PROCESSOR) {
            get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        } else {
            get_special_pos(MOVE_S_ADD_CUP_MIX, test_cup->cup_pos, &param->needle_s_param.t1_dst, FLAG_POS_NONE);
        }
        param->tube_type = test_cup->cup_sample_tube.type;
        get_special_pos(MOVE_S_CLEAN, 0, &param->needle_s_param.t2_src, FLAG_POS_NONE);
        param->take_ul = test_cup->cup_test_attr.needle_s.take_ul;
        param->curr_ul = param->take_ul;
        param->now_clean_type = test_cup->cup_test_attr.needle_s.post_clean.type;
        break;
    default:
        break;
    }
    if (param->sample_type == SAMPLE_TEMP) {
        if (param->cmd == NEEDLE_S_NORMAL_SAMPLE || param->cmd == NEEDLE_S_R1_SAMPLE) {
            if ((stmp_stat.sample_left_ul < 0.000001) && ((stmp_stat.stemp_left_ul - test_cup->cup_test_attr.needle_s.take_ul - STEM_TEMP_DEAD_UL - WASTE_SAMPLE_UL) < 0.000001)) {
                /* 缓存池已用完需要清洗缓存池 */
                param->stemp_post_clean_enable = ATTR_ENABLE;
            }
        } else if (param->cmd == NEEDLE_S_DILU_SAMPLE || param->cmd == NEEDLE_S_R1_DILU_SAMPLE) {
            if ((stmp_stat.sample_left_ul < 0.000001) && ((stmp_stat.stemp_left_ul - test_cup->cup_test_attr.needle_s.take_ul - STEM_TEMP_DEAD_UL) < 0.000001)) {
                /* 缓存池已用完需要清洗缓存池 */
                param->stemp_post_clean_enable = ATTR_ENABLE;
            }
        } else if (param->cmd == NEEDLE_S_DILU1_SAMPLE) {
            if ((stmp_stat.sample_left_ul < 0.000001) && ((stmp_stat.stemp_left_ul - dilu_cup->cup_dilu_attr.take_ul - STEM_TEMP_DEAD_UL) < 0.000001)) {
                /* 缓存池已用完需要清洗缓存池 */
                param->stemp_post_clean_enable = ATTR_ENABLE;
            }
        }
    }
    if (param->cmd == NEEDLE_S_NORMAL_SAMPLE || param->cmd == NEEDLE_S_R1_SAMPLE || param->cmd == NEEDLE_S_DILU_SAMPLE ||
        param->cmd == NEEDLE_S_R1_DILU_SAMPLE || param->cmd == NEEDLE_S_SINGLE_SP) {
        report_order_state(test_cup->order_no, OD_SAMPLECOMPLETION);
    } else if (param->cmd == NEEDLE_S_DILU1_SAMPLE) {
        report_order_state(dilu_cup->order_no, OD_SAMPLECOMPLETION);
    }
}

static void needle_s_cmd_check(struct react_cup *dilu_cup, struct react_cup *test_cup, NEEDLE_S_CMD_PARAM *param)
{
    clear_pos_param(&param->needle_s_param);
    param->orderno = 0;
    param->curr_ul = 0;
    param->stemp_pre_clean_enable = ATTR_DISABLE;
    param->stemp_post_clean_enable = ATTR_DISABLE;
    if (dilu_cup == NULL && test_cup == NULL) {
        param->cmd = NEEDLE_S_NONE;
    } else {
        if (test_cup == NULL) {
            /* 有且仅有一个稀释杯的情况 */
            if (dilu_cup->cup_dilu_attr.add_state == CUP_STAT_UNUSED && SAMPLER_ADD_START == module_sampler_add_get()) {
                param->orderno = dilu_cup->order_no;
                if (dilu_cup->cup_sample_tube.sp_hat == SP_WITH_HAT) {
                    if (stmp_stat.sample_tube_id != dilu_cup->cup_sample_tube.sample_tube_id) {
                        stmp_stat.sample_left_ul = 0;
                        stmp_stat.stemp_left_ul = 0;
                    }
                    if (stmp_stat.sample_tube_id == dilu_cup->cup_sample_tube.sample_tube_id &&
                        ((stmp_stat.stemp_left_ul-STEM_TEMP_DEAD_UL+0.000001) > dilu_cup->cup_dilu_attr.take_ul)) {
                        param->sample_type = SAMPLE_TEMP;
                        param->cmd = NEEDLE_S_DILU1_SAMPLE;
                    } else {
                        param->sample_type = SAMPLE_NORMAL;
                        param->cmd = NEEDLE_S_SP;
                    }
                } else {
                    if (dilu_cup->cup_sample_tube.rack_type == REAGENT_QC_SAMPLE) {
                        param->sample_type = SAMPLE_QC;
                    } else {
                        param->sample_type = SAMPLE_NORMAL;
                    }
                    param->cmd = NEEDLE_S_DILU1_SAMPLE;
                }
            } else {
                /* 稀释杯加完样又没有测试杯，只可能处于加样停状态（前处理有且仅有一个稀释杯） */
                param->cmd = NEEDLE_S_NONE;
                /* 更新稀释杯状态为立即丢弃，并回退此反应杯至杯盘中的第一个反应杯 */
                if (SAMPLER_ADD_STOP == module_sampler_add_get()) {
                    if (dilu_cup->cup_id == 0 && dilu_cup->order_no == 0) {
                        /* 生成的虚拟稀释杯 */
                        LOG("nothing to do with this virtual dilu cup.\n");
                    } else {
                        LOG("step back dilu cup pos & create virtual detach cup.\n");
                        /* 生成一个虚拟稀释杯供抓手来丢弃 */
                        virtual_a_detach_cup(dilu_cup->cup_pos);
                        /* 记录该稀释杯order no以便于加样停转变为待机（无反应杯时上报od_error） */
                        virtual_dilu_no_record(dilu_cup->order_no);
                        /* 终止稀释杯混匀 */
                        dilu_cup->cup_dilu_attr.add_state = CUP_STAT_UNUSED;
                        dilu_cup->cup_pos = POS_CUVETTE_SUPPLY_INIT;
                        react_cup_list_show();
                    }
                }
            }
        } else {
            param->orderno = test_cup->order_no;
            if (test_cup->cup_sample_tube.sp_hat == SP_WITH_HAT) {
                needle_s_cmd_deal_with_hat(dilu_cup, test_cup, param);
            } else {
                needle_s_cmd_deal_without_hat(dilu_cup, test_cup, param);
            }
            /* 有测试杯就没有单独的稀释杯，需要将刚才之前设置的虚拟稀释杯order no清除（避免后续误报od_error） */
            virtual_dilu_no_record(0);
        }
    }
    set_needle_s_cmd(param->cmd);
    set_needle_s_qc_type(SAMPLE_NORMAL);
    needle_s_cmd_set_pos(dilu_cup, test_cup, param);
}

static void needle_s_cmd_update(struct react_cup *dilu_cup, struct react_cup *test_cup, NEEDLE_S_CMD_PARAM *param)
{
    if (test_cup != NULL && param->cmd != NEEDLE_S_SP && param->cmd != NEEDLE_S_DILU1_SAMPLE && param->cmd != NEEDLE_S_NONE) {
        test_cup->cup_test_attr.curr_ul = param->curr_ul;
    }
    LOG("needle s curr_ul = %f\n", param->curr_ul);
    switch (param->cmd) {
    case NEEDLE_S_NORMAL_SAMPLE:
        if (param->sample_type == SAMPLE_TEMP) {
            stmp_stat.stemp_left_ul -= (test_cup->cup_test_attr.needle_s.take_ul + WASTE_SAMPLE_UL);
        } else {
            stmp_stat.stemp_left_ul = 0;
        }
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_R1_SAMPLE:
        if (param->sample_type == SAMPLE_TEMP) {
            stmp_stat.stemp_left_ul -= (test_cup->cup_test_attr.needle_s.take_ul + WASTE_SAMPLE_UL);
        } else {
            stmp_stat.stemp_left_ul = 0;
        }
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_ENABLE;
        param->pre_clean_type = test_cup->cup_test_attr.needle_r1[R1_ADD1].post_clean.type;
        break;
    case NEEDLE_S_DILU_SAMPLE:
        if (param->sample_type == SAMPLE_TEMP) {
            stmp_stat.stemp_left_ul -= (test_cup->cup_test_attr.needle_s.take_ul);
        } else {
            stmp_stat.stemp_left_ul = 0;
        }
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_R1_DILU_SAMPLE:
        if (param->sample_type == SAMPLE_TEMP) {
            stmp_stat.stemp_left_ul -= (test_cup->cup_test_attr.needle_s.take_ul);
        } else {
            stmp_stat.stemp_left_ul = 0;
        }
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_ENABLE;
        param->pre_clean_type = test_cup->cup_test_attr.needle_r1[R1_ADD1].post_clean.type;
        break;
    case NEEDLE_S_R1_ONLY:
        test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat = CUP_STAT_USED;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_DILU1_SAMPLE:
        dilu_cup->cup_dilu_attr.add_state = CUP_STAT_USED;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_DILU2_R1:
        test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat = CUP_STAT_USED;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_DILU2_R1_TWICE:
        test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat = CUP_STAT_USED;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_DILU3_MIX:
        if (param->sample_type == SAMPLE_TEMP) {
            stmp_stat.stemp_left_ul -= (dilu_cup->cup_dilu_attr.take_ul);
        } else {
            stmp_stat.stemp_left_ul = 0;
        }
        dilu_cup->cup_dilu_attr.trans_state = CUP_STAT_USED;
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_DILU3_DILU_MIX:
        if (param->sample_type == SAMPLE_TEMP) {
            stmp_stat.stemp_left_ul -= (dilu_cup->cup_dilu_attr.take_ul);
        } else {
            stmp_stat.stemp_left_ul = 0;
        }
        dilu_cup->cup_dilu_attr.trans_state = CUP_STAT_USED;
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_SP:
        stmp_stat.stemp_left_ul += param->curr_ul;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_SINGLE_SP:
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_USED;
        test_cup->cup_test_attr.pre_stat = PRE_IS_READY;
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    case NEEDLE_S_NONE:
        param->pre_clean_enable = ATTR_DISABLE;
        param->pre_clean_type = NORMAL_CLEAN;
        break;
    default:
        break;
    }
    LOG("stemp_left_ul = %f\n", stmp_stat.stemp_left_ul);
}

static void *needle_s_work_task(void *arg)
{
    struct list_head *needle_s_cup_list = NULL;
    struct react_cup *pos = NULL, *n = NULL, *dilu_cup = NULL, *test_cup = NULL;
    int res = 0;
    NEEDLE_S_CMD_PARAM needle_s_cmd_param = {0};

    /* 样本针自检后在洗针位 */
    get_special_pos(MOVE_S_CLEAN, 0, &needle_s_cmd_param.needle_s_param.t1_src, FLAG_POS_NONE);
    set_pos(&needle_s_cmd_param.needle_s_param.cur, needle_s_cmd_param.needle_s_param.t1_src.x, needle_s_cmd_param.needle_s_param.t1_src.y, NEEDLE_S_CLEAN_POS);

    while (1) {
        module_monitor_wait();   /* 阻塞等待生产模块同步 */
        if (module_fault_stat_get() & MODULE_FAULT_LEVEL2) {
            module_response_cups(MODULE_NEEDLE_S, 0);  /* 举手，且本模块停止工作 */
            continue;
        }
        LOG("needle s do something...\n");
        dilu_cup = NULL;
        test_cup = NULL;
        leave_singal_wait(LEAVE_C_GET_INIT_POS_READY);
        reset_cup_list(NEEDLE_S_CUP_LIST);
        res = module_request_cups(MODULE_NEEDLE_S, &needle_s_cup_list);
        needle_s_cup_list_show();
        if (res == 1) {
            /* 获取反应杯信息错误 */
            LOG("ERROR! needle s get cup info failed\n");
            module_response_cups(MODULE_NEEDLE_S, 0);
            continue;
        }
        list_for_each_entry_safe(pos, n, needle_s_cup_list, needle_s_sibling) {
            /* 正常获取反应杯状态时，稀释杯应该在测试杯前 */
            if (pos->cup_type == DILU_CUP) {
                dilu_cup = pos;
            } else if (pos->cup_type == TEST_CUP) {
                if (pos->cup_test_attr.pre_stat == PRE_IS_NOT_READY) {
                    test_cup = pos;
                    break;
                }
            }
        }

        needle_s_cmd_check(dilu_cup, test_cup, &needle_s_cmd_param);
        needle_s_cmd_work(&needle_s_cmd_param);
        needle_s_cmd_update(dilu_cup, test_cup, &needle_s_cmd_param);

        leave_singal_send(LEAVE_S_DILU_TRANS_READY);
        LOG("needle s done...\n");
        module_response_cups(MODULE_NEEDLE_S, 1);  /* 举手，表示模块已完成本周期工作 */
    }
    return NULL;
}

/* 称量模式 */
int needle_s_add_test(int add_ul)
{
    NEEDLE_S_CMD_PARAM param = {0};

    set_clear_needle_s_pos(0);
    clear_pos_param(&param.needle_s_param);
    param.orderno = 0;
    param.stemp_pre_clean_enable = ATTR_DISABLE;
    param.stemp_post_clean_enable = ATTR_DISABLE;
    param.sample_type = SAMPLE_NORMAL;
    param.tube_type = PP_2_7;

    if (reset_all_motors()) {
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return -1;
    }

    get_special_pos(MOVE_S_SAMPLE_NOR, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
    get_special_pos(MOVE_S_ADD_CUP_MIX, POS_PRE_PROCESSOR_MIX1, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);
    set_pos(&param.needle_s_param.cur, 0, 0, NEEDLE_S_CLEAN_POS);
    param.take_ul = (double)add_ul;
    if (add_ul < 50) {
        param.curr_ul = param.take_ul+150;
    } else {
        param.curr_ul = param.take_ul;
    }
    param.now_clean_type = NORMAL_CLEAN;
    param.cmd = NEEDLE_S_NORMAL_SAMPLE;
    param.tube_type = PP_1_8;
    leave_singal_send(LEAVE_C_FRAG18);
    needle_s_calc_mode = 1;
    needle_s_cmd_work(&param);
    needle_s_calc_mode = 0;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 12000, 50000, MOTOR_DEFAULT_TIMEOUT, 0.0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        return -1;
    }
    set_pos(&param.needle_s_param.cur, 0, 0, param.needle_s_param.cur.z);
    FAULT_CHECK_END();

    return 0;
}

/* 稀释称量模式 */
int needle_s_dilu_add_test(int dilu_add_ul, int add_ul)
{
    NEEDLE_S_CMD_PARAM param = {0};

    set_clear_needle_s_pos(0);
    clear_pos_param(&param.needle_s_param);
    param.orderno = 0;
    param.stemp_pre_clean_enable = ATTR_DISABLE;
    param.stemp_post_clean_enable = ATTR_DISABLE;
    param.sample_type = SAMPLE_NORMAL;
    param.tube_type = PP_2_7;

    if (reset_all_motors()) {
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return -1;
    }

    get_special_pos(MOVE_S_DILU, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
    get_special_pos(MOVE_S_SAMPLE_NOR, 0, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
    get_special_pos(MOVE_S_ADD_CUP_MIX, POS_PRE_PROCESSOR_MIX1, &param.needle_s_param.t2_src, FLAG_POS_NONE);
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_dst, FLAG_POS_NONE);
    set_pos(&param.needle_s_param.cur, 0, 0, NEEDLE_S_CLEAN_POS);
    param.take_ul = (double)add_ul;
    param.take_dilu_ul = (double)dilu_add_ul;
    param.curr_ul = param.take_ul + param.take_dilu_ul;
    if (param.curr_ul < 50) {
        param.curr_ul = param.take_ul + param.take_dilu_ul+150;
    } else {
        param.curr_ul = param.take_ul + param.take_dilu_ul;
    }

    param.now_clean_type = NORMAL_CLEAN;
    param.cmd = NEEDLE_S_DILU_SAMPLE;
    param.s_dilu_pos = POS_REAGENT_DILU_1;
    leave_singal_send(LEAVE_C_FRAG18);
    needle_s_calc_mode = 1;
    needle_s_cmd_work(&param);
    needle_s_calc_mode = 0;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 12000, 50000, MOTOR_DEFAULT_TIMEOUT, 0.0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        return -1;
    }
    set_pos(&param.needle_s_param.cur, 0, 0, param.needle_s_param.cur.z);
    FAULT_CHECK_END();

    return 0;
}

int needle_s_sp_add_test(int add_ul)
{
    NEEDLE_S_CMD_PARAM param = {0};

    set_clear_needle_s_pos(0);
    clear_pos_param(&param.needle_s_param);
    param.orderno = 0;

    if (reset_all_motors()) {
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return -1;
    }

    get_special_pos(MOVE_S_SAMPLE_NOR, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
    get_special_pos(MOVE_S_TEMP, 0, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);

    param.stemp_pre_clean_enable = ATTR_ENABLE;
    param.stemp_post_clean_enable = ATTR_DISABLE;
    param.sample_type = SAMPLE_NORMAL;
    param.take_ul = (double)add_ul+160;
    param.curr_ul = param.take_ul;
    param.now_clean_type = NORMAL_CLEAN;
    param.cmd = NEEDLE_S_SP;
    leave_singal_send(LEAVE_C_FRAG18);
    needle_s_calc_mode = 1;
    needle_s_cmd_work(&param);
    needle_s_calc_mode = 0;
    stmp_stat.stemp_left_ul = param.curr_ul;

    get_special_pos(MOVE_S_TEMP, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
    get_special_pos(MOVE_S_ADD_CUP_MIX, POS_PRE_PROCESSOR_MIX1, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);
    set_pos(&param.needle_s_param.cur, 0, 0, NEEDLE_S_CLEAN_POS);

    param.stemp_pre_clean_enable = ATTR_DISABLE;
    param.stemp_post_clean_enable = ATTR_ENABLE;
    param.sample_type = SAMPLE_TEMP;
    param.take_ul = (double)add_ul;
    if (add_ul < 50) {
        param.curr_ul = param.take_ul+150;
    } else {
        param.curr_ul = param.take_ul;
    }
    param.now_clean_type = NORMAL_CLEAN;
    param.cmd = NEEDLE_S_NORMAL_SAMPLE;
    leave_singal_send(LEAVE_C_FRAG18);
    needle_s_calc_mode = 1;
    needle_s_cmd_work(&param);
    needle_s_calc_mode = 0;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 12000, 50000, MOTOR_DEFAULT_TIMEOUT, 0.0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        return -1;
    }
    set_pos(&param.needle_s_param.cur, 0, 0, param.needle_s_param.cur.z);
    FAULT_CHECK_END();

    return 0;
}

/* 批量称量模式（10个） */
int needle_s_muti_add_test(int add_ul)
{
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, i = 0;
    time_fragment_t *time_frag = NULL;
    module_param_t catcher_param = {0};
    NEEDLE_S_CMD_PARAM param = {0};

    catcher_rs485_init();
    motor_attr_init(&motor_x, &motor_z);
    set_clear_needle_s_pos(0);

    if (reset_all_motors()) {
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return 1;
    }

    /* 避让抓手 */
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t1_src.x, param.needle_s_param.t1_src.y, 12000, 50000, MOTOR_DEFAULT_TIMEOUT, 0.0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        return 1;
    }
    set_pos(&param.needle_s_param.cur, param.needle_s_param.t1_src.x, param.needle_s_param.t1_src.y, NEEDLE_S_CLEAN_POS);
    FAULT_CHECK_END();

    time_frag = catcher_time_frag_table_get();

    for (i=0;i<10;i++) {
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1, &catcher_param.t1_dst, FLAG_POS_UNLOCK);

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
        /* 抓手Y轴回原点避让 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_x.step = abs(catcher_param.cur.y);
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, 0, catcher_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");

        /* 常规加样模式 */
        clear_pos_param(&param.needle_s_param);
        param.orderno = 0;
        param.stemp_pre_clean_enable = ATTR_DISABLE;
        param.stemp_post_clean_enable = ATTR_DISABLE;
        param.sample_type = SAMPLE_NORMAL;
        param.tube_type = PP_2_7;

        get_special_pos(MOVE_S_SAMPLE_NOR, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_ADD_CUP_MIX, POS_PRE_PROCESSOR_MIX1, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);
        param.take_ul = (double)add_ul;
        if (add_ul < 50) {
            param.curr_ul = param.take_ul+150;
        } else {
            param.curr_ul = param.take_ul;
        }
        param.now_clean_type = NORMAL_CLEAN;
        param.cmd = NEEDLE_S_NORMAL_SAMPLE;
        param.tube_type = PP_1_8;
        leave_singal_send(LEAVE_C_FRAG18);
        needle_s_calc_mode = 1;
        needle_s_cmd_work(&param);
        needle_s_calc_mode = 0;

        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1, &catcher_param.t2_src, FLAG_POS_UNLOCK);

        if (catcher_param.t2_src.x != catcher_param.cur.x || catcher_param.t2_src.y != catcher_param.cur.y) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t2_src.x - catcher_param.cur.x) > abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x))) {
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

/* 批量穿刺称量模式（10个） */
int needle_s_sp_muti_add_test(int add_ul)
{
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0, i = 0;
    time_fragment_t *time_frag = NULL;
    module_param_t catcher_param = {0};
    NEEDLE_S_CMD_PARAM param = {0};

    catcher_rs485_init();
    motor_attr_init(&motor_x, &motor_z);
    set_clear_needle_s_pos(0);

    if (reset_all_motors()) {
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return 1;
    }

    /* 避让抓手 */
    get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t1_src.x, param.needle_s_param.t1_src.y, 12000, 50000, MOTOR_DEFAULT_TIMEOUT, 0.0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
        return 1;
    }
    set_pos(&param.needle_s_param.cur, param.needle_s_param.t1_src.x, param.needle_s_param.t1_src.y, NEEDLE_S_CLEAN_POS);
    FAULT_CHECK_END();

    time_frag = catcher_time_frag_table_get();

    for (i=0;i<10;i++) {
        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t1_src, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1, &catcher_param.t1_dst, FLAG_POS_UNLOCK);

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
        /* 抓手Y轴回原点避让 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_x.step = abs(catcher_param.cur.y);
        calc_acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -catcher_param.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
            return 1;
        }
        set_pos(&catcher_param.cur, catcher_param.cur.x, 0, catcher_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");

        /* 穿刺加样模式 */
        clear_pos_param(&param.needle_s_param);
        param.orderno = 0;
        get_special_pos(MOVE_S_SAMPLE_NOR, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_TEMP, 0, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);

        param.stemp_pre_clean_enable = ATTR_ENABLE;
        param.stemp_post_clean_enable = ATTR_DISABLE;
        param.sample_type = SAMPLE_NORMAL;
        param.take_ul = (double)add_ul+160;
        param.curr_ul = param.take_ul;
        param.now_clean_type = NORMAL_CLEAN;
        param.cmd = NEEDLE_S_SP;
        leave_singal_send(LEAVE_C_FRAG18);
        needle_s_calc_mode = 1;
        needle_s_cmd_work(&param);
        needle_s_calc_mode = 0;
        stmp_stat.stemp_left_ul = param.curr_ul;

        get_special_pos(MOVE_S_TEMP, 0, &param.needle_s_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_ADD_CUP_MIX, POS_PRE_PROCESSOR_MIX1, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);

        param.stemp_pre_clean_enable = ATTR_DISABLE;
        param.stemp_post_clean_enable = ATTR_ENABLE;
        param.sample_type = SAMPLE_TEMP;
        param.take_ul = (double)add_ul;
        if (add_ul < 50) {
            param.curr_ul = param.take_ul+150;
        } else {
            param.curr_ul = param.take_ul;
        }
        param.now_clean_type = NORMAL_CLEAN;
        param.cmd = NEEDLE_S_NORMAL_SAMPLE;
        leave_singal_send(LEAVE_C_FRAG18);
        needle_s_calc_mode = 1;
        needle_s_cmd_work(&param);
        needle_s_calc_mode = 0;

        get_special_pos(MOVE_C_INCUBATION, POS_INCUBATION_WORK_1+i, &catcher_param.t2_dst, FLAG_POS_UNLOCK);
        get_special_pos(MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1, &catcher_param.t2_src, FLAG_POS_UNLOCK);

        if (catcher_param.t2_src.x != catcher_param.cur.x || catcher_param.t2_src.y != catcher_param.cur.y) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            if (abs(catcher_param.t2_src.x - catcher_param.cur.x) > abs((catcher_param.t2_src.y - catcher_param.cur.y) - (catcher_param.t2_src.x - catcher_param.cur.x))) {
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

int needle_s_poweron_check(int times, s_aging_speed_mode_t mode, s_aging_clean_mode_t clean_mode)
{
    NEEDLE_S_CMD_PARAM param = {0};

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    time_fragment_t time_fast[25] = {0};

    motor_attr_init(&motor_x, &motor_z);
    set_clear_needle_s_pos(0);
    clear_pos_param(&param.needle_s_param);
    param.orderno = 0;
    param.stemp_pre_clean_enable = ATTR_DISABLE;
    param.stemp_post_clean_enable = ATTR_DISABLE;
    param.sample_type = SAMPLE_NORMAL;

    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return 1;
    }

    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
        return 1;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        return 1;
    }
    set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
    motor_reset(MOTOR_NEEDLE_S_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Y);
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_X, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_X, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_X);
        return 1;
    }

    if (mode == S_NORMAL_MODE) {
        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_R1_DILU_SAMPLE));
    } else {
        time_frag = time_fast;
    }
    int cnt = 0;
    while (1) {
        if (times != 0) {
            if (cnt >= times) {
                break;
            }
        }
        LOG("==================cnt = %d===================\n", cnt);
        get_special_pos(MOVE_S_ADD_REAGENT, POS_REAGENT_TABLE_S_IN, &param.needle_s_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_DILU, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_SAMPLE_NOR, (cnt%60), &param.needle_s_param.t2_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_ADD_CUP_PRE, 0, &param.needle_s_param.t3_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t3_dst, FLAG_POS_NONE);

        param.take_ul = (double)10;
        param.take_dilu_ul = (double)100;
        param.take_r1_ul = (double)150;
        param.curr_ul = param.take_ul + param.take_dilu_ul + param.take_r1_ul;

        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (clean_mode == S_NORMAL_CLEAN_MODE) {
            s_normal_inside_clean();
        } else {
            sleep(2);
        }
        FAULT_CHECK_END();

        PRINT_FRAG_TIME("FRAG0");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.t1_src.x - param.needle_s_param.cur.x) > abs((param.needle_s_param.t1_src.y - param.needle_s_param.cur.y) - (param.needle_s_param.t1_src.x - param.needle_s_param.cur.x))) {
            motor_x.step = abs(param.needle_s_param.t1_src.x - param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t1_src.y - param.needle_s_param.cur.y) - (param.needle_s_param.t1_src.x - param.needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t1_src.x - param.needle_s_param.cur.x,
                                    param.needle_s_param.t1_src.y - param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
            return 1;
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t1_src.x, param.needle_s_param.t1_src.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        /* R1液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t1_src.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, param.needle_s_param.t1_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_absorb_ul(NEEDLE_TYPE_S, param.take_r1_ul + NEEDLE_S_R1_MORE);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG4");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param.take_r1_ul, &param.calc_pos);
        if (abs(param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x) > abs((param.needle_s_param.t1_dst.y - param.needle_s_param.cur.y + param.calc_pos.y) - abs(param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x))) {
            motor_x.step = abs(param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t1_dst.y - param.needle_s_param.cur.y + param.calc_pos.y) - abs(param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x,
                                    param.needle_s_param.t1_dst.y - param.needle_s_param.cur.y + param.calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG5].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t1_dst.x + param.calc_pos.x, param.needle_s_param.t1_dst.y + param.calc_pos.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.t1_dst.z - param.needle_s_param.cur.z + param.calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t1_dst.z - param.needle_s_param.cur.z + param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, param.needle_s_param.t1_dst.z + param.calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_release_ul(NEEDLE_TYPE_S, param.take_r1_ul, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        /* 洗针 */
        if (clean_mode == S_NORMAL_CLEAN_MODE) {
            s_normal_inside_clean();
        } else {
            sleep(2);
        }
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.t2_src.x - param.needle_s_param.cur.x) > abs(param.needle_s_param.t2_src.y - param.needle_s_param.cur.y - (param.needle_s_param.t2_src.x - param.needle_s_param.cur.x))) {
            motor_x.step = abs(param.needle_s_param.t2_src.x - param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param.needle_s_param.t2_src.y - param.needle_s_param.cur.y - (param.needle_s_param.t2_src.x - param.needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t2_src.x - param.needle_s_param.cur.x,
                                    param.needle_s_param.t2_src.y - param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG10].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
            return 1;
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t2_src.x, param.needle_s_param.t2_src.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        /* 稀释液液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t2_src.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, param.needle_s_param.t2_src.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_absorb_ul(NEEDLE_TYPE_S_DILU, param.take_dilu_ul + NEEDLE_S_DILU_MORE);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG13].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG13");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.t2_dst.x - param.needle_s_param.cur.x) > abs((param.needle_s_param.t2_dst.y - param.needle_s_param.cur.y) - (param.needle_s_param.t2_dst.x - param.needle_s_param.cur.x))) {
            motor_x.step = abs(param.needle_s_param.t2_dst.x - param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t2_dst.y - param.needle_s_param.cur.y) - (param.needle_s_param.t2_dst.x - param.needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG14].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, ul_to_step(NEEDLE_TYPE_S_DILU, 10), h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t2_dst.x - param.needle_s_param.cur.x,
                                    param.needle_s_param.t2_dst.y - param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG14].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
            return 1;
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t2_dst.x, param.needle_s_param.t2_dst.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG14");

        /* 样本液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t2_dst.z - param.needle_s_param.cur.z, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, param.needle_s_param.t2_dst.z);
        FAULT_CHECK_END();

        PRINT_FRAG_TIME("FRAG15");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_absorb_ul(NEEDLE_TYPE_S_DILU, param.take_ul);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG16");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 0);

        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG17");
        if (param.stemp_post_clean_enable == ATTR_ENABLE) {
            LOG("stemp post clean!\n");
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_calc_add_pos(NEEDLE_TYPE_S, (int)param.curr_ul, &param.calc_pos);
        if (abs(param.needle_s_param.t3_src.x - param.needle_s_param.cur.x + param.calc_pos.x) > abs((param.needle_s_param.t3_src.y - param.needle_s_param.cur.y + param.calc_pos.y) - (param.needle_s_param.t3_src.x - param.needle_s_param.cur.x + param.calc_pos.x))) {
            motor_x.step = abs(param.needle_s_param.t3_src.x - param.needle_s_param.cur.x + param.calc_pos.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t3_src.y - param.needle_s_param.cur.y + param.calc_pos.y) - (param.needle_s_param.t3_src.x - param.needle_s_param.cur.x + param.calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG18].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t3_src.x - param.needle_s_param.cur.x + param.calc_pos.x,
                                    param.needle_s_param.t3_src.y - param.needle_s_param.cur.y + param.calc_pos.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG18].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t3_src.x + param.calc_pos.x, param.needle_s_param.t3_src.y + param.calc_pos.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG18");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.t3_src.z - param.needle_s_param.cur.z + param.calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG19].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t3_src.z - param.needle_s_param.cur.z + param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, param.needle_s_param.t3_src.z + param.calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG19");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_release_ul(NEEDLE_TYPE_S_BOTH, param.take_dilu_ul+param.take_ul+10, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG20");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG21].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG21");
        if (param.mix_pos != MIX_POS_INVALID) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
            /* 启动混匀 */
            FAULT_CHECK_END();
        }
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.cur.x) > abs(param.needle_s_param.cur.y)) {
            motor_x.step = abs(param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param.needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG22].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param.needle_s_param.cur.x, param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG22].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, 0, 0, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG22");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.t3_dst.x - param.needle_s_param.cur.x) > abs((param.needle_s_param.t3_dst.y - param.needle_s_param.cur.y) - (param.needle_s_param.t3_dst.x - param.needle_s_param.cur.x))) {
            motor_x.step = abs(param.needle_s_param.t3_dst.x - param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t3_dst.y - param.needle_s_param.cur.y) - (param.needle_s_param.t3_dst.x - param.needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG23].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t3_dst.x - param.needle_s_param.cur.x,
                                    param.needle_s_param.t3_dst.y - param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG23].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t3_dst.x, param.needle_s_param.t3_dst.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG23");
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG24].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG24");
        cnt++;
    }

    return 0;
}

int needle_s_sp_aging_test(int tube_cnt, int max_pos)
{
    NEEDLE_S_CMD_PARAM param = {0};

    time_fragment_t *time_frag = NULL;
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    #if 0
    liquid_detect_arg_t needle_s_liq_detect_arg = {0};
    needle_s_liq_detect_arg.mode = NORMAL_DETECT_MODE;
    #endif

    motor_attr_init(&motor_x, &motor_z);
    set_clear_needle_s_pos(0);
    clear_pos_param(&param.needle_s_param);
    param.orderno = 0;
    param.stemp_pre_clean_enable = ATTR_ENABLE;
    param.sample_type = SAMPLE_NORMAL;
    param.take_ul = (double)0.01;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_NEEDLE_S_PUMP, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_PUMP);
        return 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
        return 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        return 1;
    }
    set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_NEEDLE_S_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Y);
        return 1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
    motor_reset(MOTOR_NEEDLE_S_X, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_X, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_X);
        return 1;
    }
    FAULT_CHECK_END();

    int cnt = 0;
    while (1) {
        LOG("====cnt = %d====tube_cnt = %d====max_pos = %d====\n", cnt, tube_cnt, max_pos);
        if (cnt >= (max_pos * tube_cnt)) {
            LOG("===================finish========================\n");
            return 0;
        }
        get_special_pos(MOVE_S_SAMPLE_NOR, (cnt%max_pos), &param.needle_s_param.t1_src, FLAG_POS_NONE);
        get_special_pos(MOVE_S_TEMP, 0, &param.needle_s_param.t1_dst, FLAG_POS_NONE);
        get_special_pos(MOVE_S_CLEAN, 0, &param.needle_s_param.t2_src, FLAG_POS_NONE);

        time_frag = s_time_frag_table_get(needle_s_cmd_trans_time_frag(NEEDLE_S_SP));

        if (param.stemp_pre_clean_enable == ATTR_ENABLE) {
            /* 准备暂存池 */
            stage_pool_self_clear(STEGE_POOL_PRE_CLEAR);
            LOG("stemp get ready!\n");
        }
        liq_s_handle_sampler(1);
        usleep(200*1000);
        liq_s_handle_sampler(0);
        PRINT_FRAG_TIME("FRAG0");
        module_sync_time(get_module_base_time(), time_frag[FRAG0].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.t1_src.x - param.needle_s_param.cur.x) > abs((param.needle_s_param.t1_src.y - param.needle_s_param.cur.y) - (param.needle_s_param.t1_src.x - param.needle_s_param.cur.x))) {
            motor_x.step = abs(param.needle_s_param.t1_src.x - param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t1_src.y - param.needle_s_param.cur.y) - (param.needle_s_param.t1_src.x - param.needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG1].cost_time);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, 640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t1_src.x - param.needle_s_param.cur.x,
                                    param.needle_s_param.t1_src.y - param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, time_frag[FRAG1].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
            return 1;
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t1_src.x, param.needle_s_param.t1_src.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG1");
        module_sync_time(get_module_base_time(), time_frag[FRAG1].end_time);
        /* 压帽液面探测 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        #if 0
        needle_s_liq_detect_arg.hat_enable = ATTR_ENABLE;
        needle_s_liq_detect_arg.needle = NEEDLE_TYPE_S;
        needle_s_liq_detect_arg.order_no = param.orderno;
        needle_s_liq_detect_arg.s_cur_step = param.needle_s_param.cur.z;
        needle_s_liq_detect_arg.take_ul = param.take_ul;
        needle_s_liq_detect_arg.reag_idx = 1;
        param.needle_s_param.cur.z = liquid_detect_start(needle_s_liq_detect_arg);
        if (param.needle_s_param.cur.z < EMAX) {
            LOG("liquid detect error!\n");
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        #else
        motor_z.step = param.needle_s_param.t1_src.z;
        motor_z.acc = calc_motor_move_in_time(&motor_z, 0.4);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t1_src.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        motor_z.step = 15000;
        motor_z.acc = calc_motor_move_in_time(&motor_z, 1.4);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, 15000, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        param.needle_s_param.cur.z = param.needle_s_param.t1_src.z+15000;
        #endif
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG2");
        module_sync_time(get_module_base_time(), time_frag[FRAG2].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        clot_write_log("sampler data: %d\t", param.orderno);
        needle_absorb_ul(NEEDLE_TYPE_S, param.take_ul); /* 穿刺模式多吸35ul */
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG3");
        module_sync_time(get_module_base_time(), time_frag[FRAG3].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_outside_clean(1);
        if (param.needle_s_param.cur.z < EMAX) {
            motor_z.acc = NEEDLE_S_Z_REMOVE_ACC / 2;
        } else {
            motor_z.step = abs(param.needle_s_param.cur.z);
            motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG4].cost_time);
        }
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        s_normal_outside_clean(0);
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
        FAULT_CHECK_END();
        slip_liquid_detect_rcd_set(NEEDLE_TYPE_S, ATTR_DISABLE);
        PRINT_FRAG_TIME("FRAG4");
        module_sync_time(get_module_base_time(), time_frag[FRAG4].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_s_calc_stemp_pos((int)param.take_ul, &param.calc_pos);
        if (abs(param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x) > abs((param.needle_s_param.t1_dst.y - param.needle_s_param.cur.y + param.calc_pos.y) - (param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x))) {
            motor_x.step = abs(param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t1_dst.y - param.needle_s_param.cur.y + param.calc_pos.y) - (param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG5].cost_time);
        /* XY移动至暂存池时弃液10ul */
        liq_s_handle_sampler(1);
        motor_move_ctl_async(MOTOR_NEEDLE_S_PUMP, CMD_MOTOR_MOVE_STEP, -640, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_PUMP].acc);
        motor_move_dual_ctl_async(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t1_dst.x - param.needle_s_param.cur.x + param.calc_pos.x,
                                    param.needle_s_param.t1_dst.y - param.needle_s_param.cur.y + param.calc_pos.y, motor_x.speed, motor_x.acc, time_frag[FRAG5].cost_time);
        if (motor_timedwait(MOTOR_NEEDLE_S_PUMP, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y pump move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_PUMP);
            return 1;
        }
        if (motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t1_dst.x + param.calc_pos.x, param.needle_s_param.t1_dst.y + param.calc_pos.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG5");
        module_sync_time(get_module_base_time(), time_frag[FRAG5].end_time);
        liq_s_handle_sampler(0);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.t1_dst.z - param.needle_s_param.cur.z + param.calc_pos.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG6].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, param.needle_s_param.t1_dst.z - param.needle_s_param.cur.z + param.calc_pos.z, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, param.needle_s_param.t1_dst.z + param.calc_pos.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG6");
        module_sync_time(get_module_base_time(), time_frag[FRAG6].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        needle_release_ul(NEEDLE_TYPE_S, param.take_ul, 0);
        sleep(1);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG7");
        module_sync_time(get_module_base_time(), time_frag[FRAG7].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG8].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, 0);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG8");
        module_sync_time(get_module_base_time(), time_frag[FRAG8].end_time);
        /* 样本针在本时间片复位一次 */
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.cur.x) > abs(param.needle_s_param.cur.y)) {
            motor_x.step = abs(param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs(param.needle_s_param.cur.y);
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG9].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_GROUP_RESET, param.needle_s_param.cur.x, param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG9].cost_time)) {
            LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, 0, 0, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG9");
        module_sync_time(get_module_base_time(), time_frag[FRAG9].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        if (abs(param.needle_s_param.t2_src.x - param.needle_s_param.cur.x) > abs((param.needle_s_param.t2_src.y - param.needle_s_param.cur.y) - (param.needle_s_param.t2_src.x - param.needle_s_param.cur.x))) {
            motor_x.step = abs(param.needle_s_param.t2_src.x - param.needle_s_param.cur.x);
        } else {
            motor_x.step = abs((param.needle_s_param.t2_src.y - param.needle_s_param.cur.y) - (param.needle_s_param.t2_src.x - param.needle_s_param.cur.x));
        }
        motor_x.acc = calc_motor_move_in_time(&motor_x, time_frag[FRAG10].cost_time);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, param.needle_s_param.t2_src.x - param.needle_s_param.cur.x,
                                    param.needle_s_param.t2_src.y - param.needle_s_param.cur.y, motor_x.speed, motor_x.acc, MOTOR_DEFAULT_TIMEOUT, time_frag[FRAG10].cost_time)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.t2_src.x, param.needle_s_param.t2_src.y, param.needle_s_param.cur.z);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG10");
        module_sync_time(get_module_base_time(), time_frag[FRAG10].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        motor_z.step = abs(param.needle_s_param.cur.z);
        motor_z.acc = calc_motor_move_in_time(&motor_z, time_frag[FRAG11].cost_time);
        if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, motor_z.speed, motor_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
            return 1;
        }
        set_pos(&param.needle_s_param.cur, param.needle_s_param.cur.x, param.needle_s_param.cur.y, NEEDLE_S_CLEAN_POS);
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG11");
        module_sync_time(get_module_base_time(), time_frag[FRAG11].end_time);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        s_normal_inside_clean();
        FAULT_CHECK_END();
        PRINT_FRAG_TIME("FRAG12");
        module_sync_time(get_module_base_time(), time_frag[FRAG12].end_time);
        PRINT_FRAG_TIME("FRAG13");
        module_sync_time(get_module_base_time(), time_frag[FRAG13].end_time);
        cnt++;
    }

    return 0;
}

int needle_s_avoid_catcher(void)
{
    pos_t pos_avoid = {0};

    set_clear_needle_s_pos(0);

    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needle S.z timeout.\n");
        return 1;
    }
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, NEEDLE_S_Z_REMOVE_SPEED, NEEDLE_S_Z_REMOVE_ACC, MOTOR_DEFAULT_TIMEOUT)) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Z);
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needle S.y timeout.\n");
        return 1;
    }
    motor_reset(MOTOR_NEEDLE_S_X, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_X, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needle S.x timeout.\n");
        return 1;
    }

    get_special_pos(MOVE_S_SAMPLE_NOR, 0, &pos_avoid, FLAG_POS_NONE);
    if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, pos_avoid.x, pos_avoid.y,
            h3600_conf_get()->motor[MOTOR_NEEDLE_S_Y].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_Y].acc, MOTOR_DEFAULT_TIMEOUT, 0.4)) {
        LOG("move needle S xy failed.\n");
        return 1;
    }

    return 0;
}

/* 初始化样本针模块 */
int needle_s_init(void)
{
    pthread_t needle_s_main_thread;

    sq_list_init();
    reag_table_occupy_flag_set(0);

    if (0 != pthread_create(&needle_s_main_thread, NULL, needle_s_work_task, NULL)) {
        LOG("needle s work thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


