include "Defs.thrift"

namespace cpp H2103_Host_Invoke

// 说明：以下接口中带有“Async”标识的均为异步接口，当下位机执行完成后，通过调用"H2103_Slave_Invoke.thrift"中的ReportAsnycInvokeResult主动上报异步接口执行结果
// 维护
service HIMaintenance {
    Defs.EXE_STATE      ReagentScanAsync(1:i32 iAreaIndex, 2:i32 iUserData)                                                 // 异步接口，试剂扫描 iAreaIndex: 扫描区域1~6.依次A~F区，其他.全盘 iUserData：用户数据
    Defs.EXE_STATE      ActiveMachineAsync(1:i32 iUserData)                                                                 // 异步接口，用于关机后开机激活仪器
    Defs.EXE_STATE      SetFluxModeAsync(1:i32 iFluxMode, 2:i32 iUserData)                                                  // 设置通量模式，0.正常模式；1.PT快速模式
    i32                 GetFluxMode()                                                                                       // 获取仪器通量模式
    Defs.EXE_STATE      ReagentRemainDetectAsync(1:list<Defs.REAGENT_POS_INFO_T> lstReagPosInfo, 2:i32 iUserData)           // 异步接口，试剂余量探测 lstReagPosInfo:待进行余量探测的试剂位置及瓶型列表, iUserData：用户数据
    Defs.EXE_STATE      ReagentMixingAsync(1:list<Defs.REAGENT_MIX_INFO_T> lstReagMixInfo, 2:i32 iUserData)                 // 异步接口，试剂混匀  lstReagMixInfo:待进行试剂混匀的试剂位置及瓶型列表, iUserData：用户数据
    // 维护定义
    // START_UP_ID                         = 1,        // 开机维护
    // SHUT_DOWN_ID                        = 2,        // 关机维护
    // INSTRU_RESET_ID                     = 3,        // 仪器复位
    // PIPLINE_FILL_ID                     = 4,        // 管路填充
    // TRUN_OFF_INSTRU_ID                  = 5,        // 关闭主机
    // REAGENT_SCAN_ID                     = 6,        // 试剂扫描
    // REAGENT_REMAIN_ID                   = 7,        // 试剂余量探测
    // TEMP_ID                             = 8,        // 温度监测
    // WEEK_ID                             = 9,        // 周维护
    // MONTH_ID                            = 10,       // 月维护
    // GAS_PRESSURE_ID                     = 11,       // 气压监测
    // COUNTER_ID                          = 12,       // 计数器
    // EMERGENCY_STOP_ID                   = 13,       // 停机维护
    // PIPLINE_EMPTYING_ID                 = 14,       // 管路排空    add by wuxiaohu 20221128
    // HIM_STOP_ID                         = 15,       // 急停 应嵌软老师增加此维护类型 add by wuxiaohu 20221202
    // QUALITYINSPECTION_GRIPPER           = 500,      // 抓手通电质检
    // QUALITYINSPECTION_SAMPLENEEDLE      = 501,      // 样本针通电质检
    // QUALITYINSPECTION_REAGENTNEEDLE     = 502,      // 试剂针通电质检
    // QUALITYINSPECTION_REAGENTBIN        = 503,      // 试剂仓通电质检
    // QUALITYINSPECTION_LIQUID            = 504,      // 液路通电质检
    // QUALITYINSPECTION_SAMPLETRAY        = 505,      // 进样盘通电质检
    // QUALITYINSPECTION_SAMPLER           = 506,      // 进样器通电质检
    // QUALITYINSPECTION_MIXMODULE         = 507,      // 混匀模块通电质检
    // QUALITYINSPECTION_MAG1_MODULE       = 508,      // 磁珠模块通电质检（底噪值）
    // QUALITYINSPECTION_MAG2_MODULE       = 509,      // 磁珠模块通电质检（检测值）
    // QUALITYINSPECTION_OPTICAL_MODULE    = 510,      // 光学模块通电质检
    Defs.EXE_STATE      RunMaintenance(1:i32 iMainID, 2:i32 iKeepCool, 3:list<i32> lstReserved1, 4:list<string> lstReserved2, 5:i32 iUserData) // 异步接口，执行指定的维护类型
    Defs.EXE_STATE      RunMaintenanceGroup(1:i32 iGroupID, 2:list<Defs.MAINTENANCE_ITEM_T> lstItems, 3:i32 iUserData)      // 异步接口，执行维护组合 iGroupID：组合ID lstItems：维护项列表 

    // 弃用 ：屏蔽仪器复位与管路填充
    //Defs.EXE_STATE    InstrumentResetAsync(1:i32 iUserData)                                                               // 异步接口，仪器复位  iUserData：用户数据
    //Defs.EXE_STATE    PipelineFillAsync(1:i32 iUserData)                                                                  // 异步接口，管路填充  iUserData：用户数据
}

