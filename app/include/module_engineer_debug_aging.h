#ifndef H3600_MODULE_ENGINEER_DEBUG_AGING_H
#define H3600_MODULE_ENGINEER_DEBUG_AGING_H

#ifdef __cplusplus
extern "C" {
#endif

/* 下位机部件老化参数，源于thrift文件 */
typedef struct
{
    int enable;                            /* 是否启动下位机部件老化：1：启动；0：停止 */
    int loop_cnt;                          /* 循环次数合理范围：仅当iIsOnOrOFF为1时有效，有效值为1-10000 */
    int sampler_enable;                    /* 是否启用进样器老化：2103无效，与2102保持一致遗留 */
    int reag_enable;                       /* 是否启用试剂仓老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效 */
    int catcher_enable;                    /* 是否启用抓手老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效 */
    int needle_s_enable;                   /* 是否启用样本针老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效 */
    int needle_r2_enable;                  /* 是否启用启动试剂针老化：1：启动；0：停止。仅当iIsOnOrOFF为1时有效 */
    int mix_enable;                        /* 是否启用反应杯混匀混匀：2103无效，与2102保持一致遗留 */
    int cuvette_enable;                    /* 是否启用反应盘加载：2103无效，与2102保持一致遗留 */
}engineer_debug_aging_t;

typedef struct
{
    engineer_debug_aging_t aging;
    int userdata;
}engineer_debug_aging_param_t;

void engineer_aging_run_set(int flag);
int engineer_aging_test(engineer_debug_aging_t *aging_param);

#ifdef __cplusplus
}
#endif

#endif

