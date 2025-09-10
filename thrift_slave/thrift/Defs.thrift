typedef bool IBOOL

// 执行状态
enum EXE_STATE
{
    SUCCESS                 = 0,                                // 成功
    FAIL                    = 1                                 // 失败
}

// 执行结果
struct EXE_RESULT_T
{
    1:EXE_STATE             state;                              // 状态
    2:i32                   iResult;                            // 结果-当结果类型为整型时使用
    3:string                strResult;                          // 结果-当结果类型为字符串时使用
}

// 样本针类型
enum NEEDLE_TYPE
{
    SAMPLENEEDLE            = 0,                                // 样本针
    REAGENTNEEDLE1          = 1,                                // 试剂针1
    REAGENTNEEDLE2          = 2,                                // 试剂针2
}

// 下位机输出IO
enum OUTPUT_IO
{
    PE_REGENT_TABLE_GATE    = 1007,                             // 试剂仓仓盖光电
    MICRO_GATE_CUVETTE      = 1020,                             // 杯盘到位光电
    PE_DILU_1               = 1021,                             // 稀释瓶1到位光电
    PE_DILU_2               = 1022,                             // 稀释瓶2到位光电
    PE_INSTRUMENT_GATE      = 1024,                             // 仪器仓盖光电
    PE_WASTE_FULL           = 1026,                             // 垃圾桶满 
    PE_WASTE_EXIST          = 1027,                             // 垃圾桶有无
    PE_WASHA                = 1028,                             // 特殊清洗液微动开关
    WASHA_BUBBLE_SENSOR     = 1030,                             // 清洗液A气泡传感器
    WASHB_BUBBLE_SENSOR     = 1033,                             // 清洗液B气泡传感器
    WASTESENSOR             = 1035,                             // 废液传感器 
    WASHSENSOR              = 1036,                             // 清洗液传感器

    MAGNETIC_BEAD_AD_1      = 4051,                             // 磁珠检测位1
    MAGNETIC_BEAD_AD_2      = 4052,                             // 磁珠检测位2
    MAGNETIC_BEAD_AD_3      = 4053,                             // 磁珠检测位3
    MAGNETIC_BEAD_AD_4      = 4054,                             // 磁珠检测位4
    OPTICAL_AD_1            = 4061,                             // 光学检测位1
    OPTICAL_AD_2            = 4062,                             // 光学检测位2
    OPTICAL_AD_3            = 4063,                             // 光学检测位3
    OPTICAL_AD_4            = 4064,                             // 光学检测位4
    OPTICAL_AD_5            = 4065,                             // 光学检测位5
    OPTICAL_AD_6            = 4066,                             // 光学检测位6
    OPTICAL_AD_7            = 4067,                             // 光学检测位7
    OPTICAL_AD_8            = 4068,                             // 光学检测位8

    // 样本槽锁状态IO
    SAMPLE_SLOT_IO1         = 3008,
    SAMPLE_SLOT_IO2         = 3009,
    SAMPLE_SLOT_IO3         = 3010,
    SAMPLE_SLOT_IO4         = 3011,
    SAMPLE_SLOT_IO5         = 3012,
    SAMPLE_SLOT_IO6         = 3013,

    // 弃用
    //MICRO_GATE_OUTSIDE      = 1005,                             // 反应杯装载位2-外部反应杯盘仓位限位光电 0: 闭合, 1: 开启
    //MICROSWITCH_SNAP_FIT_OUTSIDE = 1012,                        // 反应杯装载位2-外部反应杯盘卡扣微动开关 0: 闭合, 1: 开启
    //BRACKET                 = 2053,                             // 清洗液A、清洗液B、废液托架
    //MAGNETIC_BEAD_AD_5      = 4055,                             // 磁珠检测位5
    //MAGNETIC_BEAD_AD_6      = 4056,                             // 磁珠检测位6
    //OPTICAL_AD_9            = 4069,                             // 光学检测位9
    //OPTICAL_AD_10           = 4070,                             // 光学检测位10
    //OPTICAL_AD_11           = 4071,                             // 光学检测位11
    //OPTICAL_AD_12           = 4072,                             // 光学检测位12
    //OPTICAL_AD_13           = 4073,                             // 光学检测位13
    //OPTICAL_AD_14           = 4074,                             // 光学检测位14
    //OPTICAL_AD_15           = 4075,                             // 光学检测位15
    //OPTICAL_AD_16           = 4076,                             // 光学检测位16
}

// 温度传感器
enum TEMPERATURE_SENSOR
{
    REAGENTCASE             = 1,                                // 试剂仓
    REAGENTPIPE             = 2,                                // 试剂针
    INCUBATIONAREA          = 3,                                // 孵育区
    MAGNETICBEAD            = 4,                                // 磁珠检测区
    ENVIRONMENTAREA         = 5,                                // 环境温度
    REAGENTCASEGLASS        = 6,                                // 试剂仓玻璃片
    OPTICAL1                = 7,                                // 光学检测1区
    OPTICAL2                = 8,                                // 光学检测2区
}