// 实时监控
service HIRealMonitor
{
    Defs.EXE_STATE      GetIOAsync(1:Defs.OUTPUT_IO sensor, 2:i32 iUserData)                                                // 查询IO状态
    Defs.EXE_STATE      GetTemperatureAsync(1:Defs.TEMPERATURE_SENSOR sensor, 2:i32 iUserData)                              // 查询仪器温度
    Defs.EXE_STATE      GetPressureAsync(1:i32 sensor, 2:i32 iUserData)                                                     // 查询指定传感器的空气压力，单位为KPa sensor请参见Defs.thrift中“气压传感器”注释
    Defs.EXE_STATE      SetIOAsync(1:Defs.INPUT_IO sensor, 2:i32 iState, 3:i32 iUserData)                                   // 设置IO状态,sensor指传感器编号,iState指设定的状态
    Defs.EXE_STATE      ManualUnlockSlotAsync(1:Defs.INPUT_IO sensor, 2:i32 iUserData)                                      // 手动解锁槽（架）,sensor槽的传感器IO 注：下位机判定能否解锁，可解锁时先删除该槽订单，再执行SetIO
    Defs.EXE_STATE      GetLightSignalAsync(1:i32 iWave, 2:i32 iUserData)                                                   // 光源信号值检测,iWave指波长
    Defs.EXE_STATE      SetIndicatorLightAsync(1:i32 iIndicatorLightNo, 2:i32 iColor, 3:i32 iBlink, 4:i32 iUserData)        // 指示灯控制 iIndicatorLightNo：指示灯编号，1：仪器状态指示灯 2：反应盘1指示灯 3：反应盘2指示灯; iColor指颜色,1:绿 2：黄 3：红; iBlink是长亮或闪烁 1：常亮 2：闪烁
    Defs.EXE_STATE      SetAlarmSoundAsync(1:Defs.IBOOL bOpen, 2:i32 iSound, 3:i32 iUserData)                               // 报警声音控制 bOpen: 1:打开; 0:关闭  iSound:报警声音频率 1低频、2中频、3高频,此参数令bOpen为1时有效
    Defs.EXE_STATE      ManualStopAsync(1:i32 iUserData)                                                                    // 手动停机 tPiplineFillInfo:管路清洗信息
    Defs.EXE_STATE      SetConsumablesInfo(1:Defs.CONSUMABLES_INFO_T tConsumablesInfo)                                      // 设置耗材是否可用等信息 tConsumablesInfo: 耗材信息
    // GetInstrumentState返回值定义
    // HS_WARM_UP                                             = 0;        // 预热（下位机暂无此状态）
    // HS_STAND_BY                                            = 1;        // 待机
    // HS_RUNNING                                             = 2;        // 运行
    // HS_SAMPLE_STOP                                         = 3;        // 加样停（下位机暂无此状态）
    // HS_STOP                                                = 4;        // 停机
    // HS_MAINTENANCE                                         = 5;        // 维护
    // HS_OFFLINE                                             = 6;        // 脱机（下位机暂无此状态）
    // HS_DISABLE                                             = 7;        // 禁用（下位机暂无此状态）
    // HS_MANUAL_SAMPLE_STOP                                  = 8;        // 手动加样停
    i32                 GetCurrentInstrumentState()                                                                         // 获取仪器当前所处的状态
    // iCounterID计算器ID由嵌软输出，详情见《仪器动作计数表_********.xlsx》
    // 计算单位说明：次数.数字，使用次数；时间.单位分钟；日期.具体日期，如20240520
    Defs.EXE_STATE      SetCounterOrTimer(1:i32 iCounterOrTimerID, 2:i32 iNumberOfTimesOrElapsedTime)                       // 设置易损件计数次数(以次数为单位)或计时时间（以分钟为单位)，注意DS_ONLINE_FILTER_USED_COUNT（在线过虑器计数）需特殊处理 add by wuxiaohu 20231011
    Defs.EXE_STATE      SetTotalCounterOrTimer(1:i32 iCounterOrTimerID, 2:i32 iTotalNumberOfTimesOrElapsedTime)             // 设置易损件累计计数次数(以次数为单位)或计时时间（以分钟为单位)，注意DS_ONLINE_FILTER_USED_COUNT（在线过虑器计数）需特殊处理 add by wuxiaohu 20231025
    list<Defs.SLAVE_COUNTEROR_TIMER_INFO_T> GetAllCounterOrTimer()                                                          // 获取所有易损件计数次数(以次数为单位)或计时时间（以分钟为单位)，如为时间，则转换为小时显示，注意DS_ONLINE_FILTER_USED_COUNT（在线过虑器计数）需特殊处理 add by wuxiaohu 20231025



    // 弃用 
    //Defs.EXE_STATE    SetAssayGroupTestsInfo(1:list<Defs.ASSAY_GROUP_TESTS_T> lstAssayGroupTests)                         // 设置项目组测试数 ASSAY_GROUP_TESTS_T: 项目组测试数材信息
    //i32               GetConsumableCupIsUsing(1:i32 iCupNo)                                                               // 获取指定反应盘编号的是否正在使用 iCupNo: 1: 反应盘1; 2: 反应盘2 返回值 1: 正在使用 0: 暂未使用
}

