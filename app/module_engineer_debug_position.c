#include <log.h>
#include <module_common.h>
#include <thrift_service_software_interface.h>
#include <module_engineer_debug_position.h>
#include <module_monitor.h>

static eng_is_run_t engineer_run_stat = ENGINEER_NOT_RUN;

/* 临时改变指定电机速度及加速度 */
int motor_para_set_temp(int motor_id, const thrift_motor_para_t *motor_para)
{
    memcpy(&h3600_conf_get()->motor[motor_id], motor_para, sizeof(thrift_motor_para_t));
    return 0;
}

/* 实现 样本针 标定流程。 返回值 0:成功 -1:失败 */
static int engineer_debug_pos_s(void *arg)
{
    engineer_debug_calibration_pos_t *debug_pos = (engineer_debug_calibration_pos_t *)arg;
    int ret = 0;

    if (ret == 0 && debug_pos->z_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(debug_pos->z_motor, 1);
        FAULT_CHECK_END();
    }

    /* 有干涉电机 让位 (需定制化代码) */


    /* 关联电机R 到位 */
    if (ret == 0 && debug_pos->r_enable == 1 && debug_pos->r_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->r_motor, *(debug_pos->r_steps));
        FAULT_CHECK_END();
    }

    /* 自身电机 到位 */
    if (ret == 0 && (debug_pos->x_enable == 1 || debug_pos->r_enable == 1) && debug_pos->x_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->x_motor, *(debug_pos->x_steps));
        FAULT_CHECK_END();
    }

    if (ret == 0 && debug_pos->y_enable == 1 && debug_pos->y_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->y_motor, *(debug_pos->y_steps));
        FAULT_CHECK_END();
    }

    LOG("ret:%d\n", ret);
    return ret;
}

/* 实现 启动试剂针 标定流程。 返回值 0:成功 -1:失败 */
static int engineer_debug_pos_r2(void *arg)
{
    engineer_debug_calibration_pos_t *debug_pos = (engineer_debug_calibration_pos_t *)arg;
    int ret = 0;

    if (ret == 0 && debug_pos->z_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(debug_pos->z_motor, 1);
        FAULT_CHECK_END();
    }

    /* 有干涉电机 让位 (需定制化代码) */


    /* 关联电机R 到位 */
    if (ret == 0 && debug_pos->r_enable == 1 && debug_pos->r_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->r_motor, *(debug_pos->r_steps));
        FAULT_CHECK_END();
    }

    /* 自身电机 到位 */
    if (ret == 0 && debug_pos->y_enable == 1 && debug_pos->y_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->y_motor, *(debug_pos->y_steps));
        FAULT_CHECK_END();
    }

    LOG("ret:%d\n", ret);
    return ret;
}

/* 实现   抓手 标定流程。 返回值 0:成功 -1:失败 */
static int engineer_debug_pos_catcher(void *arg)
{
    engineer_debug_calibration_pos_t *debug_pos = (engineer_debug_calibration_pos_t *)arg;
    int ret = 0;

    if (ret == 0 && debug_pos->z_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(debug_pos->z_motor, 1);
        FAULT_CHECK_END();
    }

    /* 有干涉电机 让位 (需定制化代码) */
    if (ret == 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(MOTOR_NEEDLE_S_Y, h3600_conf_get()->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_1]);
        FAULT_CHECK_END();
    }

    /* 关联电机 到位 */
    if (ret == 0 && debug_pos->r_enable == 1 && debug_pos->r_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->r_motor, *(debug_pos->r_steps));
        FAULT_CHECK_END();
    }

    /* 自身电机 到位 */
    if (ret == 0 && debug_pos->x_enable == 1 && debug_pos->x_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->x_motor, *(debug_pos->x_steps));
        FAULT_CHECK_END();
    }

    if (ret == 0 && debug_pos->y_enable == 1 && debug_pos->y_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->y_motor, *(debug_pos->y_steps));
        FAULT_CHECK_END();
    }

    LOG("ret:%d\n", ret);
    return ret;
}

