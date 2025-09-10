#include <iostream>

#include "HISampleDetectHandler.h"

#include <module_cup_monitor.h>
#include <h3600_cup_param.h>

#include <log.h>
#include <work_queue.h>
#include <../thrift_handler.h>
#include <float.h>
#include <h3600_needle.h>
#include <module_reagent_table.h>
#include <module_cup_monitor.h>
#include "thrift_service_software_interface.h"
#include <h3600_maintain_utils.h>
#include <module_magnetic_bead.h>
#include <module_reagent_table.h>
#include <module_incubation.h>
#include <module_optical.h>
#include <module_monitor.h>
#include <module_catcher.h>
#include <module_cup_mix.h>
#include <module_needle_s.h>
#include <module_liquid_detect.h>
#include <module_engineer_debug_position.h>

using namespace std;

#define CUP_MAX_VOL         250
#define BLEND_MAX_VOL       200.0   /* 混匀量，混匀时最大的混合物量 */
#define BLEND_MIN_VOL       200.0   /* 混匀量，混匀时最小的混合物量 */
#define SAMPLE_MIN_VOL      5.0     /* 最小加样量 */

HISampleDetectHandler::HISampleDetectHandler() {
    // Your initialization goes here
}

typedef struct {
    int32_t user_data;
} self_test_para_t;

static int dilu_func_double_dilu(double reaction_vol, double dilu_factor_up, double dilu_factor_down, double take_ul0, struct dilu_attr *cup_vol)
{
    int ret = 0;
    int retry = 0;
    double samp_conc = 0;
    double dilu_vol = 0;
    double limit = 20.0;
    double samp_vol = reaction_vol * dilu_factor_up / dilu_factor_down;

    cup_vol[0].take_ul = take_ul0;
    //if (fabs(take_ul0 - (double)SAMPLE_MIN_VOL) < 0.000001) {
        if ((cup_vol[0].take_ul / (double)BLEND_MAX_VOL) < (((double)1 / limit) - 0.000001)) {
            LOG("dilu_func: up to m = %lf.\n", limit);
            cup_vol[0].take_ul = (double)BLEND_MAX_VOL / limit;
        }
    //}

again:
    cup_vol[0].dilu_ul = (double)BLEND_MAX_VOL - cup_vol[0].take_ul; /* 第一杯混合物中，稀释液的体积不可能小于最小吸样量 */
    samp_conc = cup_vol[0].take_ul / (double)BLEND_MAX_VOL;  /* 当前样本的浓度 */
    cup_vol[1].take_ul = samp_vol / samp_conc;              /* 需要移入第二杯的混合物量，此时样本保证为符合稀释比例的量 */
    LOG("dilu_func: take_ul[0] = %lf, dilu_ul[0] = %lf, take_ul[1] = %lf.\n", cup_vol[0].take_ul, cup_vol[0].dilu_ul, cup_vol[1].take_ul);
    if (cup_vol[1].take_ul < (double)SAMPLE_MIN_VOL) {
        if (cup_vol[0].take_ul > (double)SAMPLE_MIN_VOL) {
            LOG("dilu_func: cup_vol[1].take_ul(%lf) < SAMPLE_MIN_VOL(%lf), retry %lf.\n", cup_vol[1].take_ul, (double)SAMPLE_MIN_VOL, cup_vol[0].take_ul - 1);
            cup_vol[0].take_ul -= 1;
            goto again;
        } else {
            ret = -1;
            LOG("dilu_func: calc failed.\n");
            goto out;
        }
    }
    dilu_vol = cup_vol[1].take_ul - samp_vol;               /* 当前含有的稀释液量 */
    cup_vol[1].dilu_ul = (reaction_vol - samp_vol) - dilu_vol; /* 还需要增加的稀释液量 */
    if (cup_vol[1].dilu_ul != 0 && cup_vol[1].dilu_ul < (double)SAMPLE_MIN_VOL) {
        LOG("dilu_func: dilu_ul[1](%lf) < SAMPLE_MIN_VOL(%lf), retry.\n", cup_vol[1].dilu_ul, (double)SAMPLE_MIN_VOL);
        if (retry < 5) {
            cup_vol[0].take_ul = SAMPLE_MIN_VOL + (retry + 1) * SAMPLE_MIN_VOL; /* 10,15,20,25,30 */
            retry++;
            goto again;
        } else {
            LOG("dilu_func: dilu_ul[1] = %lf, still error.\n", cup_vol[1].dilu_ul);
            ret = -1;
            goto out;
        }
    }
    LOG("dilu_func: <double dilu> take_ul[0] = %lf, dilu_ul[0] = %lf, take_ul[1] = %lf, dilu_ul[1] = %lf.\n",
        cup_vol[0].take_ul, cup_vol[0].dilu_ul, cup_vol[1].take_ul, cup_vol[1].dilu_ul);

out:
    return ret;
}