// 样本检测
service HISampleDetect
{
    Defs.EXE_STATE      InstrumentSelfTestAsync(1:list<Defs.REAGENT_MIX_INFO_T> lstReagMixInfo, 2:list<Defs.REAGENT_POS_INFO_T> lstReagPosInfo, 3:i32 iUserData)            // 异步接口，仪器自检 lstReagMixInfo:待进行试剂混匀的试剂位置、瓶型及其它信息列表, lstReagPosInfo:待进行余量探测的试剂位置及瓶型列表, iUserData：用户数据
    Defs.EXE_STATE      CreateSampleOrder(1:Defs.SAMPLE_ORDER_INFO_T tSampleOrderInfo)                                      // 创建检测订单 tOrderInfo:订单详细信息, iUserData：用户数据
    Defs.EXE_STATE      UpdateSTATSampleOrder(1:i32 iSampleOrderNo)                                                         // 更新为急诊订单 iSampleOrderNo:测试样本编号
    Defs.EXE_STATE      RemoveSlotOrder(1:i32 iSlotNo)                                                                      // 卸载槽订单信息 iSlotNo槽号（编号1~6）
    i32                 IsTestFinished()                                                                                    // 同步接口，检测是否完成，返回值:0.未完成， 1.完成
    Defs.EXE_STATE      NormalStopAsync(1:i32 iUserData)                                                                    // 异步接口，正常停机流程  iUserData：用户数据
    Defs.EXE_STATE      ConsumablesStopAsync(1:i32 iUserData)                                                               // 异步接口，耗材不足加样停接口 iUserData：用户数据， 下位机收到此命令，通过ReportOrderState上报停止订单
    Defs.EXE_STATE      SetSampleStopAsync(1:i32 iSampleStop, 2:i32 iUserData)                                              // 异步接口，设定或恢复手动加样停，iSampleStop：是否手动加样停 1：加样停 0：恢复加样, iUserData：用户数据
    list<i32>           QueryIsOpenReagentBinCoverOrDiluentCover()                                                          // 同步接口，查询是否打开过试剂仓库或稀释液仓盖，返回的list<int>不为空，表示打开过，其内存储曾移动到试剂仓口的试剂仓位置编号列表及稀释液位置编号

    // 暂未使用
    Defs.EXE_STATE      DeleteOrder(1:i32 iOrderNo)                                                                         // 删除订单，终止此订单的检测 iOrderNo:订单编号，上下位机的唯一识别依据, iUserData：用户数据
    // iReagentType（更新订单类型）定义
    // HRT_QC_SAMPLE                                          = 1;        // 质控样本(试剂仓质控)
    // HRT_FACTOR_PLASMA                                      = 2;        // 乏因子血浆
    // HRT_DILUENT                                            = 4;        // 稀释液
    // HRT_INCUBATION_REAGENT                                 = 8;        // 孵育试剂
    // HRT_DETECTION_REAGENT                                  = 16;       // 启动试剂
    Defs.EXE_STATE      UpdateOrder(1:i32 iOrderNo, 2:i32 iReagentType, 3:Defs.ORDER_INFO_T tOrderInfo, 4:i32 iSamplePos)   // 更新订单 iOrderNo:订单编号 iReagentType: 参见iReagentType（更新订单类型）定义，其为按位进行运算，可能同时更新多个, tOrderInfo：订单信息; iSamplePos: 质控品位置，仅在更新试剂仓质控时有效，且有效值为1-36，为0或其它值时无效


    // 弃用
    //Defs.EXE_STATE    MoveSampleRackAsync(1:Defs.IBOOL bScanSampleBarcode, 2:i32 iUserData)                               // 异步接口，常规样本进架 bScanSampleBarcode:是否扫描样本管条码 1:是，扫描； 0:否，不扫描，iUserData：用户数据
    //Defs.EXE_STATE    MoveSTATSampeAsync(1:Defs.IBOOL bScanSampleBarcode, 2:i32 iUserData)                                // 异步接口，急诊进样 bScanSampleBarcode:是否扫描样本管条码 1:是，扫描； 0:否，不扫描，iUserData：用户数据
    //Defs.EXE_STATE    MoveOutSampleRackAsync(1:i32 iUserData)                                                             // 常规样本退架 iUserData：用户数据
    //Defs.EXE_STATE    MoveOutSTATSampeAsync(1:i32 iUserData)                                                              // 急诊退架 iUserData：用户数据
    //i32               OpenOrCloseLoadReagentOnlineDoorAsync(1:i32 iOpenOrClose, 2:i32 iUserData)                          // 异步接口，打开或关闭在线加卸载试剂仓门 iOpenOrClose: 1: 打开 0: 关闭 , iUserData：用户数据 返回值：0：成功；其它值：失败对应的错误码
    //Defs.EXE_STATE    LoadReagentOnlineAsync(1:i32 i5MLPosIndex, 2:i32 i15MLPosIndex, 3:i32 iUserData)                    // 异步接口，在线装载试剂 i5MLPosIndex:5ml瓶放置位置，为0时表示无5ml规格瓶空位 i15MLPosIndex:15ml瓶放置位置，为0时表示无15ml规格瓶空位 iUserData：用户数据
    //Defs.EXE_STATE    UnloadReagentOnlineAsync(1:i32 iPosIndex, 2:i32 iUserData)                                          // 异步接口，在线卸载试剂 iPosIndex:待卸载的试剂瓶位置 iUserData：用户数据
    //i32               GetIsExistRackOnBelt()                                                                              // 同步接口，获取常规进样皮带上是否存在样本架, 1: 存在， 0：不存在
    //i32               QueryIsExistRackPosInRerunArea(1:i32 iRackIndex, 2:i32 iPosIndex)                                   // 同步接口，查询指定架号及位置号的样本是否存在, 1: 存在，0：不存在
    //i32               QueryIsAddingSampleDone()                                                                           // 同步接口，查询是否已下订单均已完成加样，1：已完成；0：未完成
	//Defs.EXE_STATE    SetScanSampleBarcode(1:Defs.IBOOL bScan)                                                            // 同步接口，设置进架时是否扫描样本管条码，bScan扫描标识:true扫描；false不扫描
}