/* 实现 试剂盘 标定流程。返回值 0:成功 -1:失败 */
static int engineer_debug_pos_reagent_table(void *arg)
{
    engineer_debug_calibration_pos_t *debug_pos = (engineer_debug_calibration_pos_t *)arg;
    int ret = 0;

    if (ret == 0 && debug_pos->z_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_reset(debug_pos->z_motor, 1);
        FAULT_CHECK_END();
    }

    /* 有干涉电机 让位 (需定制化代码) */


    /* 关联电机 到位 */
    if (ret == 0 && debug_pos->r_enable == 1 && debug_pos->r_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->r_motor, *(debug_pos->r_steps));
        FAULT_CHECK_END();
    }

    if (ret == 0 && debug_pos->x_enable == 1 && debug_pos->x_motor != 0) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        ret = thrift_motor_move_to(debug_pos->x_motor, *(debug_pos->x_steps));
        FAULT_CHECK_END();
    }

    LOG("ret:%d\n", ret);
    return ret;
}

/* 工程师调试 位置参数表 */
const engineer_debug_calibration_pos_t engineer_debug_pos_tbl[] = 
{
    {10,1,"样本针",       "取样位1",               0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_SAMPLE_NOR_1, H3600_CONF_POS_S_SAMPLE_NOR_1, H3600_CONF_POS_S_SAMPLE_NOR_1),                60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,2,"样本针",       "取样位10",              0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_SAMPLE_NOR_10, H3600_CONF_POS_S_SAMPLE_NOR_10, H3600_CONF_POS_S_SAMPLE_NOR_10),             60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,3,"样本针",       "取样位60",              0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_SAMPLE_NOR_60, H3600_CONF_POS_S_SAMPLE_NOR_60, H3600_CONF_POS_S_SAMPLE_NOR_60),             60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,4,"样本针",       "加样位",                0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_SAMPLE_ADD_PRE, H3600_CONF_POS_S_SAMPLE_ADD_PRE, H3600_CONF_POS_S_SAMPLE_ADD_PRE),          60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,5,"样本针",       "孵育混匀位1",             0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_SAMPLE_ADD_MIX1, H3600_CONF_POS_S_SAMPLE_ADD_MIX1, H3600_CONF_POS_S_SAMPLE_ADD_MIX1),       60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,6,"样本针",       "孵育混匀位2",             0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_SAMPLE_ADD_MIX2, H3600_CONF_POS_S_SAMPLE_ADD_MIX2, H3600_CONF_POS_S_SAMPLE_ADD_MIX2),       60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,7,"样本针",       "内圈取R1试剂位",           1,1,1,1, POS_ONE(MOTOR_REAGENT_TABLE,H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_REAGENT_TABLE_IN, H3600_CONF_POS_S_REAGENT_TABLE_IN, H3600_CONF_POS_S_REAGENT_TABLE_IN),     60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,8,"样本针",       "外圈取R1试剂位",           1,1,1,1, POS_ONE(MOTOR_REAGENT_TABLE,H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_REAGENT_TABLE_OUT, H3600_CONF_POS_S_REAGENT_TABLE_OUT, H3600_CONF_POS_S_REAGENT_TABLE_OUT), 60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,9,"样本针",       "样本洗针位",              0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_CLEAN, H3600_CONF_POS_S_CLEAN, H3600_CONF_POS_S_CLEAN),                                     60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,10,"样本针",      "样本缓存位",              0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_TEMP, H3600_CONF_POS_S_TEMP, H3600_CONF_POS_S_TEMP),                                        60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,11,"样本针",      "稀释液位1",              0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_DILU_1, H3600_CONF_POS_S_DILU_1, H3600_CONF_POS_S_DILU_1),                                  60000,60000,60000, 0,0,0, engineer_debug_pos_s},
    {10,12,"样本针",      "稀释液位2",              0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_S_X,MOTOR_NEEDLE_S_Y,MOTOR_NEEDLE_S_Z,H3600_CONF_POS_S_DILU_2, H3600_CONF_POS_S_DILU_2, H3600_CONF_POS_S_DILU_2),                                  60000,60000,60000, 0,0,0, engineer_debug_pos_s},

    {11,1,"R2试剂针",     "内圈取R2试剂位",           1,0,1,1, POS_ONE(MOTOR_REAGENT_TABLE,H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN), POS_LOAD(MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Z,H3600_CONF_POS_R2_REAGENT_IN, H3600_CONF_POS_R2_REAGENT_IN, H3600_CONF_POS_R2_REAGENT_IN),                60000,60000,60000, 0,0,0, engineer_debug_pos_r2},
    {11,2,"R2试剂针",     "外圈取R2试剂位",           1,0,1,1, POS_ONE(MOTOR_REAGENT_TABLE,H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT), POS_LOAD(MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Z,H3600_CONF_POS_R2_REAGENT_OUT, H3600_CONF_POS_R2_REAGENT_OUT, H3600_CONF_POS_R2_REAGENT_OUT),            60000,60000,60000, 0,0,0, engineer_debug_pos_r2},
    {11,3,"R2试剂针",     "光学混匀位",              0,0,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Z,H3600_CONF_POS_R2_MIX_1, H3600_CONF_POS_R2_MIX_1, H3600_CONF_POS_R2_MIX_1),                               60000,60000,60000, 0,0,0, engineer_debug_pos_r2},
    {11,4,"R2试剂针",     "磁珠位1",               0,0,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Z,H3600_CONF_POS_R2_MAG_1, H3600_CONF_POS_R2_MAG_1, H3600_CONF_POS_R2_MAG_1),                               60000,60000,60000, 0,0,0, engineer_debug_pos_r2},
    {11,5,"R2试剂针",     "磁珠位4",               0,0,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Z,H3600_CONF_POS_R2_MAG_4, H3600_CONF_POS_R2_MAG_4, H3600_CONF_POS_R2_MAG_4),                               60000,60000,60000, 0,0,0, engineer_debug_pos_r2},
    {11,6,"R2试剂针",     "洗针位",                0,0,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Y,MOTOR_NEEDLE_R2_Z,H3600_CONF_POS_R2_CLEAN, H3600_CONF_POS_R2_CLEAN, H3600_CONF_POS_R2_CLEAN),                               60000,60000,60000, 0,0,0, engineer_debug_pos_r2},

    {12,1,"反应杯抓手",   "加样位",                  0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_PRE,H3600_CONF_POS_C_PRE,H3600_CONF_POS_C_PRE),                                                60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,2,"反应杯抓手",   "孵育混匀位1",               0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_MIX_1,H3600_CONF_POS_C_MIX_1,H3600_CONF_POS_C_MIX_1),                                          60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,3,"反应杯抓手",   "孵育混匀位2",               0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_MIX_2,H3600_CONF_POS_C_MIX_2,H3600_CONF_POS_C_MIX_2),                                          60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,4,"反应杯抓手",   "孵育位1",                 0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_INCUBATION1,H3600_CONF_POS_C_INCUBATION1,H3600_CONF_POS_C_INCUBATION1),                        60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,5,"反应杯抓手",   "孵育位10",                0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_INCUBATION10,H3600_CONF_POS_C_INCUBATION10,H3600_CONF_POS_C_INCUBATION10),                     60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,6,"反应杯抓手",   "孵育位30",                0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_INCUBATION30,H3600_CONF_POS_C_INCUBATION30,H3600_CONF_POS_C_INCUBATION30),                     60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,7,"反应杯抓手",   "磁珠位1",                 0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_MAG_1,H3600_CONF_POS_C_MAG_1,H3600_CONF_POS_C_MAG_1),                                          60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,8,"反应杯抓手",   "磁珠位4",                 0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_MAG_4,H3600_CONF_POS_C_MAG_4,H3600_CONF_POS_C_MAG_4),                                          60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,9,"反应杯抓手",   "光学混匀位",                0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_OPTICAL_MIX,H3600_CONF_POS_C_OPTICAL_MIX,H3600_CONF_POS_C_OPTICAL_MIX),                        60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,10,"反应杯抓手",  "光学检测位1",               0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_OPTICAL_1,H3600_CONF_POS_C_OPTICAL_1,H3600_CONF_POS_C_OPTICAL_1),                              60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,11,"反应杯抓手",  "光学检测位8",               0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_OPTICAL_8,H3600_CONF_POS_C_OPTICAL_8,H3600_CONF_POS_C_OPTICAL_8),                              60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,12,"反应杯抓手",  "丢杯位",                  0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_DETACH,H3600_CONF_POS_C_DETACH,H3600_CONF_POS_C_DETACH),                                       60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},
    {12,13,"反应杯抓手",  "进杯位",                  0,1,1,1, POS_ONE(0,0), POS_LOAD(MOTOR_CATCHER_X,MOTOR_CATCHER_Y,MOTOR_CATCHER_Z,H3600_CONF_POS_C_CUVETTE,H3600_CONF_POS_C_CUVETTE,H3600_CONF_POS_C_CUVETTE),                                    60000,60000,60000, 0,0,0, engineer_debug_pos_catcher},

    {13,1,"试剂存储",     "内圈取R2试剂位",            0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN,0,0),                                                                                            60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
    {13,2,"试剂存储",     "外圈取R2试剂位",            0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT,0,0),                                                                                           60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
    {13,3,"试剂存储",     "内圈取R1试剂位",            0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN,0,0),                                                                                             60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
    {13,4,"试剂存储",     "外圈取R1试剂位",            0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT,0,0),                                                                                            60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
    {13,5,"试剂存储",     "试剂混匀位内",              0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_IN,0,0),                                                                                           60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
    {13,6,"试剂存储",     "试剂混匀位外",              0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_OUT,0,0),                                                                                          60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
    {13,7,"试剂存储",     "试剂仓扫码位",              0,1,0,0, POS_ONE(0,0), POS_LOAD(MOTOR_REAGENT_TABLE,0,0,H3600_CONF_POS_REAGENT_TABLE_FOR_SCAN,0,0),                                                                                             60000,60000,60000, 0,0,0, engineer_debug_pos_reagent_table},
};

