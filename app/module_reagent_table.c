#include "thrift_handler.h"
#include "module_reagent_table.h"

static int table_curr_steps = 0;
static reag_tab_stage_t table_stage = 0;
static int chg_list_init_complete = 0;
static int last_table_move_time = 0;
static reag_scan_info_t reag_prop = {0};
static int table_occupied_flag = -1;//试剂盘占用标志
static struct list_head reag_change_list;
static sem_t scanner_restart_sem;//用于扫码重启等待信号
static uint8_t reag_table_area[REAG_IDX_END] = {0};//试剂仓区域
static int door_open_flag = 0;//舱门开启指令
static int32_t auto_cali_stop_set = 0;//自动标定停止标志
static int pos_for_cali_result = 0;//自动标定结果
static int gate_report_flag = 0;//增此标志为补充在各维护时区分不了试剂仓转动等操作，为1表示屏蔽报警，0表示接受报警。
//记录试剂瓶id数组用于开盖更新相关位置余量探测，增加多余位置为表示在首尾处获取前后瓶id
static uint8_t bottle_group[45] = {35, 36, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,\
                                   22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 1, 2, 3, 4, 5, 6, 7};

void restart_sem_post(void)
{
  sem_post(&scanner_restart_sem);
}

/**
* @brief: 自动标定停止标志。
* @para: flag 标志的值。
*/
void auto_calibrate_stop_set(int32_t flag)
{
    LOG("engineer position calibration stop flag set %d.\n", flag);
    auto_cali_stop_set = flag;
}

/**
 * @brief: 检测试剂仓状态。
 * @return table_occupied_flag ：当前试剂仓占用状态。
 */
reag_tab_stage_t reag_table_stage_check(void)
{
    LOG("reagent_table : current stage %d.\n",table_stage);
    return table_stage;
}

/**
 *@brief: 获取试剂仓占用状态。
 *@return: 返回试剂仓状态值。
 */
int reag_table_occupy_flag_get(void)
{
    return table_occupied_flag;
}

/**
 * @brief: 设置试剂仓状态。
 * @param: stage 需设置的状态值。
 */
void reag_table_occupy_flag_set(int stage)
{
    LOG("reagent_table : set stage %d.\n",stage);
    table_occupied_flag = stage;
}

/**
 * @brief: 获取试剂仓位置转换值。
 * @param: stage 需转换的位置值。
 */
int transfer_to_enum(int needle_pos)
{
    return needle_pos;
}

/**
 * @brief: 内部设置试剂仓运行状态。
 * @param: stage 需设置的状态值。
**/
static void reagent_table_stage_set(reag_tab_stage_t stage)
{
    LOG("reagent_table : self_module stage is %d.\n",stage);
    table_stage = stage;
}

static void gate_time_open_print(void)
{
    struct timeval tv = {0};
    struct tm cur_tm = {0};

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &cur_tm);
    LOG("gate open time is：day:%d--%d:%d:%d.\n",(int)cur_tm.tm_mday, (int)cur_tm.tm_hour, (int)cur_tm.tm_min, (int)cur_tm.tm_sec);
}

/**
 * @brief: 根据位置获取瓶型。
 * @param: idx 位置索引。
 * @return: 返回瓶型 0表示小瓶 1表示大瓶。
**/
int reagent_table_bottle_type_get(needle_pos_t idx)
{
    if ((idx % 6) == 3 || (idx % 6) == 4) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief: 试剂信息初始化。
 * @return: 初始化链表执行结果 0成功 -1失败。
 */
static int reag_info_change_list_init(void)
{
    int idx = 0;
    int ret = 0;
    reag_consum_info_t *info = NULL;

    if (!chg_list_init_complete) {
        INIT_LIST_HEAD(&reag_change_list);
        for (idx = 1; idx < DILU_IDX_END; idx++) {
            info = (reag_consum_info_t *)malloc(sizeof(reag_consum_info_t));
            if (!info) {
                LOG("reag_remain_func: reag_info_change_list_init out of memory.\n");
                ret = -1;
                break;
            }
            info->remain_flag = 0;
            info->pos_idx = idx;
            info->time = sys_uptime_sec();
            list_add_tail(&info->possibling, &reag_change_list);
        }
        chg_list_init_complete = 1;
        LOG("reag_remain_func: list INIT.\n");
    }
    return ret;
}

/**
 * @brief: 试剂仓耗材信息设置。
 */
void reag_change_idx_get(int *array)
{
    int idx = 0;
    reag_consum_info_t *n = NULL;
    reag_consum_info_t *pos = NULL;
    time_t ct = sys_uptime_sec();
    static int log_flag = -1;//防止日志连续打印，随上位机更改后关闭

    if (!chg_list_init_complete) {
        reag_info_change_list_init();
    }
    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
        if (!pos->remain_flag && (pos->pos_idx < DILU_IDX_START)) {//与上位机沟通不返回稀释液位置号
            array[idx++] = pos->pos_idx;
        } else if (pos->remain_flag) {
             if ((ct - pos->time) > SEC_OF_DAY) {
                /* 距离上次探测已超过一天时间，防止挥发误差，重新余量探测 */
                if (log_flag == 0 || log_flag < 0) {
                    //LOG("reag_remain_func: pos-%d already timedout(%ld - %ld, %d - %d), re-check.\n", ct, pos->time, pos->pos_idx, pos->time, pos->pos_idx);
                    log_flag = 1;
                }

                pos->time = ct;
                if (pos->pos_idx < DILU_IDX_START) {//与上位机沟通不返回稀释液位置号
                    array[idx++] = pos->pos_idx;
                }
             } else {
                if (log_flag == 1 || log_flag < 0) {
                    //LOG("reag_remain_func: pos-%d already done, ignore.\n", pos->pos_idx);
                    log_flag = 0;
                }
             }
        }
    }
}

/**
 * @brief: 余量探测完成后设置探测状态。
 * @param: idx 试剂索引。
 */
 void reag_remain_detected_set(needle_pos_t idx)
{
    reag_consum_info_t *n = NULL;
    reag_consum_info_t *pos = NULL;

    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
        if (pos->pos_idx == idx) {
            LOG("reagent_table:pos %d has been detected.\n",idx);
            pos->remain_flag = 1;
        }
    }
}

/**
 * @brief: 试剂仓扫码，定点扫描共3次，失败后进入连续扫码模式再扫描一次，成功则直接返回。
 * @param: idx 试剂索引。
 */
