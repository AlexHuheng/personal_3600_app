#include "HIMaintenanceHandler.h"
#include "../thrift_slave/thrift_handler.h"


#include <movement_config.h>
#include <log.h>
#include <stdint.h>
#include <work_queue.h>
#include <h3600_cup_param.h>
#include <module_common.h>
#include <h3600_maintain_utils.h>
#include <module_monitor.h>
#include <module_cup_monitor.h>
#include <module_liquid_detect.h>
#include <module_reagent_table.h>
#include <module_magnetic_bead.h>
#include <module_optical.h>
#include <module_needle_r2.h>
#include <module_liquied_circuit.h>
#include <module_cuvette_supply.h>
#include <module_cup_mix.h>
#include <h3600_com_maintain_utils.h>

using namespace  ::H2103_Host_Invoke;

HIMaintenanceHandler::HIMaintenanceHandler() {
    // Your initialization goes here
}

/* 维护功能ID 来源于上位机的定义 */
#define HIM_START_UP         1    // 开机维护
#define HIM_SHUT_DOWN        2    // 关机维护
#define HIM_INSTRU_RESET     3    // 仪器复位
#define HIM_PIPLINE_FILL     4    // 管路填充
#define HIM_TRUN_OFF_INSTRU  5    // 关闭主机
#define HIM_EMERGENCY_STOP   13    // 停机维护
#define HIM_PIPLINE_EMPTYING_ID 14  // 管路排空
/* 整机上的模块通电质检ID */
#define QUALITYINSPECTION_GRIPPER       500 /* 抓手通电质检 */
#define QUALITYINSPECTION_SAMPLENEEDLE  501 /* 样本针通电质检 */
#define QUALITYINSPECTION_REAGENTNEEDLE 502 /* R2通电质检 */
#define QUALITYINSPECTION_REAGENTBIN    503 /* 试剂仓通电质检 */
#define QUALITYINSPECTION_LIQUID_SP     504 /* 液路 暂存池管路通电质检 */
#define QUALITYINSPECTION_SAMPLETRAY    505 /* 进杯盘通电质检 */
#define QUALITYINSPECTION_SAMPLER       506 /* 进样器通电质检 */
#define QUALITYINSPECTION_MIXMODULE     507 /* 混匀模块通电质检 */
#define QUALITYINSPECTION_MAG1_MODULE    508 /* 磁珠模块通电质检(底噪值) */
#define QUALITYINSPECTION_MAG2_MODULE    509 /* 磁珠模块通电质检(检测值) */
#define QUALITYINSPECTION_OPTICAL_MODULE 510 /* 光学模块通电质检 */
#define QUALITYINSPECTION_LIQUID_R2      511 /* 液路 R2管路   通电自检      */
#define QUALITYINSPECTION_LIQUID_S       512 /* 液路 S管路通电自检         */
#define QUALITYINSPECTION_LIQUID_SCLR    513 /* 液路 特殊清洗液管路通电自检 */
#define QUALITYINSPECTION_LIQUID_NWP     514 /* 液路 洗针池管路通电自检 */

::EXE_STATE::type HIMaintenanceHandler::ReagentScanAsync(const int32_t iAreaIndex, const int32_t iUserData) {
    // Your implementation goes here
    LOG("interface adandoned.\n");

    return EXE_STATE::SUCCESS;

}

::EXE_STATE::type HIMaintenanceHandler::ActiveMachineAsync(const int32_t iUserData) {
    // Your implementation goes here
    LOG("ActiveMachineAsync starting...\n");

    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    if (!userData) return EXE_STATE::FAIL;
    *userData = iUserData;

    work_queue_add(upper_start_active_machine, userData);

    return EXE_STATE::SUCCESS;
}

