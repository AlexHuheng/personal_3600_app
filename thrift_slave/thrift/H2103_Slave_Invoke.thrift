include "Defs.thrift"

namespace cpp H2103_Slave_Invoke


// 维护
service SIMaintenance 
{
    Defs.EXE_STATE        ReportReagentInfo(1:Defs.REAGENT_SCAN_INFO_T tReagentScanInfo)                          // 上报试剂扫描信息，详见REAGENT_SCAN_INFO_T定义
    Defs.EXE_STATE        ReportReagentRemain(1:i32 iReagPosIndex, 2:i32 iRemainVolume, 3:i32 iOrderNo)           // 上报试剂余量 iReagPosIndex:试剂位置索引 iRemainVolume:试剂余量，以ul为单位 iOrderNo:测试订单编号，不存在时填0

    Defs.EXE_STATE        ReportPositionCalibtion(1:Defs.ENGINEER_DEBUG_POS_CALIB_T tInfo)                        // 上报自动标定位置信息（Service软件使用)，详见ENGINEER_DEBUG_POS_CALIB_T定义
    Defs.EXE_STATE        ReportPositionCalibtion_H(1:Defs.ENGINEER_DEBUG_MODULE_PARA_T tInfo)                    // 上报自动标定位置信息，详见ENGINEER_DEBUG_MODULE_PARA_T定义

    Defs.EXE_STATE        ReportMaintenanceItemResult(1:Defs.MAINTENANCE_ITEM_T tItem, 2:Defs.IBOOL bStart, 3:Defs.IBOOL bStatus)   // 上报维护项结果 tItem：维护项 bStart true.开始 false.结束  bStatus：true.成功 false.失败 
    Defs.EXE_STATE        ReportMaintenanceRemainTime(1:i32 iRemainTime)                                          // 上报维护结束剩余时间 iRemainTime:剩余时间(单位:秒)
}

service SIRealMonitor
{
    Defs.EXE_STATE        ReportIOState(1:Defs.OUTPUT_IO sensor, 2:i32 iState)                                    // 上报IO状态, sensor:传感器编号 iState: 1: 打开（清洗泵或废液泵启动，打开加热等），存在（如样本管探测），到位（反应装载到位）等; 0:关闭，不存在，未到位等(此定义不一定合理，针对不类别的IO)
    Defs.EXE_STATE        ReportReagentSupplyConsume(1:Defs.REAGENT_SUPPLY_TYPE eType, 2:i32 iPosIndex, 3:i32 iVol) // 上报试剂或耗材消耗量，如eType为REAGENT、DILUENT、WASH_B、WASH_A时，iVol指消耗的体积（以μL为单位）；如eType为CUP、WASTE_CUP时，iVol指数量（如1、2、3...，以"个"为单位)
                                                                                                                  // 如eType为REAGENT、DILUENT, iPosIndex指试剂或稀释液位置, 如eType为CUP时, iPosIndex为1时指反应盘1，为2时指反应盘2，如eType为WASTE_CUP时, iPosIndex为1时指垃圾桶1，为2时指垃圾桶2，
                                                                                                                  // 如类型为PUNCTURE_NEEDLE时，iVol指穿刺针使用次数（以次为单位）iPosIndex无效
                                                                                                                  // 如类型为AIR_PUMP时，iVol指气泵的使用时间（以分钟为单位）iPosIndex无效；eType为其它类型时，iPosIndex无效
    Defs.EXE_STATE        ReportReagentBracketRotating(1:i32 iBracketIndex)                                       // 试剂仓旋转通知 iBracketIndex: 1~6表示试剂仓A~F区，7表示稀释液区域，其它值无效

    // 弃用
    //Defs.EXE_STATE      ReportExistReagentBottleAfterUnloadReagentOnline(1:i32 iExist)                          // 试剂的线卸载完成后，当下位机检测到试剂瓶托架上仍存在试剂瓶时，主动上报至上位机 iExist: 是否存在试剂瓶 1: 存在 0: 不存
    //Defs.EXE_STATE      ReportButtonPushed(1:Defs.BUTTON_IO eButtonID, 2:Defs.IBOOL bIsIn)                      // 上报按钮被按下， eButtonID 按钮ID;当为BTN_STAT时，bIsIn: true，进；false: 出，当为BTN_LMS_ONLINE_OFFLINE时 bIsIn: true，联机；false: 脱机
}