/* 执行标定指令 */
int engineer_debug_pos_run(struct ENGINEER_DEBUG_MOTOR_PARA_TT *motor_param)
{
    int i = 0;
    int ret = 0;
    int motor_id = 0;

    if (motor_param == NULL) {
        LOG("motor_param is NULL\n");
        return -1;
    }

    for (i=0; i<sizeof(engineer_debug_pos_tbl)/sizeof(engineer_debug_pos_tbl[0]); i++) {
        if (motor_param->iModuleIndex==engineer_debug_pos_tbl[i].module_idx && motor_param->iVirtualPosIndex==engineer_debug_pos_tbl[i].virtual_pos_idx) {
            if (motor_param->eAxisType == R_AXIS) {
                motor_id = engineer_debug_pos_tbl[i].r_motor;
            } else if (motor_param->eAxisType == X_AXIS) {
                motor_id = engineer_debug_pos_tbl[i].x_motor;
            } else if (motor_param->eAxisType == Y_AXIS) {
                motor_id = engineer_debug_pos_tbl[i].y_motor;
            } else if (motor_param->eAxisType == Z_AXIS) {
                motor_id = engineer_debug_pos_tbl[i].z_motor;
            }

            switch (motor_param->eActionType) {
            case MOVE_TO:
                if (engineer_debug_pos_tbl[i].hander) {
                    ret = engineer_debug_pos_tbl[i].hander((void*)&engineer_debug_pos_tbl[i]);
                } else {
                    ret = -1;
                }
                break;
            case MOVE:
                ret = thrift_motor_move(motor_id, motor_param->iSteps);
                break;
            case RESET:
                if ((motor_param->eAxisType==X_AXIS || motor_param->eAxisType==Y_AXIS) && engineer_debug_pos_tbl[i].z_motor != 0) {
                    ret = thrift_motor_reset(engineer_debug_pos_tbl[i].z_motor, 0);
                }

                if (ret == 0) {
                    ret = thrift_motor_reset(motor_id, 0);
                }
                break;
            case POWER_ON_RESET:
                if ((motor_param->eAxisType==X_AXIS || motor_param->eAxisType==Y_AXIS) && engineer_debug_pos_tbl[i].z_motor != 0) {
                    ret = thrift_motor_reset(engineer_debug_pos_tbl[i].z_motor, 1);
                }

                if (ret == 0) {
                    ret = thrift_motor_reset(motor_id, 1);
                }
                break;
            default:
                LOG("not support type:%d\n", motor_param->eActionType);
                ret = -1;
                break;
            }

            break;
        }
    }

    if (i >= sizeof(engineer_debug_pos_tbl)/sizeof(engineer_debug_pos_tbl[0])) {
        LOG("can not find cali pos:%d,%d\n", motor_param->iModuleIndex, motor_param->iVirtualPosIndex);
        ret = -1;
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        LOG("detect fault\n");
        ret = -1;
    }

    return ret;
}