// 其它
service HIOther
{
    // EnableWasteBinIO                      是否启用废料已满IO
    // EnableInstrumentGateIO                是否启用仪器盖IO
    // EnableReagentCabinetIO                是否启用试剂仓IO
    // JSon示例：{"EnableInstrumentGateIO":1,"EnableReagentCabinetIO":1,"EnableWasteBinIO":1}

    Defs.EXE_STATE      SetSystemBaseData(1:string strJson)                                                            		// 同步接口，设置系统基础数据 strJson：Json字符串
    Defs.EXE_STATE      ExecuteScriptAsync(1:string strFileNmae, 2:i32 iUserData)                                           // 异步接口，执行特殊指令（脚本） strFileNmae:脚本名称，iUserData：用户数据
    //Defs.EXE_STATE    UpgradeFileAsync(1:binary hexData, 2:string strFile, 3:string strPath, 4:i32 iUserData)             // 异步接口，升级文件 hexData:二进制数据流  strFile:文件名 strPath:路径，iUserData：用户数据
    //list<Defs.SLAVE_PROGRAM_UPDATE_RESULT_T> UpgradeSlaveProgram(1:list<Defs.SLAVE_PROGRAM_T> lstSlaveprogram)            // 升级下位机子程序 lstSlaveprogram：下位机子程序列表
    Defs.EXE_STATE      UpgradeSlaveProgramAsync(1:Defs.SLAVE_PROGRAM_T tSlaveprogram, 2:i32 iUserData)                     // 异步接口,升级下位机子程序 tSlaveprogram：下位机子程序，iUserData：用户数据
    //Defs.EXE_STATE    UpgradeAssemblyAsync(1:binary hexData, 2:string strFile, 3:string strPath, 4:i32 iUserData)         // 异步接口，升级固件程序，iUserData：用户数据
    string              GetVersion(1:i32 iType)                                                                             // 查询版本信息 iType:类别，暂无法定义
    Defs.EXE_STATE      SetInstrumentNo(1:string strInstrumentNo)                                                           // 设置出厂编号 strInstrumentNo:出厂编号
    string              GetInstrumentNo()                                                                                   // 查询出厂编号
    Defs.EXE_STATE      SetSystemTime(1:Defs.DATE_TIME_T tDateTime)                                                         // 设置下位机系统日期时间
    Defs.DATE_TIME_T    GetSystemTime()                                                                                     // 获取下位机系统日期时间
    Defs.EXE_STATE      SetBootStrategy(1:list<Defs.BOOT_PARAM_T> lstcBootParams, 2:list<string> lstMAC)                    // 设置自动开机策略
    Defs.EXE_STATE      HeartbeatAsync(1:i32 iUserData)                                                                     // 异步接口，心跳，用于查询是否与下位机正常连接，定时发送检测
    Defs.EXE_STATE      ThriftMotorParaSet(1:Defs.THRIFT_MOTOR_PARA_T tMotorPara)                                           // 设置指定电机速度及加速度
    Defs.THRIFT_MOTOR_PARA_T    ThriftMotorParaGet(1:i32 iMotorID)                                                          // 获取指定电机速度及加速度
    Defs.EXE_STATE      ThriftMotorPosSet(1:i32 iMotorID, 2:i32 iPos, 3:i32 iStep)                                          // 设置指定电机指定标定参数步数
    list<i32>           ThriftMotorPosGet(1:i32 iMotorID)                                                                   // 获取指定电机的相关标定参数步数
    Defs.EXE_STATE      ThriftMotorReset(1:i32 iMotorID, 2:i32 iIsFirst)                                                    // 电机复位 iIsFirst:1：上电复位，2：正常复位
    Defs.EXE_STATE      ThriftMotorMove(1:i32 iMotorID, 2:i32 iStep)                                                        // 电机正向（步数为正数）或反向（步数为负数）移动
    Defs.EXE_STATE      ThriftMotorMoveTo(1:i32 iMotorID, 2:i32 iStep)                                                      // 电机移动到
    string              ThriftReadBarcode(1:i32 iReaderID)                                                                  // 扫描一维码或二维码 1：常规扫码，2：急诊扫码，3：试剂仓扫码
    i32                 ThriftLiquidDetect(1:i32 iNeedleID)                                                                 // 液面探测 1：样本针，2：R1，3：R2，返回值为液面探测实际运动步数，当探测失败时，返回负值
    Defs.EXE_STATE      ThriftRackMoveIn()                                                                                  // 样本架进架
    Defs.EXE_STATE      ThriftRackMoveOutHorizontal()                                                                       // 样本架横向出架
    Defs.EXE_STATE      RotatingReagentBin(1:i32 iReagentPos)                                                               // 旋转试剂仓 iReagentPos:试剂位置编号
    Defs.EXE_STATE      EngineerDebugPosSet(1:Defs.ENGINEER_DEBUG_MODULE_PARA_T tModulePara)                                // 工程师调试-设置指定模块、指定虚拟位置的R、X、Y、Z的标定参数步数
    list<Defs.ENGINEER_DEBUG_MODULE_PARA_T>  EngineerDebugPosGet(1:i32 iModuleIndex)                                        // 工程师调试-获取指定模块所有虚拟位置的R、X、Y、Z的标定参数步数
	list<Defs.ENGINEER_DEBUG_VIRTUAL_POSITION_T>  EngineerDebugGetVirtualPosition()                                         // 工程师调试-获取指定模块所有虚拟位置的R、X、Y、Z的标定参数步数
    Defs.EXE_STATE      EngineerDebugMotorActionExecuteAsync(1:Defs.ENGINEER_DEBUG_MOTOR_PARA_T tMotorPara, 2:i32 iUserData) // 工程师调试-电机动作执行，异步返回值为此电机执行指令结束后的步数 iUserData：用户数据
    // iNeedType定义
    // NT_SAMPLE_NORMAL_ADDING_WITHOUT_PUNCTURE               = 0;        // 样本针常规加样（平头针）
    // NT_SAMPLE_DILUENT_ADDING_WITHOUT_PUNCTURE              = 1;        // 样本针稀释加样（平头针）
    // NT_SAMPLE_NORMAL_ADDING_WITH_PUNCTURE                  = 2;        // 样本针常规加样（穿刺针）
    // NT_SAMPLE_DILUENT_ADDING_WITH_PUNCTURE                 = 3;        // 样本针稀释加样（穿刺针）
    // NT_R1_ADDING                                           = 4;        // 试剂针R1加样
    // NT_R2_ADDING                                           = 5;        // 试剂针R2加样    
    Defs.EXE_STATE      EngineerDebugWeighingAsync(1:i32 iNeedType, 2:i32 iSampleOrReagentVol, 3:i32 iDiulentVol, 4:i32 iCups, 5:i32 iUserData) // 工程师调试-称量指令，异步返回值为此称量指令执行的成功或失败 iNeedType：针类型 iSampleOrReagentVol：样本或试剂加注体积，为0时表示不加注 iDiulentVol：稀释液加注体积，为0时表示不加注 iCups：反应杯一次称量个数 iUserData：用户数据
    Defs.EXE_STATE      EngineerDebugAutoCalibrationAsync(1:i32 iCalibID, 2:i32 iType, 3:i32 iUserData)                     // 工程师调试-自动标定（异步）： iCalibID：标定模块ID 0.针 1.抓手 2.试剂仓； iType.处理类型 0.标定 1.写入 2.停止，iUserData.用户数据
    Defs.EXE_STATE      EngineerDebugInjectorKBSet(1:Defs.ENGINEER_DEBUG_INJECTOR_KB_T tInjectorKB)                         // 工程师调试-设置注射器的KB值
    list<Defs.ENGINEER_DEBUG_INJECTOR_KB_T>  EngineerDebugInjectorKBGet()                                                   // 工程师调试-获取所有注射器的KB值
    // EngineerDebugRunAsync执行参数如下所示 add by wuxiaohu 20230213
    //模块索引 模块名称      模块调试命令索引 模块调试命令名称
    //1        样本针         1               柱塞泵复位
    //1        样本针         2               管路填充
    //1        样本针         3               管路排空
    //1        样本针         4               普通清洗
    //1        样本针         5               特殊清洗
    //1        样本针         6               液面探测
    //2        R1试剂针       1               柱塞泵复位
    //2        R1试剂针       2               管路填充
    //2        R1试剂针       3               管路排空
    //2        R1试剂针       4               普通清洗
    //2        R1试剂针       5               特殊清洗
    //2        R1试剂针       6               液面探测
    //3        R2试剂针       1               柱塞泵复位
    //3        R2试剂针       2               管路填充
    //3        R2试剂针       3               管路排空
    //3        R2试剂针       4               普通清洗
    //3        R2试剂针       5               特殊清洗
    //3        R2试剂针       6               液面探测
    //4        反应杯抓手A    1               开启气爪
    //4        反应杯抓手A    2               关闭气爪
    //4        反应杯抓手A    3               孵育试剂混匀1启动
    //4        反应杯抓手A    4               孵育试剂混匀1停止
    //4        反应杯抓手A    5               孵育试剂混匀2启动
    //4        反应杯抓手A    6               孵育试剂混匀2停止
    //5        反应杯抓手B    1               开启气爪
    //5        反应杯抓手B    2               关闭气爪
    //5        反应杯抓手B    3               启动试剂混匀1启动
    //5        反应杯抓手B    4               启动试剂混匀1停止
    //5        反应杯抓手B    5               启动试剂混匀2启动
    //5        反应杯抓手B    6               启动试剂混匀2停止
    //6        反应杯抓手C    1               开启气爪
    //6        反应杯抓手C    2               关闭气爪
    //6        反应杯抓手C    3               外盘进杯
    //6        反应杯抓手C    4               内盘进杯
    //7        转运模块       1               开启吹气阀
    //7        转运模块       2               关闭吹气阀
    //8        进样器         1               常规位扫码
    //8        进样器         2               急诊位扫码
    //9        试剂存储       1               常温区扫码
    //9        试剂存储       2               试剂仓扫码
    //9        试剂存储       3               试剂仓混匀启动
    //9        试剂存储       4               试剂仓混匀停止
    //10       在线试剂加载   1               开启气爪
    //10       在线试剂加载   2               关闭气爪
    Defs.EXE_STATE      EngineerDebugRunAsync(1:i32 iModuleIndex, 2:i32 iCmd, 3:i32 iUserData)                              // 工程师调试-运行调试指令 iModuleIndex：模块ID，iCmd：指令ID
    Defs.EXE_STATE      ThriftConfigPara(1:Defs.THRIFT_CONFIG_T tThriftConfig, 2:i32 iUserData)                             // 设置上下位机Thrift通信参数
    Defs.EXE_STATE      SetTimeOut(1:i32 iType, 2:i32 iSeconds)                                                             // 设置下位机超时时间，iType: 1：试剂仓仓盖打开超时时间；iSeconds：超时时间，单位为秒，当为0秒时表示关闭试剂仓仓盖监测
    Defs.EXE_STATE      GetUploadBackupFile(1:i32 iFileType, 2:i32 iRandNo)                                                 // 获取下位机文件：iFileType：文件类型，1：下位机日志，2：仪器标定参数 iRandNo：随机数，下位机上传时需按上位机给定的值回传
    Defs.EXE_STATE      RestoreConfigFile(1:string strFileName, 2:i32 iFileType, 3:binary hexConfigFile, 4:string strMD5)   // strFileName:文件名称 iFileType：文件类型，SFT_SLAVE_LOG_FILE：下位机日志，SFT_SLAVE_CONFIG_FILE：仪器标定参数;
                                                                                                                            // hexConfigFile：文件内容；strMD5：MD5校验码
    Defs.EXE_STATE      EngineerAgingRunAsync(1:Defs.SLAVE_ASSEMBLY_AGING_PARA_T tAssemblyAgingPara, 2:i32 iUserData)       // 工程师部件老化指令启动或停止 tAssemblyAgingPara：老化参数

    list<Defs.CHANNEL_STATUS_T> GetChannelStatus()                                                                          // 获取检测通道是否禁用信息
    Defs.EXE_STATE      SetChannelStatus(1:list<Defs.CHANNEL_STATUS_T> lstChannelStatus)                                    // 设置检测通道是否禁用信息
    Defs.EXE_STATE      StartAdjustChannelAsync(1:i32 iUserData)                                                            // 启动光学通道校准
    Defs.EXE_STATE      SetOpticalLED(1:i32 iOnOrOff, 2:i32 iWave)                                                          // 控制光学LED灯开关，iOnOrOff：1：开，0：关， iWave：0：所有波长 340:340nm 405:405nm 570:570nm 660:660nm 800:800nm
    list<Defs.CHANNEL_AD_T> GetChannelAD()                                                                                  // 获取所有通道AD
    Defs.CHANNEL_GAIN_T GetChannelGain()                                                                                    // 获取增益

    // 弃用
    //i32                 GetIsLMSMode()                                                                                      // 获取是否为流水线模式 返回值：1：是，0：否
    //Defs.EXE_STATE      SetIsLMSMode(1:i32 iIsLISMode)                                                                      // 设置是否为流水线模式 1：是，0：否
    //list<Defs.SLAVE_ADDING_PARA_T> GetInstrumentAddingPara()                                                                // 获取下位机多吸量参数
    //Defs.EXE_STATE      SetInstrumentAddingPara(1:list<Defs.SLAVE_ADDING_PARA_T> lstSlaveAddingParas)                       // 设置下位机多吸量参数
}