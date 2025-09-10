#ifndef __DEVICE_STATUS_COUNT_H__
#define __DEVICE_STATUS_COUNT_H__

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_STATUS_COUNT_LOG  "/root/maccura/log/device_status_count.json" /* 仪器动作计数文件 */
#define DEVICE_STATUS_COUNT_LOG_BACK  "/root/maccura/log/device_status_count.json.back" /* 仪器动作计数文件(备份) */

#define DS_DEVICE_RUN_TIME              1 /* 仪器开机运行时间；开机总时长累积 */
#define DS_COLD_REGENT_TABLE_RUN_TIME   2 /* 试剂仓制冷运行时间；制冷总时长累积 */
#define DS_OPTICAL_LED_RUN_TIME         3 /* 光源LED点亮时间，亮灯总时长累积 */
#define DS_FAN_DETECT_RUN_TIME          4 /* 检测室扇热风扇运行时间；风扇运行总时长累积 */
#define DS_FAN_AIR_PUMP_RUN_TIME        5 /* 磁珠板扇热风扇运行时间；风扇运行总时长累积 */
#define DS_FAN_REGENT_TABLE_RUN_TIME    6 /* 试剂仓扇热风扇运行时间；风扇运行总时长累积 */
#define DS_FAN_MAIN_BOARD_RUN_TIME      7 /* 主控板扇热风扇运行时间；风扇运行总时长累积 */
#define DS_HEAT_INCUBATION_RUN_TIME     8 /* 孵育检测池加热时长；加热总时长累积 */
#define DS_HEAT_MAGNETIC_RUN_TIME       9 /* 磁珠检测池加热时长；加热总时长累积 */
#define DS_HEAT_OPTICAL1_RUN_TIME       10 /* 光学检测池1加热时长；加热总时长累积 */
#define DS_HEAT_R2_RUN_TIME             11 /* 启动试剂R2加热时长；加热总时长累积 */
#define DS_HEAT_REGENT_GLASS_RUN_TIME   12 /* 试剂仓玻璃片加热时长；加热总时长累积 */
#define DS_MAG1_USED_COUNT              19 /* 磁珠检测区1使用计数；每完成一次磁珠检测，计数+1 */
#define DS_MAG2_USED_COUNT              20 /* 磁珠检测区2使用计数；每完成一次磁珠检测，计数+1 */
#define DS_MAG3_USED_COUNT              21 /* 磁珠检测区3使用计数；每完成一次磁珠检测，计数+1 */
#define DS_MAG4_USED_COUNT              22 /* 磁珠检测区4使用计数；每完成一次磁珠检测，计数+1 */
#define DS_OPTICAL1_USED_COUNT          23 /* 光学检测区1使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL2_USED_COUNT          24 /* 光学检测区2使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL3_USED_COUNT          25 /* 光学检测区3使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL4_USED_COUNT          26 /* 光学检测区4使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL5_USED_COUNT          27 /* 光学检测区5使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL6_USED_COUNT          28 /* 光学检测区6使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL7_USED_COUNT          29 /* 光学检测区7使用计数；每完成一次光学检测，计数+1 */
#define DS_OPTICAL8_USED_COUNT          30 /* 光学检测区8使用计数；每完成一次光学检测，计数+1 */
#define DS_S_PIERCE_USED_COUNT          31 /* 穿刺针使用计数；每穿刺一次，计数+1 */
#define DS_S_PUSH_USED_COUNT            32 /* 穿刺针洗针拭子使用计数；每加样一次，计数+1 */
#define DS_R2_USED_COUNT                33 /* 启动试剂针R2使用计数；每加液一次，计数+1 */
#define DS_GRAP_USED_COUNT              34 /* 抓手使用计数；每抓、放一次，计数+1 */
#define DS_INCUBATION_MIX1_USED_COUNT   35 /* 孵育混匀1使用计数；每混匀一次，计数+1 */
#define DS_INCUBATION_MIX2_USED_COUNT   36 /* 孵育混匀2使用计数；每混匀一次，计数+1 */
#define DS_OPTICAL_MIX1_USED_COUNT      37 /* 光学混匀1使用计数；每混匀一次，计数+1 */
#define DS_SCAN_NENERAL_USED_COUNT      38 /* 常规扫码器使用计数；每扫码一次，计数+1 */
#define DS_SCAN_TALBE_USED_COUNT        39 /* 试剂仓扫码器使用计数；每扫码一次，计数+1 */
#define DS_SAMPLER_ELE1_COUNT           40 /* 进样器电磁铁1使用计数：每上锁一次，计数+1 */
#define DS_SAMPLER_ELE2_COUNT           41 /* 进样器电磁铁2使用计数：每上锁一次，计数+1 */
#define DS_SAMPLER_ELE3_COUNT           42 /* 进样器电磁铁3使用计数：每上锁一次，计数+1 */
#define DS_SAMPLER_ELE4_COUNT           43 /* 进样器电磁铁4使用计数：每上锁一次，计数+1 */
#define DS_SAMPLER_ELE5_COUNT           44 /* 进样器电磁铁5使用计数：每上锁一次，计数+1 */
#define DS_SAMPLER_ELE6_COUNT           45 /* 进样器电磁铁6使用计数：每上锁一次，计数+1 */
#define DS_N_CLEAN_FILTER_USED_COUNT    46 /* 普通清洗液过滤器使用计数；下位机仅存初始值 */
#define DS_STMP_FILTER_USED_COUNT       47 /* 暂存池排废过滤器；下位机仅存初始值 */
#define DS_C_CLEAN_FILTER_USED_COUNT    48 /* 洗针池排废过滤器；下位机仅存初始值 */
#define DS_SC_CLEAN_FILTER_USED_COUNT   49 /* 拭子排废过滤器；下位机仅存初始值 */
#define DS_REAG_FILTER_USED_COUNT       50 /* 试剂仓排废过滤器；下位机仅存初始值 */
#define DS_PUMP_S_USED_COUNT            51 /* 样本针柱塞泵使用计数；每吸吐一次，计数+1 */
#define DS_PUMP_5ML_USED_COUNT          52 /* 液路柱塞泵5ML使用计数；每吸吐一次，计数+1 */
#define DS_PUMP_R2_USED_COUNT           53 /* 启动试剂R2柱塞泵使用计数；每吸吐一次，计数+1 */
#define DS_WALVE_Q1_USED_COUNT          54 /* 样本针管路填充泵（Q1）使用计数；每动作一次，计数+1 */
#define DS_WALVE_Q2_USED_COUNT          55 /* 启动试剂针管路填充泵（Q2）使用计数；每动作一次，计数+1 */
#define DS_WALVE_Q4_USED_COUNT          56 /* 洗针池清洗泵（Q4）使用计数；每动作一次，计数+1 */
#define DS_WALVE_Q3_USED_COUNT          57 /* 暂存池清洗泵（Q3）使用计数；每动作一次，计数+1 */
#define DS_WALVE_F2_USED_COUNT          58 /* 暂存池排废泵（F2）使用计数；每动作一次，计数+1 */
#define DS_WALVE_F3_USED_COUNT          59 /* 洗针池排废泵（F3）使用计数；每动作一次，计数+1 */
#define DS_WALVE_F1_USED_COUNT          60 /* 拭子排废泵（F1）使用计数；每动作一次，计数+1 */
#define DS_WALVE_F4_USED_COUNT          61 /* 拭子排废泵2（F4）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV1_USED_COUNT         62 /* 样本针柱塞泵填充阀（SV1）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV2_USED_COUNT         63 /* 样本针柱塞泵排气泡阀（SV2）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV4_USED_COUNT         64 /* 特殊清洗液柱塞泵阀（SV4）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV3_USED_COUNT         65 /* 启动试剂针柱塞泵阀SV（SV3）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV5_USED_COUNT         66 /* 特殊清洗液进液阀（SV5）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV6_USED_COUNT         67 /* 试剂仓排冷凝水阀（SV6）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV9_USED_COUNT         68 /* 暂存池特殊清洗液阀（SV9）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV10_USED_COUNT        69 /* 洗针池特殊清洗液阀（SV10）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV7_USED_COUNT         70 /* 拭子清洗液阀（SV7）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV8_USED_COUNT         71 /* 特殊清洗液管路阀（SV8）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV12_USED_COUNT        72 /* 洗针池排废阀（SV12）使用计数；每动作一次，计数+1 */
#define DS_WALVE_SV11_USED_COUNT        73 /* 暂存池排废阀（SV11）使用计数；每动作一次，计数+1 */

#define DS_COUNT_MAX                    74
#define DS_ALL                          65535 /* 设置所有计数或计时值（重置计数） */

void device_status_reset_flag_set(int data);
void device_status_count_add(int index, uint32_t data);
uint32_t device_status_cur_count_get(int index);
void device_status_cur_count_set(int index, uint32_t data);
uint32_t device_status_total_count_get(int index);
void device_status_total_count_set(int index, uint32_t data);
void device_status_count_gpio_check(int gpio_index);
void device_status_count_scanner_check(int scan_type);
int device_status_reset();
int device_status_count_load_file();
int device_status_count_update_file();
int device_status_count_init();

#ifdef __cplusplus
}
#endif

#endif

