#ifndef H3600_MODULE_ENGINEER_DEBUG_CMD_H
#define H3600_MODULE_ENGINEER_DEBUG_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*engineer_cmd_callback_t)(void *arg);

/* 工程师调试 调试命令参数, 定义来源于<工程师调试模式模块及电机编号索引.xlxs>文件的调试表 */
typedef struct 
{
    int module_idx; /* 模块索引 */
    char *module_name; /* 模块名称 */
    int cmd_idx; /* 模块调试命令索引 */
    char *module_cmd_name; /* 模块调试命令名称 */
    engineer_cmd_callback_t hander; /* 调试命令实现函数 */
}engineer_debug_debug_cmd_t;

/* 调试指令参数, 定义来源于thrift文件 */
struct ENGINEER_DEBUG_CMD_TT
{
    int iModuleIndex; /* 模块索引 */
    int iCmd; /* 模块调试命令索引 */
    int userdata;
};

/* 调试指令执行返回结果, 定义来源于thrift文件 */
struct ENGINEER_DEBUG_RUN_RESULT_TT
{
    int iRunResult;                             // 执行结果：0：成功，其它值：失败，其值为对应的错误码
    char strBarcode[SCANNER_BARCODE_LENGTH];    // 条码内容，仅执行读取条码时适用，当读取条码成功时，其值为读取的条码内容
};

/* 老化测试参数, 定义来源于thrift文件 */
typedef struct
{
    int test_enable; /* （总开关）是否启动下位机部件老化：1：启动；0：停止 */
    int loop_count; /* 循环次数 */

    int sampler_enable; /* 是否启用进样器老化：1：启动；0：停止 */
    int regeant_table_enable; /* 是否启用试剂仓老化：1：启动；0：停止 */
    int grap_a_enable; /* 是否启用抓手A老化：1：启动；0：停止 */
    int grap_b_enable; /* 是否启用抓手A老化：1：启动；0：停止 */
    int grap_c_enable; /* 是否启用抓手A老化：1：启动；0：停止 */
    int s_enable; /* 是否启用样本针老化：1：启动；0：停止 */
    int r1_enable; /* 是否启用孵育试剂针老化：1：启动；0：停止 */
    int r2_enable; /* 是否启用启动试剂针老化：1：启动；0：停止 */
    int online_load_enable; /* 是否启用在线装载老化：1：启动；0：停止 */
    int belt_enable; /* 是否启用反应杯转运皮带老化：1：启动；0：停止 */
    int mix_enable; /* 是否启用反应杯混匀老化：1：启动；0：停止 */
    int cuvette_enable; /* 是否启用反应杯杯盘老化：1：启动；0：停止 */

    int32_t userdata;
}thrift_engineer_aging_test_t;

int engineer_debug_cmd_run(struct ENGINEER_DEBUG_CMD_TT *engineer_cmd_param, struct ENGINEER_DEBUG_RUN_RESULT_TT *reuslt);
int engineer_aging_test_run(thrift_engineer_aging_test_t *engineer_cmd_param);

#ifdef __cplusplus
}
#endif

#endif