static int dilu_func_expand(double reaction_vol, double dilu_factor_up, double dilu_factor_down, struct dilu_attr *cup_vol)
{
    int ret = 0;
    double limit = 20.0;
    double multiple = 0;

    multiple = BLEND_MIN_VOL / reaction_vol;
    cup_vol[0].take_ul = multiple * (reaction_vol * dilu_factor_up / dilu_factor_down);
    cup_vol[0].dilu_ul = multiple * (reaction_vol - reaction_vol * dilu_factor_up / dilu_factor_down);
    cup_vol[1].take_ul = reaction_vol;
    cup_vol[1].dilu_ul = 0;

    LOG("dilu_func: after expand, take_ul[0] = %lf, dilu_ul[0] = %lf, take_ul[1] = %lf.\n",
        cup_vol[0].take_ul, cup_vol[0].dilu_ul, cup_vol[1].take_ul);
    if ((cup_vol[0].take_ul / (double)BLEND_MIN_VOL) < (((double)1 / limit) - 0.000001)) {
        if (dilu_func_double_dilu(reaction_vol, dilu_factor_up, dilu_factor_down, cup_vol[0].take_ul, cup_vol) < 0) {
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

/*
 *  reaction_vol: 反应量
 *  dilu_factor_up / dilu_factor_down: 稀释比例
 *  retval: 返回使用杯子数
 */
int dilu_handle(double reaction_vol, double dilu_factor_up, double dilu_factor_down, struct dilu_attr *cup_vol)
{
    double sample_vol, dilu_vol, total_vol, multiple;
    int cup = 1;
    double limit = 20.0;

    LOG("dilu_func: reaction_vol: %lf, ratio: %lf / %lf.\n", reaction_vol, dilu_factor_up, dilu_factor_down);
    if (reaction_vol > (double)CUP_MAX_VOL) {
        LOG("dilu_func: WARNING reaction_vol(%lf) > CUP_MAX_VOL(%lf).\n", reaction_vol, CUP_MAX_VOL);
        cup = 0;
        goto done;
    } else {
        sample_vol = reaction_vol * dilu_factor_up / dilu_factor_down;
        if (sample_vol >= (double)SAMPLE_MIN_VOL) {
            dilu_vol  = reaction_vol - sample_vol;
            total_vol = sample_vol + dilu_vol;
            if (total_vol > (double)BLEND_MAX_VOL) {
                LOG("dilu_func: total_vol(%lf) > BLEND_MAX_VOL(%lf), double_dilu.\n", total_vol, (double)BLEND_MAX_VOL);
                if (dilu_func_double_dilu(reaction_vol, dilu_factor_up, dilu_factor_down, (double)SAMPLE_MIN_VOL, cup_vol) < 0) {
                    cup = 0;
                    goto done;
                }
                cup++;
            } else {
                cup_vol[0].take_ul = sample_vol;
                cup_vol[0].dilu_ul = dilu_vol;
                LOG("dilu_func: <normal> take_ul[0] = %lf, dilu_ul[0] = %lf.\n", cup_vol[0].take_ul, cup_vol[0].dilu_ul);
            }
        } else { /* 不满足最小吸样量 */
            if (!sample_vol) { /* AT-III 稀释倍数0/1已在前生成测试杯处理 */
                LOG("dilu_func: cant be here.\n");
                cup = 0;
                goto done;
            } else {
                dilu_vol = reaction_vol - sample_vol;
                LOG("dilu_func: <sample_vol(%lf) < SAMPLE_MIN_VOL(%lf)>, dilu_vol = %lf.\n", sample_vol, (double)SAMPLE_MIN_VOL, dilu_vol);
                multiple    = SAMPLE_MIN_VOL / sample_vol;  /* 样本达到最小吸样量需扩大的倍数 */
                sample_vol  = SAMPLE_MIN_VOL;
                dilu_vol    = dilu_vol * multiple;          /* 稀释液量也需要同步扩大相同倍数 */
                total_vol   = sample_vol + dilu_vol;        /* 样本和稀释液都扩大后的反应物总体积 */
                LOG("dilu_func: m = %lf, s = %lf, d = %lf, t = %lf.\n", multiple, sample_vol, dilu_vol, total_vol);
                if (total_vol < (double)BLEND_MIN_VOL) {
                    LOG("dilu_func: total_vol(%lf) < BLEND_MIN_VOL(%lf), expand.\n", total_vol, (double)BLEND_MIN_VOL);
                    if (dilu_func_expand(reaction_vol, dilu_factor_up, dilu_factor_down, cup_vol) < 0) {
                        cup = 0;
                        goto done;
                    }
                    cup++;
                } else if (fabs(total_vol - (double)BLEND_MIN_VOL) < 0.000001 ||
                    fabs(total_vol - (double)BLEND_MAX_VOL) < 0.000001) {
                    cup_vol[0].take_ul = sample_vol;
                    cup_vol[0].dilu_ul = dilu_vol;
                    if ((cup_vol[0].take_ul / (double)BLEND_MAX_VOL) < (((double)1 / limit) - 0.000001)) {
                        LOG("dilu_func: up to m = %lf.\n", limit);
                        cup_vol[0].take_ul = (double)BLEND_MAX_VOL / limit;
                        if (dilu_func_double_dilu(reaction_vol, dilu_factor_up, dilu_factor_down, cup_vol[0].take_ul, cup_vol) < 0) {
                            cup = 0;
                            goto done;
                        }
                        cup++;
                    } else {
                        cup_vol[1].take_ul = reaction_vol;
                        cup_vol[1].dilu_ul = 0;
                        cup++;
                        LOG("dilu_func: <total_val(%lf) = BLEND_MIN_VOL(%lf)> take_ul[0] = %lf, dilu_ul[0] = %lf, take_ul[1] = %lf.\n",
                            total_vol, (double)BLEND_MIN_VOL, cup_vol[0].take_ul, cup_vol[0].dilu_ul, cup_vol[1].take_ul);
                    }
                } else {
                    /* 分杯稀释 */
                    if (dilu_func_double_dilu(reaction_vol, dilu_factor_up, dilu_factor_down, (double)SAMPLE_MIN_VOL, cup_vol) < 0) {
                        cup = 0;
                        goto done;
                    }
                    cup++;
                }
            }
        }
    }

done:
    LOG("dilu_func: cup = %d.\n", cup);
    return cup;
}

static void set_sample_stop_async_handler(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

//    needle_err_count_clear();
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
}

static void normal_stop_async_handler(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    int idx = 0;

    module_start_control(MODULE_CMD_STOP);
    LOG("wait_detect_period_finish\n");
    wait_detect_period_finish();
    set_detect_period_flag(0);
    LOG("ensure_detect_period_finish\n");
    catcher_reset_standby();
    set_clear_catcher_pos(1);
    set_clear_needle_s_pos(1);

    /* 磁珠驱动力还原为 正常 */
    for (idx=0; idx<MAGNETIC_CH_NUMBER; idx++) {
        slip_magnetic_pwm_duty_set((magnetic_pos_t)idx, 0);
    }

    async_return.return_type = RETURN_INT;
    async_return.return_int = 0;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    if (get_machine_stat() != MACHINE_STAT_STOP) {
        set_machine_stat(MACHINE_STAT_STANDBY);
    }

    free(arg);
}

/* 自检流程：先复位再自检 */
static void instrument_self_test_async(void *arg)
{
    int ret = 0;
    self_test_para_t *data = (self_test_para_t *)arg;
    react_cup_del_all();
    machine_maintence_state_set(1);
    init_module_work_flag();
    reinit_incubation_data();
    reinit_cup_mix_data();
    reinit_magnetic_data();
    reinit_optical_data();
    clear_exist_dilu_cup();
    clear_slip_list_motor();
    set_machine_stat(MACHINE_STAT_RUNNING);
    sq_list_clear();

    /* 自检部分 */
    ret = reset_all_motors();
    if (ret == 0) {
        if (ENGINEER_IS_RUN == engineer_is_run_get()) {/* 执行过工程师模式需要清杯 */
            ret = instrument_self_check(1, 1);
            engineer_is_run_set(ENGINEER_NOT_RUN);
        } else {
            ret = instrument_self_check(0, 1);
        }
        set_clear_catcher_pos(1);
        set_clear_needle_s_pos(1);
        if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
            ret = -1;
        }
    }
    cup_pos_used_clear();
    module_sampler_add_set(SAMPLER_ADD_START);

    report_asnyc_invoke_result(data->user_data, ret == 0 ? 0 : 1, NULL);
    LOG("instrument_self_test_async finish, ret:%d\n", ret);
    machine_maintence_state_set(0);
    free(arg);
}

/* 仪器自检 */
::EXE_STATE::type HISampleDetectHandler::InstrumentSelfTestAsync(const std::vector< ::REAGENT_MIX_INFO_T> & lstReagMixInfo, const std::vector< ::REAGENT_POS_INFO_T> & lstReagPosInfo, const int32_t iUserData) {
	LOG("InstrumentSelfTestAsync\n");
    int32_t idx;
    int32_t size = lstReagMixInfo.size();
    int32_t regent_size = lstReagPosInfo.size();
    const REAGENT_MIX_INFO_T *ReagMixInfo = NULL;
    const REAGENT_POS_INFO_T *ReagPosInfo = NULL;
    reag_consum_info_t reag_consum_info = {-1};
    self_test_para_t *data = NULL;

    if (get_machine_stat() == MACHINE_STAT_RUNNING) {
        LOG("machine is running!\n");
        return EXE_STATE::FAIL;
    }

    if (machine_maintence_state_get() == 1) {
        LOG("last maintenance is running!\n");
        return EXE_STATE::FAIL;
    }

    /* 检查 与X子板连接情况 */
    if (check_all_board_connect() == -1) {
        return EXE_STATE::FAIL;
    }

    data = (self_test_para_t *)calloc(1, sizeof(self_test_para_t));
    if (size > 0) {
        LOG("reagent_mix_func: start in self test, size = %d.\n", size);
        for (idx = 0; idx < size; idx++) {
            ReagMixInfo = &lstReagMixInfo[idx];
            memset(&reag_consum_info, -1, sizeof(reag_consum_info_t));
            if (ReagMixInfo->iMixingType >= 1 && ReagMixInfo->iMixingType <= 3) {
               reag_consum_info.pos_idx = ReagMixInfo->iPosIndex;
               reag_consum_info.mix_type = ReagMixInfo->iMixingType;
               reag_consum_info.bottle_type = ReagMixInfo->iBottleType;
               reag_consum_info.mix_flag = 1;
            }
            reag_consum_param_set(&reag_consum_info, 1);
        }
    }

    if (regent_size > 0) {
        LOG("reag_remain_func: start in self test, size = %d.\n", regent_size);
        for (idx = 0; idx < regent_size; idx++) {
            ReagPosInfo = &lstReagPosInfo[idx];
            memset(&reag_consum_info, -1, sizeof(reag_consum_info_t));
            reag_consum_info.pos_idx = ReagPosInfo->iPosIndex;
            reag_consum_info.bottle_type = ReagPosInfo->iBottleType;
            reag_consum_info.reag_category = ReagPosInfo->iReagentCategory;
            reag_consum_info.rx = ReagPosInfo->iRx;
            liquid_detect_remain_add_tail(ReagPosInfo->iPosIndex, ReagPosInfo->iBottleType,
                ReagPosInfo->iReagentCategory, ReagPosInfo->iRx, ReagPosInfo->iBottleMaterial);
        }
        reag_consum_param_set(&reag_consum_info, 0);
    }

    data->user_data = iUserData;
    work_queue_add(instrument_self_test_async, data);
    return EXE_STATE::SUCCESS;
}

static struct needle_r_x_attr reagent_transform(const std::vector<REAGENT_INFO_T> &reagents)
{
    const REAGENT_INFO_T *reagent = NULL;
    struct needle_r_x_attr needle_r_x;
    int32_t j = 0;
    int32_t size = reagents.size();

    memset(&needle_r_x, 0, sizeof(struct needle_r_x_attr));

    if (size == 0) {
        reagent = &reagents[j];
    } else {
        for (j = 0; j < size; j++) { /* 按当前(截至2023-09-28)设定 size为1 */
            reagent = &reagents[j];
            needle_r_x.pos_idx = reagent->iPosIndex;
            needle_r_x.needle_pos = (needle_pos_t)transfer_to_enum((needle_pos_t)reagent->iPosIndex);
            needle_r_x.bottle_type = reagent->iBottleType;
            needle_r_x.take_ul = reagent->iAspriationVol;
            needle_r_x.r_x_add_stat = CUP_STAT_UNUSED;
            needle_r_x.needle_heat_enable = reagent->bEnableHeat ? ATTR_ENABLE : ATTR_DISABLE;

            needle_r_x.prev_clean.type = reagent->tPreRinseInfo.bEnableWashA ? SPECIAL_CLEAN : NORMAL_CLEAN;
            needle_r_x.prev_clean.status = CUP_STAT_UNUSED;

            needle_r_x.post_clean.type = reagent->tPostRinseInfo.bEnableWashA ? SPECIAL_CLEAN : NORMAL_CLEAN;
            needle_r_x.post_clean.status = CUP_STAT_UNUSED;
        }
    }

    return needle_r_x;
}

::EXE_STATE::type HISampleDetectHandler::CreateSampleOrder(const  ::SAMPLE_ORDER_INFO_T& tSampleOrderInfo) {
    struct react_cup *test_cup = NULL; /* 测试杯 */
    struct react_cup *diluent_cup = NULL; /* 稀释杯 */
    ORDER_INFO_T *order = NULL;
    REAGENT_INFO_T *reagent = NULL;
    struct sample_tube cup_sample_tube = {0};
    struct dilu_attr dilu_cup[2] = {0}; /* 稀释杯参数，最多两步稀释 */
    int32_t order_size = tSampleOrderInfo.lstOrderInfo.size();
    int cups = 0;
    uint32_t idx = 0;
    sp_para_t sp_para = {0};
    double take_vol = 0;
    double sample_vol = 0;
    double dilu_vol = 0;
    double total_dilu = 0; /* 统计总共需要使用的稀释液量上报上位机进行扣除 */
    double total_qc = 0; /* 试剂仓质控也涉及到吸稀释液后再吸质控品，需要计算后上报上位机扣除 */
    uint32_t r1_size = 0; /* 孵育试剂添加信息 */
    uint32_t r2_size = 0; /* 启动试剂添加信息 */
    sq_info_t * sq_info = NULL; /* 样本质量检测 */

    LOG("CreateSampleOrder: iRackIndex:%d iSamplePosIndex:%d order_size:%d\n",
            tSampleOrderInfo.iRackIndex,
            tSampleOrderInfo.iSamplePosIndex,
            order_size);

    /* print sample_quality configure */
    if (tSampleOrderInfo.tSampleQualitySet.bIsCheckAnticoagulationRatio ||
        tSampleOrderInfo.tSampleQualitySet.bIsCheckClot) {
        LOG("sample_quality: rack = %d, pos = %d, tube_id = %d.\n",
            tSampleOrderInfo.iRackIndex, tSampleOrderInfo.iSamplePosIndex, tSampleOrderInfo.iSampleOrderNo);
        if (tSampleOrderInfo.tSampleQualitySet.bIsCheckAnticoagulationRatio) {
            LOG("sample_quality: CHECK_AR enable = 1, tube_type = %d, ar_mode = %d, D = %f, V = %f, F = %f.\n",
                (sample_tube_type_t)tSampleOrderInfo.eSampleTubeType,
                tSampleOrderInfo.tSampleQualitySet.iAnticoagulationRatioHandlingMode,
                tSampleOrderInfo.tSampleQualitySet.dTubeInsideDiameter,
                tSampleOrderInfo.tSampleQualitySet.dBloodCapacity,
                tSampleOrderInfo.tSampleQualitySet.dBloodCapacityFloat);
        }
        if (tSampleOrderInfo.tSampleQualitySet.bIsCheckClot) {
            LOG("sample_quality: CHECK_CLOT enable = 1, clot_sa = %d, clot_mode = %d.\n",
                tSampleOrderInfo.tSampleQualitySet.iClotSensitivity, tSampleOrderInfo.tSampleQualitySet.iClotHandlingMode);
        }
    }

    /* 空订单仅记录订单下发动作 */
    if (!order_size) {
        goto bad;
    }

    /* 非空订单处理 */
    for (int32_t i = 0; i < order_size; i++) {
        test_cup =  NULL;
        diluent_cup = NULL;
        order = (ORDER_INFO_T *)&tSampleOrderInfo.lstOrderInfo[i];

        cup_sample_tube.sq_report = 0;
        cup_sample_tube.sample_tube_id = tSampleOrderInfo.iSampleOrderNo;
        cup_sample_tube.rack_idx = tSampleOrderInfo.iRackIndex;
        cup_sample_tube.sample_index = tSampleOrderInfo.iSamplePosIndex;
        cup_sample_tube.sp_spec_clean_stat = CUP_STAT_UNUSED;
        cup_sample_tube.check_anticoagulation_ratio = tSampleOrderInfo.tSampleQualitySet.bIsCheckAnticoagulationRatio;
        cup_sample_tube.check_clot = tSampleOrderInfo.tSampleQualitySet.bIsCheckClot;
        cup_sample_tube.type = (sample_tube_type_t)tSampleOrderInfo.eSampleTubeType;
        cup_sample_tube.sample_volume = (double)order->iSampleVolume; /* 样本量需要计算得出 */
        cup_sample_tube.diluent_volume = 0;   /* 稀释液量需要计算得出 */
        cup_sample_tube.diluent_pos = (pos_diluent_t)(order->iDiluentPos - DILU_IDX_START); /* 稀释液位从43开始,1-42为试剂仓内位置 */
        cup_sample_tube.r_x_add_stat = CUP_STAT_UNUSED;
        if (cup_sample_tube.rack_idx == 300) {  /* 试剂仓质控 */
            cup_sample_tube.rack_type = REAGENT_QC_SAMPLE;
        } else {
            cup_sample_tube.rack_type = NORMAL_SAMPLE;
        }
        cup_sample_tube.check_priority = tSampleOrderInfo.iPriority;
        cup_sample_tube.slot_id = tSampleOrderInfo.iSlotNo;
        if (cup_sample_tube.rack_idx == 300) {  /* 试剂仓质控 */
            cup_sample_tube.sp_hat = SP_NO_HAT;
        } else {
            cup_sample_tube.sp_hat = get_sp_is_carry_hat(&cup_sample_tube);
        }
        cup_sample_tube.test_cnt = order_size;
        if (i == order_size - 1) { /* 当前样本管的最后一个订单 */
            cup_sample_tube.sp_spec_clean = ENABLE_SP_CLEAN;   /* 更换样本管之前需要特殊清洗 */
            cup_sample_tube.is_last = 1;
        } else {
            cup_sample_tube.sp_spec_clean = DISABLE_SP_CLEAN;
            cup_sample_tube.is_last = 0;
        }
        LOG("cup_sample_tube.rack_index = %d sample_index:%d hat:%d\n", cup_sample_tube.rack_idx, cup_sample_tube.sample_index, cup_sample_tube.sp_hat);
        cup_sample_tube.rack_idx = cup_sample_tube.sample_index + ((cup_sample_tube.slot_id -1) *10);

        if (cup_sample_tube.check_anticoagulation_ratio || cup_sample_tube.check_clot) {
            LOG("sample_quality(rack = %d, sample = %d, tube_id = %d).\n",
                cup_sample_tube.rack_idx, cup_sample_tube.sample_index, cup_sample_tube.sample_tube_id);
            sq_info = (sq_info_t *)calloc(1, sizeof(sq_info_t));
            if (sq_info) {
                sq_info->rack_idx = cup_sample_tube.rack_idx;
                sq_info->pos_idx = cup_sample_tube.sample_index;
                sq_info->tube_id = cup_sample_tube.sample_tube_id;
                sq_info->order_no = order->iOrderNo;
                if (cup_sample_tube.check_anticoagulation_ratio &&
                    (cup_sample_tube.type == PP_1_8 || cup_sample_tube.type == PP_2_7)) {
                    /* 抗凝比例检查仅针对每个样本管的第一个订单检测 */
                    if (i == 0) {
                        sq_info->sq_report |= CHECK_AR;
                        sq_info->d = tSampleOrderInfo.tSampleQualitySet.dTubeInsideDiameter;
                        sq_info->v = tSampleOrderInfo.tSampleQualitySet.dBloodCapacity;
                        sq_info->f = tSampleOrderInfo.tSampleQualitySet.dBloodCapacityFloat;
                        sq_info->sq.ar_handle_mode = tSampleOrderInfo.tSampleQualitySet.iAnticoagulationRatioHandlingMode;
                    }
                }
                if (cup_sample_tube.check_clot) {
                    sq_info->sq_report |= CHECK_CLOT;
                    sq_info->sq.clot_sa = tSampleOrderInfo.tSampleQualitySet.iClotSensitivity;
                    sq_info->sq.clot_handle_mode = tSampleOrderInfo.tSampleQualitySet.iClotHandlingMode;
                    LOG("clot check handle mode [%d]\n", sq_info->sq.clot_handle_mode);
                }
                LOG("sample_quality: rack = %d, pos = %d, tube_id = %d, iOrderNo = %d, report_flag = %d.\n",
                    cup_sample_tube.rack_idx, cup_sample_tube.sample_index, cup_sample_tube.sample_tube_id,
                    order->iOrderNo, sq_info->sq_report);
                sq_add_tail(sq_info);
                report_list_show();
            }
        }

        LOG("order->iSampleVolume = %d, order->DiluentRatio = %d/%d\n", order->iSampleVolume, order->tDiluentRatio.iUp, order->tDiluentRatio.iDown);
        if (!order->tDiluentRatio.iUp && order->iSampleVolume) {
            if (order->iCalibratorIndex != -1) {  /* 0/1 校准 */
                cups = 2;
                dilu_cup[0].dilu_ul = BLEND_MIN_VOL;
                dilu_cup[0].take_ul = 0;
                dilu_cup[1].dilu_ul = 0;
                dilu_cup[1].take_ul = order->iSampleVolume;
            } else {    /* AT-III 稀释倍数0/1,全部加稀释液,可直接生成测试杯 */
                cups = 1;
                dilu_cup[0].take_ul = 0;
                dilu_cup[0].dilu_ul = order->iSampleVolume;
                LOG("dilu_func: <case dilu_factor_up == 0> take_ul = %lf, dilu_ul = %lf.\n", dilu_cup[0].take_ul, dilu_cup[0].dilu_ul);
            }
        } else if (order->tDiluentRatio.iUp == 1 && order->tDiluentRatio.iDown == 1) { /* 1/1 不需要稀释 */
            cups = 1;
            dilu_cup[0].take_ul = order->iSampleVolume;
            dilu_cup[0].dilu_ul = 0;
            LOG("dilu_func: <case dilu_ratio == 1> take_ul = %lf, dilu_ul = 0.\n", dilu_cup[0].take_ul);
        } else {
            /* 稀释处理 */
            cups = dilu_handle((double)order->iSampleVolume, (double)order->tDiluentRatio.iUp, (double)order->tDiluentRatio.iDown, dilu_cup);
            if (cups == 0) {
                goto bad;
            }
        }

        LOG("cup_sample_tube1, order_no:%d ,rack_index: %d, sample_index: %d\n", cup_sample_tube.sample_tube_id, cup_sample_tube.rack_idx, cup_sample_tube.sample_index);

        if (cups == 2) { /* 第一个为稀释杯 */
            LOG("add a DILU_CUP, order_no = %d.\n", order->iOrderNo);
            diluent_cup = new_a_cup();
            diluent_cup->cup_id = get_next_cup_id();
            diluent_cup->order_no = order->iOrderNo;
            diluent_cup->rerun = order->iOrderType;
            diluent_cup->cup_active = CUP_ACTIVE;
            diluent_cup->cup_delay_active = CUP_ACTIVE;
            diluent_cup->cup_pos = POS_CUVETTE_SUPPLY_INIT;
            diluent_cup->priority = cup_sample_tube.check_priority;
            LOG("cup_sample_tube2, order_no:%d ,rack_index: %d, sample_index: %d\n", cup_sample_tube.sample_tube_id, cup_sample_tube.rack_idx, cup_sample_tube.sample_index);
            memcpy(&diluent_cup->cup_sample_tube, &cup_sample_tube, sizeof(struct sample_tube));
            LOG("diluent_cup1, order_no:%d ,rack_index: %d, sample_index: %d\n", diluent_cup->order_no, diluent_cup->cup_sample_tube.rack_idx, diluent_cup->cup_sample_tube.sample_index);
            diluent_cup->cup_sample_tube.sp_spec_clean = DISABLE_SP_CLEAN;
            diluent_cup->cup_type = DILU_CUP;
            diluent_cup->cup_dilu_attr.take_ul = dilu_cup[0].take_ul;
            diluent_cup->cup_dilu_attr.dilu_ul = dilu_cup[0].dilu_ul;
            diluent_cup->cup_dilu_attr.take_mix_ul = dilu_cup[1].take_ul;
            total_dilu += (diluent_cup->cup_dilu_attr.dilu_ul > 0.000001 ? diluent_cup->cup_dilu_attr.dilu_ul + NEEDLE_ABSORB_MORE_DILU : 0);
            LOG("calc dilu_vol = %lf(dilu = %lf).\n", total_dilu, diluent_cup->cup_dilu_attr.dilu_ul);

            if (h3600_conf.pierce_support) {
                if (!sp_para.temp_store && (i == 0 && order_size >= 1)) {
                    diluent_cup->sp_para.temp_store = sp_para.temp_store = 1;
                }
                /* 已有暂存样本，后续测试均从缓存位取样 */
                if (sp_para.temp_store) {
                    diluent_cup->sp_para.temp_take = 1;
                }
            }
            add_cup_handler(diluent_cup, REACT_CUP_LIST);
            LOG("diluent_cup2, order_no:%d ,rack_index: %d, sample_index: %d\n", diluent_cup->order_no, diluent_cup->cup_sample_tube.rack_idx, diluent_cup->cup_sample_tube.sample_index);
            LOG("DILU_CUP rack = %d, pos = %d, tubeid = %d, order = %d.\n",
                diluent_cup->cup_sample_tube.rack_idx,
                diluent_cup->cup_sample_tube.sample_index,
                diluent_cup->cup_sample_tube.sample_tube_id,
                diluent_cup->order_no);
        }

        /* 测试杯 */
        test_cup = new_a_cup();
        test_cup->cup_id = get_next_cup_id();
        test_cup->order_no = order->iOrderNo;
        test_cup->rerun = order->iOrderType;
        test_cup->cup_active = CUP_ACTIVE;
        test_cup->cup_delay_active = CUP_ACTIVE;
        test_cup->cup_type = TEST_CUP;
        test_cup->cup_pos = POS_CUVETTE_SUPPLY_INIT;
        test_cup->priority = cup_sample_tube.check_priority;
        test_cup->order_state = OD_INIT;
        LOG("diluent_cup0, tube_id:%d ,rack_index: %d, sample_index: %d, order_type:%d\n", cup_sample_tube.sample_tube_id, cup_sample_tube.rack_idx, cup_sample_tube.sample_index, test_cup->rerun);
        memcpy(&test_cup->cup_sample_tube, &cup_sample_tube, sizeof(struct sample_tube));

        /* 采样针 */
        test_cup->cup_test_attr.needle_s.take_ul = cups == 1 ? dilu_cup[0].take_ul : dilu_cup[1].take_ul;
        test_cup->cup_test_attr.needle_s.take_dilu_ul = cups == 1 ? dilu_cup[0].dilu_ul : dilu_cup[1].dilu_ul;
        test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_UNUSED;
        test_cup->cup_test_attr.needle_s.post_clean.type = (cup_sample_tube.sp_spec_clean == ENABLE_SP_CLEAN)?SPECIAL_CLEAN:NORMAL_CLEAN;
        test_cup->cup_test_attr.needle_s.post_clean.status = CUP_STAT_UNUSED;
        total_dilu += (test_cup->cup_test_attr.needle_s.take_dilu_ul > 0.000001 ? test_cup->cup_test_attr.needle_s.take_dilu_ul + NEEDLE_ABSORB_MORE_DILU : 0);

        LOG("calc dilu_vol = %lf(dilu = %lf).\n", total_dilu, test_cup->cup_test_attr.needle_s.take_dilu_ul);
        LOG("TEST_CUP rack = %d, pos = %d, tubeid = %d, order = %d.\n",
            test_cup->cup_sample_tube.rack_idx, test_cup->cup_sample_tube.sample_index,
            test_cup->cup_sample_tube.sample_tube_id, test_cup->order_no);

        /* 处理暂存相关信息 */
        if (h3600_conf.pierce_support) {
            /* 穿刺暂存：未被空订单缓存 且 同管订单大于2才进行缓存 */
            if (!sp_para.temp_store && (i == 0 && order_size >= 1)) {
                test_cup->sp_para.temp_store = sp_para.temp_store = 1;
            }
            /* 已有暂存样本，后续测试均从缓存位取样 */
            if (sp_para.temp_store) {
                test_cup->sp_para.temp_take = 1;
            }
        }

        if (cups == 2) {
            LOG("cups = 2! take_ul = %lf, dilu_ul = %lf.(order = %d).\n",
                diluent_cup->cup_dilu_attr.take_ul, diluent_cup->cup_dilu_attr.dilu_ul, i);
            sample_vol = diluent_cup->cup_dilu_attr.take_ul;
            dilu_vol   = diluent_cup->cup_dilu_attr.dilu_ul;
        } else {
            LOG("cups = 1! take_ul = %lf, dilu_ul = %lf.(order = %d).\n",
                test_cup->cup_test_attr.needle_s.take_ul, test_cup->cup_test_attr.needle_s.take_dilu_ul, i);
            sample_vol = test_cup->cup_test_attr.needle_s.take_ul;
            dilu_vol   = test_cup->cup_test_attr.needle_s.take_dilu_ul;
        }

        LOG("take_ul = %lf, dilu_ul = %lf.(order = %d).\n", sample_vol, dilu_vol, i);
        /* 样本针一个周期内只吸取样本，则多吸NEEDLE_ABSORB_MORE - NEEDLE_RELEASE_MORE */
        take_vol += sample_vol + (dilu_vol ? 0 : NEEDLE_ABSORB_MORE - NEEDLE_RELEASE_MORE);
        LOG("take_ul = %lf.(order = %d).\n", take_vol, i);

        /* R1试剂 */
        r1_size = order->lstIncuReagentInfos.size();
#if 0
        test_cup->cup_test_attr.needle_r1 = reagent_transform(order->lstIncuReagentInfos);
#else
        memset(test_cup->cup_test_attr.needle_r1, 0, sizeof(test_cup->cup_test_attr.needle_r1));
        if (order->bAddFactorDeficientPlasma) { /* 乏因子血浆 */
            /* 优先加乏因子血浆 */
            reagent = &order->tFactorDeficientPlasmaInfo;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].pos_idx = reagent->iPosIndex;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos = (needle_pos_t)transfer_to_enum((needle_pos_t)reagent->iPosIndex);
            test_cup->cup_test_attr.needle_r1[R1_ADD1].bottle_type = reagent->iBottleType;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul = reagent->iAspriationVol;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat = CUP_STAT_UNUSED;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_heat_enable = reagent->bEnableHeat ? ATTR_ENABLE : ATTR_DISABLE;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].prev_clean.type = reagent->tPreRinseInfo.bEnableWashA ? SPECIAL_CLEAN : NORMAL_CLEAN;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].prev_clean.status = CUP_STAT_UNUSED;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].post_clean.type = reagent->tPostRinseInfo.bEnableWashA ? SPECIAL_CLEAN : NORMAL_CLEAN;
            test_cup->cup_test_attr.needle_r1[R1_ADD1].post_clean.status = CUP_STAT_UNUSED;
        }
        idx = order->bAddFactorDeficientPlasma ? R1_ADD2 : R1_ADD1; /* 需要添加乏因子血浆，则试剂后加 */
        if (r1_size == 0) {
            if (!order->bAddFactorDeficientPlasma) {
                test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat = CUP_STAT_USED;
            }
            test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat = CUP_STAT_USED;
        } else {
            reagent = &order->lstIncuReagentInfos[0];
            test_cup->cup_test_attr.needle_r1[idx].pos_idx = reagent->iPosIndex;
            test_cup->cup_test_attr.needle_r1[idx].needle_pos = (needle_pos_t)transfer_to_enum((needle_pos_t)reagent->iPosIndex);
            test_cup->cup_test_attr.needle_r1[idx].bottle_type = reagent->iBottleType;
            test_cup->cup_test_attr.needle_r1[idx].take_ul = reagent->iAspriationVol;
            test_cup->cup_test_attr.needle_r1[idx].r_x_add_stat = CUP_STAT_UNUSED;
            test_cup->cup_test_attr.needle_r1[idx].needle_heat_enable = reagent->bEnableHeat ? ATTR_ENABLE : ATTR_DISABLE;
            test_cup->cup_test_attr.needle_r1[idx].prev_clean.type = reagent->tPreRinseInfo.bEnableWashA ? SPECIAL_CLEAN : NORMAL_CLEAN;
            test_cup->cup_test_attr.needle_r1[idx].prev_clean.status = CUP_STAT_UNUSED;
            test_cup->cup_test_attr.needle_r1[idx].post_clean.type = reagent->tPostRinseInfo.bEnableWashA ? SPECIAL_CLEAN : NORMAL_CLEAN;
            test_cup->cup_test_attr.needle_r1[idx].post_clean.status = CUP_STAT_UNUSED;
            if (r1_size == 1 && (!order->bAddFactorDeficientPlasma)) {
                test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat = CUP_STAT_USED;
            }
        }