// 气压传感器
// sensor0 = 0    暂未使用
// sensor1 = 1    暂未使用
// sensor2 = 2    正压1
// sensor3 = 3    正压2
// sensor4 = 4    样本针吸样压力
// sensor5 = 5    负压

// 上位机控制IO
enum INPUT_IO
{
    CUPLOADPOS1             = 1,                                // 反应杯装载位1
    CUPLOADPOS2             = 2,                                // 反应杯装载位2
    
    // 样本槽锁
    SAMPLE_SLOT_IO1         = 3001,
    SAMPLE_SLOT_IO2         = 3002,
    SAMPLE_SLOT_IO3         = 3003,
    SAMPLE_SLOT_IO4         = 3004,
    SAMPLE_SLOT_IO5         = 3005,
    SAMPLE_SLOT_IO6         = 3006,
}

// 下位机按钮
//enum BUTTON_IO
//{
//    BTN_EMERGENCY_STOP      = 1,                                 // 紧急停止按钮
//    BTN_START               = 2,                                 // 开始按钮
//    BTN_REAGENT_ROTATE      = 3,                                 // 试剂仓旋转

    // 弃用
    //BTN_STAT                = 3,                               // 急诊进出仓按钮
    //BTN_WASTE_CUP_H         = 4,                               // 上侧固体废物进出按钮
    //BTN_WASTE_CUP_L         = 5,                               // 下侧固体废物进出按钮
    //BTN_LMS_ONLINE_OFFLINE  = 7,                               // 流水线联机-脱机按钮
//}

enum BOTTLE_TYPE
{
    BT_UNKNOWN              = 0,                                 // 未知
    BT_REAGENT_7_ML         = 1,                                 // 试剂瓶7ml
    BT_REAGENT_15_ML        = 2,                                 // 试剂瓶15ml
    BT_REAGENT_25_ML        = 3,                                 // 25ml试剂瓶（清洗液B）
    BT_REAGENT_3_ML         = 4,                                 // 3ml试剂瓶（乏因子血浆、光学质控品）
    BT_REAGENT_200_ML       = 5,                                 // 200ml试剂瓶（清洗液A）
    BT_TUBE_MICRO           = 6,                                 // 微量杯
    BT_TUBE_EP15            = 7,                                 // EP管1.5ml
}

// 清洗信息
struct RINSE_INFO_T
{
    1:IBOOL                 bEnable;                            // 是否清洗,true:是，false:否
    2:IBOOL                 bEnableWashA;                       // 是否进行特殊清洗，true:是，false:否
}

// 订单试剂加注混匀信息
struct REAGENT_INFO_T
{
    1:i32                   iR;                                 // 试剂类别 1:Ra; 2:Rb; 3:Rc; 4:Rd; 5:Factor-Deficient Plasma(乏因子血浆)
    2:i32                   iPosIndex;                          // 试剂盘试剂位索引
    3:i32                   iBottleType;                        // 试剂瓶型，暂不确定详细的瓶型类别
    4:RINSE_INFO_T          tPreRinseInfo;                      // 前清洗参数
    5:RINSE_INFO_T          tPostRinseInfo;                     // 后清洗参数
    6:i32                   iAspriationVol;                     // 吸取量，单位为uL
    7:IBOOL                 bEnableHeat;                        // 是否加热: true:是，false:否
    8:IBOOL                 bEnableMixing;                      // 是否混匀: true:是，false:否
    9:i32                   iMixingType;                        // 当bEnableMixing为1时使用，混匀类型 0:不混匀 1:短周期混匀 2:长周期混匀
    10:i32                  iMixingRate;                        // 当bEnableMixing为1时使用，混匀速率，指振荡混匀电机的转速，单位为rpm（每分钟转速 默认为13056rpm/min）
}

// 稀释比例 - 当稀释比例分子iUp为0时，表示不加样本，全部加稀释液；当iUp与iDown均为1时（1/1），表示原倍，即不稀释，全部加样本；
// 稀释比例分子iUp表示样本占比，稀释比例分母iDown表示样本+稀释液总体积
struct DILUTION_FRACTION_T
{
    1:i32                   iUp;                                // 稀释比例分子
    2:i32                   iDown;                              // 稀释比例分母
}

