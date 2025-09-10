#ifndef H3600_MODULE_ENGINEER_DEBUG_POSITION_H
#define H3600_MODULE_ENGINEER_DEBUG_POSITION_H

#include "thrift_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

extern h3600_conf_t h3600_conf;
/* const变量不允许使用函数来引用全局变量，编译器会报错 */
#define POS_ONE(a,pos) (a),&h3600_conf.motor_pos[(a)][(pos)]
#define POS_LOAD(x,y,z,pos1,pos2,pos3) (x),(y),(z),&h3600_conf.motor_pos[(x)][(pos1)],&h3600_conf.motor_pos[(y)][(pos2)],&h3600_conf.motor_pos[(z)][(pos3)]

typedef int (*engineer_move_callback_t)(void *arg);

/* 工程师调试位置参数, 定义来源于<工程师调试模式模块及电机编号索引.xlxs>文件 */
typedef struct 
{
    /* 自身组件 参数 */
    int module_idx; /* 模块索引 */
    int virtual_pos_idx; /* 虚拟位置索引 */
    char *module_name; /* 模块名称 */
    char *virtual_pos_name; /* 虚拟位置名称 */

    int r_enable; /* R-ENABLE, R轴就是X轴 */
    int x_enable; /* X-ENABLE */
    int y_enable; /* Y-ENABLE */
    int z_enable; /* Z-ENABLE */

    int r_motor; /* R电机号 */
    int *r_steps; /* R默认值 */

    int x_motor; /* X电机号 */
    int y_motor; /* Y电机号 */
    int z_motor; /* Z电机号 */

    int *x_steps; /* X默认值 */
    int *y_steps; /* Y默认值 */
    int *z_steps; /* Z默认值 */

    int x_steps_max; /* X最大值 */
    int y_steps_max; /* Y最大值 */
    int z_steps_max; /* Z最大值 */
 
    int x_steps_min; /* X最小值 */
    int y_steps_min; /* Y最小值 */
    int z_steps_min; /* Z最小值 */

    engineer_move_callback_t hander; /* 组件运动实现函数 */
}engineer_debug_calibration_pos_t;

/* 工程师调试(R、X、Y、Z轴)类型,定义来源于thrift文件 */
enum ENGINEER_DEBUG_AXIS_TYPE_TT
{
    R_NONE = 0, // 无，表示此参数无效
    R_AXIS = 1, // R轴
    X_AXIS = 2, // X轴
    Y_AXIS = 3, // Y轴
    Z_AXIS = 4, // Z轴
};

/* 工程师调试指令（（正向或反向）移动、移动到、正常复位、上电复位、液面探测）类型,定义来源于thrift文件 */
enum ENGINEER_DEBUG_ACTION_TYPE_TT
{
    EDAT_NONE       = 0,    // 无，表示此参数无效
    MOVE            = 1,    // （正向或反向）移动
    MOVE_TO         = 2,    // 移动到
    RESET           = 3,    // 正常复位
    POWER_ON_RESET  = 4,    // 上电复位
    LIQUID_DETECT   = 5,    // 液面探测
};

/* 工程师调试(步进或伺服)电机参数,定义来源于thrift文件 */
struct ENGINEER_DEBUG_MOTOR_PARA_TT
{
    int iModuleIndex;                               // 模块索引, 待嵌软老师明确
    int iVirtualPosIndex;                           // 虚拟位置索引, 其可能包括一个或多个子位置参数, 待嵌软老师明确
    enum ENGINEER_DEBUG_AXIS_TYPE_TT eAxisType;     // 轴类型
    enum ENGINEER_DEBUG_ACTION_TYPE_TT eActionType; // 调试指令
    int iSteps;                                     // 步数，仅在eActionType为MOVE、MOVE_TO有效，其它情况无效
    int userdata;
};

/* 工程师调试(模块位置)参数,定义来源于thrift文件 */
struct ENGINEER_DEBUG_MODULE_PARA_TT
{
    struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_TT tVirautlPosPara;  // 工程师调试(虚拟位置)参数
    int iIsExistRelativeViraultPosPara;                         // 是否存在关联的虚拟位置参数 0：不存在 1：存在
    struct ENGINEER_DEBUG_VIRTUAL_POS_PARA_TT tRelativeVirautlPosPara;// 关联的工程师调试(虚拟位置)参数，仅当iIsExistRelativeViraultPosPara为1时有效
};

typedef enum {
    ENGINEER_NOT_RUN = 0,
    ENGINEER_IS_RUN,
}eng_is_run_t;

void reinit_engineer_position_data();
int engineer_debug_pos_run(struct ENGINEER_DEBUG_MOTOR_PARA_TT *motor_param);
int engineer_debug_pos_count(int module_idx);
int engineer_debug_pos_get(int module_idx, int count, struct ENGINEER_DEBUG_MODULE_PARA_TT *module_param);
int engineer_debug_pos_set(const struct ENGINEER_DEBUG_MODULE_PARA_TT *module_param);
void engineer_is_run_set(eng_is_run_t run_stat);
eng_is_run_t engineer_is_run_get();
int motor_para_set_temp(int motor_id, const thrift_motor_para_t *motor_para);
void engineer_needle_pos_report(int module_idx, int pos_idx, int x, int y, int z, int r);
int engineer_debug_pos_all_count(void);

#ifdef __cplusplus
}
#endif

#endif