#endif

        /* R2试剂 */
        r2_size = order->lstDeteReagentInfos.size();
        test_cup->cup_test_attr.needle_r2 = reagent_transform(order->lstDeteReagentInfos);

        test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable = ATTR_DISABLE;
        if (r1_size) {
            reagent = &order->lstIncuReagentInfos[0];
            test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable = reagent->bEnableMixing ? ATTR_ENABLE : ATTR_DISABLE;
            test_cup->cup_test_attr.test_cup_incubation.mix_type = reagent->iMixingType;
            test_cup->cup_test_attr.test_cup_incubation.mix_rate = reagent->iMixingRate;
            test_cup->cup_test_attr.test_cup_incubation.mix_time = 20000;
        }
        test_cup->cup_test_attr.test_cup_incubation.incubation_mix_stat = CUP_STAT_UNUSED;
        test_cup->cup_test_attr.test_cup_incubation.incubation_time = order->iIncubationSeconds;
        test_cup->cup_test_attr.test_cup_incubation.incubation_stat = CUP_STAT_UNUSED;

        test_cup->cup_test_attr.test_cup_magnectic.magnectic_enable = ATTR_DISABLE;
        test_cup->cup_test_attr.test_cup_optical.optical_enable = ATTR_DISABLE;

        /* 检测方式 0.双磁路磁珠法，1.光学-免疫透射比浊法,2.光学-发色底物法 */
        if (order->iMethod == 0) {
            test_cup->cup_test_attr.test_cup_magnectic.magnectic_enable = ATTR_ENABLE;
        } else {
            test_cup->cup_test_attr.test_cup_optical.optical_enable = ATTR_ENABLE;
        }

        /* 磁珠法参数 */
        test_cup->cup_test_attr.test_cup_magnectic.mag_beed_clot_percent = order->iMagBeedClotPercent;
        test_cup->cup_test_attr.test_cup_magnectic.magnectic_power = order->iMagBeedDriveForce;
        test_cup->cup_test_attr.test_cup_magnectic.mag_beed_max_detect_seconds = order->iMagBeedMaxDetectSeconds;
        test_cup->cup_test_attr.test_cup_magnectic.mag_beed_min_detect_seconds = order->iMagBeedMinDetectSeconds;
        test_cup->cup_test_attr.test_cup_magnectic.magnectic_stat = CUP_STAT_UNUSED;

        /* 光学法参数 */
        test_cup->cup_test_attr.test_cup_optical.main_wave = order->iMainWavelenght;
        test_cup->cup_test_attr.test_cup_optical.vice_wave = order->iSubWavelenght;
        test_cup->cup_test_attr.test_cup_optical.optical_main_seconds = order->iOpticsMainMeasurementSeconds;
        test_cup->cup_test_attr.test_cup_optical.optical_vice_seconds = order->iOpticsSubMeasurementSeconds;
        test_cup->cup_test_attr.test_cup_optical.optical_stat = CUP_STAT_UNUSED;
        test_cup->cup_test_attr.test_cup_optical.optical_mix_enable = ATTR_DISABLE;
        if (r2_size) {
            reagent = &order->lstDeteReagentInfos[0];
            test_cup->cup_test_attr.test_cup_optical.optical_mix_enable = reagent->bEnableMixing ? ATTR_ENABLE : ATTR_DISABLE;
            test_cup->cup_test_attr.test_cup_optical.optical_mix_type = reagent->iMixingType;
            test_cup->cup_test_attr.test_cup_optical.optical_mix_rate = reagent->iMixingRate;
            test_cup->cup_test_attr.test_cup_optical.optical_mix_time = 4500;
            test_cup->cup_test_attr.test_cup_optical.optical_mix_stat = CUP_STAT_UNUSED;
        }

        add_cup_handler(test_cup, REACT_CUP_LIST);
        LOG("test_cup, order_no:%d ,rack_index: %d, sample_index: %d\n", test_cup->order_no, test_cup->cup_sample_tube.rack_idx, test_cup->cup_sample_tube.sample_index);

        /* 计算试剂仓质控的消耗量 */
        if (cup_sample_tube.rack_idx == 300) {
            total_qc += (dilu_cup[0].dilu_ul > 0.000001 ?
                dilu_cup[0].take_ul : (dilu_cup[0].take_ul + NEEDLE_ABSORB_MORE - NEEDLE_RELEASE_MORE));
        }
    }

    cal_sample_volume_by_order(tSampleOrderInfo.iSampleOrderNo, take_vol); /* 累计样本加样量 */
    react_cup_list_show();

    report_order_remain_checktime(get_order_checktime());

    if ((module_start_stat_get() == MODULE_CMD_STOP) && (module_fault_stat_get() == MODULE_FAULT_NONE)) {
        module_start_control(MODULE_CMD_START);
        module_monitor_start(NULL);
        set_is_test_finished(0);
    }

    return EXE_STATE::SUCCESS;
