#ifndef _THRIFT_HANDLER_H_
#define _THRIFT_HANDLER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <slip/slip_port.h>
#include <slip/slip_msg.h>
#include <slip_cmd_table.h>
#include "module_sampler_ctl.h"


/* 下位机仪器默认的IP、PORT */
#define THRIFT_SLAVE_SERVER_IP "192.168.33.200"
#define THRIFT_SLAVE_SERVER_PORT  6000

/* 上位机PC默认的IP、PORT */
#define THRIFT_MASTER_SERVER_IP "192.168.33.100"
#define THRIFT_MASTER_SERVER_PORT 5000

typedef struct {
    char ip[32];
    int port;
}thrift_master_t;

typedef enum {
    RETURN_VOID,
    RETURN_INT,
    RETURN_DOUBLE,
    RETURN_STRING
}return_type_t;

typedef struct {
    return_type_t return_type; /* 返回类型:0：无，1：i32, 2:double 3:string, 参考ASYNC_RETURN_T*/
    union {
        int return_int;
        int return_double;
        char return_string[256];
    };
}async_return_t;

typedef enum {
    ALARM_COMMUNICATION = 1001, /* 通信故障 */
}alarm_code_t;

#if 0
typedef enum {
    REAGENT_CASECOVER           = 1,        /* 试剂仓盖 */
    INSTRUMENT_CASECOVER        = 2,        /* 仪器仓盖 */
    WASTE_SENSOR                = 3,        /* 废液传感器 */
    WASH_SENSOR                 = 4,        /* 清洗液传感器 */
    CUP_LOAD_POS1               = 5,        /* 反应杯装载位1 */
    CUP_LOAD_POS2               = 6,        /* 反应杯装载位2 */
    STAT_POS1                   = 7,        /* 急诊位1 */
    STAT_POS2                   = 8,        /* 急诊位2 */
    STAT_POS3                   = 9,        /* 急诊位3 */
    STAT_POS4                   = 10,       /* 急诊位4 */
    STAT_POS5                   = 11,       /* 急诊位5 */
	SAMPLE_RACK_FULL_DETECT     = 3001,     /* 回收满料光电 */
	SAMPLE_RACK_EXIST_DETECT    = 3007,     /* 进样检测光电 */
}output_io_t;
#else
typedef enum output_io {
    TYPE_PE_INSTRUMENT_GATE = 1024,
    TYPE_MICRO_GATE_INISIDE = 1020,
    TYPE_MICROSWITCH_SNAP_FIT_INSIDE = 1019,
    TYPE_REAGENT_CASECOVER = 1008,

    TYPE_PE_DILU_1 = 1021,
    TYPE_PE_DILU_2  = 1022,
    TYPE_PE_REGENT_TABLE_TABLE = 1007,

    TYPE_WASTESENSOR = 2051,
    TYPE_WASHSENSOR = 2052,
    TYPE_WASHB_BUBBLE_SENSOR = 2054,

    TYPE_SAMPLE_SLOT_IO1 = 3008,
    TYPE_SAMPLE_SLOT_IO2 = 3009,
    TYPE_SAMPLE_SLOT_IO3 = 3010,
    TYPE_SAMPLE_SLOT_IO4 = 3011,
    TYPE_SAMPLE_SLOT_IO5 = 3012,
    TYPE_SAMPLE_SLOT_IO6 = 3013,

    TYPE_MAGNETIC_BEAD_AD_1 = 4051,
    TYPE_MAGNETIC_BEAD_AD_2 = 4052,
    TYPE_MAGNETIC_BEAD_AD_3 = 4053,
    TYPE_MAGNETIC_BEAD_AD_4 = 4054,

    TYPE_OPTICAL_AD_1 = 4061,
    TYPE_OPTICAL_AD_2 = 4062,
    TYPE_OPTICAL_AD_3 = 4063,
    TYPE_OPTICAL_AD_4 = 4064,
    TYPE_OPTICAL_AD_5 = 4065,
    TYPE_OPTICAL_AD_6 = 4066,
    TYPE_OPTICAL_AD_7 = 4067,
    TYPE_OPTICAL_AD_8 = 4068,

    TYPE_GARBAGE_FULL_PE = 1026,
    TYPE_DUSTBIN_EXIST_PE = 1027,
}output_io_t;
#endif