static int reag_barcode_match_get(char *barcode)
{
     int ret = -1;
     thrift_motor_para_t reag_table_motor_para = {0};

     memset(barcode, 0, SCANNER_BARCODE_LENGTH);
     ret = scanner_read_barcode_sync(SCANNER_REAGENT, barcode, SCANNER_BARCODE_LENGTH);

     if (ret < 0) {
         scanner_compensate_read_barcode(barcode, SCANNER_BARCODE_LENGTH, 1);

         FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
         thrift_motor_para_get(MOTOR_REAGENT_TABLE, &reag_table_motor_para);
         if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                             CMD_MOTOR_MOVE_STEP,
                             -120,
                             reag_table_motor_para.speed/128,
                             reag_table_motor_para.acc/64,
                             MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("wait reagent table timeout\n");
            FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
         }

         if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                             CMD_MOTOR_MOVE_STEP,
                             200,
                             reag_table_motor_para.speed/1024,
                             reag_table_motor_para.acc/512,
                             MOTOR_DEFAULT_TIMEOUT) < 0) {
             LOG("wait reagent table timeout\n");
             FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
         }

         if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                             CMD_MOTOR_MOVE_STEP,
                             -80,
                             reag_table_motor_para.speed/128,
                             reag_table_motor_para.acc/64,
                             MOTOR_DEFAULT_TIMEOUT) < 0) {
             LOG("wait reagent table timeout\n");
             FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
         }
         FAULT_CHECK_END();

         sleep(1);
         memset(barcode, 0, SCANNER_BARCODE_LENGTH);
         scanner_compensate_read_barcode(barcode, SCANNER_BARCODE_LENGTH, 0);
     }

     if (strlen(barcode) > 10) {
         LOG("reagent_scan: scan successed & get bottle barcode is %s.\n", barcode);
         ret = 1;
     } else {/* 若扫描长度小于10则判断是否为位置条形码 */
         if ('M' == barcode[0]) {
             ret = 4;
             LOG("reagent_scan: scan successed & get background bar is %s\n", barcode);
         } else {
            ret = 0;
            LOG("reagent_table: scan table barcode faild.\n");
         }
     }

    return ret;
}

static int scanner_idle_judge(void)
{
    int ret = 0;

    ret = scanner_get_communicate();//确认通信是否存在
    if (ret == 0) {
        ret = scanner_restart(); /* 重启 */
        sem_wait(&scanner_restart_sem);
        LOG("reagent scanner restart done.");
    }
    return ret;
}

/**
 * @brief: 试剂仓扫码接口，扫码后自动上报结果。
 * @param: 暂无参数输入。
 * @return:扫码结果。
 */
void reagent_scan_interface(void)
{
    int i, ret = -1,judge_ret = 0;
    reag_table_cotl_t scan_table_cotl = {0};
    unsigned char motor_z[2] = {MOTOR_NEEDLE_S_Z, MOTOR_NEEDLE_R2_Z};

    machine_maintence_state_set(1);
    gate_report_flag = 1;//扫码时开盖不进行报警
    if ((gpio_get(PE_NEEDLE_S_Z) == 0) ||  (gpio_get(PE_NEEDLE_R2_Z) == 1)) {
        motor_move_async(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, 1);
        motor_move_async(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, 1);
        if (0 != motors_timedwait(motor_z, ARRAY_SIZE(motor_z), MOTOR_DEFAULT_TIMEOUT)) {
            LOG("reset  s/r1/r2 z timeout.\n");
            machine_maintence_state_set(0);
            return;
        }
    }

    scan_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
    if (reagent_table_move_interface(&scan_table_cotl) < 0) {
        goto out;
    }

    for (i = REAG_IDX_START; i < REAG_IDX_END; i++) {
        if ((gpio_get(PE_REGENT_TABLE_GATE) && ins_io_get().reag_io) || MACHINE_STAT_STOP == get_machine_stat()) {
            ret = 1;
            if (gpio_get(PE_REGENT_TABLE_GATE)) {
                LOG("reagent scan: table cover just opened reag io status : %d.\n", ins_io_get().reag_io);
                FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_NONE, MOUDLE_FAULT_SCANNER_FAILD);
                ret = 0;//扫码过程中开盖反馈执行扫码成功
            }
            LOG("reagent scanning interrupt!\n");
            goto out;
        }

        memset(&scan_table_cotl, 0, sizeof(scan_table_cotl));
        scan_table_cotl.table_dest_pos_idx = i + 1;
        scan_table_cotl.table_move_type = TABLE_SCAN_MOVE;
        if (reagent_table_move_interface(&scan_table_cotl) < 0) {
            LOG("reagent table move faild.\n");
            ret = 1;
            goto out;
        }

        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
        ret = reag_barcode_match_get(reag_prop.barcode);
        if (ret == 0) {
            judge_ret = scanner_idle_judge();
            if (judge_ret == 2) {//重启成功后再次扫描当前位置
                memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
                ret = reag_barcode_match_get(reag_prop.barcode);
            }
        }
        reag_prop.braket_index = (i / 6) + 1;
        reag_prop.braket_exist = 1;
        reag_prop.pos_index = i+1;
        reag_prop.pos_iexist_status = ret;
        if (report_reagent_info(&reag_prop) < 0) break;
        LOG("reagent match result:%d pos_exist:%d barcode:%s\n", ret, reag_prop.pos_iexist_status, reag_prop.barcode);
        FAULT_CHECK_END();
    }
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    ret = 0;//扫码完成后需更新返回值
    FAULT_CHECK_END();
out:
    memset(&scan_table_cotl, 0, sizeof(scan_table_cotl));
    scan_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
    reagent_table_move_interface(&scan_table_cotl);
    LOG("reagent scan barcode collection done. return value: %d\n",ret);
    gate_report_flag = 0;
    machine_maintence_state_set(0);
}

/**
 * @brief: 仪器自检时，更新上位机下发的耗材信息，包括余量探测和混匀。
 * @param：reag_info 耗材信息记录结构体。
 * @param：type 为0表示余量探测，为1表示混匀。
 */
void reag_consum_param_set(reag_consum_info_t *reag_info, int type)
{
    reag_consum_info_t *n = NULL;
    reag_consum_info_t *pos = NULL;

    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
        if (pos->pos_idx == reag_info->pos_idx) {
            if (type) {
                pos->mix_flag = reag_info->mix_flag;
                pos->mix_type = reag_info->mix_type;
            } else {
                pos->remain_flag = reag_info->remain_flag;
                pos->reag_category = reag_info->reag_category;
                pos->rx = reag_info->rx;
            }
            pos->bottle_type = reag_info->bottle_type;
        }
    }
}

/**
 * @brief: 试剂混匀功能实现。
 * @param: pos 混匀试剂位置索引。
 * @return:执行混匀结果。
 */
static int do_reagent_mix(reag_consum_info_t * pos)
{
    reag_table_cotl_t mix_table_cotl = {0};

    mix_table_cotl.req_pos_type = REAGENT_OPERATION_MIX;
    mix_table_cotl.table_dest_pos_idx = pos->pos_idx;
    mix_table_cotl.table_move_type = TABLE_COMMON_MOVE;
    if (reagent_table_move_interface(&mix_table_cotl) < 0) {
        LOG("reagent table move faild\n");
        return 0;
    }

    if (-1 == slip_bldc_ctl_set(BLDC_MAX, REAGENT_MIX_SPEED_NORMAL)) {
        LOG("reagent_mix_func: set mix speed failed.\n");
        goto out;
    }

    if (-1 == slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_REVERSE)) {
        LOG("reagent_mix_func: mix bldc start failed.\n");
        goto out;
    }

    sleep(REAGENT_MIX_TIME_DEFAULT);

    if (-1 == slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_STOP)) {
        LOG("reagent_mix_func: mix bldc stop failed.\n");
        goto out;
    }

    LOG("reagent_mix_func: cup pos(%d) mix successed.\n", mix_table_cotl.table_dest_pos_idx);
    return 0;