bad:
    return EXE_STATE::FAIL;
}

/* 更新为急诊订单 iSampleOrderNo:测试样本编号 */
::EXE_STATE::type HISampleDetectHandler::UpdateSTATSampleOrder(const int32_t  iSampleOrderNo) {
	LOG("UpdateSTATSampleOrder\n");

    return EXE_STATE::SUCCESS;
}

/* 卸载槽订单信息 iSlotNo槽号（编号1~6） */
::EXE_STATE::type HISampleDetectHandler::RemoveSlotOrder(const int32_t  iSlotNo) {
    int ret = 0;

    LOG("RemoveSlotOrder iSlotNo:%d\n", iSlotNo);

    if (iSlotNo > RACK_NUM_MAX || !iSlotNo) {
        LOG("iSlotNo invalid! iSlotNo:%d\n", iSlotNo);
        return EXE_STATE::FAIL;
    }

    ret = del_cup_by_slot_id(iSlotNo);
    return (0 == ret) ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}

/* 同步接口，检测是否完成，返回值:0.未完成， 1.完成 */
int32_t HISampleDetectHandler::IsTestFinished()
{
    return get_is_test_finished();
}

::EXE_STATE::type HISampleDetectHandler::NormalStopAsync(const int32_t iUserData) {
    // Your implementation goes here
    LOG("NormalStopAsync\n");
    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    *userData = iUserData;

    work_queue_add(normal_stop_async_handler, userData);
    
    return EXE_STATE::SUCCESS;
}