typedef enum {  
    REAGENT     = 0,    // 试剂
    DILUENT     = 1,    // 稀释液
    CUP         = 2,    // 反应杯
	WASTE_CUP   = 3,    // 反应杯
    WASH_B      = 4,    // 清洗液B（常规清洗液）
	WASH_A      = 5,    // 清洗液A（特殊清洗液）
	WASTE_WATER = 6,    /* 废液 */
	PUNCTURE_NEEDLE = 7,/* 穿刺针穿刺次数 */
	AIR_PUMP = 8,       /* 气源使用时长 */
	REAGENT_QC   = 9,    /* 试剂仓质控 */
} reagent_supply_type_t;

typedef enum {
    TUBE_EMERG = 0,    /* 0.急诊 */
    TUBE_REVIEW,       /* 1.复查 */
    TUBE_QC,           /* 2.试剂仓质控 */
    TUBE_NORMAL,       /* 3.样本/校准/质控 */
} order_priority_type_t;


/* 耗材信息 */
typedef struct consum_info {
    bool                    binit;
    int                     type;
    int                     enable;
    int                     index;
    int                     priority;
    int                     serno;
    char                    strlotno[16];
} consum_info_t;

typedef enum sq_handle_mode {
    ANDLING_MODE_OUTPUT_VAL                         = 0,    // 输出数值
    HANDLING_MODE_OUTPUT_VAL_WITH_LOW_CREDIBILITY   = 1,    // 输出带有低可信度标识的数值
    HANDLING_MODE_OUTPUT_HIDDEN_VAL                 = 2,    // 输出隐藏数值
    HANDLING_MODE_DO_NOT_DETECT_THIS_SAMPLE         = 3,    // 不检测该样本
} sq_handle_mode_t;

/* 样本质量信息 */
typedef struct
{
    int check_anticoagulation_ratio;    /* 是否进行了抗凝比例检查, 1: 是, 0: 否 */
    int ar_error;                       /* 抗凝比例是否异常, 1: 异常, 0: 正常 */
    int ar_handle_mode;                 /* 抗凝比例异常处理方式 */
    int check_hil;                      /* 是否进行了HIL检查, 1: 是, 0:否 */
    int check_hil_hem;                  /* 是否筛查溶血, 1: 是, 0:否 */
    int hemolysis;                      /* 是否存在溶血, 1: 是, 0: 否 */
    int hem_sa;                         /* 溶血检测灵敏度0-5 */
    int hem_handle_mode;                /* 溶血检测处理方式 */
    int check_hil_ict;                  /* 是否筛查黄疸, 1: 是, 0:否 */
    int icterus;                        /* 是否存在黄疸, 1: 是, 0: 否 */
    int ict_sa;                         /* 黄疸检测灵敏度0-5 */
    int ict_handle_mode;                /* 黄疸检测处理方式 */
    int check_hil_lip;                  /* 是否筛查脂血, 1: 是, 0:否 */
    int lipemia;                        /* 是否存在脂血, 1: 是, 0: 否 */
    int lip_sa;                         /* 脂血检测灵敏度0-5 */
    int lip_handle_mode;                /* 脂血检测处理方式 */
    int check_clot;                     /* 是否进行了凝块检查, 1: 是, 0: 否 */
    int clot_error;                     /* 凝块检查是否异常, 1: 异常; 0: 正常 */
    int clot_sa;                        /* 凝块检测灵敏度0-5 */
    int clot_handle_mode;               /* 凝块检测处理方式 */
}sample_quality_t;

typedef enum
{
    OD_SAMPLECOMPLETION     = 0,    /* 加样中 */
    OD_INCUBATING           = 1,    /* 孵育中 */
    OD_DETECTING            = 2,    /* 检测中 */
    OD_COMPLETION           = 3,    /* 检测完成 */
    OD_ERROR                = 4,    /* 检测出错 */
	OD_INIT					= 5    /* 订单初始化 */
}order_state_t;

/* 清洗信息 */
typedef struct
{
    int enable;                 /* 是否清洗, 1: 是, 9: 否 */
    int enable_special_wash;    /* 是否进行特殊清洗, 1: 是, 0: 否 */
}rinse_info_t;