/* 设置通量模式，0.正常模式；1.PT快速模式 */
::EXE_STATE::type HIMaintenanceHandler::SetFluxModeAsync(const int32_t         iFluxMode, const int32_t iUserData) {
    // Your implementation goes here
    LOG("SetFluxModeAsync\n");
    throughput_mode_t *data = (throughput_mode_t *)calloc(1, sizeof(throughput_mode_t));

    data->mode = iFluxMode;
    data->iuserdata = iUserData;
    work_queue_add(set_throughput_mode, data);

    return EXE_STATE::SUCCESS;
}

/* 获取仪器通量模式 */
int32_t HIMaintenanceHandler::GetFluxMode()
{
    return get_throughput_mode();
}

::EXE_STATE::type HIMaintenanceHandler::ReagentRemainDetectAsync(const std::vector< ::REAGENT_POS_INFO_T> & lstReagPosInfo, const int32_t iUserData) {
    //Your implementation goes here
     LOG("ReagentRemainDetectAsync\n");
    int32_t size = lstReagPosInfo.size();
    const REAGENT_POS_INFO_T *ReagPosInfo = NULL;
    int32_t idx = 0;
    int32_t *userdata = (int32_t *)calloc(1, sizeof(int32_t));
    if (!userdata) {
        return EXE_STATE::FAIL;
    }
    *userdata = iUserData;

    if (get_machine_stat() == MACHINE_STAT_RUNNING || machine_maintence_state_get() == 1) {
        LOG("reag_remain_detect: machine or maintenance is running!\n");
        return EXE_STATE::FAIL;
    }

    if (size > 0) {
        LOG("reag_remain_detect: start, size = %d, userdata = %d.\n", size, *userdata);
        set_machine_stat(MACHINE_STAT_RUNNING);
        machine_maintence_state_set(1);
        for (idx = 0; idx < size; idx++) {
            ReagPosInfo = &lstReagPosInfo[idx];
            liquid_detect_remain_add_tail(ReagPosInfo->iPosIndex, ReagPosInfo->iBottleType,
                ReagPosInfo->iReagentCategory, ReagPosInfo->iRx, ReagPosInfo->iBottleMaterial);
        }
        work_queue_add(liquid_detect_remain_async, userdata);
    }

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIMaintenanceHandler::ReagentMixingAsync(const std::vector< ::REAGENT_MIX_INFO_T> & lstReagMixInfo, const int32_t iUserData) {
    // Your implementation goes here
    LOG("ReagentMixingAsync\n");

    return EXE_STATE::SUCCESS;
}

static void power_on_maintain_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    react_cup_del_all();
    clear_slip_list_motor();
    module_fault_stat_clear();
    liquid_detect_remain_del_all();

    ret = power_on_maintain();
    if (ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void power_off_maintain_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("power_off_maintain start\n");
    react_cup_del_all();
    clear_slip_list_motor();
    module_fault_stat_clear();
    liquid_detect_remain_del_all();
    ret = power_off_maintain();
    if (ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void shutdown_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("shutdown start\n");
    react_cup_del_all();
    clear_slip_list_motor();
    module_fault_stat_clear();
    liquid_detect_remain_del_all();
    ret = power_down_maintain(user_data[1]);
    if (ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    }
    LOG("shutdown finish, ret: %d\n", ret);;
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(user_data[0], ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void pipeline_fill_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("pipeline_fill start\n");
    react_cup_del_all();
    clear_slip_list_motor();
    module_fault_stat_clear();
    liquid_detect_remain_del_all();

    ret = pipeline_fill_maintain();
    if (ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    }

    LOG("pipeline_fill finish, ret: %d\n", ret);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void emergency_stop_maintain_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("emergency_stop_maintain start\n");
    react_cup_del_all();
    clear_slip_list_motor();
    pump_cur_steps_set(LIQ_PERF_FULL_STEPS);/* 设定特殊清洗液泵位置为最大步长，防止柱塞泵维护前异常打液 */
    module_fault_stat_clear();
    liquid_detect_remain_del_all();
    ret = emergency_stop_maintain();
    if (ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    }

    LOG("emergency_stop_maintain finish, ret: %d\n", ret);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void instrument_reset_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("instrument reset start\n");
    react_cup_del_all();
    clear_slip_list_motor();
    module_fault_stat_clear();
    liquid_detect_remain_del_all();
    ele_unlock_by_status();
    ret = reset_all_motors_maintain();
    if (ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    }

    LOG("instrument reset finish, ret: %d\n", ret);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void pipeline_clear_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("pipeline release start.\n");

    ret = pipe_remain_release();
    work_queue_add(exit_program, NULL);

    LOG("pipeline release end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void catcher_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("catcher poweron check start.\n");

    ret = needle_s_avoid_catcher();
    if (ret) {
        LOG("needle s avoid failed!\n");
    } else {
        ret = catcher_poweron_check(30, 1);
    }

    LOG("catcher poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void needle_s_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("needle s poweron check start.\n");

    ret = needle_s_poweron_check(2, S_NORMAL_MODE, S_NO_CLEAN_MODE);
    if (ret == 0) {
        ret = needle_s_poweron_check(2, S_FAST_MODE, S_NO_CLEAN_MODE);
    }

    LOG("needle s poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void needle_r2_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("needle s poweron check start.\n");

    ret = needle_r2_poweron_check(2, R2_NORMAL_MODE, R2_NO_CLEAN_MODE);
    if (ret == 0) {
        ret = needle_r2_poweron_check(2, R2_FAST_MODE, R2_NO_CLEAN_MODE);
    }

    LOG("needle s poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void reagent_table_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("reagent table poweron check start.\n");

    ret = reagent_table_onpower_selfcheck_interface();

    LOG("reagent table poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void liquid_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("liquid s_p_clr poweron check start.\n");

    stage_pool_clean_onpower_selfcheck();

    ret = 0;

    LOG("liquid s_p_clr poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void liquid_r2_pipeline_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("liquid r2_pp poweron check start.\n");

    ret = r2_normal_clean_onpower_selfcheck();

    LOG("liquid r2_pp poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void liquid_s_pipeline_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("liquid s poweron check start.\n");

    s_noraml_clean_onpower_selfcheck();
    ret = 0;

    LOG("liquid s poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void liquid_sclr_pipeline_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("liquid sclr poweron check start.\n");

    ret = spcl_cleaner_fill_onpower_selfcheck();

    LOG("liquid sclr poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void liquid_nwp_pipeline_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("liquid poweron check start.\n");

    wash_pool_onpower_selfcheck();
    ret = 0;

    LOG("liquid poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void cuvette_supply_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("cuvette supply poweron check start.\n");
    ret = thrift_cuvette_supply_func(BLDC_TEST);
    LOG("cuvette supply poweron check end.\n");

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void sampler_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("sampler poweron check start.\n");

    ret = sampler_power_on_func();

    LOG("sampler poweron check end.\n");
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void mix_module_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("mix module poweron check start.\n");
    ret = mix_poweron_test(MIX_POS_INCUBATION1);
    if (ret == 0) {
        ret = mix_poweron_test(MIX_POS_INCUBATION2);
    }

    if (ret == 0) {
        ret = mix_poweron_test(MIX_POS_OPTICAL1);
    }

    LOG("mix module poweron check end. ret:%d\n", ret);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void mag1_module_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    char fail_str[256] = {0};

    LOG("mag1 module poweron check start.\n");

    ret = magnetic_poweron_test(0, fail_str);

    LOG("mag1 module poweron check end. ret:%d\n", ret);
    memset(&async_return, 0, sizeof(async_return));
    async_return.return_type = RETURN_STRING;
    memcpy(async_return.return_string, fail_str, strlen(fail_str));
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void mag2_module_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    char fail_str[256] = {0};

    LOG("mag2 module poweron check start.\n");

    ret = magnetic_poweron_test(1, fail_str);

    LOG("mag2 module poweron check end. ret:%d\n", ret);
    memset(&async_return, 0, sizeof(async_return));
    async_return.return_type = RETURN_STRING;
    memcpy(async_return.return_string, fail_str, strlen(fail_str));
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

static void optical_module_poweron_check_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    char fail_str[256] = {0};

    LOG("optical module poweron check start.\n");

    ret = optical_poweron_test(0, fail_str);
    if (ret == 0) {
        ret = optical_poweron_test(1, fail_str);
    }

    LOG("optical module poweron check end. ret:%d\n", ret);
    memset(&async_return, 0, sizeof(async_return));
    async_return.return_type = RETURN_STRING;
    memcpy(async_return.return_string, fail_str, strlen(fail_str));
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
    machine_maintence_state_set(0);
}

/*
 iMainID: 来源于上位机的定义
 #define HIM_START_UP         1    // 开机维护 iUserData：用户数据, 其它参数无效
 #define HIM_SHUT_DOWN        2    // 关机维护 iUserData：用户数据, 其它参数无效
 #define HIM_INSTRU_RESET     3    // 仪器复位 iUserData：用户数据, 其它参数无效
 #define HIM_PIPLINE_FILL     4    // 管路填充 iUserData：用户数据, 其它参数无效
 #define HIM_TRUN_OFF_INSTRU  5    // 关闭主机 iKeepCool:1:保持试剂仓制冷，0:直接关闭主机
 #define HIM_EMERGENCY_STOP   13   // 停机维护 iUserData：用户数据, 其它参数无效
 #define HIM_PIPLINE_EMPTYING_ID  14  // 管路排空 iUserData：用户数据, 其它参数无效	
 #define HIM_STOP_ID          15    // 急停 iUserData：用户数据, 其它参数无效
*/
::EXE_STATE::type HIMaintenanceHandler::RunMaintenance(const int32_t iMainID, const int32_t iKeepCool, const std::vector<int32_t> & lstReserved1, const std::vector<std::string> & lstReserved2, const int32_t iUserData) {
	// Your implementation goes here
    LOG("RunMaintenance: iMainID:%d, iKeepCool:%d, lstReserved1:%d\n", iMainID, iKeepCool, lstReserved1.empty());
    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    *userData = iUserData;
    work_queue_function func = NULL;

    /* lstReserved1为空时，表示正常检测模式；非空时，表示工程师调试模式 */
    if (lstReserved1.empty()) {
        if (get_machine_stat() == MACHINE_STAT_RUNNING) {
            LOG("machine is running!\n");
            return EXE_STATE::FAIL;
        }

        if (machine_maintence_state_get() == 1) {
            LOG("last maintenance is running!\n");
            return EXE_STATE::FAIL;
        }
        set_machine_stat(MACHINE_STAT_RUNNING);
    }

    machine_maintence_state_set(1);
    switch (iMainID) {
    case HIM_START_UP:
        func = power_on_maintain_async;
        break;
    case HIM_EMERGENCY_STOP:
        func = emergency_stop_maintain_async;
        break;
    case HIM_SHUT_DOWN:
        func = power_off_maintain_async;
        break;
    case HIM_INSTRU_RESET:
        func = instrument_reset_async;
        break;
    case HIM_PIPLINE_FILL:
        func = pipeline_fill_async;
        break;
    case HIM_TRUN_OFF_INSTRU:
        userData = (int32_t *)realloc(userData, 2*sizeof(int32_t));
        userData[0] = iUserData;
        userData[1] = iKeepCool;
        func = shutdown_async;
        break;
    case HIM_PIPLINE_EMPTYING_ID:
        func = pipeline_clear_async;
        break;
    case QUALITYINSPECTION_GRIPPER:
        func = catcher_poweron_check_async;
        break;
    case QUALITYINSPECTION_SAMPLENEEDLE:
        func = needle_s_poweron_check_async;
        break;
    case QUALITYINSPECTION_REAGENTNEEDLE:
        func = needle_r2_poweron_check_async;
        break;
    case QUALITYINSPECTION_REAGENTBIN:
        func = reagent_table_poweron_check_async;
        break;
    case QUALITYINSPECTION_LIQUID_SP:
        func = liquid_poweron_check_async;
        break;
    case QUALITYINSPECTION_SAMPLETRAY:
        func = cuvette_supply_poweron_check_async;
        break;
    case QUALITYINSPECTION_SAMPLER:
        func = sampler_poweron_check_async;
        break;
    case QUALITYINSPECTION_MIXMODULE:
        func = mix_module_poweron_check_async;
        break;
    case QUALITYINSPECTION_MAG1_MODULE:
        func = mag1_module_poweron_check_async;
        break;
    case QUALITYINSPECTION_MAG2_MODULE:
        func = mag2_module_poweron_check_async;
        break;
    case QUALITYINSPECTION_OPTICAL_MODULE:
        func = optical_module_poweron_check_async;
        break;
    case QUALITYINSPECTION_LIQUID_R2:
        func = liquid_r2_pipeline_poweron_check_async;
        break;
    case QUALITYINSPECTION_LIQUID_S:
        func = liquid_s_pipeline_poweron_check_async;
        break;
    case QUALITYINSPECTION_LIQUID_SCLR:
        func = liquid_sclr_pipeline_poweron_check_async;
        break;
    case QUALITYINSPECTION_LIQUID_NWP:
        func = liquid_nwp_pipeline_poweron_check_async;
        break;

    default:
        machine_maintence_state_set(0);
        LOG("undefine maintaince ID!\n");
        break;
    }

    if (func) {
        work_queue_add(func, userData);
        return EXE_STATE::SUCCESS;
    } else {
        return EXE_STATE::FAIL;
    }

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIMaintenanceHandler::RunMaintenanceGroup(const int32_t iGroupID, const std::vector< ::MAINTENANCE_ITEM_T> & lstItems, const int32_t iUserData)
{
    LOG("RunMaintenanceGroup: iGroupID:%d\n", iGroupID);

    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    *userData = iUserData;
    work_queue_function func = NULL;
    int32_t item_size = lstItems.size();
    int32_t i = 0;

    if (get_machine_stat() == MACHINE_STAT_RUNNING) {
        LOG("machine is running!\n");
        return EXE_STATE::FAIL;
    }
    if (machine_maintence_state_get() == 1) {
        LOG("last maintenance is running!\n");
        return EXE_STATE::FAIL;
    }
    set_machine_stat(MACHINE_STAT_RUNNING);
    machine_maintence_state_set(1);

    LOG("items size = %d\n", item_size);
    set_com_maintain_size(item_size);

    for (i=0; i< item_size; i++) {
        LOG("%d:[%d | %d]\n", i, lstItems[i].iItemID, lstItems[i].iParam);
        set_com_maintain_param(i, lstItems[i].iItemID, lstItems[i].iParam);
    }

    if (iGroupID == HIM_START_UP || iGroupID == HIM_SHUT_DOWN) {
        react_cup_del_all();
        clear_slip_list_motor();
        module_fault_stat_clear();
        liquid_detect_remain_del_all();
    } else if (iGroupID == HIM_EMERGENCY_STOP) {
        react_cup_del_all();
        clear_slip_list_motor();
        module_fault_stat_clear();
        liquid_detect_remain_del_all();
        pump_cur_steps_set(LIQ_PERF_FULL_STEPS);/* 设定特殊清洗液泵位置为最大步长，防止柱塞泵维护前异常打液 */
    } else {
        clear_slip_list_motor();
        module_fault_stat_clear();
        liquid_detect_remain_del_all();
    }

    func = com_maintain_task;

    if (func) {
        work_queue_add(func, userData);
        return EXE_STATE::SUCCESS;
    } else {
        return EXE_STATE::FAIL;
    }
    return EXE_STATE::SUCCESS;
}