out:
    LOG("reagent_mix_func: cup pos(%d) mix failed.\n", mix_table_cotl.table_dest_pos_idx);
    return -1;
}

/**
 * @brief: 试剂混匀功能接口。
 * @return:执行混匀结果。
 */
int reagent_mix_interface(void)
{
    int ret = 0;
    reag_consum_info_t *pos = NULL;
    reag_consum_info_t *n = NULL;
    reag_table_cotl_t mix_table_cotl = {0};
    unsigned char motor_z[2] = {MOTOR_NEEDLE_S_Z, MOTOR_NEEDLE_R2_Z};

    if ((gpio_get(PE_NEEDLE_S_Z) == 0) ||  (gpio_get(PE_NEEDLE_R2_Z) == 1)) {
        motor_move_async(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, 1);
        motor_move_async(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, 1);
        if (0 != motors_timedwait(motor_z, ARRAY_SIZE(motor_z), MOTOR_DEFAULT_TIMEOUT)) {
            LOG("reset  s/r1/r2 z timeout.\n");
            return -1;
        }
    }

    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
        if (pos->mix_flag == 1) {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            pos->mix_type = 2;
            ret = do_reagent_mix(pos);
            if (ret == -1) {
                FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_MIX_MOTOR);
                break;
            }
            pos->mix_flag = 0;
            FAULT_CHECK_END();
        }
    }

    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        mix_table_cotl.table_move_type = TABLE_COMMON_RESET;
        reagent_table_move_interface(&mix_table_cotl);
        FAULT_CHECK_END();
    }

    return ret;
}

/**
 * @brief: 转动补偿获取并做对应补偿。
 * @param: type 请求部件类型。
 * @param: reagent_pos 移动位置索引。
 * @return: 试剂仓移动步长。 
 */
static int reagent_move_steps_get(int type, int reagent_pos)
{
    move_pos_t mp_idx = 0;
    cup_pos_t cp_idx = 0;
    needle_pos_t np_start = 1;
    int steps = 0, move_steps = 0, pos_offset = 0, com_steps = 0, change_scan_setps = 0;
    pos_t rt_para = {0};

    LOG("reagent_table: ready move destination pos = %d type = %d\n", reagent_pos, type);
    switch (type) {
        case NEEDLE_S:
            mp_idx = MOVE_REAGENT_TABLE_FOR_S;
            cp_idx = ((reagent_pos % 2) != 0) ? POS_REAGENT_TABLE_S_IN : POS_REAGENT_TABLE_S_OUT;
        break;
        case NEEDLE_R2:
            mp_idx = MOVE_REAGENT_TABLE_FOR_R2;
            cp_idx = ((reagent_pos % 2) != 0) ? POS_REAGENT_TABLE_R2_IN : POS_REAGENT_TABLE_R2_OUT;
        break;
        case REAGENT_OPERATION_MIX:
            mp_idx = MOVE_REAGENT_TABLE_FOR_MIX;
            cp_idx = ((reagent_pos % 2) != 0) ? POS_REAGENT_TABLE_MIX_IN : POS_REAGENT_TABLE_MIX_OUT;
        break;
        case REAGENT_SCAN_ENGINEER:
            mp_idx = MOVE_REAGENT_TABLE_FOR_SCAN;
            if ((reagent_pos % 6) == 5) {//维护或工程师扫码时5、6号位置按照机械要求进行位置调整
                change_scan_setps -= 100;
            } else if ((reagent_pos % 6) == 0) {
                change_scan_setps += 100;
            }
        break;
        default:
            LOG("Error! invalid type = %d.\n", type);
        return 0;
    }

    if (cp_idx == POS_REAGENT_TABLE_MIX_OUT || cp_idx == POS_REAGENT_TABLE_R2_OUT || cp_idx == POS_REAGENT_TABLE_S_OUT) {
        np_start = 2;
    }

    get_special_pos(mp_idx, cp_idx, &rt_para, FLAG_POS_UNLOCK);
    LOG("reag_table_func: calibration pos = %d.\n",rt_para.x);
    pos_offset = ((reagent_pos - np_start) * ONES_TO_STEPS);
    com_steps = ((reagent_pos - np_start) * ONES_TO_STEPS) / 1000;
    steps = (rt_para.x ) > 0 ? rt_para.x : (rt_para.x + TABLE_FULL_STEPS);
    move_steps = (steps + com_steps - pos_offset - table_curr_steps) % TABLE_FULL_STEPS + change_scan_setps;
    if (abs(move_steps) > TABLE_HALF_STEPS) {
        move_steps = - (move_steps / abs(move_steps)) * (TABLE_FULL_STEPS - abs(move_steps));
    }
    LOG("reag_table_func: cur = %d, move_step = %d, comp_steps = %d.\n", table_curr_steps, move_steps, com_steps);

    return move_steps;
}

/**
 * @brief: 扫码模式时的试剂仓移动。
 * @param: reagent_pos 移动位置索引。
 * @param: reagent_move_attr 试剂仓控制结构。
 * @return: 执行移动结果。
 */
static int reagent_scan_move_func(int reagent_pos, motor_time_sync_attr_t *reagent_move_attr)
{
    pos_t rt_para = {0};
    int move_steps = 0, res = 1;

    if (reagent_pos == 1) {
        get_special_pos(MOVE_REAGENT_TABLE_FOR_SCAN, 0, &rt_para, FLAG_POS_UNLOCK);
        move_steps = rt_para.x;
        LOG("reagent_table:get pos from json file is : %d.\n",move_steps);
    } else {
        move_steps = -ONES_TO_STEPS;
        if ((reagent_pos % 3) != 1) {
            move_steps -= 1;
        }
    }

    /* 试剂仓5号位置不标准 做位置补偿 */
    if ((reagent_pos % 6) == 5) {
        move_steps -= 100;
    } else if ((reagent_pos % 6) == 0) {
        move_steps += 100;
    }

    LOG("reagent_table scan move pos = %d, steps = %d.\n", reagent_pos, move_steps);

    reagent_move_attr->step = abs(move_steps);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    res = motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                              CMD_MOTOR_MOVE_STEP,
                              move_steps,
                              reagent_move_attr->speed/2,
                              reagent_move_attr->acc/2,
                              MOTOR_DEFAULT_TIMEOUT);
    FAULT_CHECK_END();
    if (res < 0) {
        LOG("reagent_table: motor table move faild.\n");
        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
    } else {
        table_curr_steps += move_steps;
    }

    return res;
}

/**
 * @brief: 试剂仓盖开启后更新相关位置试剂瓶余量探测状态。
 */