/* 订单试剂加注混匀信息 */
typedef struct
{
    int             reagent_type;   /* 试剂类别 1:Ra; 2:Rb; 3:Rc; 4:Rd; 5:Factor-Deficient Plasma(乏因子血浆) */
    int             pos;            /* 试剂盘试剂位索引 */
    int             bottle_type;    /* 试剂瓶型，暂不确定详细的瓶型类别 */
    rinse_info_t    pre_rinse;      /* 前清洗参数 */
    rinse_info_t    post_rinse;     /* 后清洗参数 */
    int             aspriation_vol; /* 吸取量，单位为uL */
    int             enable_heat;    /* 是否加热, 1: 是, 0: 否 */
    int             enable_mixing;  /* 是否混匀, 1: 是, 0: 否 */
    int             mixing_type;      /* 当enable_mixing为1时使用, 混匀时间, 单位为毫秒 */
    int             mixing_rate;    /* 当enable_mixing为1时使用，混匀速率，指振荡混匀电机的转速，单位为rpm（每分钟转速）*/
}reagent_info_t;

/* 检测结果状态 */
typedef enum
{
    NORMAL                  = 0,    // 正常检测完成
    UN_CLOT                 = 1,    // 未凝固，仅磁珠法适用
    ABNORMAL                = 2,    // 异常，即指强凝，仅针对磁珠法
    AD_OUT_OF_RANGE         = 3,     // 信号值超出上下限，光学与磁珠均可
    NO_BEAD                 = 4     //没有磁珠或样本
}order_result_state_t;

/* 检测结果及信号值 */
typedef struct
{
    order_result_state_t    result_state;   /* 结果状态 */
    int                     detect_pos;     /* 检测位信息： 从1开始 */
    int                     clot_time;      /* 凝固时间，仅磁珠法适用，光学法填0即可，以十分之1秒为单位，如32.1秒表示为321 */
    int                     *AD_data;       /* int数组, 磁珠AD或光学主波长AD值 */
    int                     AD_size;        /* AD_data数 */
    int                     *sub_AD_data;   /* int数组, 光学副波长AD值，仅光学法及存在副波长时使用 */
    int                     sub_AD_size;    /* sub_AD_data数 */    
    char                    cuvette_strno[16];  /* 反应杯盘批号 */
    int                     cuvette_serialno;   /* 反应杯盘序列号 */
}order_result_t;

/* 下位机按钮ID */
typedef enum
{
	BUTTON_EMERGENCY_STOP      = 1,                                 // 紧急停止按钮
    BUTTON_START               = 2,                                 // 开始按钮
    BUTTON_REAGENT_ROTATE      = 3,                                 // 试剂仓旋转
}button_io_t;

typedef struct reag_scan_info {
    int braket_index;                       /* 托架索引 */
    int braket_exist;                       /* 托架是否存在 0:不存在          1:存在 */
    int pos_index;                          /* 试剂位置索引 */
    int pos_iexist_status;                  /* 位置扫描状态 */
    char barcode[SCANNER_BARCODE_LENGTH];   /* 样本条码 */
} reag_scan_info_t;

typedef enum {
    IH_PT_DISCHARGING_WASTE_MODE = 1,
    IH_PT_ALARM_SOUND_MODE
} other_para_t;

/* 工程师调试(虚拟位置)参数,定义来源于thrift文件 */
struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_TT
{
    int iModuleIndex;                      // 模块索引, 待嵌软老师明确
    int iVirtualPosIndex;                  // 虚拟位置索引, 其可能包括一个或多个子位置参数, 待嵌软老师明确
    char *strVirtualPosName;               // 虚拟位置名称
    int iEnableR;                          // 调试位置R是否有效 0: 不生效 1：生效
    int iR_Steps;                          // 调试位置R对应的步数，当iEnableR为1时有效
    int iR_MaxSteps;                       // 调试位置R对应的最大步数，当iEnableR为1时有效
    int iEnableX;                          // 调试位置X是否有效 0: 不生效 1：生效
    int iX_Steps;                          // 调试位置X对应的步数，当iEnableX为1时有效
    int iX_MaxSteps;                       // 调试位置X对应的最大步数，当iEnableX为1时有效
    int iEnableY;                          // 调试位置Y是否有效 0: 不生效 1：生效
    int iY_Steps;                          // 调试位置Y对应的步数，当iEnableY为1时有效
    int iY_MaxSteps;                       // 调试位置Y对应的最大步数，当iEnableY为1时有效
    int iEnableZ;                          // 调试位置Z是否有效 0: 不生效 1：生效
    int iZ_Steps;                          // 调试位置Z对应的步数，当iEnableZ为1时有效
    int iZ_MaxSteps;                       // 调试位置Z对应的最大步数，当iEnableZ为1时有效
};

void thrift_slave_client_connect_ctl(int flag);
int thrift_slave_client_connect_ctl_get();
void thrift_slave_server_connect_ctl(int flag);
int thrift_slave_server_connect_get();
int thrift_salve_heartbeat_flag_get();