// 测试订单信息
struct ORDER_INFO_T
{
    // 基础信息参数
    1:i32                   iOrderNo;                           // 订单编号(上下位机的唯一识别依据，以测试为单位)
    2:string                strAssayGroup;                      // 项目组信息
    3:i32                   iOrderType;                         // 订单类型 0:正常检测订单 1:复查订单 2:再分析订单 3:条件对比分析订单
    // 检测方法学参数
    4:i32                   iMethod;                            // 检测方式 0.双磁路磁珠法，1.光学-免疫透射比浊法,2.光学-发色底物法
    // 磁珠法参数
    5:i32                   iMagBeedClotPercent;                // 仅双磁路磁珠法使用，凝固判定百分比，范围为1-100（对应表示1%至100%,通常为50%:正常凝固，80%:弱凝）
    6:i32                   iMagBeedDriveForce;                 // 仅双磁路磁珠法使用，磁珠驱动力，0:正常，1:弱, 2:强
    7:i32                   iMagBeedMaxDetectSeconds;           // 仅双磁路磁珠法使用，最大检测时间，以秒为单位，当达到此时间后仍未凝固时，则需上报结果状态码为"不凝"
    8:i32                   iMagBeedMinDetectSeconds;           // 仅双磁路磁珠法使用，最小检测时间，以秒为单位，当小于此时间凝固时，则需上报结果状态码为"异常"，另需针对钢珠未起振时，则需上报结果状态为“反应杯无钢珠”
    // 光学法参数
    9:i32                   iOpticsMainMeasurementSeconds;      // 仅光学法使用，主波长检测时间，以秒为单位，当达到此时间后，主波长信号采集完成
    10:i32                  iMainWavelenght;                    // 副波长 340:340nm 405:405nm 570:570nm 660:660nm 800:800nm
    11:i32                  iGain;                              // 增益（尚不确定此参数作用，是否指放大倍数), 1:Low, 2:Middle, 3:High
    12:i32                  iOpticsSubMeasurementSeconds;       // 仅光学法使用，副波长检测时间，以秒为单位，当达到此时间后，副波长信号采集完成
    13:i32                  iSubWavelenght;                     // 副波长 340:340nm 405:405nm 570:570nm 660:660nm 800:800nm
    // 样本量加注清洗参数
    14:i32                  iSampleVolume;                      // 样本总量，指最终向反应杯内添加的用于检测样本（与稀释液）混合物总量，当不稀释时，则全部为样本
    15:DILUTION_FRACTION_T  tDiluentRatio;                      // 稀释比例，按照DILUTION_FRACTION_T定义执行: 1/1时表示不稀释; 0/X时表示全加稀释液，不加样本
    16:i32                  iDiluentPos;                        // 稀释液位置
    17:RINSE_INFO_T         tSamplePreRinseInfo;                // 加样前清洗参数
    18:RINSE_INFO_T         tSamplePostRinseInfo;               // 加样后清洗参数
    // 乏因子血浆参数
    19:IBOOL                bAddFactorDeficientPlasma;          // 是否添加乏因子血浆: true:是，false:否
    20:REAGENT_INFO_T       tFactorDeficientPlasmaInfo;         // 当bAddFactorDeficientPlasma为1时有效，乏因子血浆信息
    // 孵育试剂加注清洗参数
    21:list<REAGENT_INFO_T> lstIncuReagentInfos;                // 孵育试剂添加信息
    22:i32                  iIncubationSeconds;                 // 孵育时间，单位为秒
    // 启动试剂加注清洗参数
    23:list<REAGENT_INFO_T> lstDeteReagentInfos;                // 启动试剂添加信息
    24:i32                  iCalibratorIndex;                   // 调试使用，校准订单有效，表示校准订单校准点序号 0: 无效 1: 校准点1 2: 校准点2...
}

enum SAMPLE_TUBE_TYPE
{
    STANARD_CUP             = 0,                                // 标准杯
    EP_CUP                  = 1,                                // EP管
    PP_1_8                  = 2,                                // 蓝头采血管1.8毫升，PP材质
    PP_2_7                  = 3,                                // 蓝头采血管2.7毫升，PP材质
    BT_V3                   = 4,                                // 3ML试剂瓶，用于试剂仓质控
    BT_V7                   = 5,                                // 7ML试剂瓶，用于试剂仓质控
    BT_V15                  = 6,                                // 15ML试剂瓶，用于试剂仓质控
}


// 抗凝比例异常、HIL筛查异常、凝块异常的处理方式
// 0.输出数值
// 1.输出带有低可信度标识的数值
// 2.输出隐藏数值
// 3.不检测该样本

// 样本质量设置信息
struct SAMPLE_QUALITY_SET_INFO_T
{
    1:IBOOL                 bIsCheckAnticoagulationRatio;       // 是否启用抗凝比例检查: true:启用；false:不启用
    2:i32                   iAnticoagulationRatioHandlingMode;  // 处理方式，参见抗凝比例异常、HIL筛查异常、凝块异常的处理方式的定义
    3:double                dTubeInsideDiameter;                // 采血管内径（mm）
    4:double                dBloodCapacity;                     // 采血量（mL）
    5:double                dBloodCapacityFloat;                // 采血量浮动率（0~10%）
    6:IBOOL                 bIsCheckClot;                       // 是否启用凝块检查: true:启用；false:不启用
    7:i32                   iClotSensitivity;                   // 凝块检查的灵敏度：有效范围为0-5，仅当bIsCheckClot为true时有效
    8:i32                   iClotHandlingMode;                  // 凝块处理方式，参见抗凝比例异常、HIL筛查异常、凝块异常的处理方式的定义
}