void engineer_is_run_set(eng_is_run_t run_stat)
{
    engineer_run_stat = run_stat;
}

eng_is_run_t engineer_is_run_get()
{
    return engineer_run_stat;
}

/* 统计所有模块的所有标定值个数 */
int engineer_debug_pos_all_count(void)
{
    int i = 0;
    int count = 0;

    /* 统计 模块的所有虚拟位置个数 */
    for (i=0; i<sizeof(engineer_debug_pos_tbl)/sizeof(engineer_debug_pos_tbl[0]); i++) {
        count++;
    }

    return count;
}

/* 统计 某模块的所有标定值个数 */
int engineer_debug_pos_count(int module_idx)
{
    int i = 0;
    int count = 0;

    /* 统计 模块的所有虚拟位置个数 */
    for (i=0; i<sizeof(engineer_debug_pos_tbl)/sizeof(engineer_debug_pos_tbl[0]); i++) {
        if (module_idx == engineer_debug_pos_tbl[i].module_idx) {
            count++;
        }
    }

    return count;
}

/* 获取 某模块的所有标定值内容 */
int engineer_debug_pos_get(int module_idx, int count, struct ENGINEER_DEBUG_MODULE_PARA_TT *module_param)
{
    int i = 0, j = 0;
    int ret = 0;

    if (module_param == NULL) {
        LOG("motor_param is NULL\n");
        return -1;
    }

    /* 找出 模块的所有虚拟位置 */
    for (i=0; i<sizeof(engineer_debug_pos_tbl)/sizeof(engineer_debug_pos_tbl[0]); i++) {
        if (module_idx == engineer_debug_pos_tbl[i].module_idx) {
            /* 自身组件位置数据, 关联组件为R */
            module_param[j].tVirautlPosPara.iModuleIndex = engineer_debug_pos_tbl[i].module_idx;
            module_param[j].tVirautlPosPara.iVirtualPosIndex = engineer_debug_pos_tbl[i].virtual_pos_idx;
            module_param[j].tVirautlPosPara.strVirtualPosName = engineer_debug_pos_tbl[i].virtual_pos_name;
            module_param[j].tVirautlPosPara.iEnableR = engineer_debug_pos_tbl[i].r_enable;/* R轴是关联组件 */
            module_param[j].tVirautlPosPara.iR_Steps = *(engineer_debug_pos_tbl[i].r_steps);/* R轴是关联组件 */
            module_param[j].tVirautlPosPara.iR_MaxSteps = engineer_debug_pos_tbl[i].x_steps_max;/* R轴是关联组件 */
            module_param[j].tVirautlPosPara.iEnableX = engineer_debug_pos_tbl[i].x_enable;
            module_param[j].tVirautlPosPara.iX_Steps = *(engineer_debug_pos_tbl[i].x_steps);
            module_param[j].tVirautlPosPara.iX_MaxSteps = engineer_debug_pos_tbl[i].x_steps_max;
            module_param[j].tVirautlPosPara.iEnableY = engineer_debug_pos_tbl[i].y_enable;
            module_param[j].tVirautlPosPara.iY_Steps = *(engineer_debug_pos_tbl[i].y_steps);
            module_param[j].tVirautlPosPara.iY_MaxSteps = engineer_debug_pos_tbl[i].y_steps_max;
            module_param[j].tVirautlPosPara.iEnableZ = engineer_debug_pos_tbl[i].z_enable;
            module_param[j].tVirautlPosPara.iZ_Steps = *(engineer_debug_pos_tbl[i].z_steps);
            module_param[j].tVirautlPosPara.iZ_MaxSteps = engineer_debug_pos_tbl[i].z_steps_max;

            if (++j >= count) {
                LOG("find finish\n");
                break;
            }
        }
    }

    return ret;
}