/* thrift server初始化 */
void thrift_slave_server_init(const thrift_master_t *thrift_master);
void thrift_slave_server_restart(const char *ip, int port);
/* thrift client初始化 */
void thrift_slave_client_init(const thrift_master_t *thrift_master);

/* 上报上位机调用异步接口的执行结果 user_data:用户数据, exe_state:执行结果, 成功(0), 失败(-1), async_return: 返回值 */
int report_asnyc_invoke_result(int32_t user_data, int32_t exe_state, const async_return_t *async_return);
/* 上报报警信息 alarm_code:报警代码 alarm_info:报警详细信息 */
int report_device_abnormal(alarm_code_t alarm_code, const char *alarm_info);
/* 上传 下位机xx文件 */
int upload_device_file(int32_t iFileType, int32_t iRandNo, const char *hexData, int32_t dataLen,int32_t iSeqNo, int32_t iIsEnd, const char *strMD5);
/* 上传 
    1.下位机部件老化流程 
    iRunType: 运行类型, iTimes: 运行次数; iReserve: 保留位
*/
int report_run_times(int32_t iRunType, int32_t iTimes, int32_t iReserve);
/* 上报试剂信息 reag_prop */
int report_reagent_info(reag_scan_info_t *reag_prop);
/* 上报试剂余量 reag_pos:试剂位置索引 remain_volume:试剂余量，以ul为单位 */
int report_reagent_remain(int reag_pos, int remain_volume, int ord_no);
/* 上报自动标定位置信息，详见ENGINEER_DEBUG_POS_CALIB_T定义 */
int report_position_calibration(int cali_id, int pos_id, int old_v, int new_v);
/* 上报自动标定位置信息，详见ENGINEER_DEBUG_MODULE_PARA_T定义 */
int report_position_calibration_H(struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_TT param);
/* 上报维护项结果 iItemID：维护项ID bStart true.开始 false.结束  bStatus：true.成功 false.失败   */
int report_maintenance_item_result(int item_ID, int param, bool start, bool status);
/* 上报维护结束剩余时间 iRemainTime:剩余时间(单位:秒)  */
int report_maintenance_remain_time(int remain_time);

/* 上报IO状态, 
 * sensor: 传感器编号 
 * state: 
 * 1: 打开(清洗泵或废液泵启动，打开加热等)，存在（如样本管探测），到位（反应装载到位）等; 
 * 0:关闭，不存在，未到位等(此定义不一定合理，针对不类别的IO)
*/
int report_io_state(output_io_t sensor, int state);
/* 试剂仓旋转 bracket_index: 试剂托架编号 1：A；2：B；3：C；4: D；5：E；6: F 其他值无效 */
int report_button_bracket_rotating(int bracket_index);
/* 上报报警信息 alarm_code:报警代码 alarm_info:报警详细信息 */
int report_alarm_message(const int32_t iOrderNo, const char *alarm_info);
/* 上报样本架信息   lstTubeInfos:样本架号、位置、是否存在及条码等信息 */
int report_rack_info(sample_info_t *ample_info);
/* 上报试剂或耗材消耗量，
 * type为REAGENT、DILUENT、WASH_B、WASH_A时, vol指消耗的体积(以μL为单位)
 * type为CUP、WASTE_CUP时, vol指数量（如1、2、3...，以"个"为单位)
 * type为REAGENT、DILUENT, pos指试剂或稀释液位置
 * type为CUP时, pos为1时指反应盘1，为2时指反应盘2
 * type为其它类型时，pos无效
*/
int report_reagent_supply_consume(reagent_supply_type_t type, int pos, int vol);
/* 上报订单检测结果信息 order_no:订单编号 order_result:检测结果及信号值信息 */
int report_order_result(int order_no, const order_result_t *order_result);
/* 上报订单状态 order_no: 订单编号, state: 状态 */
int report_order_state(int order_no,  order_state_t state);
/* 上报样本架拉出 index: 槽号 1 - 6 */
int report_pull_out_rack(uint8_t index);
/* 上报样本槽进架错误 */
int report_rack_push_in_error(uint8_t index, uint8_t err_code);
/* 上报订单检测结束剩余时间 iRemainTime:剩余时间(单位:秒) */
int report_order_remain_checktime(int sec_count);
/* 上报样本质量检测结果 */
int report_sample_quality(int sample_tube_id,  sample_quality_t *sample_quality);


#ifdef __cplusplus
}
#endif

#endif