static void reagent_bottle_update(void)
{
    int res = 0,rela_idx = 0, i = 0;

    LOG("reagent_table: cur pos = %d.\n",table_curr_steps);
    res = -(table_curr_steps % TABLE_FULL_STEPS);
    if (res < 0) {
        res += TABLE_FULL_STEPS;
    }
    rela_idx = res / ONES_TO_STEPS;
    LOG("reagent table: relative index = %d.\n",rela_idx);

    //更新以仓盖中心线靠左第一个试剂瓶为起始的左4右5共10个位置的试剂瓶状态
    for (i=0; i<10; i++) {
        reag_table_area[bottle_group[rela_idx+i] - 1] = 1;
    }
}

/**
 * @brief: 按键模式时的试剂仓移动，同时记录移动的区域。
 * @param: pan_idx 移动区域索引。
 * @param: mode 移动模式包括 按键和thrift。
 * @return: 执行移动结果。
 */
static int reagent_button_move(int pan_idx , reagnet_mode_t mode)
{
    int rela_dist = 0, pan_shift_steps = 0, res = 0, idx = 0;
    thrift_motor_para_t reag_table_motor_para = {0};
    reag_consum_info_t *n = NULL;
    reag_consum_info_t *pos = NULL;
    static int rotate_flag = 1;//第一次使用时进行一次复位
    
    LOG("reagent button move get idx = %d, mode is %d.\n", pan_idx, mode);
    gate_report_flag = 1;//旋转试剂仓时不检测仓盖
    if (rotate_flag || door_open_flag) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        motor_reset(MOTOR_REAGENT_TABLE, 1);//初次使用上电复位，其余正常复位
        if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT * 2)) {/* 试剂盘上电复位耗时最长大约40s */
            LOG("reagent_table: onpower reset faild.\n");
            FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
        } else {
            rotate_flag = 0;
            table_curr_steps = 0;
        }
        FAULT_CHECK_END();
    }

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    res = motor_current_pos_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT);
    table_curr_steps = res;
    LOG("reagent table button move : relative distence : %d.\n", res);
    rela_dist = res % REAGENT_SPIN_STEP;

    if (mode == MODE_BUTTON) {
        if (door_open_flag) {
            door_open_flag = 0;
            pan_shift_steps = 0;
        } else {
            if (rela_dist == 0) {
                pan_shift_steps = -REAGENT_SPIN_STEP;
            } else {
                if (abs(rela_dist) < HALF_SECTOR_STEPS) {
                    pan_shift_steps = -(rela_dist % HALF_SECTOR_STEPS);
                } else {
                    pan_shift_steps = (rela_dist / abs(rela_dist)) * (REAGENT_SPIN_STEP - abs(rela_dist));
                }
            }
        }
    } else {
        door_open_flag = 0;
        pan_shift_steps = (-(pan_idx - 1) * REAGENT_SPIN_STEP - res) % TABLE_FULL_STEPS;
        if (abs(pan_shift_steps) > TABLE_HALF_STEPS) {
            pan_shift_steps = - (pan_shift_steps / abs(pan_shift_steps)) * (TABLE_FULL_STEPS - abs(pan_shift_steps));
        }
    }
    FAULT_CHECK_END();

    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &reag_table_motor_para);
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    LOG("reagent_table: button move steps = %d.\n",pan_shift_steps);
    res = motor_move_ctl_sync(MOTOR_REAGENT_TABLE, CMD_MOTOR_MOVE_STEP,
                              pan_shift_steps,
                              reag_table_motor_para.speed,
                              reag_table_motor_para.acc,
                              MOTOR_DEFAULT_TIMEOUT);
    FAULT_CHECK_END();

    if (res < 0) {
        LOG("reagent_table: motor table move faild.\n");
        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
    } else {
        table_curr_steps += pan_shift_steps;
        if (gpio_get(PE_REGENT_TABLE_GATE) == 1) {
            reagent_bottle_update();
            for (idx = 1; idx <= REAG_IDX_END; idx++) {
                if (reag_table_area[idx-1] == 1) {
                    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
                        if ( pos->pos_idx == idx) {
                            LOG("reag_remain_func: set pos---%d not done.\n",idx);
                            pos->remain_flag = 0;
                            break;
                        }
                    }
                }
            }
            LOG("reagent_table: remain flag set done.\n");
            memset(reag_table_area, 0, sizeof(reag_table_area));
        }
        last_table_move_time = sys_uptime_sec(); /* 成功则记录转动时间以匹配试剂区域更新 */
    }

    gate_report_flag = 0;
    return res;
}

/**
 * @brief: 试剂仓常规移动请求。
 * @param: reag_table_cotl 移动控制结构体。
 * @param: reagent_move_attr 电机参数结构体。
 * @return: 执行移动结果。
 */
static int reagent_move_func(reag_table_cotl_t *reag_table_cotl, motor_time_sync_attr_t *reagent_move_attr)
{
    int move_steps = 0, rm_acc = 0, res = 1;
    
    move_steps = reagent_move_steps_get(reag_table_cotl->req_pos_type, reag_table_cotl->table_dest_pos_idx);
    if (!move_steps) {
        return 0;
    }
    LOG("reag_table_func: %d move: test_steps = %d, destination pos is = %d\n", reag_table_cotl->req_pos_type, move_steps, reag_table_cotl->table_dest_pos_idx);
    reagent_move_attr->step = abs(move_steps);
    if (1 == get_throughput_mode()) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        res = motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                CMD_MOTOR_MOVE_STEP,
                                move_steps,
                                reagent_move_attr->speed,
                                reagent_move_attr->acc,
                                MOTOR_DEFAULT_TIMEOUT);
        FAULT_CHECK_END();
    } else {
        rm_acc = calc_motor_move_in_time(reagent_move_attr, reag_table_cotl->move_time);
        LOG("reagent_table : times = %f calc acc is %f.\n", reag_table_cotl->move_time,rm_acc);
        if (reag_table_cotl->is_auto_cal == 1) {
            res = motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                      CMD_MOTOR_MOVE_STEP,
                                      move_steps,
                                      reagent_move_attr->speed,
                                      rm_acc,
                                      MOTOR_DEFAULT_TIMEOUT);
        } else {
            FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            res = motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                      CMD_MOTOR_MOVE_STEP,
                                      move_steps,
                                      reagent_move_attr->speed,
                                      rm_acc,
                                      MOTOR_DEFAULT_TIMEOUT);
            FAULT_CHECK_END();
        }
    }

    if (res < 0) {
        LOG("reag_table_func: reagent table move timeout\n");
        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
    } else {
        table_curr_steps += move_steps;
        LOG("reag_table_func: reagent table move done\n");
    }

    return res;
}

/**
 * @brief:  重置试剂仓状态。
 * @return: 执行结果。
 */
int reinit_reagent_table_data(void)
{
    table_curr_steps = 0;
    memset(reag_table_area, 0, sizeof(reag_table_area));
    reagent_table_stage_set(TABLE_IDLE);
    LOG("reagent_table:reinit done.\n");
    return 0;
}

/**
 * @brief: 试剂仓移动接口，包括上电/正常复位、按键、扫码和常规移动，试剂仓以A-F方向相同为正，相反为负。
 * @param: reag_table_cotl 移动控制结构体。
 * @return: 执行移动结果。 
 */