// 样本测试订单信息
struct SAMPLE_ORDER_INFO_T
{
    1:i32                   iSampleOrderNo;                     // 测试样本编号，样本的唯一识别依据
    2:SAMPLE_QUALITY_SET_INFO_T tSampleQualitySet;              // 样本质量设置信息
    3:SAMPLE_TUBE_TYPE      eSampleTubeType;                    // 样本管类型
    4:i32                   iSlotNo;                            // 样本槽编号（1~6）
    5:i32                   iRackIndex;                         // 样本架号（常规架:5001-5008，试剂仓:300）
    6:i32                   iSamplePosIndex;                    // 样本架样本位置1~10（需结合iRackPosIndex来判定样本位置）
    7:i32                   iPriority;                          // 新增优先级 0.急诊>1.复查>2.试剂仓质控>3.样本/校准/质控
    8:list<ORDER_INFO_T>    lstOrderInfo;                       // 样本待测订单列表
}

// 样本质量信息
struct SAMPLE_QUALITY_T
{
    1:IBOOL                 bIsCheckAnticoagulationRatio;       // 是否进行了抗凝比例检查: true:是；false:否
    2:IBOOL                 bIsARError;                         // 抗凝比例是否异常，true: 异常; false: 正常    
    3:IBOOL                 bIsCheckClot;                       // 是否进行了凝块检查: true:是；false:否
    4:IBOOL                 bIsClotError;                       // 凝块检查是否异常，true: 异常; false: 正常    
}

// 定时开机参数
struct BOOT_PARAM_T
{
    1:i32                   iWeek;                              // 1:星期一；2:星期二;...7:星期日
    2:i32                   iHour;                              // 开机时间，小时0~23
    3:i32                   iMinute;                            // 分0~59
    4:IBOOL                 bEnable;                            // 是否启用开机，true:启用  false:不启用
}

// 管路填充信息
struct PIPELINE_FILL_INFO_T
{
    1:RINSE_INFO_T          tSampleRinseInfo;
    2:RINSE_INFO_T          tReagnet1RinseInfo;
    3:RINSE_INFO_T          tReagnet2RinseInfo;
}

// 试剂位置及瓶型
struct REAGENT_POS_INFO_T
{
    1:i32                   iPosIndex;                          // 试剂盘试剂位索引
    2:i32                   iIsRemainDetect;                    // 是否需要余量探测 0:不需要, 1:需要
    3:i32                   iBottleType;                        // 试剂瓶型
    4:string                strReagentName;                     // 试剂名称（调试使用）
    5:string                strReagentLot;                      // 试剂批号 （调试使用）
    6:i32                   iReagentSerialNo;                   // 试剂序列号 （调试使用）
    7:i32                   iReagentCategory;                   // 试剂类别 0.试剂，1. 乏因子血浆，2. 稀释液，3. 清洗液A，即特殊清洗液，4. 质控品，-1.未知 （调试使用）
    8:i32                   iRx;                                // 试剂索引 0:未使用  1:Ra  2:Rb  3：Rc  4：Rd （调试使用）此参数仅针对试剂有效
	9:i32                   iBottleMaterial;                    // 瓶材料 0:非镀膜 1：镀膜 2：其他（等同于镀膜处理）
}

// 仪器自检及试剂混匀维护流程中使用，试剂混匀位置、瓶型、试剂名称（调试使用）、批号（调试使用）及序列号（调试使用）
struct REAGENT_MIX_INFO_T
{
    1:i32                   iPosIndex;                          // 试剂盘试剂位索引
    2:i32                   iMixingType;                        // 试剂混匀类型 0:不混匀, 1:弱 2:中 3:强
    3:i32                   iBottleType;                        // 试剂瓶型
    4:string                strReagentName;                     // 试剂名称（调试使用）
    5:string                strReagentLot;                      // 试剂批号 （调试使用）
    6:i32                   iReagentSerialNo;                   // 试剂序列号 （调试使用）
    7:i32                   iReagentCategory;                   // 试剂类别 0.试剂，1. 乏因子血浆，2. 稀释液，3. 清洗液A，即特殊清洗液，4. 质控品，-1.未知 （调试使用）
    8:i32                   iRx;                                // 试剂索引 0:未使用  1:Ra  2:Rb  3：Rc  4：Rd （调试使用）此参数仅针对试剂有效
}

// 订单状态
enum ORDER_STATE
{
    OD_SAMPLECOMPLETION     = 0,                                // 加样完成
    OD_INCUBATING           = 1,                                // 孵育中
    OD_DETECTING            = 2,                                // 检测中
    OD_COMPLETION           = 3,                                // 检测完成
    OD_ERROR                = 4,                                // 检测出错
    OD_INIT                 = 5,                                // 订单初始化
    OD_DEPRECATED           = 6,                                // 放弃订单
}

