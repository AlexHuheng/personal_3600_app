#ifndef H3600_MODULE_ENGINEER_DEBUG_WEIGH_H
#define H3600_MODULE_ENGINEER_DEBUG_WEIGH_H

#ifdef __cplusplus
extern "C" {
#endif

/* 称量类型needle_type定义,来源于 thrift原始定义文件 */
#define NT_SAMPLE_NORMAL_ADDING_WITHOUT_PUNCTURE                0  // 样本针常规加样（平头针）
#define NT_SAMPLE_DILUENT_ADDING_WITHOUT_PUNCTURE               1  // 样本针稀释加样（平头针）
#define NT_SAMPLE_NORMAL_ADDING_WITH_PUNCTURE                   2  // 样本针常规加样（穿刺针）
#define NT_SAMPLE_DILUENT_ADDING_WITH_PUNCTURE                  3  // 样本针稀释加样（穿刺针）
#define NT_R1_ADDING                                            4  // 试剂针R1加样
#define NT_R2_ADDING                                            5  // 试剂针R2加样
#define NT_SAMPLE_TEMP_ADDING_WITH_PUNCTURE                     6  // 样本针暂存加样（穿刺针）
#define NT_SAMPLE_MUTI_NORMAL_ADDING_WITH_PUNCTURE              7  // 样本针批量常规加样（穿刺针）
#define NT_SAMPLE_MUTI_TEMP_ADDING_WITH_PUNCTURE                8  // 样本针批量暂存加样（穿刺针）
#define NT_R2_MUTI_ADDING                                       9  // 试剂针R2批量加样

/* 称量参数 */
typedef struct
{
    int needle_type; /* 针类型 */
    int sample_reagent_vol;  /* 样本或试剂加注体积，为0时表示不加注 */
    int diulent_vol;  /* 稀释液加注体积，为0时表示不加注  */
    int cups;  /* 反应杯一次称量个数 */
    int userdata;
}engineer_weighing_param_t;

int needle_s_weigh(int needle_take_ul);
int needle_s_dilu_weigh(int needle_take_dilu_ul, int needle_take_ul);
int needle_s_sp_weigh(int needle_take_ul);
int needle_r2_weigh(int needle_take_ul);
int needle_s_muti_weigh(int needle_take_ul);
int needle_s_sp_muti_weigh(int needle_take_ul);
int needle_r2_muti_weigh(int needle_take_ul);

#ifdef __cplusplus
}
#endif

#endif