int reagent_table_move_interface(reag_table_cotl_t *reag_table_cotl)
{
    int ret = -1, reagent_rst_acc = 0;
    thrift_motor_para_t reag_table_motor_para = {0};
    motor_time_sync_attr_t reagent_move_attr = {0};
    int steps = 0;

    if (table_stage != TABLE_IDLE) {
        LOG("reagent_table: cur_stage is:%d\n",table_stage);
        return ret;
    }

    /* 当前位置需去除整数圈，若之前反向运动致当前步数为负值则正向处理 */
    table_curr_steps = (table_curr_steps < 0) ? ((table_curr_steps % TABLE_FULL_STEPS) + TABLE_FULL_STEPS) : (table_curr_steps % TABLE_FULL_STEPS);
    LOG("reagent_table move start : cur_pos is %d, type is %d\n", table_curr_steps, reag_table_cotl->table_move_type);

    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &reag_table_motor_para);
    reagent_move_attr.v0_speed = MOTOR_TABLE_V0SPEED;
    reagent_move_attr.speed = reag_table_motor_para.speed;
    reagent_move_attr.vmax_speed = reag_table_motor_para.speed;
    reagent_move_attr.acc = reag_table_motor_para.acc;
    reagent_move_attr.max_acc = reag_table_motor_para.acc;
    LOG("reagent_table move speed is:%d acc is: %d.\n", reagent_move_attr.speed, reagent_move_attr.acc);
    reagent_table_stage_set(TABLE_MOVING);

    switch (reag_table_cotl->table_move_type) {
        case TABLE_COMMON_RESET:
            if (table_curr_steps != 0) {
                if (table_curr_steps > 500) { /* 500脉冲使挡片完全出光电 */
                    if (table_curr_steps > TABLE_HALF_STEPS) {
                        steps = TABLE_FULL_STEPS - table_curr_steps + 500;
                    } else {
                        steps = -table_curr_steps + 500;
                    }
                    reagent_move_attr.step = table_curr_steps + 500;
                    reagent_rst_acc = calc_motor_move_in_time(&reagent_move_attr, reag_table_cotl->move_time);
                   FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
                    if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                            CMD_MOTOR_MOVE_STEP,
                                            steps,
                                            reag_table_motor_para.speed,
                                            reagent_rst_acc,
                                            MOTOR_DEFAULT_TIMEOUT)) {
                        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
                        ret = -1;
                    } else {
                        ret = 0;
                        table_curr_steps = 0;
                    }
                    FAULT_CHECK_END();

                }

               FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
                if (motor_move_sync(MOTOR_REAGENT_TABLE, CMD_MOTOR_RST, 0, 1, MOTOR_DEFAULT_TIMEOUT*2)) {
                    LOG("reagent_table: common reset faild.\n");
                    FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
                    ret = -1;
                } else {
                    ret = 0;
                    table_curr_steps = 0;
                }
                FAULT_CHECK_END();

            }

        break;
        case TABLE_ONPOWER_RESET:
           FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            motor_reset(MOTOR_REAGENT_TABLE, 1);
            if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT * 2)) {/* 试剂盘上电复位耗时最长大约40s */
                LOG("reagent_table: onpower reset faild.\n");
                FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
                ret = -1;
            } else {
                ret = 0;
                table_curr_steps = 0;
                LOG("reagent_table: onpower reset done!\n");
            }
            FAULT_CHECK_END();

        break;
        case TABLE_COMMON_MOVE:
            if (reag_table_cotl->is_auto_cal == 1) {
                ret = reagent_move_func(reag_table_cotl, &reagent_move_attr);
                if (ret < 0) {
                    LOG("reagent_table: common move faild.\n");
                    ret = -1;
                } else {
                    ret = 0;
                    LOG("reagent_table: common move done!\n");
                }
            } else {
                FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
                ret = reagent_move_func(reag_table_cotl, &reagent_move_attr);
                if (ret < 0) {
                    LOG("reagent_table: common move faild.\n");
                    FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
                    ret = -1;
                } else {
                    ret = 0;
                    LOG("reagent_table: common move done!\n");
                }
                FAULT_CHECK_END();
            }

        break;
        case TABLE_SCAN_MOVE:
           FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            ret = reagent_scan_move_func(reag_table_cotl->table_dest_pos_idx, &reagent_move_attr);
            if (ret < 0) {
                LOG("reagent_table: scan move faild.\n");
                FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
                ret = -1;
            } else {
                ret = 0;
            }
            FAULT_CHECK_END();

        break;
        case TABLE_BOTTON_MOVE:
           FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
            ret = reagent_button_move(reag_table_cotl->pan_idx, reag_table_cotl->button_mode);
            if (ret < 0) {
                LOG("reagent_table: botton move faild.\n");
                FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
                ret = -1;
            } else {
                ret = 0;
                LOG("reagent_table: bottom move done!\n");
            }
            FAULT_CHECK_END();

        break;
        default:
            LOG("reagent table move mode invalid\n");
            ret = -1;
    }

    reagent_table_stage_set(TABLE_IDLE);
    return ret;
}

/* 混匀检测时获取电机实时转速 */
static void bldc_mix_speed_task(void *arg)
{
    int speed = 0;
    int *speed_ctl = (int *)arg;

    while (speed_ctl[0]) {
        speed = slip_bldc_rads_get(1);
        if (speed) {
            speed_ctl[1] += speed;
            speed_ctl[0]--;
        }
        usleep(200*1000);
    }
}

/* 混匀功能检测 */
int reagent_mix_onpower_selfcheck(void)
{
    int i = 0;
    int speed = 0, ret = 0;
    int mix_speed_get[2] = {0};
    reag_table_cotl_t mix_table_cotl = {0};

    mix_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
    if (reagent_table_move_interface(&mix_table_cotl) < 0) {
        goto out;
    }

    for (i=0; i<2; i++) {
        mix_table_cotl.table_dest_pos_idx = i+1;
        mix_table_cotl.table_move_type = TABLE_COMMON_MOVE;
        mix_table_cotl.req_pos_type = REAGENT_OPERATION_MIX;
        if (reagent_table_move_interface(&mix_table_cotl) < 0) {
            LOG("reagent table move faild\n");
            return 0;
        }
        if (-1 == slip_bldc_ctl_set(BLDC_MAX, REAGENT_MIX_SPEED_NORMAL)) {
            LOG("reagent_mix_func: set mix speed failed.\n");
            ret = 1;
            goto out;
        }

        mix_speed_get[0] = 3;
        work_queue_add(bldc_mix_speed_task, mix_speed_get);

        if (-1 == slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_REVERSE)) {
            LOG("reagent_mix_func: mix bldc start failed.\n");
            ret = 1;
            goto out;
        }

        sleep(REAGENT_MIX_TIME_DEFAULT);
        
        if (-1 == slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_STOP)) {
            LOG("reagent_mix_func: mix bldc stop failed.\n");
            ret = 1;
            goto out;
        }
        mix_speed_get[1] = mix_speed_get[1] / (3 - mix_speed_get[0]);
        mix_speed_get[0] = 0;
        speed = mix_speed_get[1];
        LOG("get mix bldc speed is %d.", speed);
        ret = speed != 0 ? 0 : 1;
    }