// 样本条码信息
struct SAMPLE_TUBE_INFO_T
{
    1:i32                   iSlotNo;                            // 样本槽编号（1~6）
    2:i32                   iRackIndex;                         // 样本架号（常规架:5001-5008，试剂仓:300）
    3:i32                   iPosIndex;                          // 样本架位置索引
    4:IBOOL                 bScanStatus;                        // 扫描样本管是否成功 true:成功 false:失败
    5:IBOOL                 bExist;                             // 样本管是否存在 true:是，即存在; false:否，即不存在
    6:string                barcode;                            // 样本条码
    7:i32                   iTubeType;                          // 样本管类型, 0: 标准管（指采血管） 1: 微量管 -1: 未知管型
    8:i32                   iIsExistCap;                        // 是否存在样本管帽，0：不存在 1：存在
}

// 检测结果状态
enum RESULT_STATE
{
    NORMAL                  = 0,                                // 正常检测完成
    UN_CLOT                 = 1,                                // 未凝固，仅磁珠法适用
    ABNORMAL                = 2,                                // 异常，即指强凝，仅针对磁珠法
    AD_OUT_OF_RANGE         = 3,                                // 信号值超出上下限，光学与磁珠均可
    NO_BEAD                 = 4,                                // 无钢珠，仅针对磁珠法
}

// 检测结果及信号值
struct RESULT_INFO_T
{
    1:RESULT_STATE          eResultState;                       // 结果状态
    2:i32                   iDetectPos;                         // 检测位编号 下位机默认从1开始
    3:i32                   iClotTime;                          // 凝固时间，仅磁珠法适用，光学法填0即可，以十分之1秒为单位，如32.1秒表示为321
    4:list<i32>             lstADData;                          // 磁珠AD或光学主波长AD值
    5:list<i32>             lstSubADData;                       // 光学副波长AD值，仅光学法及存在副波长时使用
    6:string                strCupLotNo;                        // 反应杯盘批号
    7:i32                   iCupSerialNo;                       // 反应杯盘序列号
}

// 试剂/耗材类型
enum REAGENT_SUPPLY_TYPE
{
    REAGENT                 = 0,                                // 试剂
    DILUENT                 = 1,                                // 稀释液
    CUP                     = 2,                                // 反应杯
    WASTE_CUP               = 3,                                // 废反应杯
    WASH_B                  = 4,                                // 清洗液B（常规清洗液）
    WASH_A                  = 5,                                // 清洗液A（特殊清洗液）
    WASTE_WATER             = 6,                                // 废液
    PUNCTURE_NEEDLE         = 7,                                // 穿刺针
    AIR_PUMP                = 8,                                // 气泵
    REAGENT_QC              = 9,                                // 试剂仓质控
}

// 异步执行的返回值
struct ASYNC_RETURN_T
{
    1:i32                   iReturnType;                       // 返回类型:0：无，1：i32, 2:double 3:string
    2:i32                   iReturn;                           // i32返回值
    3:double                dReturn;                           // double返回值
    4:string                strReturn;                         // string返回值
}

// Service及调试参数
// 电机参数设置
struct THRIFT_MOTOR_PARA_T
{
    1:i32                   iMotorID;                          // 电机序号
    2:i32                   iSpeed;                            // 速度
    3:i32                   iAcc;                              // 加速度
}

// 日期时间
struct DATE_TIME_T
{
    1:i32                   iYear                              // 年
    2:i32                   iMonth                             // 月
    3:i32                   iDayOfWeek                         // 周
    4:i32                   iDay                               // 日
    5:i32                   iHour                              // 时
    6:i32                   iMinute                            // 分
    7:i32                   iSecond                            // 秒
}

// 耗材信息及是否可用
struct CONSUMABLES_INFO_T
{
    1:REAGENT_SUPPLY_TYPE   iConsumableType;                   // 耗材类型 
    2:i32                   iEnable;                           // 是否允许使用 1: 允许使用，0: 禁用
    3:i32                   iIndex;                            // iIndex指存在两个同类型耗材的序号，如eType为CUP时, iPosIndex为1时指反应盘1，为2时指反应盘2，如eType为WASTE_CUP时, iPosIndex为1时指上方的废料桶，为2时指下方的废料桶，其它类型此参数无效
    4:i32                   iPriorityOfUse;                    // 是否优先使用 1: 优先使用，0: 备用 仅当存在两个同类型耗材时有效，如反应杯，废料桶 
    5:string                strLotNo;                          // 批号
    6:i32                   iSerialNo;                         // 序列号
    7:IBOOL                 bInit;                             // 是否初始化 true:是，false：更换
}

