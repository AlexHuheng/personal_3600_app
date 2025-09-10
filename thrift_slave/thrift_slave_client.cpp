#include <Defs_types.h>
#include <log.h>
#include <errno.h>
#include <module_monitor.h>
#include <h3600_maintain_utils.h>

#include "thrift_handler.h"
#include "thrift_connect_pool.h"

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace H2103_Slave_Invoke;

static int thrift_salve_client_connect_flag = 1; /* 控制 下位机是否连接上位机 0：不连接 1：连接(默认值)*/
static int thrift_salve_heartbeat_flag = 0; /* 下位机到上位机的连接心跳 0：失败 1：正常*/

/* 上报上位机调用异步接口的执行结果 user_data:用户数据, exe_state:执行结果, 成功(0), 失败(1), async_return: 返回值 */
int report_asnyc_invoke_result(int32_t user_data, int32_t exe_state, const async_return_t *async_return)
{
    EXE_STATE::type exeState = exe_state == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
    ASYNC_RETURN_T result;
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    result.iReturnType = 0;
    if (async_return) {
        result.iReturnType = async_return->return_type;
        switch (async_return->return_type) {
            case RETURN_VOID:
            break;
            case RETURN_INT:
                result.iReturnType = 1;
                result.iReturn = async_return->return_int;
            break;
            case RETURN_DOUBLE:
                result.iReturnType = 2;
                result.dReturn = async_return->return_double;
            break;
            case RETURN_STRING:
                result.iReturnType = 3;
                result.strReturn = async_return->return_string;
            break;
            default:
                LOG_ERROR("Unknown return type: %d\n", async_return->return_type);
            break;
        }
    }

    try {
        ret = spThriftClient->pSIOtherClient->ReportAsnycInvokeResult(user_data, exeState, result);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }

    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报报警信息 alarm_code:报警代码 alarm_info:报警详细信息 */
int report_device_abnormal(const int32_t iOrderNo, const char *alarm_info)
{
    std::string info = alarm_info;
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportAlarmMessage(iOrderNo, info);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上传 下位机xx文件 */
int upload_device_file(int32_t iFileType, int32_t iRandNo, const char *hexData, int32_t dataLen, 
                                int32_t iSeqNo, int32_t iIsEnd, const char *strMD5) 
{
    std::string data(hexData, dataLen);
    std::string str_md5(strMD5);
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIOtherClient->UploadBackupFile(iFileType, iRandNo, data, iSeqNo, iIsEnd, str_md5);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上传 
    1.下位机部件老化流程 
    iRunType: 运行类型, iTimes: 运行次数; iReserve: 保留位
*/
int report_run_times(int32_t iRunType, int32_t iTimes, int32_t iReserve)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIOtherClient->ReportRunTimes(iRunType, iTimes, iReserve);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报试剂信息 reag_prop */
int report_reagent_info(reag_scan_info_t *reag_prop)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;
    REAGENT_SCAN_INFO_T rci;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    LOG("reag_prop->barcode : %s reag_prop->barcode len:%d\n", reag_prop->barcode , strlen(reag_prop->barcode));
    rci.iReagBracketIndex   = reag_prop->braket_index;
    rci.iIsReagBracketExist = reag_prop->braket_exist;
    rci.iReagPosIndex       = reag_prop->pos_index;
    rci.iExistStatus        = reag_prop->pos_iexist_status;
    rci.strBarcode          = std::string(reag_prop->barcode);
    LOG("rci.strBarcode : %s\n", rci.strBarcode.c_str());

    try {
        ret = spThriftClient->pSIMaintenanceClient->ReportReagentInfo(rci);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报试剂余量 reag_pos:试剂位置索引 remain_volume:试剂余量，以ul为单位 */
int report_reagent_remain(int reag_pos, int remain_volume, int ord_no)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIMaintenanceClient->ReportReagentRemain(reag_pos, remain_volume, ord_no);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报自动标定位置信息，详见ENGINEER_DEBUG_POS_CALIB_T定义 */
int report_position_calibration(int cali_id, int pos_id, int old_v, int new_v)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;
    ENGINEER_DEBUG_POS_CALIB_T rci;

    LOG("cali_id : %d pos_id:%d old:%d new:%d\n", cali_id , pos_id, old_v, new_v);

    rci.iCalibID = cali_id;
    rci.iPosID = pos_id;
    rci.iOldValue = old_v;
    rci.iCurValue = new_v;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIMaintenanceClient->ReportPositionCalibtion(rci);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报自动标定位置信息，详见ENGINEER_DEBUG_MODULE_PARA_T定义 */
int report_position_calibration_H(struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_TT param)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;
    ENGINEER_DEBUG_MODULE_PARA_T rci;

    rci.tVirautlPosPara.iModuleIndex = param.iModuleIndex;
    rci.tVirautlPosPara.iVirtualPosIndex = param.iVirtualPosIndex;
    rci.tVirautlPosPara.strVirtualPosName = param.strVirtualPosName;
    rci.tVirautlPosPara.iEnableR = param.iEnableR;
    rci.tVirautlPosPara.iR_Steps = param.iR_Steps;
    rci.tVirautlPosPara.iR_MaxSteps = param.iR_MaxSteps;
    rci.tVirautlPosPara.iEnableX = param.iEnableX;
    rci.tVirautlPosPara.iX_Steps = param.iX_Steps;
    rci.tVirautlPosPara.iX_MaxSteps = param.iX_MaxSteps;
    rci.tVirautlPosPara.iEnableY = param.iEnableY;
    rci.tVirautlPosPara.iY_Steps = param.iY_Steps;
    rci.tVirautlPosPara.iY_MaxSteps = param.iY_MaxSteps;
    rci.tVirautlPosPara.iEnableZ = param.iEnableZ;
    rci.tVirautlPosPara.iZ_Steps = param.iZ_Steps;
    rci.tVirautlPosPara.iZ_MaxSteps = param.iZ_MaxSteps;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIMaintenanceClient->ReportPositionCalibtion_H(rci);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报维护项结果 iItemID：维护项ID bStart true.开始 false.结束  bStatus：true.成功 false.失败   */
int report_maintenance_item_result(int item_ID, int param, bool start, bool status)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;
    MAINTENANCE_ITEM_T rci;

    /* code */
    rci.iItemID = item_ID;
    rci.iParam = param;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIMaintenanceClient->ReportMaintenanceItemResult(rci, start, status);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报维护结束剩余时间 iRemainTime:剩余时间(单位:秒)  */
int report_maintenance_remain_time(int remain_time)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    /* code */

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSIMaintenanceClient->ReportMaintenanceRemainTime(remain_time);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报IO状态, 
 * sensor: 传感器编号 
 * state: 
 *   1: 打开(清洗泵或废液泵启动，打开加热等)，存在（如样本管探测），到位（反应装载到位）等; 
 *   0:关闭，不存在，未到位等(此定义不一定合理，针对不类别的IO)
*/
int report_io_state(output_io_t sensor, int state)
{
    OUTPUT_IO::type         type;
    EXE_STATE::type         ret         = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
        LOG("get thrift fail\n");
        return -1;
    }

    switch (sensor) {
        case TYPE_REAGENT_CASECOVER:
            type = OUTPUT_IO::PE_REGENT_TABLE_GATE;
            break;
        case TYPE_SAMPLE_SLOT_IO1:
            type = OUTPUT_IO::SAMPLE_SLOT_IO1;
            break;
        case TYPE_SAMPLE_SLOT_IO2:
            type = OUTPUT_IO::SAMPLE_SLOT_IO2;
            break;
        case TYPE_SAMPLE_SLOT_IO3:
            type = OUTPUT_IO::SAMPLE_SLOT_IO3;
            break;
        case TYPE_SAMPLE_SLOT_IO4:
            type = OUTPUT_IO::SAMPLE_SLOT_IO4;
            break;
        case TYPE_SAMPLE_SLOT_IO5:
            type = OUTPUT_IO::SAMPLE_SLOT_IO5;
            break;
        case TYPE_SAMPLE_SLOT_IO6:
            type = OUTPUT_IO::SAMPLE_SLOT_IO6;
            break;
        case TYPE_PE_DILU_1:
            type = OUTPUT_IO::PE_DILU_1;
            break;
        case TYPE_PE_DILU_2:
            type = OUTPUT_IO::PE_DILU_2;
            break;
        case TYPE_PE_REGENT_TABLE_TABLE:
            type = OUTPUT_IO::PE_REGENT_TABLE_GATE;
            break;
        case TYPE_DUSTBIN_EXIST_PE:
            type = OUTPUT_IO::PE_WASTE_EXIST;
            break;

        default:
            break;
    }

    try {
        ret = spThriftClient->pSIRealMonitorClient->ReportIOState(type, state);
    } catch (TException & e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 试剂仓旋转 bracket_index: 试剂托架编号 1：A；2：B；3：C；4: D；5：E；6: F 其他值无效 */
int report_button_bracket_rotating(int bracket_index)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    if (bracket_index > 6 || bracket_index < 1) {
        LOG("param invalid\n");
        return -1;
    } else {
        LOG("report rotated to pointed pos:%d\n", bracket_index);
    }

    try {
        ret = spThriftClient->pSIRealMonitorClient->ReportReagentBracketRotating(bracket_index);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报报警信息 alarm_code:报警代码 alarm_info:报警详细信息 */
int report_alarm_message(const int32_t iOrderNo, const char *alarm_info)
{
    return report_device_abnormal(iOrderNo, alarm_info);
}

/* 上报样本信息 */
int report_rack_info(sample_info_t *sample_info)
{
    vector<SAMPLE_TUBE_INFO_T> sample_infos;
    SAMPLE_TUBE_INFO_T info;
    EXE_STATE::type ret = EXE_STATE::FAIL;
    int i;
    sample_info_t *gl = sample_info;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;    
    }

    for(i = 0 ; i < 10 ; i++) {
        info.iSlotNo = gl->rack_number;
        info.iRackIndex = gl->rack_index;
        info.iPosIndex = gl->sample_index + 1;
        info.bScanStatus = gl->scan_status;
        info.bExist = gl->exist;
        info.barcode = gl->barcode;
        info.iIsExistCap = gl->with_hat;
        info.iTubeType = (gl->exist) ? (gl->tube_type - 1) : 0;
        sample_infos.push_back(info);
        gl++;
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportSampleRack(sample_infos);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报样本架拉出 */
int report_pull_out_rack(uint8_t index)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportPullRack(index);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报样本槽进架错误 */
int report_rack_push_in_error(uint8_t index, uint8_t err_code)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportPushRackError(index, err_code);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报试剂或耗材消耗量，
 * type为REAGENT、DILUENT、WASH_B、WASH_A时, vol指消耗的体积(以μL为单位)
 * type为CUP、WASTE_CUP时, vol指数量（如1、2、3...，以"个"为单位)
 * type为REAGENT、DILUENT, pos指试剂或稀释液位置
 * type为CUP时, pos为1时指反应盘1，为2时指反应盘2
 * type为其它类型时，pos无效
*/
int report_reagent_supply_consume(reagent_supply_type_t type, int pos, int vol)
{
    REAGENT_SUPPLY_TYPE::type supply_type;
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    switch (type) {
        case REAGENT:
            supply_type = REAGENT_SUPPLY_TYPE::REAGENT;
            break;
        case DILUENT:
            supply_type = REAGENT_SUPPLY_TYPE::DILUENT;
            break;
        case CUP:
            supply_type = REAGENT_SUPPLY_TYPE::CUP;
            break;
        case WASTE_CUP:
            supply_type = REAGENT_SUPPLY_TYPE::WASTE_CUP;
            break;
        case WASH_B:
            supply_type = REAGENT_SUPPLY_TYPE::WASH_B;
            break;
        case WASH_A:
            supply_type = REAGENT_SUPPLY_TYPE::WASH_A;
            break;
        case PUNCTURE_NEEDLE:
            supply_type = REAGENT_SUPPLY_TYPE::PUNCTURE_NEEDLE;
            break;
        case AIR_PUMP:
            supply_type = REAGENT_SUPPLY_TYPE::AIR_PUMP;
            break;
        case REAGENT_QC:
            supply_type = REAGENT_SUPPLY_TYPE::REAGENT_QC;
            break;
        default:
            LOG("Unknown reagent supply type: %d\n", type);
            break;
    }

    try {
        ret = spThriftClient->pSIRealMonitorClient->ReportReagentSupplyConsume(supply_type, pos, vol);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报订单检测结果信息 order_no:订单编号 order_result:检测结果及信号值信息 */
int report_order_result(int order_no, const order_result_t *order_result)
{
    if(order_no == 0) {
        LOG("order_no is zero!\n");
        return 0;
    }
    RESULT_INFO_T result;
    EXE_STATE::type ret = EXE_STATE::FAIL;
    int i;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    switch (order_result->result_state) {
        case NORMAL:
            result.eResultState = RESULT_STATE::NORMAL;
        break;
        case UN_CLOT:
            result.eResultState = RESULT_STATE::UN_CLOT;
        break;
        case ABNORMAL:
            result.eResultState = RESULT_STATE::ABNORMAL;
        break;
        case AD_OUT_OF_RANGE:
            result.eResultState = RESULT_STATE::AD_OUT_OF_RANGE;
        break;        
        case NO_BEAD:
            result.eResultState = RESULT_STATE::NO_BEAD;
        break;
        default:
            LOG("UnKnown order result state: %d\n", order_result->result_state);
        break;
    }
    result.iDetectPos = order_result->detect_pos;
    result.strCupLotNo = order_result->cuvette_strno;
    result.iCupSerialNo = order_result->cuvette_serialno;

    result.iClotTime = order_result->clot_time;
    for (i = 0; i < order_result->AD_size; i++) {
        result.lstADData.push_back(order_result->AD_data[i]);
    }
    for (i = 0; i < order_result->sub_AD_size; i++) {
        result.lstADData.push_back(order_result->sub_AD_data[i]);
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportOrderResult(order_no, result);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

/* 上报订单检测结束剩余时间 iRemainTime:剩余时间(单位:秒) */
int report_order_remain_checktime(int sec_count)
{
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportTestRemainTime(sec_count);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;

}

/* 上报样本质量信息 sample_tube_id:样本订单编号 sample_quality: 样本质量信息 */
int report_sample_quality(int sample_tube_id,  sample_quality_t *sample_quality)
{
    SAMPLE_QUALITY_T quality;
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    quality.bIsCheckAnticoagulationRatio = sample_quality->check_anticoagulation_ratio;
    quality.bIsARError = sample_quality->ar_error;
    quality.bIsCheckClot = sample_quality->check_clot;
    quality.bIsClotError = sample_quality->clot_error;

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportSampleQuality(sample_tube_id, quality);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}


/* 上报订单状态 order_no: 订单编号, state: 状态 */
int report_order_state(int order_no,  order_state_t state)
{
    LOG("order_no:%d state:%d\n", order_no, state);

    if (order_no == 0) {
        return 0;
    }
    ORDER_STATE::type type;
    EXE_STATE::type ret = EXE_STATE::FAIL;

    // 从连接池获取连接
    BOOL bThriftException = FALSE;
    SP_I2103_THRIFT_CLIENT_T spThriftClient;
    if (!GetThriftClient(spThriftClient)) {
       LOG("get thrift fail\n");
       return -1;
    }

    switch (state) {
        case OD_SAMPLECOMPLETION:
            type = ORDER_STATE::OD_SAMPLECOMPLETION;
        break;
        case OD_INCUBATING:
            type = ORDER_STATE::OD_INCUBATING;
        break;
        case OD_DETECTING:
            type = ORDER_STATE::OD_DETECTING;
        break;
        case OD_COMPLETION:
            type = ORDER_STATE::OD_COMPLETION;
        break;
        case OD_ERROR:
            type = ORDER_STATE::OD_ERROR;
        break;
        case OD_INIT:
            type = ORDER_STATE::OD_INIT;
        break;
        default:
            LOG("Unknown order state: %d\n", state);
        break;
    }

    try {
        ret = spThriftClient->pSISampleDetectClient->ReportOrderState(order_no, type, 0);
    } catch (TException &e) {
        LOG("%s\n", e.what());
        bThriftException = TRUE;
    } catch (...) {
        LOG("other exception\n");
        bThriftException = TRUE;
    }
    // 向连接池归还连接
    ReturnThriftClient(spThriftClient, bThriftException);

    return ret == EXE_STATE::SUCCESS ? 0 : -1;
}

static void *connect_server(void *arg)
{
    thrift_master_t *server = (thrift_master_t *)arg;
    int heartbeat_flag_back = 0;
    uint32_t error_cnt = 0;

    LOG("thrift heart start\n");
    set_connect_ipport(server->ip, server->port);
    while (1) {
        if (thrift_salve_client_connect_flag == 1) {
            // 从连接池获取连接
            SP_I2103_THRIFT_CLIENT_T spThriftClient;
            BOOL bThriftException = FALSE;

            if (!GetThriftClient(spThriftClient)) {
                if (error_cnt == 0) {
                    LOG("get thrift fail(heatbeat)\n");
                }

                if (++error_cnt >= 2) {
                    thrift_salve_heartbeat_flag = 0;
                }
            }else{
                try {
                    spThriftClient->pSIOtherClient->Heartbeat();
                    thrift_salve_heartbeat_flag = 1;

                    if (error_cnt > 0) {
                        LOG("heartbeat thrift error resume. err_cnt:%d\n", error_cnt);
                    }
                    error_cnt = 0;
                } catch (TException& e) {
                    LOG("%s. err_cnt:%d\n", e.what(), error_cnt);
                    bThriftException = TRUE;

                    if (++error_cnt >= 2) {
                        thrift_salve_heartbeat_flag = 0;
                    }
                }  catch (...) {
                    LOG("other exception. err_cnt:%d\n", error_cnt);
                    bThriftException = TRUE;
                    if (++error_cnt >= 2) {
                        thrift_salve_heartbeat_flag = 0;
                    }
                }

                // 向连接池归还连接
                ReturnThriftClient(spThriftClient, bThriftException);
            }
        } else {
            thrift_salve_heartbeat_flag = 0;
        }

        if (get_machine_stat() == MACHINE_STAT_RUNNING && thrift_salve_heartbeat_flag == 0) {
            LOG("lost connected, stop all\n");
            FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
            set_machine_stat(MACHINE_STAT_STOP);
            module_start_control(MODULE_CMD_STOP);
//            cuvette_supply_led_ctrl(REACTION_CUP_MAX, LED_NONE_BLINK);

            /* 关阀、停止电机运动 */
            usleep(400*1000);
            emergency_stop();

            indicator_led_set(LED_MACHINE_ID, LED_COLOR_GREEN, LED_OFF);
            indicator_led_set(LED_MACHINE_ID, LED_COLOR_YELLOW, LED_OFF);
            indicator_led_set(LED_MACHINE_ID, LED_COLOR_RED, LED_OFF);
        }

        if (thrift_salve_client_connect_flag==1 && heartbeat_flag_back==1 && thrift_salve_heartbeat_flag==0) {
            indicator_led_set(LED_MACHINE_ID, LED_COLOR_GREEN, LED_OFF);
            indicator_led_set(LED_MACHINE_ID, LED_COLOR_YELLOW, LED_OFF);
            indicator_led_set(LED_MACHINE_ID, LED_COLOR_RED, LED_OFF);
        }
        heartbeat_flag_back = thrift_salve_heartbeat_flag;

        /* 心跳失败时，间隔30s重连接 */
        if (thrift_salve_heartbeat_flag == 1) {
            sleep(3);
        } else {
            sleep(30);
        }
    }

    return NULL;
}

void thrift_slave_client_init(const thrift_master_t *thrift_master)
{
    pthread_t tid;

    pthread_create(&tid, NULL, connect_server, (void *)thrift_master);
}

/* 控制 是否连接上位机 0：不连接 1：连接(默认值)*/
void thrift_slave_client_connect_ctl(int flag)
{
    thrift_salve_client_connect_flag = flag;
}

int thrift_slave_client_connect_ctl_get()
{
    return thrift_salve_client_connect_flag;
}

/* 获取 下位机到上位机的连接心跳 0：失败 1：正常*/
int thrift_salve_heartbeat_flag_get()
{
    return thrift_salve_heartbeat_flag;
}