out:
    return ret;
}

/* 试剂仓运动功能通电自检 */
int  reagent_table_onpower_selfcheck(void)
{
    int ret = 0;
    thrift_motor_para_t reag_table_motor_para = {0};
    long long start_time = 0, end_time = 0;
    int rotate_speed = 0;
    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &reag_table_motor_para);

    start_time = get_time();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    ret = motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                              CMD_MOTOR_MOVE_STEP,
                              32000,
                              reag_table_motor_para.speed,
                              reag_table_motor_para.acc,
                              MOTOR_DEFAULT_TIMEOUT);
    FAULT_CHECK_END();
    if (ret < 0) {
        LOG("reagent_table: motor table move faild.\n");
        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_TABLE);
    }
    end_time = get_time();

    rotate_speed = (int)((end_time - start_time) / 1000 * 60);
    LOG("reagent table rotate speed : %d.\n", rotate_speed);

    ret = rotate_speed >= 60 ? 0 : 1;

    return ret;
}

/* 扫码器通电自检流程 更改为通信回复方式确认 */
int scanner_onpower_selfcheck(void)
{
    int i,j;
    int ret = 0;
    reag_table_cotl_t scan_table_cotl = {0};

    for (j=0; j<20; j++) {
        scan_table_cotl.table_move_type = TABLE_ONPOWER_RESET;
        if (reagent_table_move_interface(&scan_table_cotl) < 0) {
            goto out;
        }

        for (i = REAG_IDX_START; i < REAG_IDX_END; i++) {
            memset(&scan_table_cotl, 0, sizeof(scan_table_cotl));
            scan_table_cotl.table_dest_pos_idx = i + 1;
            scan_table_cotl.table_move_type = TABLE_SCAN_MOVE;
            if (reagent_table_move_interface(&scan_table_cotl) < 0) {
                LOG("reagent table move faild.\n");
                ret = 1;
                goto out;
            }
            memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
            ret = reag_barcode_match_get(reag_prop.barcode);
            if (ret == 0) {
                LOG("self check faild pos is %d round count %d.\n", i, j);
                ret = scan_table_cotl.table_dest_pos_idx;
                goto out;
            }
        }
        ret++;
    }
    LOG("reagnet_table: scan times = %d.\n", ret);
    ret = 0;
out:
    return ret;
}

/* 试剂仓通电自检 */
int reagent_table_onpower_selfcheck_interface(void)
{
    int ret = 0;
    onpower_selfcheck_node result = NODE_NULL;

    ret = reagent_table_onpower_selfcheck();
    if (ret != 0) {
        LOG("reagent_table: move function check falid.\n");
        result = REAGENT_TABLE_MOTOR_NODE;
    }

    ret = reagent_mix_onpower_selfcheck();
    if (ret != 0 ) {
        LOG("reagent_table: mix function check faild.\n");
        result = MIX_MOROT_NODE;
    }

    ret = scanner_onpower_selfcheck();
    if (ret != 0) {
        LOG("reagent_table: scan function check faild.\n");
        result = SCAN_NODE;
    }

    return result;
}