// 试剂扫描信息
struct REAGENT_SCAN_INFO_T
{
    1:i32                   iReagBracketIndex;                 // 试剂托架索引 1：A；2：B；3：C；4: D；5：E；6: F 7: 稀释液区域 0: 无效
    2:i32                   iIsReagBracketExist;               // 试剂托架是否存在 0：不存在 1：存在 2: 存在但试剂托架放置错误（备注：条码信息中为对应的试剂托架内容，如A、B、C...F），为稀释液区域时，必定为1
    3:i32                   iReagPosIndex;                     // 试剂位置索引 0: 不生效；1-42: 试剂对应位置 43-47: 稀释液对应位置
    4:i32                   iExistStatus;                      // 条码识别状态 0: 否，即不存在 / 或扫码失败（不存在）; 1: 试剂扫码正常，条码见barcode; 2: 识别为大适配器，条码见barcode; 3: 识别为小适配器，条码见barcode; 4: 识别为提篮对应孔位号(未放置适配器时)，条码见barcode; 5: 识别为稀释液位置描述码，条码见barcode
    5:string                strBarcode;                        // 条码信息
}

// 工程师调试(R、X、Y、Z轴)类型
enum ENGINEER_DEBUG_AXIS_TYPE
{
    R_NONE                  = 0,                                // 无，表示此参数无效
    R_AXIS                  = 1,                                // R轴
    X_AXIS                  = 2,                                // X轴
    Y_AXIS                  = 3,                                // Y轴
    Z_AXIS                  = 4,                                // Z轴
}

// 工程师调试(虚拟位置)参数
struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_T
{
    1:i32                   iModuleIndex;                      // 模块索引, 待嵌软老师明确
    2:i32                   iVirtualPosIndex;                  // 虚拟位置索引, 其可能包括一个或多个子位置参数, 待嵌软老师明确
    3:string                strVirtualPosName;                 // 虚拟位置名称
    4:i32                   iEnableR;                          // 调试位置R是否有效 0: 不生效 1：生效
    5:i32                   iR_Steps;                          // 调试位置R对应的步数，当iEnableR为1时有效
    6:i32                   iR_MaxSteps;                       // 调试位置R对应的最大步数，当iEnableR为1时有效
    7:i32                   iEnableX;                          // 调试位置X是否有效 0: 不生效 1：生效
    8:i32                   iX_Steps;                          // 调试位置X对应的步数，当iEnableX为1时有效
    9:i32                   iX_MaxSteps;                       // 调试位置X对应的最大步数，当iEnableX为1时有效
    10:i32                  iEnableY;                          // 调试位置Y是否有效 0: 不生效 1：生效
    11:i32                  iY_Steps;                          // 调试位置Y对应的步数，当iEnableY为1时有效
    12:i32                  iY_MaxSteps;                       // 调试位置Y对应的最大步数，当iEnableY为1时有效
    13:i32                  iEnableZ;                          // 调试位置Z是否有效 0: 不生效 1：生效
    14:i32                  iZ_Steps;                          // 调试位置Z对应的步数，当iEnableZ为1时有效
    15:i32                  iZ_MaxSteps;                       // 调试位置Z对应的最大步数，当iEnableZ为1时有效
}

// 工程师调试虚拟位置
struct ENGINEER_DEBUG_VIRTUAL_POSITION_T
{
    1:ENGINEER_DEBUG_VIRTUAL_POS_PARA_T tOldVirautlPosPara;    // 旧的(虚拟位置)参数
    2:ENGINEER_DEBUG_VIRTUAL_POS_PARA_T tCurVirautlPosPara;    // 当前(虚拟位置)参数
}

// 工程师调试(模块位置)参数
struct ENGINEER_DEBUG_MODULE_PARA_T
{
    1:ENGINEER_DEBUG_VIRTUAL_POS_PARA_T tVirautlPosPara;       // 工程师调试(虚拟位置)参数
    2:i32                   iIsExistRelativeViraultPosPara;    // 是否存在关联的虚拟位置参数 0：不存在 1：存在
    3:ENGINEER_DEBUG_VIRTUAL_POS_PARA_T tRelativeVirautlPosPara;// 关联的工程师调试(虚拟位置)参数，仅当iIsExistRelativeViraultPosPara为1时有效
}

// 工程师调试(直流类电机或IO)参数
struct ENGINEER_DEBUG_MOTOR_OR_IO_PARA_T
{
    1:i32                   iMotorOrIOIndex;                   // 直流类电机或IO索引
    2:i32                   iOnOrOff;                          // IO开或关：1：打开(启动)；0：关闭（停止）
}

// 工程师调试指令（（正向或反向）移动、移动到、正常复位、上电复位、液面探测）类型
enum ENGINEER_DEBUG_ACTION_TYPE
{
    EDAT_NONE                 = 0,                             // 无，表示此参数无效
    MOVE                      = 1,                             // （正向或反向）移动
    MOVE_TO                   = 2,                             // 移动到
    RESET                     = 3,                             // 正常复位
    POWER_ON_RESET            = 4,                             // 上电复位
    LIQUID_DETECT             = 5,                             // 液面探测
}