/* 设置并保存标定值 */
int engineer_debug_pos_set(const struct ENGINEER_DEBUG_MODULE_PARA_TT *module_param)
{
    int i = 0;
    int ret = 0;

    if (module_param == NULL) {
        LOG("motor_param is NULL\n");
        return -1;
    }

    /* 找出 模块的特定虚拟位置 */
    for (i=0; i<sizeof(engineer_debug_pos_tbl)/sizeof(engineer_debug_pos_tbl[0]); i++) {
        if (module_param->tVirautlPosPara.iModuleIndex == engineer_debug_pos_tbl[i].module_idx && 
            module_param->tVirautlPosPara.iVirtualPosIndex == engineer_debug_pos_tbl[i].virtual_pos_idx) {
            if (module_param->tVirautlPosPara.iEnableR == 1) {
                *(engineer_debug_pos_tbl[i].r_steps) =  module_param->tVirautlPosPara.iR_Steps;
            }

            if (module_param->tVirautlPosPara.iEnableX == 1) {
                *(engineer_debug_pos_tbl[i].x_steps) =  module_param->tVirautlPosPara.iX_Steps;
            }

            if (module_param->tVirautlPosPara.iEnableY == 1) {
                *(engineer_debug_pos_tbl[i].y_steps) = module_param->tVirautlPosPara.iY_Steps;
            }

            if (module_param->tVirautlPosPara.iEnableZ == 1) {
                *(engineer_debug_pos_tbl[i].z_steps) = module_param->tVirautlPosPara.iZ_Steps;
            }

            break;
        }
    }

    /* 保存到json文件 */
    thrift_engineer_position_set();

    return ret;
}