/* 试剂仓扫码自动标定 */
int reagent_table_scan_pos_auto_calibrate(int32_t mode)
{
    int ret = 0, find_steps = 0, i = 0, final_steps = 0, threshold_count = 0;
    thrift_motor_para_t reag_table_motor_para = {0};
    int start_steps = SCAN_AUTO_CALB_NEERBY_POS;
    unsigned char motor_z[2] = {MOTOR_NEEDLE_S_Z, MOTOR_NEEDLE_R2_Z};
    pos_t old_pos = {0};
    int calibrate_count = 0;
    reag_table_cotl_t table_cotl = {0};

    module_fault_stat_clear();
    get_special_pos(MOVE_REAGENT_TABLE_FOR_SCAN, 0, &old_pos, FLAG_POS_UNLOCK);

    if ((gpio_get(PE_NEEDLE_S_Z) == 0) ||  (gpio_get(PE_NEEDLE_R2_Z) == 1)) {
        LOG("reset z axis of needles.\n");
        motor_move_async(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, 1);
        motor_move_async(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, 1);
        if (0 != motors_timedwait(motor_z, ARRAY_SIZE(motor_z), MOTOR_DEFAULT_TIMEOUT)) {
            LOG("reset z axis of needles timeout.\n");
            ret = -1;
            goto out;
        }
    }

    motor_reset(MOTOR_REAGENT_TABLE, 1);
    if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT * 2)) {/* 试剂盘上电复位耗时最长大约40s */
        LOG("reagent table reset faild.\n");
        ret = 0;
        goto out;
    }
    thrift_motor_para_get(MOTOR_REAGENT_TABLE, &reag_table_motor_para);
    if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                              CMD_MOTOR_MOVE_STEP,
                              start_steps,
                              reag_table_motor_para.speed,
                              reag_table_motor_para.acc,
                              MOTOR_DEFAULT_TIMEOUT) < 0) {
        LOG("reagent table move faild.\n");
        ret = -1;
        goto out;
    }

    memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
    ret = scanner_read_barcode_sync(SCANNER_REAGENT, reag_prop.barcode, SCANNER_BARCODE_LENGTH);

    if (ret == 0) {
        do {
            if (auto_cali_stop_set) {
                LOG("reagent table: auto calibrate stop!!!\n");
                ret = -1;
                goto out;
            }
            if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                      CMD_MOTOR_MOVE_STEP,
                                      10,
                                      reag_table_motor_para.speed,
                                      reag_table_motor_para.acc,
                                      MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("reagent table move faild.\n");
                ret = -1;
                goto out;
            }

            memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
            ret = scanner_read_barcode_sync(SCANNER_REAGENT, reag_prop.barcode, SCANNER_BARCODE_LENGTH);
            find_steps += 10;
            start_steps += 10;
            threshold_count++;
            if (threshold_count > 66) {
                LOG("except range scan faild.\n");
                ret = -1;
                goto out;
            }//做阈值设置 防止扫码器挂起后不能退出
        } while(ret == 0);
    } else {
        ret = -1;
        for (i=0; i<33; i++) {
            if (auto_cali_stop_set) {
                LOG("reagent table: auto calibrate stop!!!");
                ret = -1;
                goto out;
            }
            if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                      CMD_MOTOR_MOVE_STEP,
                                      10,
                                      reag_table_motor_para.speed,
                                      reag_table_motor_para.acc,
                                      MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("reagent table move faild.\n");
                ret = -1;
                goto out;
            }

            memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
            ret = scanner_read_barcode_sync(SCANNER_REAGENT, reag_prop.barcode, SCANNER_BARCODE_LENGTH); 
            find_steps += 10;
            start_steps += 10;
            if (ret == 0) {
                LOG("scan sucess & steps is %d.\n", find_steps);
                break;
            }
        }

        if (ret < 0) {
            if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                      CMD_MOTOR_MOVE_STEP,
                                      -330,
                                      reag_table_motor_para.speed,
                                      reag_table_motor_para.acc,
                                      MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("reagent table move faild.\n");
                ret = -1;
                goto out;
            }
            find_steps = 0;
            for (i=0; i<33; i++) {
                if (auto_cali_stop_set) {
                    LOG("reagent table: auto calibrate stop!!!");
                    ret = -1;
                    goto out;
                }

                if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                          CMD_MOTOR_MOVE_STEP,
                                          -10,
                                          reag_table_motor_para.speed,
                                          reag_table_motor_para.acc,
                                          MOTOR_DEFAULT_TIMEOUT) < 0) {
                    LOG("reagent table move faild.\n");
                    ret = -1;
                    goto out;
                }

                memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
                ret = scanner_read_barcode_sync(SCANNER_REAGENT, reag_prop.barcode, SCANNER_BARCODE_LENGTH); 
                find_steps -= 10;
                start_steps -= 10;
                if (ret == 0) {
                    LOG("scan sucess & steps is %d.\n", find_steps);
                    break;
                }
            }
        }

        if (ret < 0) {
            LOG("auto position set faild.\n");
            goto out;
        }
    }

    threshold_count = 0;
    do {
        if (auto_cali_stop_set) {
            LOG("reagent table: auto calibrate stop!!!");
            ret = -1;
            goto out;
        }
        if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                  CMD_MOTOR_MOVE_STEP,
                                  -(abs(find_steps) / find_steps) * 10,//取偏移值的符号并走相反方向
                                  reag_table_motor_para.speed,
                                  reag_table_motor_para.acc,
                                  MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("reagent table move faild.\n");
            ret = -1;
            goto out;
        }
        memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
        ret = scanner_read_barcode_sync(SCANNER_REAGENT, reag_prop.barcode, SCANNER_BARCODE_LENGTH);
        start_steps += -(abs(find_steps) / find_steps) * 10;
        threshold_count++;
        if (threshold_count > 33) {
            LOG("except range scan faild.\n");
            ret = -1;
            goto out;
        }//做阈值设置 防止扫码器挂起后不能退出
    } while (ret != 0);

    threshold_count = 0;
    do {
        if (auto_cali_stop_set) {
            LOG("reagent table: auto calibrate stop!!!");
            ret = -1;
            goto out;
        }
        if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                  CMD_MOTOR_MOVE_STEP,
                                  -(abs(find_steps) / find_steps) * 10,
                                  reag_table_motor_para.speed,
                                  reag_table_motor_para.acc,
                                  MOTOR_DEFAULT_TIMEOUT) < 0) {
            LOG("reagent table move faild.\n");
            ret = -1;
            goto out;
        }
        memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
        ret = scanner_read_barcode_sync(SCANNER_REAGENT, reag_prop.barcode, SCANNER_BARCODE_LENGTH);
        final_steps += (abs(find_steps) / find_steps) * 10;//此值为负，因此调整值与之相反
        threshold_count++;
        if (threshold_count > 66) {
            LOG("except range scan faild.\n");
            ret = -1;
            goto out;
        }//做阈值设置 防止扫码器挂起后不能退出

        if (ret != 0 && abs(final_steps) < 150) {
            LOG("restart detect edge of barcode, times: %d.\n",calibrate_count);
            calibrate_count++;
            if (motor_move_ctl_sync(MOTOR_REAGENT_TABLE,
                                      CMD_MOTOR_MOVE_STEP,
                                      -final_steps,
                                      reag_table_motor_para.speed,
                                      reag_table_motor_para.acc,
                                      MOTOR_DEFAULT_TIMEOUT) < 0) {
                LOG("reagent table move faild.\n");
                ret = -1;
                goto out;
            }
            final_steps = 0;
        }

        if ((ret != 0 && (abs(final_steps) > 150)) || calibrate_count > 10) {
            LOG("get start steps is %d, final steps is %d.\n",start_steps, final_steps);
            pos_for_cali_result = -final_steps / 2 + start_steps + 1333;//偏移值的符号和取相反的初始值
            LOG("reaent table auto set pos is %d.\n", pos_for_cali_result);
            if (calibrate_count > 10) {
                ret = -1;
            } else {
                ret = 0;
                if (mode < 10) { //service标定上报
                    report_position_calibration(2, 0, old_pos.x, pos_for_cali_result);
                } else {
                    for(i = 0; i<6; i++) {
                        LOG("report un-relative position for auto calibrate.\n");
                        engineer_needle_pos_report(13, i+1, h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][i], 0, 0, 0);
                        usleep(1000*1000);
                    }
                    LOG("report calibrate postion for scan\n");
                    engineer_needle_pos_report(13, 7, pos_for_cali_result, 0, 0, 0);
                }
            }
            goto out;
        }
    } while(1);

out:
    ret = (ret == 0) ? 0 : 7;//失败则上报子位置号，扫码位为7
    auto_calibrate_stop_set(0);
    table_cotl.table_move_type = TABLE_COMMON_RESET;
    reagent_table_move_interface(&table_cotl);
    return ret;
}

int reagent_table_set_pos(void)
{
    if (pos_for_cali_result == 0) {
        LOG("error : write position is not aviable!\n");
            return -1;
    }
    thrift_motor_pos_set(MOTOR_REAGENT_TABLE, 6, pos_for_cali_result);

    return 0;
}

/* 扇区扫码开放供维护和工程师模式使用，pos_idx：1-6表示对应的A-F盘扫码 */
void reagent_scan_maintence_mode(int pos_idx)
{
    int ret = -1, i = 0;
    reag_table_cotl_t scan_move_attr = {0};

    for (i = (pos_idx - 1) * 6 + 1; i < pos_idx * 6 + 1; i++) {
        if (MACHINE_STAT_STOP == get_machine_stat() || (gpio_get(PE_REGENT_TABLE_GATE) && ins_io_get().reag_io)) {
            LOG("engineer scan proecss jump out io status: %d.\n", ins_io_get().reag_io);
            break;
        }
        scan_move_attr.table_dest_pos_idx = i;
        scan_move_attr.table_move_type = TABLE_COMMON_MOVE;
        scan_move_attr.req_pos_type = REAGENT_SCAN_ENGINEER;
        reagent_table_move_interface(&scan_move_attr);
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        memset(reag_prop.barcode, 0, SCANNER_BARCODE_LENGTH);
        ret = reag_barcode_match_get(reag_prop.barcode);
        reag_prop.pos_iexist_status = ret;
        reag_prop.braket_index = (i / 6) + 1;
        reag_prop.braket_exist = 1;
        reag_prop.pos_index = i;
        if (report_reagent_info(&reag_prop) < 0) break;
        LOG("enginner scan get barcode:%s\n", reag_prop.barcode);
        FAULT_CHECK_END();
    }

}

/**
 * @brief: 工程师模式对试剂仓指定位置进行扫码并上报扫码内容。
 * @return: 执行移动结果。
 */