// 工程师调试(步进或伺服)电机参数
struct ENGINEER_DEBUG_MOTOR_PARA_T
{
    1:i32                   iModuleIndex;                      // 模块索引, 待嵌软老师明确
    2:i32                   iVirtualPosIndex;                  // 虚拟位置索引, 其可能包括一个或多个子位置参数, 待嵌软老师明确
    3:ENGINEER_DEBUG_AXIS_TYPE eAxisType;                      // 轴类型
    4:ENGINEER_DEBUG_ACTION_TYPE eActionType;                  // 调试指令
    5:i32                   iSteps;                            // 步数，仅在eActionType为MOVE、MOVE_TO有效，其它情况无效
}

// 项目组测试数
struct ASSAY_GROUP_TESTS_T
{
    1:i32                   iAssayGroupID;                     // 项目组ID
    2:i32                   iAssayGroupHostID;                 // 项目组通道号
    3:string                strAssayGroupName;                 // 项目组名称
    4:i32                   iTests;                            // 项目组测试数
}

// 注射器ID定义
// #define EDI_SAMPLE_NORMAL_ADDING                0;          // 样本针常规加样（单次吸/吐液)
// #define EDI_R1_ADDING                           1;          // 孵育试剂针加样
// #define EDI_R2_ADDING                           2;          // 启动试剂针加样
// #define EDI_SAMPLE_DILUENT_ADDING               3;          // 样本针稀释加样（同时吸取稀释液和样本）
// #define EDI_SAMPLE_DILUENT_ADDINGWITH_PUNCTUR   4;          // 样本针稀释吐样（穿刺针特有，同时吐出稀释液和样本）

// 工程师位置标定
struct ENGINEER_DEBUG_POS_CALIB_T
{
    1:i32                   iCalibID;                          // 标定ID
    2:i32                   iPosID;                            // 位置ID
    3:i32                   iOldValue;                         // 旧值
    4:i32                   iCurValue;                         // 当前标定值
}

// 注射器KB值
struct ENGINEER_DEBUG_INJECTOR_KB_T
{
    1:i32                   iInjectID;                         // 注射器ID
    2:string                strInjectName;                     // 注射器名称
    3:double                dK;                                // 注射器K值
    4:double                dB;                                // 注射器B值
}

// 调试指令执行返回结果
struct ENGINEER_DEBUG_RUN_RESULT_T
{
    1:i32                   iRunResult;                        // 执行结果：0：成功，其它值：失败，其值为对应的错误码
    2:string                strBarcode;                        // 条码内容，仅执行读取条码时适用，当读取条码成功时，其值为读取的条码内容
}

// 上下位机Thrift通信配置
struct THRIFT_CONFIG_T
{
    1:string               strSlaveIP;                        // 下位机IP地址，点分十分进制，如“192.168.1.12”
    2:i32                  iSlavePort;                        // 下位机Thrift监听端口
    3:string               strHostIP;                         // 上位机IP地址，点分十分进制，如“192.168.1.100”
    4:i32                  iHostPort;                         // 上位机Thrift监听端口
    5:i32                  iIsPunctureNeedle;                 // 是否为穿刺针 1：穿刺针；0：非穿刺针
}

// 下位机文件类型定义
// #define SFT_SLAVE_LOG_FILE                      1;        // 下位机日志文件
// #define SFT_SLAVE_CONFIG_FILE                   2;        // 下位机仪器标定参数文件

// 下位机子程序定义
// #define SP_MAIN_BOARD_LINUX                     1;         // 主控板Linux
// #define SP_MAIN_BOARD_MCU                       2;         // 主控板MCU
// #define SP_MAIN_BOARD_FPGA                      3;         // 主控板FPGA
// #define SP_SAMPLE_BOARD_FPGA                    4;         // 进样器FPGA
// #define SP_SAMPLE_BOARD_MCU                     5;         // 进样器MCU
// #define SP_LIQ_CIRCUIT_BOARD_MCU                6;         // 液路板MCU
// #define SP_TEMPERATE_BOARD_MCU                  7;         // 温控板MCU
// #define SP_MAGNEIC_BOARD_MCU                    8;         // 磁珠板MCU
// #define SP_OPTICAL1_BOARD_MCU                   9;         // 光学检测板1 MCU
// #define SP_OPTICAL2_BOARD_MCU                   10;        // 光学检测板2 MCU
// #define SP_OPTICAL3_BOARD_MCU                   11;        // 光学检测板3 MCU
// #define SP_LIQ_DETECT_S_BOARD_MCU               12;        // 样本针液面探测板 MCU
// #define SP_LIQ_DETECT_R1_BOARD_MCU              13;        // 试剂针1液面探测板 MCU
// #define SP_LIQ_DETECT_R2_BOARD_MCU              14;        // 试剂针2液面探测板 MCU