/* 异步接口，耗材不足加样停接口 iUserData：用户数据， 下位机收到此命令，通过ReportOrderState上报停止订单 */
::EXE_STATE::type HISampleDetectHandler::ConsumablesStopAsync(const int32_t  iUserData) {
    LOG("ConsumablesStopAsync\n");

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HISampleDetectHandler::SetSampleStopAsync(const int32_t iSampleStop, const int32_t iUserData) {
    // Your implementation goes here
    LOG("SetSampleStopAsync iSampleStop = %d\n", iSampleStop);

    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    *userData = iUserData;

    module_sampler_add_set((1 == iSampleStop) ? SAMPLER_ADD_STOP : SAMPLER_ADD_START); /* 设置加样停标志 */

    work_queue_add(set_sample_stop_async_handler, userData);
    return EXE_STATE::SUCCESS;
}

void HISampleDetectHandler::QueryIsOpenReagentBinCoverOrDiluentCover(std::vector<int32_t> & _return) {
    int idx = 0;
    int chg[DILU_IDX_END] = {0};

//    LOG("reag_remain_func: QueryIsOpenReagentBinCoverOrDiluentCover\n");
    reag_change_idx_get(chg);
    for (idx = 0; idx < REAG_IDX_END; idx++) {
        if (chg[idx] != 0) {
            _return.push_back(chg[idx]);
//            LOG("reag_remain_func: report idx = %d.\n", chg[idx]);
        }
    }
}

/* 删除订单，终止此订单的检测 iOrderNo:订单编号，上下位机的唯一识别依据, iUserData：用户数据 */
::EXE_STATE::type HISampleDetectHandler::DeleteOrder(const int32_t  iOrderNo) {
    LOG("DeleteOrder : %d\n", iOrderNo);

    delete_order_by_order_no(iOrderNo);
    report_order_remain_checktime(get_order_checktime());

    return EXE_STATE::SUCCESS;
}

/* 更新订单 
iOrderNo:订单编号 
iReagentType: 参见iReagentType（更新订单类型）定义，其为按位进行运算，可能同时更新多个
tOrderInfo：订单信息; 
iSamplePos: 质控品位置，仅在更新试剂仓质控时有效，且有效值为1-36，为0或其它值时无效 
*/
::EXE_STATE::type HISampleDetectHandler::UpdateOrder(const int32_t iOrderNo, const int32_t iReagentType, const  ::ORDER_INFO_T& tOrderInfo, const int32_t iSamplePos) {
    LOG("UpdateOrder\n");
    const REAGENT_INFO_T * reagent = NULL;
    uint32_t size = 0;
    int pos = 0;
    int pos1 = 0;
    int ret = -1;

    LOG("reag_switch: UpdateOrder, OrderNo: %d, reagentType: %d, sample_pos: %d\n", iOrderNo, iReagentType, iSamplePos);
    if (iReagentType == UPDATE_DILU) {
        pos = (pos_diluent_t)(tOrderInfo.iDiluentPos - DILU_IDX_START);
        LOG("reag_switch: UpdateOrder: UPDATE_DILU(order = %d), new_pos = %d.\n", iOrderNo, pos);
        ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
    } else if (iReagentType == UPDATE_QC) {
        pos = iSamplePos;
        LOG("reag_switch: UpdateOrder: UPDATE_QC(order = %d), new_pos = %d.\n", iOrderNo, pos);
        ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
    } else if (iReagentType == UPDATE_R2) {
        size = tOrderInfo.lstDeteReagentInfos.size();
        LOG("reag_switch: UpdateOrder: UPDATE_R2(order = %d), size = %d.\n", iOrderNo, size);
        if (size) {
            reagent = &tOrderInfo.lstDeteReagentInfos[0];
            pos = reagent->iPosIndex;
            LOG("reag_switch: UpdateOrder: UPDATE_R2(order = %d), new_pos = %d.\n", iOrderNo, pos);
            ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
        }
    } else if (TBIT(iReagentType, UPDATE_FACTOR) || TBIT(iReagentType, UPDATE_R1) || TBIT(iReagentType, UPDATE_R2)) {
        if (iReagentType == UPDATE_FACTOR) {
            size = tOrderInfo.lstIncuReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_FDP(order = %d), size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstIncuReagentInfos[0];
                pos = reagent->iPosIndex;
                LOG("reag_switch: UpdateOrder: UPDATE_FDP(order = %d), new_pos = %d.\n", iOrderNo, pos);
                ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
            }
        } else if (iReagentType == UPDATE_R1) {
            size = tOrderInfo.lstIncuReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_R1(order = %d), size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstIncuReagentInfos[0];
                pos = reagent->iPosIndex;
                LOG("reag_switch: UpdateOrder: UPDATE_R1(order = %d), new_pos = %d.\n", iOrderNo, pos);
                ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
            }
        } else if (iReagentType == UPDATE_R1 + UPDATE_FACTOR) {
            size = tOrderInfo.lstIncuReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_R1_FDP(order = %d), size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstIncuReagentInfos[1];
                pos = reagent->iPosIndex;
                LOG("reag_switch: UpdateOrder: UPDATE_R1_FDP(order = %d), new_pos = %d.\n", iOrderNo, pos);
                ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
            }
        } else if (iReagentType == UPDATE_R1 + UPDATE_R2) {
            size = tOrderInfo.lstIncuReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d), r1_size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstIncuReagentInfos[0];
                pos = reagent->iPosIndex;
            }
            size = tOrderInfo.lstDeteReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d), r2_size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstDeteReagentInfos[0];
                pos1 = reagent->iPosIndex;
            }
            if (pos && pos1) {
                LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d), new_pos = %d - %d.\n", iOrderNo, pos, pos1);
                ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
            } else {
                LOG("reag_switch: UpdateOrder: pos invalid %d - %d, please cheack!\n", pos, pos1);
            }
        } else if (iReagentType == UPDATE_FACTOR + UPDATE_R1 + UPDATE_R2) {
            size = tOrderInfo.lstIncuReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d), r1_size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstIncuReagentInfos[1];
                pos = reagent->iPosIndex;
            }
            size = tOrderInfo.lstDeteReagentInfos.size();
            LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d), r2_size = %d.\n", iOrderNo, size);
            if (size) {
                reagent = &tOrderInfo.lstDeteReagentInfos[0];
                pos1 = reagent->iPosIndex;
            }
            if (pos && pos1) {
                LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d), new_pos = %d - %d.\n", iOrderNo, pos, pos1);
                ret = update_reag_pos_info(iReagentType, iOrderNo, pos, pos1);
            } else {
                LOG("reag_switch: UpdateOrder: pos invalid %d - %d, please cheack!\n", pos, pos1);
            }
        }
    } else {
        LOG("reag_switch: UpdateOrder: Cant be here(type = %d), please cheack!\n", iReagentType);
    }

    return (ret == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL);
}