void engineer_needle_pos_report(int module_idx, int pos_idx, int x, int y, int z, int r)
{
    int i = 0;
    struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_TT param = {0};

    for (i = 0; i < sizeof(engineer_debug_pos_tbl) / sizeof(engineer_debug_pos_tbl[0]); i++) {
        if (module_idx == engineer_debug_pos_tbl[i].module_idx && pos_idx == engineer_debug_pos_tbl[i].virtual_pos_idx) {
            param.iModuleIndex = engineer_debug_pos_tbl[i].module_idx;
            param.iVirtualPosIndex = engineer_debug_pos_tbl[i].virtual_pos_idx;
            param.strVirtualPosName = engineer_debug_pos_tbl[i].virtual_pos_name;
            param.iEnableR = engineer_debug_pos_tbl[i].r_enable;
            if (module_idx == 11 && (pos_idx == 1 || pos_idx == 2)) {
                /* r2试剂仓内外位置会调整 */
                param.iR_Steps = r;
            } else {
                param.iR_Steps = *(engineer_debug_pos_tbl[i].r_steps);
            }
            param.iR_MaxSteps = engineer_debug_pos_tbl[i].x_steps_max;
            param.iEnableX = engineer_debug_pos_tbl[i].x_enable;
            param.iX_Steps = x;
            param.iX_MaxSteps = engineer_debug_pos_tbl[i].x_steps_max;
            param.iEnableY = engineer_debug_pos_tbl[i].y_enable;
            param.iY_Steps = y;
            param.iY_MaxSteps = engineer_debug_pos_tbl[i].y_steps_max;
            param.iEnableZ = engineer_debug_pos_tbl[i].z_enable;
            param.iZ_Steps = z;
            param.iZ_MaxSteps = engineer_debug_pos_tbl[i].z_steps_max;
        }
    }
    report_position_calibration_H(param);
}