// 下位机子程序
struct SLAVE_PROGRAM_T
{
    1:i32                  iSlaveProgramNo;                   // 下位机子程序编号，参见下位机子程序定义
    2:string               strSlaveProgramFileName;           // 下位机子程序HEX文件文件名
    3:binary               hexSlaveProgram;                   // 下位机子程序HEX文件(HEX文件中包含程序版本号)
    4:string               strMD5;                            // 下位机子程序HEX文件MD5校验
    5:i32                  iHexSlaveProgramLen;               // 下位机子程序HEX文件长度（以字节为单位）
}

// 下位机子程序升级结果
struct SLAVE_PROGRAM_UPDATE_RESULT_T
{
    1:i32                  iSlaveProgramNo;                   // 下位机子程序编号，参见下位机子程序定义
    2:i32                  iUpdateResult;                     // 更新结果，0：成功，其它值：失败，其值为对应的错误码
}

// 下位机部件老化参数
struct SLAVE_ASSEMBLY_AGING_PARA_T
{
    1:i32                  iIsOnOrOFF;                        // 是否启动下位机部件老化：1：启动；0：停止
    2:i32                  iLoopCounts;                       // 循环次数合理范围：仅当iIsOnOrOFF为1时有效，有效值为1-10000
    3:i32                  iIsEnableSampler;                  // 是否启用进样器老化：2103无效，与2102保持一致遗留
    4:i32                  iIsEnableReagentBin;               // 是否启用试剂仓老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效
    5:i32                  iIsEnableGripperA;                 // 是否启用抓手老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效
    6:i32                  iIsEnableGripperB;                 // 是否启用抓手B老化：2103无效，与2102保持一致遗留
    7:i32                  iIsEnableGripperC;                 // 是否启用抓手C老化：2103无效，与2102保持一致遗留
    8:i32                  iIsEnableSampleNeedle;             // 是否启用样本针老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效
    9:i32                  iIsEnableIncubationReagentNeedle;  // 是否启用孵育试剂针老化：2103无效，与2102保持一致遗留
    10:i32                 iIsEnableDetectionReagentNeedle;   // 是否启用启动试剂针老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效
    11:i32                 iIsEnableOnlineLoad;               // 是否启用在线装载老化：2103无效，与2102保持一致遗留
    12:i32                 iIsEnableReactionCupTransferBelt;  // 是否启用反应杯转运皮带老化：2103无效，与2102保持一致遗留
    13:i32                 iIsEnableReactionCupMixing;        // 是否启用反应杯混匀混匀：2103无效，与2102保持一致遗留
    14:i32                 iIsEnableReactionCupLoad;          // 是否启用反应盘加载：2103无效，与2102保持一致遗留
}

// 下位机反馈的易损件计数或计时信息 add by wuxiaohu 20231025
struct SLAVE_COUNTEROR_TIMER_INFO_T
{
    1:i32                  iCounterOrTimerID;                 // 易损件ID
    2:i32                  iNumberOfTimesOrElapsedTime;       // 易损件计数次数(以次数为单位)或计时时间（以分钟为单位)
    3:i32                  iTotalNumberOfTimesOrElapsedTime;  // 易损件累计计数次数(以次数为单位)或计时时间（以分钟为单位)
}

// 检测通道状态 add by wuxiaohu 20240104
struct CHANNEL_STATUS_T
{
    1:i32                 iChannelNo;                        // 通道编号，从0开始，0表示通道1，1表示通道2...
    2:i32                 iChannelType;                      // 通道类型，0：磁珠法检测通道，1：光学法检测通道
    3:i32                 iDisable;                           // 通道状态，0：启用（即未禁用），1：禁用
}

// 340:340nm 405:405nm 570:570nm 660:660nm 800:800nm

// 检测通道的增益 add by wuxiaohu 20240104
struct CHANNEL_GAIN_T
{
    1:i32                 i340Gain;                          // 340nm增益
    2:i32                 i405Gain;                          // 405nm增益
    3:i32                 i570Gain;                          // 570nm增益
    4:i32                 i660Gain;                          // 660nm增益
    5:i32                 i800Gain;                          // 800nm增益
}

// 检测通道的波长、AD值 add by wuxiaohu 20240104
struct CHANNEL_AD_T
{
    1:i32                 iChannelNo;                        // 通道编号，从0开始，0表示通道1，1表示通道2...
    2:i32                 iChannelType;                      // 通道类型，0：磁珠法检测通道，1：光学法检测通道
    3:i32                 i340AD;                            // 340nmAD
    4:i32                 i405AD;                            // 405nmAD
    5:i32                 i570AD;                            // 570nmAD
    6:i32                 i660AD;                            // 660nmAD
    7:i32                 i800AD;                            // 800nmAD
    8:i32                 iReserved;                         // 保留字段
}

// 维护项 add by songxiaosong 20241014
struct MAINTENANCE_ITEM_T
{
    1:i32                 iItemID;                          // 维护项ID，详见《维护单项目.xlsx》中ID定义
    2:i32                 iParam;                           // 参数 -1.无效
}