void reagent_scan_engineer_mode(int32_t para)
{
    reag_table_cotl_t scan_move_attr = {0};
    static int first = 1;
    
    if (gpio_get(PE_RST_REAG_TABLE) || first) {
        memset(&scan_move_attr, 0, sizeof(scan_move_attr));
        scan_move_attr.table_move_type = TABLE_ONPOWER_RESET;
        reagent_table_move_interface(&scan_move_attr);
        first = 0;
    }

    LOG("get pos idx from master is %d.\n", para);
    reagent_scan_maintence_mode(para);

    memset(&scan_move_attr, 0, sizeof(scan_move_attr));
    scan_move_attr.table_move_type = TABLE_COMMON_RESET;
    reagent_table_move_interface(&scan_move_attr);
}

/**
 * @brief: 试剂仓盖检测线程，检测仓盖开启状态、更新开启关闭试剂和判断并更新需要扫码的区域。
 */
static void *reag_gate_check_task(void *arg)
{
    int idx = 0, i = 0, door_flag = 0, first = 1;
    reag_consum_info_t *n = NULL;
    reag_consum_info_t *pos = NULL;
    int pe_regent_gate_state = 0, pe_dustbin_state = 0, pe_dustbin_old_state = 0;
    int door_state = 0, dustbin_report_state = 0;
    diluent_monitor_t dilu_monitor_ctl[2] = {{.diluent_idx = PE_DILU_1, .last_stage = (bool)gpio_get(PE_DILU_1), .dilu_idx = POS_REAGENT_DILU_1}, \
                                             {.diluent_idx = PE_DILU_2, .last_stage = (bool)gpio_get(PE_DILU_2), .dilu_idx = POS_REAGENT_DILU_2}};

    while (1) {
        pe_regent_gate_state = gpio_get(PE_REGENT_TABLE_GATE);
        dilu_monitor_ctl[0].curr_satge = (bool)gpio_get(PE_DILU_1);
        dilu_monitor_ctl[1].curr_satge = (bool)gpio_get(PE_DILU_2);
        pe_dustbin_state = gpio_get(PE_WASTE_CHECK);

        if (pe_dustbin_state == 1 && pe_dustbin_old_state == 0) {
            if (thrift_salve_heartbeat_flag_get() && dustbin_report_state == 0) {
                LOG("report dustbin state(out).\n");
                report_io_state(TYPE_DUSTBIN_EXIST_PE, false);
                dustbin_report_state = 1;
            }
            pe_dustbin_old_state = 1;
        } else if (pe_dustbin_state == 0 && pe_dustbin_old_state == 1) {
            if (thrift_salve_heartbeat_flag_get() && dustbin_report_state == 0) {
                LOG("report dustbin state(in).\n");
                report_io_state(TYPE_DUSTBIN_EXIST_PE, true);
                dustbin_report_state = 1;
            }
            pe_dustbin_old_state = 0;
        } else {
            dustbin_report_state = 0;
        }

        if (pe_regent_gate_state == 1 && door_flag) {
            door_flag = 0;//舱门开启后标记
            door_open_flag = 1;//用于试剂仓按键复位
            gate_time_open_print();//打印仓盖开启时间
            if (get_machine_stat() == MACHINE_STAT_STANDBY) {
                if (first) {
                    /* 上电处于待机状态，但试剂仓位置不可控，更新试剂仓所有区域状态 */
                    LOG("reag_remain_func: reag gate opened(first time),update all area.\n");
                    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
                        if (pos->pos_idx <= REAG_IDX_END) {
                            LOG("reag_remain_func: set pos-%d not done.\n", pos->pos_idx);
                            pos->remain_flag = 0;
                        }
                    }
                    first = 0;
                } else {
                   /* 待机情况试剂仓结合按键转动进行区域状态更新;更新原则：更新用户可能变动的所有区域。*/
                   reagent_bottle_update();
                    for (idx = 1; idx <= REAG_IDX_END; idx++) {
                        if (reag_table_area[idx-1] == 1) {
                            list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
                                if ( pos->pos_idx == idx) {
                                    LOG("reag_remain_func: set pos---%d not done.\n",idx);
                                    pos->remain_flag = 0;
                                    break;
                                }
                            }
                        }
                    }
                    LOG("reagent_table: remain detect flag set done.\n");
                    memset(reag_table_area, 0, sizeof(reag_table_area));
                }
                door_state = 1;
            }else if (get_machine_stat() == MACHINE_STAT_STOP) {
                LOG("reag_remain_func: reag gate opened, machine state = STOP, update all area.\n");
                /* 异常停机情况试剂仓位置不可控，更新试剂仓所有区域状态 */
                list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
                    if (pos->pos_idx <= REAG_IDX_END) {
                        LOG("reag_remain_func: set pos-%d not done.\n", pos->pos_idx);
                        pos->remain_flag = 0;
                        break;
                    }
                }
            } else if (get_machine_stat() == MACHINE_STAT_RUNNING || machine_maintence_state_get() == 1) {
                if (gate_report_flag == 0 && ins_io_get().reag_io) {//旋转试剂仓或扫码时屏蔽
                    FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_REAGENT_GATE_OPENED);//运行时开启仓盖报警
                }
            }
        }else if (pe_regent_gate_state == 0) {
            door_flag = 1;
            if (door_state == 1) {
                door_state = 0;
                LOG("reagent_table: report gate status.\n");
                report_io_state(TYPE_PE_REGENT_TABLE_TABLE, 0);
                door_open_flag = 0;//仓门关闭后清除该标记
            }
        }

        for (i = 0; i < 2; i++) {
            if (dilu_monitor_ctl[i].curr_satge != dilu_monitor_ctl[i].last_stage || dilu_monitor_ctl[i].report_flag) {
                if (dilu_monitor_ctl[i].curr_satge) {
                    dilu_monitor_ctl[i].report_flag = 1;
                    if (thrift_salve_heartbeat_flag_get()) {
                        LOG("report diluent state.\n");
                        report_io_state(dilu_monitor_ctl[i].diluent_idx, false);
                        dilu_monitor_ctl[i].report_flag = 0;
                    }
                    dilu_monitor_ctl[i].last_stage =  dilu_monitor_ctl[i].curr_satge;
                } else {
                    list_for_each_entry_safe(pos, n, &reag_change_list, possibling) {
                        if (pos->pos_idx == dilu_monitor_ctl[i].dilu_idx) {
                            pos->remain_flag = 0;
                            LOG("report diluent state!\n");
                            report_io_state(dilu_monitor_ctl[i].diluent_idx, true);
                            dilu_monitor_ctl[i].last_stage =  dilu_monitor_ctl[i].curr_satge;
                        }
                    }
                }
            }
        }

        usleep(500 * 1000);
    }
    return NULL;

}

/* 初始化函数 */
int module_reagent_table_init(void)
{
    pthread_t reag_gate_check_thread;

    sem_init(&scanner_restart_sem, 0, 0);
    /* 记录所有试剂/稀释液有关是否余量探测链表(余量探测使用) */
    if (reag_info_change_list_init() < 0) {
        LOG("reag_remain_func: list_init failed.\n");
        return -1;
    }
    if (0 != pthread_create(&reag_gate_check_thread, NULL, reag_gate_check_task, NULL)) {
        LOG("reag_gate_check_thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