service SISampleDetect
{
    Defs.EXE_STATE        ReportPullRack(1:i32 iSlotNo)                                                           // 上报槽拉出样本架   iSlotNo:槽号
    Defs.EXE_STATE        ReportPushRackError(1:i32 iSlotNo, 2:i32 iErrorCode)                                    // 上报槽进架错误信息   iSlotNo:槽号 iErrorCode错误码
    Defs.EXE_STATE        ReportSampleRack(1:list<Defs.SAMPLE_TUBE_INFO_T> lstTubeInfos)                          // 上报样本架信息   lstTubeInfos:样本架号、位置、是否存在及条码等信息
    Defs.EXE_STATE        ReportSampleQuality(1:i32 iSampleOrderNo,  2:Defs.SAMPLE_QUALITY_T tSampleQuality)      // 上报样本质量信息 iSampleOrderNo:样本订单编号 tSampleQuality: 样本质量信息
    Defs.EXE_STATE        ReportOrderState(1:i32 iOrderNo,  2:Defs.ORDER_STATE state, 3:i32 iIncruOrDetectPos)    // 上报订单状态 iOrderNo:订单编号 state: 状态 iIncruOrDetectPos: 当时所在的孵育位或检测位编号 0:表示无效
    Defs.REAGENT_INFO_T   QuerySpareReagent(1:i32 iOrderNo, 2:Defs.REAGENT_INFO_T reagentInfo)                    // 请求备用瓶 iOrderNo:订单编号，reagentInfo:当前试剂信息 返回值:REAGENT_INFO_T 备用瓶信息
    Defs.EXE_STATE        ReportAlarmMessage(1:i32 iOrderNo, 2:string strAlarmCode)                               // 上报报警信息 iOrderNo:订单编号(为0时无效) strAlarmCode:报警代码
    Defs.EXE_STATE        ReportOrderResult(1:i32 iOrderNo, 2:Defs.RESULT_INFO_T tResultInfo)                     // 上报订单检测结果信息 iOrderNo:订单编号 tResultInfo:检测结果及信号值信息
    Defs.EXE_STATE        ReportTestRemainTime(1:i32 iRemainTime)                                                 // 上报订单检测结束剩余时间 iRemainTime:剩余时间(单位:秒)

    // 废弃
    //Defs.EXE_STATE      ReportSTATSample(1:list<Defs.SAMPLE_TUBE_INFO_T> lstTubeInfos)                        // 上报急诊进样信息 lstTubeInfos:样本架号、位置、是否存在及条码等信息
}

service SIOther
{
    Defs.EXE_STATE        ReportAsnycInvokeResult(1:i32 iUserData, 2:Defs.EXE_STATE eExeState, 3:Defs.ASYNC_RETURN_T tReturn) // 上报上位机调用异步接口的执行结果 iUserData:用户数据，eExeState:执行结果
    Defs.EXE_STATE        ReportDeviceAbnormal(1:i32 iOrderNo, 2:string strAlarmCode)                             // 上报报警信息 iOrderNo:订单编号(为0时无效) strAlarmCode:报警代码
    Defs.EXE_STATE        Heartbeat()                                                                             // 心跳
    Defs.EXE_STATE        UploadBackupFile(1:i32 iFileType, 2:i32 iRandNo, 3:binary hexData, 4:i32 iSeqNo, 5:i32 iIsEnd, 6:string strMD5)      // 上传文件iFileType：文件类型，SFT_SLAVE_LOG_FILE：下位机日志，SFT_SLAVE_CONFIG_FILE：仪器标定参数; 
                                                                                                                  // iRandNo：随机数，下位机上传时需按上位机给定的值回传; hexData: 文件块内容（分段传输时可能为部分文件内容）；
                                                                                                                  // iSeqNo：文件块编号，从0开始；iIsEnd：是否为最后文件块，1：是（为是时表示整个文件已完成传输）；0：否；
                                                                                                                  // strMD5：文件的MD5校验码，文件传输完成后需进行MD5校验

    // iRunType定义 1.下位机部件老化流程
    Defs.EXE_STATE        ReportRunTimes(1:i32 iRunType, 2:i32 iTimes, 3:i32 iReserve)                            // iRunType: 运行类型, iTimes: 运行次数; iReserve: 保留位
}
