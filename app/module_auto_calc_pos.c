/******************************************************
自动标定功能的实现文件
******************************************************/
#include <stdint.h>
#include <unistd.h>

#include "thrift_handler.h"
#include "h3600_maintain_utils.h"
#include "movement_config.h"
#include "module_catcher_rs485.h"
#include "module_auto_calc_pos.h"
#include "module_common.h"
#include "module_monitor.h"
#include "h3600_cup_param.h"
#include "thrift_service_software_interface.h"
#include "module_auto_cal_needle.h"
#include "module_engineer_debug_position.h"

/* 自动标定操作步骤 */
#define CATCH_POS_NUM       13

/* 夹爪阈值，小于此值，视为抓取成功 */
#define GRIP_THRESHOLD      95

/* 一个底座的宽度 */
#define GRIP_ONE_SEAT       340

/* 复位光电信号值 */
#define PE_TRIGGER_Z     0
#define PE_NO_TRIGGER_Z  1

/* X轴标定时的Z轴电流设置 */
#define GRRIP_Z_CURR         400
#define GRRIP_Z_SRC_CURR     0

/* 停止标志 */
static uint8_t grip_auto_calc_stop_flag = 0;

typedef struct
{
    /* 电机号 */
    uint8_t motor_id;
    /* 电流值 */
    uint32_t cur;
}__attribute__((packed)) cur_result_t;

int catcher_auto_check_pos_z(move_pos_t sub_type, cup_pos_t index, uint8_t dir);
int catcher_auto_check_pos_y(move_pos_t sub_type, cup_pos_t index, uint8_t dir);
int catcher_auto_check_pos_y_logic(move_pos_t sub_type, cup_pos_t index, uint8_t dir);
int catcher_auto_check_pos_x(move_pos_t sub_type, cup_pos_t index, uint8_t dir);
int catcher_auto_check_pos_mirco_z(move_pos_t sub_type, cup_pos_t index, uint8_t dir);

typedef struct auto_cali_stru {
    uint8_t index;             /* 自动标定步骤索引值 */
    move_pos_t pos_aera;       /* 标定位置区域 */
    cup_pos_t pos_idnex;       /* 标定位置在区域内的索引值 */
    uint8_t y_dir;             /* y轴运动方向 */
    int y_zero;                /* y轴自动标定前的初始位置 */
    int extra_offest_z;        /* z轴偏移 */
    int extra_offest_y;        /* y轴偏移 */
    int extra_offest_x;        /* x轴偏移 */
    int (*move_func_z) (move_pos_t, cup_pos_t, uint8_t); /* 某步骤x轴调用函数 */
    int (*move_func_y) (move_pos_t, cup_pos_t, uint8_t); /* 某步骤y轴调用函数 */
    int (*move_func_x) (move_pos_t, cup_pos_t, uint8_t); /* 某步骤z轴调用函数 */
    int (*move_func_z_mir) (move_pos_t, cup_pos_t, uint8_t); /* 某步骤z轴细分调用函数 */
    int extra_offest_y_2;      /* y轴偏移, 供孵育位使用 */
}auto_calibra_t;

/* 需要标定的位置 */
static auto_calibra_t cali_opr[CATCH_POS_NUM] = {
    {POS_12, MOVE_C_NEW_CUP, POS_CUVETTE_SUPPLY_INIT, 1, 1200, 220, 830, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_8, MOVE_C_OPTICAL_MIX, POS_OPTICAL_MIX, 0, 990, 90, 620, 0, CALC(z), CALC(y_logic), CALC(x), CALC(mirco_z), 0},
    {POS_0, MOVE_C_PRE, POS_PRE_PROCESSOR, 1, 990, 90, 620, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_1, MOVE_C_MIX, POS_PRE_PROCESSOR_MIX1, 1, 990, 90, 620, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_2, MOVE_C_MIX, POS_PRE_PROCESSOR_MIX2, 1, 990, 90, 620, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_3, MOVE_C_INCUBATION, POS_INCUBATION_WORK_1, 0, 8870, 90, 8540, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 650},
    {POS_5, MOVE_C_INCUBATION, POS_INCUBATION_WORK_30, 0, 1480, 90, 1160, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 8030},
    {POS_4, MOVE_C_INCUBATION, POS_INCUBATION_WORK_10, 0, 1480, 90, 1160, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 8030},
    {POS_6, MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_1, 1, 1130, 90, 780, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_7, MOVE_C_MAGNETIC, POS_MAGNECTIC_WORK_4, 1, 7160, 90, 6800, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_9, MOVE_C_OPTICAL, POS_OPTICAL_WORK_1, 1, 1570, 90, 1210, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_10, MOVE_C_OPTICAL, POS_OPTICAL_WORK_8, 1, 11050, 90, 10660, 0, CALC(z), CALC(y), CALC(x), CALC(mirco_z), 0},
    {POS_11, MOVE_C_DETACH, POS_REACT_CUP_DETACH, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0},
};

typedef struct src_pos_stru {
    move_pos_t pos_aera;       /* 标定位置区域 */
    cup_pos_t pos_idnex;       /* 标定位置在区域内的索引值 */

    pos_t pos;
}src_pos_t;

/* 标定前的原始位置 */
static src_pos_t src_p[CATCH_POS_NUM];

static void motor_attr_init(motor_time_sync_attr_t *motor_x, motor_time_sync_attr_t *motor_z)
{
    motor_x->v0_speed = 100;
    motor_x->vmax_speed = h3600_conf_get()->motor[MOTOR_CATCHER_X].speed;
    motor_x->speed = h3600_conf_get()->motor[MOTOR_CATCHER_X].speed;
    motor_x->max_acc = h3600_conf_get()->motor[MOTOR_CATCHER_X].acc;
    motor_x->acc = h3600_conf_get()->motor[MOTOR_CATCHER_X].acc;

    motor_z->v0_speed = 100;
    motor_z->vmax_speed = h3600_conf_get()->motor[MOTOR_CATCHER_Z].speed;
    motor_z->speed = h3600_conf_get()->motor[MOTOR_CATCHER_Z].speed;
    motor_z->max_acc = h3600_conf_get()->motor[MOTOR_CATCHER_Z].acc;
    motor_z->acc = h3600_conf_get()->motor[MOTOR_CATCHER_Z].acc;
}

static int cur_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    cur_result_t *data = (cur_result_t *)arg;
    cur_result_t *result = (cur_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (data->motor_id == result->motor_id) {
        data->cur = result->cur;
        return 0;
    }

    return -1;
}

void grip_auto_calc_stop_flag_set(uint8_t v)
{
    grip_auto_calc_stop_flag = v;
    LOG("auto cali stop!\n");
}

static int grriper_z_motor_curr_set(uint32_t cur)
{
    cur_result_t data;
    cur_result_t result;

    data.motor_id = MOTOR_CATCHER_Z;
    data.cur = cur;

    slip_send_node(slip_node_id_get(), SLIP_NODE_A9_RTOS, 0x0, CONFIGURE_TYPE, CONFIG_SET_CUR_SUBTYPE, sizeof(cur_result_t), &data);

    result.motor_id = MOTOR_CATCHER_Z;
    result.cur = 0;
    if (0 == result_timedwait(CONFIGURE_TYPE, CONFIG_SET_CUR_SUBTYPE, cur_set_result, &result, 1500)) {
        return result.cur;
    }
    return -1;
}

int get_channel_offest_by_pos(cup_pos_t index)
{
    uint8_t i;

    for (i = 0; i < CATCH_POS_NUM; i++) {
        if (index == cali_opr[i].pos_idnex) {
            return cali_opr[i].y_zero;
        }
    }

    return -1;
}

/*  获取当前缓存中的位置坐标 */
void get_record_pos(move_pos_t sub_type, cup_pos_t index, pos_t *pos)
{
    uint8_t i;

    for (i = 0; i < CATCH_POS_NUM; i++) {
        if (sub_type == src_p[i].pos_aera && index == src_p[i].pos_idnex) {
            memcpy(pos, &src_p[i].pos, sizeof(pos_t));
        }
    }
}

int catcher_auto_check_pos_z(move_pos_t sub_type, cup_pos_t index, uint8_t dir)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int catcher_calc_z = 0;
    int curr_catcher_step = 0;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y - 500) -(calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y - 500) -(calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);

    /* 光学混匀位 */
    if (POS_OPTICAL_MIX != index) {
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, calc_pos.t1_src.x - calc_pos.cur.x,
                                    calc_pos.t1_src.y - calc_pos.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }
    } else { /* 光学混匀位位置靠后，因此先走Y，再走X */
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, \
                                    calc_pos.t1_src.y - calc_pos.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }

        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, calc_pos.t1_src.x - calc_pos.cur.x, \
                                    0, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }
    }

    /* 进杯位，由于结构不同，需要单独补偿 */
    if (POS_CUVETTE_SUPPLY_INIT == index) {
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -500, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }
    }

    catcher_calc_z = (calc_pos.t1_src.z > 1000) ? (calc_pos.t1_src.z - 500) : 0;
    if(motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_calc_z , motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    while (1) {
        if (grip_auto_calc_stop_flag || (catcher_calc_z >= calc_pos.t1_src.z + 400))
            return -1;

        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, 40, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            return -1;
        }

        if (catcher_ctl(CATCHER_AUTO_CLOSE)) {
            LOG("catcher close failed\n");
            return -1;
        }

        curr_catcher_step = get_catcher_curr_step();
        if (GRIP_THRESHOLD > curr_catcher_step) {
            LOG("calc pos curr_catcher_step = %d\n", curr_catcher_step);
            if (catcher_ctl(CATCHER_OPEN)) {
                LOG("catcher close failed\n");
                return -1;
            }
            sleep(1);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 1, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                return -1;
            }

#if 0
            /* 光学混匀位 */
            if (POS_OPTICAL_MIX == index) {
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, 1000, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                    return -1;
                }
            }
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, 1, 1, 0, 0, MOTOR_DEFAULT_TIMEOUT, 0)) {
                LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                return -1;
            }
#else
            if(slip_motor_reset_timedwait(MOTOR_CATCHER_Y, 0, 10000)) {
                return -1;
            }
#endif
            break;
        }
        usleep(300 * 1000);
        catcher_calc_z += 40;
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher close failed\n");
            return -1;
        }
    }
    LOG("calc pos z = %d\n", catcher_calc_z - 20);

    return catcher_calc_z - 20;
}

int catcher_auto_check_pos_mirco_z(move_pos_t sub_type, cup_pos_t index, uint8_t dir)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int catcher_calc_z = 0;
    int curr_catcher_step = 0;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y - 500) -(calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y - 500) -(calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);

    /* 光学混匀位 */
    if (POS_OPTICAL_MIX != index) {
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, calc_pos.t1_src.x - calc_pos.cur.x,
                                    calc_pos.t1_src.y - calc_pos.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }
    } else { /* 光学混匀位位置靠后，因此先走Y，再走X */
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, \
                                    calc_pos.t1_src.y - calc_pos.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }

        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, calc_pos.t1_src.x - calc_pos.cur.x, \
                                    0, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }
    }


    /* 进杯位，由于结构不同，需要单独补偿 */
    if (POS_CUVETTE_SUPPLY_INIT == index) {
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -500, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }
    }

    catcher_calc_z = (POS_CUVETTE_SUPPLY_INIT == index) ? (calc_pos.t1_src.z - 300) : (calc_pos.t1_src.z - 100);
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, catcher_calc_z , motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    while (1) {
        if (grip_auto_calc_stop_flag)
            return -1;

        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, 10, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            return -1;
        }

        if (catcher_ctl(CATCHER_AUTO_CLOSE)) {
            LOG("catcher close failed\n");
            return -1;
        }
        curr_catcher_step = get_catcher_curr_step();
        if (GRIP_THRESHOLD > curr_catcher_step) {
            LOG("calc pos curr_catcher_step = %d\n", curr_catcher_step);
            if (catcher_ctl(CATCHER_OPEN)) {
                LOG("catcher close failed\n");
                return -1;
            }
            sleep(1);
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                return -1;
            }
            
            /* 光学混匀位 */
            if (POS_OPTICAL_MIX == index) {
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, 1000, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                    return -1;
                }
            }
            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, 1, 1, 0, 0, MOTOR_DEFAULT_TIMEOUT, 0)) {
                LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                return -1;
            }
            break;
        }
        usleep(300 * 1000);
        catcher_calc_z += 10;
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher close failed\n");
            return -1;
        }
    }
    LOG("calc pos z = %d\n", catcher_calc_z);

    return catcher_calc_z;
}

/* 确定孵育池类型        */
incubation_pool_type_t incubation_pool_type_get(move_pos_t sub_type, cup_pos_t index)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int y_offest = 300; /* 确定孵育池类型，在基准位置，后退300 */
    int extra_offst = 40;
    int count = 0;
    incubation_pool_type_t ret = INCUBATION_POOL_OLD;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                calc_pos.t1_src.y - calc_pos.cur.y - y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
        return -1;
    }

    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, calc_pos.t1_src.z + extra_offst, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    y_offest = -50;

    while (count++ < 12) { /* 最多向后走600个脉冲 */
        if (grip_auto_calc_stop_flag)
            return -1;

        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }

        if (catcher_ctl(CATCHER_AUTO_CLOSE)) {
            LOG("catcher close failed\n");
            return -1;
        }
        if (GRIP_THRESHOLD < get_catcher_curr_step()) {
            ret = INCUBATION_POOL_NEW;
            goto out;
        }
        usleep(300 * 1000);
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher close failed\n");
            return -1;
        }
    }

out:
    if (catcher_ctl(CATCHER_OPEN)) {
        LOG("catcher close failed\n");
        return -1;
    }

    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 1, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    if(slip_motor_reset_timedwait(MOTOR_CATCHER_Y, 0, 10000)) {
        return -1;
    }

    return ret;
}

/* dir 1：后->前 0：前->后 */
int catcher_auto_check_pos_y(move_pos_t sub_type, cup_pos_t index, uint8_t dir)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int catcher_calc_y = 0;
    int y_offest = get_channel_offest_by_pos(index);
    uint8_t gr_flag = 0;
    int extra_offst = (POS_OPTICAL_MIX == index || POS_PRE_PROCESSOR == index || POS_PRE_PROCESSOR_MIX1 == index || POS_PRE_PROCESSOR_MIX2 == index) ? 100 : 40;

    y_offest *= (dir) ? 1 : -1;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                calc_pos.t1_src.y - calc_pos.cur.y - y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
        return -1;
    }

    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, calc_pos.t1_src.z + extra_offst, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    catcher_calc_y = calc_pos.t1_src.y - calc_pos.cur.y - y_offest;
    y_offest = (dir) ? 50 : -50;

    while (1) {
        if (grip_auto_calc_stop_flag)
            return -1;

        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }

        if (catcher_ctl(CATCHER_AUTO_CLOSE)) {
            LOG("catcher close failed\n");
            return -1;
        }
        if (GRIP_THRESHOLD > get_catcher_curr_step()) {
            if (catcher_ctl(CATCHER_OPEN)) {
                LOG("catcher close failed\n");
                return -1;
            }

            if (!gr_flag) {
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -y_offest * 2, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                    return -1;
                }
                catcher_calc_y -= y_offest;
                y_offest = (dir) ? 10 : -10;
                gr_flag = 1;
                continue;
            } else {
                sleep(1);
                if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                    return -1;
                }
#if 0
                /* 光学混匀位 */
                if (POS_OPTICAL_MIX == index) {
                    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, 1000, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                        return -1;
                    }
                }
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, 1, 1, 0, 0, MOTOR_DEFAULT_TIMEOUT, 0)) {
                    LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                    return -1;
                }
#else
                if(slip_motor_reset_timedwait(MOTOR_CATCHER_Y, 0, 10000)) {
                    return -1;
                }
#endif
                break;
            }

        }
        usleep(300 * 1000);
        catcher_calc_y += y_offest;
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher close failed\n");
            return -1;
        }
    }
    LOG("calc pos y = %d\n", catcher_calc_y);

    return catcher_calc_y;
}

/* 孵育位 dir 1：后->前 0：前->后 */
int catcher_auto_check_pos_y_incubation(move_pos_t sub_type, cup_pos_t index, uint8_t dir)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int catcher_calc_y = 0;
    int y_offest = (POS_INCUBATION_WORK_1 == index) ? 0 : 7500;
    uint8_t gr_flag = 0;
    int extra_offst = 40;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                calc_pos.t1_src.y - calc_pos.cur.y - y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
        return -1;
    }

    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, calc_pos.t1_src.z + extra_offst, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    catcher_calc_y = calc_pos.t1_src.y - calc_pos.cur.y - y_offest;
    y_offest = -50;

    while (1) {
        if (grip_auto_calc_stop_flag)
            return -1;

        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }

        if (catcher_ctl(CATCHER_AUTO_CLOSE)) {
            LOG("catcher close failed\n");
            return -1;
        }
        if (GRIP_THRESHOLD < get_catcher_curr_step()) {
            if (catcher_ctl(CATCHER_OPEN)) {
                LOG("catcher close failed\n");
                return -1;
            }

            if (!gr_flag) {
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -y_offest * 2, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                    return -1;
                }
                catcher_calc_y -= y_offest;
                y_offest = -10;
                gr_flag = 1;
                continue;
            } else {
                sleep(1);
                if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                    return -1;
                }
#if 0
                /* 光学混匀位 */
                if (POS_OPTICAL_MIX == index) {
                    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, 1000, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                        return -1;
                    }
                }
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, 1, 1, 0, 0, MOTOR_DEFAULT_TIMEOUT, 0)) {
                    LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                    return -1;
                }
#else
                if(slip_motor_reset_timedwait(MOTOR_CATCHER_Y, 0, 10000)) {
                    return -1;
                }
#endif
                break;
            }

        }
        usleep(300 * 1000);
        catcher_calc_y += y_offest;
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher close failed\n");
            return -1;
        }
    }
    LOG("calc pos incubation y = %d\n", catcher_calc_y);

    return catcher_calc_y;
}

/* dir 1：后->前 0：前->后 */
int catcher_auto_check_pos_y_logic(move_pos_t sub_type, cup_pos_t index, uint8_t dir)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int catcher_calc_y = 0;
    int y_offest = 0;
    uint8_t gr_flag = 0;
    int extra_offst = (POS_OPTICAL_MIX == index || POS_PRE_PROCESSOR == index || POS_PRE_PROCESSOR_MIX1 == index || POS_PRE_PROCESSOR_MIX2 == index) ? 100 : 40;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);
    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                calc_pos.t1_src.y - calc_pos.cur.y, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
        return -1;
    }

    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, calc_pos.t1_src.z + extra_offst, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        return -1;
    }

    catcher_calc_y = calc_pos.t1_src.y - calc_pos.cur.y;
    y_offest = 50;

    while (1) {
        if (grip_auto_calc_stop_flag)
            return -1;

        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, y_offest, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            return -1;
        }

        if (catcher_ctl(CATCHER_AUTO_CLOSE)) {
            LOG("catcher close failed\n");
            return -1;
        }

        if (GRIP_THRESHOLD < get_catcher_curr_step()) {
            if (catcher_ctl(CATCHER_OPEN)) {
                LOG("catcher close failed\n");
                return -1;
            }

            if (!gr_flag) {
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, -y_offest * 2, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                    return -1;
                }
                catcher_calc_y -= y_offest;
                y_offest = 10;
                gr_flag = 1;
                continue;
            } else {
                sleep(1);
                if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                    return -1;
                }
#if 0
                /* 光学混匀位 */
                if (POS_OPTICAL_MIX == index) {
                    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, 1000, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                        return -1;
                    }
                }
                if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, 1, 1, 0, 0, MOTOR_DEFAULT_TIMEOUT, 0)) {
                    LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_XY);
                    return -1;
                }
#else
                if(slip_motor_reset_timedwait(MOTOR_CATCHER_Y, 0, 10000)) {
                    return -1;
                }
#endif
                break;
            }

        }
        usleep(300 * 1000);
        catcher_calc_y += y_offest;
        if (catcher_ctl(CATCHER_OPEN)) {
            LOG("catcher close failed\n");
            return -1;
        }
    }
    LOG("calc pos y = %d\n", catcher_calc_y);

    return catcher_calc_y;
}


int catcher_auto_check_pos_x(move_pos_t sub_type, cup_pos_t index, uint8_t dir)
{
    calc_pos_param_t calc_pos = {0};
    motor_time_sync_attr_t motor_x = {0}, motor_z = {0};
    int calc_acc = 0;
    int catcher_calc_x[2] = {0};
    int catcher_close_pos = 0, catcher_close_pos_now = 0;
    int divide_init_var = 0;
    int add_step = 0;
    int lr_divide_step = 0;
    int y_offst = (POS_CUVETTE_SUPPLY_INIT == index) ? -400 : 0;
    int z_offst = (POS_CUVETTE_SUPPLY_INIT == index) ? -120 : 0;

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_attr_init(&motor_x, &motor_z);
    get_record_pos(sub_type, index, &calc_pos.t1_src);
    grriper_z_motor_curr_set(GRRIP_Z_CURR);

    if (abs(calc_pos.t1_src.x - calc_pos.cur.x) > abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x))) {
        motor_x.step = abs(calc_pos.t1_src.x - calc_pos.cur.x);
    } else {
        motor_x.step = abs((calc_pos.t1_src.y - calc_pos.cur.y) - (calc_pos.t1_src.x - calc_pos.cur.x));
    }
    calc_acc = calc_motor_move_in_time(&motor_x, 0.8);

    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0,
                                calc_pos.t1_src.y - calc_pos.cur.y + y_offst, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
        return -1;
    }

    if (catcher_ctl(CATCHER_AUTO_CLOSE_X)) {
        LOG("catcher close failed\n");
        return -1;
    }
    catcher_calc_x[0] = calc_pos.t1_src.x - calc_pos.cur.x;
    catcher_close_pos = PE_TRIGGER_Z;
    add_step = 10;

    while (divide_init_var < 500) {
        if (grip_auto_calc_stop_flag)
            return -1;

        /* 触底动作 */
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, calc_pos.t1_src.z + z_offst + 120, motor_z.speed, motor_z.acc / 3, MOTOR_DEFAULT_TIMEOUT)) {
            return -1;
        }
        usleep(200 * 1000);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, -(calc_pos.t1_src.z + z_offst +  60), motor_z.speed, motor_z.acc / 3, MOTOR_DEFAULT_TIMEOUT)) {
            return -1;
        }

        /* 凭光电信号判断是否触底 */
        catcher_close_pos_now = gpio_get(PE_GRAP_Z);
        if ((PE_TRIGGER_Z == catcher_close_pos_now) && (PE_NO_TRIGGER_Z == catcher_close_pos)) {
            sleep(1);
            /* 复位 */
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                goto bad;
            }
            break;
        }

        /* 复位 */
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            goto bad;
        }

        if (PE_NO_TRIGGER_Z == catcher_close_pos_now) {
            divide_init_var += abs(add_step);
            catcher_calc_x[0] += add_step;

            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, add_step, 0, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                goto bad;
            }
        }
        catcher_close_pos = catcher_close_pos_now;
    }

    lr_divide_step = (POS_CUVETTE_SUPPLY_INIT == index) ? 230 : 350;
    catcher_calc_x[1] = catcher_calc_x[0] - lr_divide_step;
    catcher_close_pos = PE_TRIGGER_Z;
    add_step = -5;
    divide_init_var = 0;

    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, -lr_divide_step, 0, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
        goto bad;
    }

    while (divide_init_var < 800) {
        if (grip_auto_calc_stop_flag)
            return -1;

        /* 触底动作 */
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, calc_pos.t1_src.z + z_offst + 120, motor_z.speed, motor_z.acc / 3, MOTOR_DEFAULT_TIMEOUT)) {
            return -1;
        }
        usleep(200 * 1000);
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_MOVE_STEP, -(calc_pos.t1_src.z + z_offst + 60), motor_z.speed, motor_z.acc / 3, MOTOR_DEFAULT_TIMEOUT)) {
            return -1;
        }

        /* 凭光电信号判断是否触底 */
        catcher_close_pos_now = gpio_get(PE_GRAP_Z);
        if ((PE_TRIGGER_Z == catcher_close_pos_now) && (PE_NO_TRIGGER_Z == catcher_close_pos)) {
            sleep(1);

            /* 复位 */
            if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
                goto bad;
            }
            break;
        }

        /* 复位 */
        if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
            goto bad;
        }

        if (PE_NO_TRIGGER_Z == catcher_close_pos_now) {
            divide_init_var += abs(add_step);
            catcher_calc_x[1] += add_step;

            if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, add_step, 0, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
                goto bad;
            }
        }
        catcher_close_pos = catcher_close_pos_now;
    }

    /* 恢复原Z电机电流 */
    grriper_z_motor_curr_set(GRRIP_Z_SRC_CURR);

    /* 复位 */
    if (motor_move_ctl_sync(MOTOR_CATCHER_Z, CMD_MOTOR_RST, 0, motor_z.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT)) {
        goto bad;
    }

    /* 光学混匀位 */
    if (POS_OPTICAL_MIX == index) {
        if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_DUAL_MOVE, 0, 1000, motor_x.speed, calc_acc, MOTOR_DEFAULT_TIMEOUT, 0.8)) {
            goto bad;
        }
    }

    if (motor_move_dual_ctl_sync(MOTOR_CATCHER_X, CMD_MOTOR_GROUP_RESET, 1, 1, 0, 0, MOTOR_DEFAULT_TIMEOUT, 0)) {
        LOG("[%s:%d] x-y move timeout\n", __func__, __LINE__);
        goto bad;
    }

    LOG("calc pos right: %d left:%d\n", catcher_calc_x[0], catcher_calc_x[1]);

    /* 补偿 */
    if (abs(catcher_calc_x[0] - catcher_calc_x[1]) < GRIP_ONE_SEAT) {
        catcher_calc_x[1] = catcher_calc_x[0] - GRIP_ONE_SEAT;
        LOG("comp -> calc pos right: %d left:%d\n", catcher_calc_x[0], catcher_calc_x[1]);
    }

    LOG("calc pos %d\n", (catcher_calc_x[0] + catcher_calc_x[1]) / 2);
    return ((catcher_calc_x[0] + catcher_calc_x[1]) / 2);

bad:
    grriper_z_motor_curr_set(GRRIP_Z_SRC_CURR);
    return -1;
}

/* 上报标定后的位置 */
void report_auto_calc_pos(uint8_t index, int old_v, int new_v, uint8_t axis)
{
    uint8_t pos_id;

    pos_id = index * 3;
    pos_id += (0 == axis || 3 == axis) ? 2 : (1 == axis ? 1 : 0);

    report_position_calibration(ENG_DEBUG_GRIP, pos_id , old_v, new_v);
}

/* 写入标定后的位置 */
int eng_gripper_write_auto_calc_pos(void)
{
    uint8_t i, j;
    int ret = 0;
    int motor, write_step;

    for (i = 0; i < CATCH_POS_NUM; i++) {
        for (j = 0; j < 3; j++) {
            switch(j) {
                case 0: motor = MOTOR_CATCHER_X; write_step = src_p[i].pos.x; break;
                case 1: motor = MOTOR_CATCHER_Y; write_step = src_p[i].pos.y; break;
                case 2: motor = MOTOR_CATCHER_Z; write_step = src_p[i].pos.z; break;
            }

            if (-1 == thrift_motor_pos_grip_set(motor, cali_opr[i].index, write_step)) {
                return -1;
            }
        }
        LOG("new pos! x:%d y:%d z:%d \n", src_p[i].pos.x, src_p[i].pos.y, src_p[i].pos.z);
    }

    /* 写入 */
    save_grip_cali_value();

    /* 更新 */
    catcher_record_pos_reinit(h3600_conf_get());

    return ret;
}

/* 工程师模式自动标定需按照手动调整位置的格式上报，将自动标定的顺序转换为手动调整时规定的顺序 */
static void auto_grip_cal_pos_to_eng_pos(cup_pos_t idx, int *module_idx, int *pos_idx)
{
    *module_idx = 12;

    if (idx == POS_CUVETTE_SUPPLY_INIT) {
        *pos_idx = 13;
    } else if (idx == POS_OPTICAL_MIX) {
        *pos_idx = 9;
    } else if (idx == POS_PRE_PROCESSOR) {
        *pos_idx = 1;
    } else if (idx == POS_PRE_PROCESSOR_MIX1) {
        *pos_idx = 2;
    } else if (idx == POS_PRE_PROCESSOR_MIX2) {
        *pos_idx = 3;
    } else if (idx == POS_INCUBATION_WORK_1) {
        *pos_idx = 4;
    } else if (idx == POS_INCUBATION_WORK_30) {
        *pos_idx = 6;
    } else if (idx == POS_INCUBATION_WORK_10) {
        *pos_idx = 5;
    } else if (idx == POS_MAGNECTIC_WORK_1) {
        *pos_idx = 7;
    } else if (idx == POS_MAGNECTIC_WORK_4) {
        *pos_idx = 8;
    } else if (idx == POS_OPTICAL_WORK_1) {
        *pos_idx = 10;
    } else if (idx == POS_OPTICAL_WORK_8) {
        *pos_idx = 11;
    } else if (idx == POS_REACT_CUP_DETACH) {
        *pos_idx = 12;
    }
}

/* 自动标定外部调用函数 */
int eng_gripper_auto_cali_func(int cali_id)
{
    uint8_t i, j;
    int ret = 0;
    uint8_t motor_id = 0;
    int offest_steps = 0;
    int (*move_func) (move_pos_t, cup_pos_t, uint8_t);
    int *pos_axi = NULL;
    int grip_z_src[CATCH_POS_NUM] = {0};
    int eng_module_idx = 0, eng_pos_idx = 0;
    incubation_pool_type_t pool_type;

    /* 获取标定前的原始位置 */
    catcher_record_pos_reinit(h3600_conf_get());
    grip_auto_calc_stop_flag = 0;

    for (i = 0; i < CATCH_POS_NUM; i++) {
        src_p[i].pos_aera = cali_opr[i].pos_aera;
        src_p[i].pos_idnex = cali_opr[i].pos_idnex;
        get_special_pos(cali_opr[i].pos_aera, cali_opr[i].pos_idnex, &src_p[i].pos, FLAG_POS_UNLOCK);
        grip_z_src[i] = src_p[i].pos.z;
        LOG("src pos! x:%d y:%d z:%d \n", src_p[i].pos.x, src_p[i].pos.y, src_p[i].pos.z);
    }

    if (catcher_rs485_init()) {
        LOG("catcher init failed\n");
        return -1;
    }

    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    motor_reset(MOTOR_NEEDLE_R2_Z, 1);
    motor_reset(MOTOR_CATCHER_Z, 1);

    /* 等待针的Z轴复位完成 */
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles R2.z timeout.\n");
        return -1;
    }

    /* 等待抓手的Z轴复位完成 */
    if (0 != motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset graps A.z timeout.\n");
        return -1;
    }

    motor_reset(MOTOR_NEEDLE_S_Y, 1);
    motor_reset(MOTOR_NEEDLE_R2_Y, 1);
    motor_reset(MOTOR_CATCHER_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.x timeout.\n");
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles R2.y timeout.\n");
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_CATCHER_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.y timeout.\n");
        return -1;
    }

    motor_reset(MOTOR_NEEDLE_S_X, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_X, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.y timeout.\n");
        return -1;
    }

    motor_reset(MOTOR_CATCHER_X, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT*2)) {
        LOG("reset catcher.x timeout.\n");
        return -1;
    }

    if (slip_motor_dual_step_timedwait(MOTOR_NEEDLE_S_Y, 10000, 0, 10000) <= 0) {
        LOG("needle s aviod fail!\n");
        return -1;
    }

    for (i = 0 ; i < CATCH_POS_NUM; i++) {
        /* 获取工程师调试界面坐标索引值 */
        auto_grip_cal_pos_to_eng_pos(cali_opr[i].pos_idnex, &eng_module_idx, &eng_pos_idx);

        for (j = 0; j < 4; j++) {
            switch (j) {
                case 0:
                    motor_id = MOTOR_CATCHER_Z;
                    move_func = cali_opr[i].move_func_z;
                    offest_steps = cali_opr[i].extra_offest_z;
                    pos_axi = &src_p[i].pos.z;
                    break;
                case 1:
                    motor_id = MOTOR_CATCHER_Y;

                    move_func = cali_opr[i].move_func_y;
                    offest_steps = cali_opr[i].extra_offest_y;

                    if ((POS_INCUBATION_WORK_1 == cali_opr[i].pos_idnex) || \
                        (POS_INCUBATION_WORK_10 == cali_opr[i].pos_idnex) || \
                        (POS_INCUBATION_WORK_30 == cali_opr[i].pos_idnex)) {

                        if (POS_INCUBATION_WORK_1 == cali_opr[i].pos_idnex) {
                            pool_type = incubation_pool_type_get(cali_opr[i].pos_aera, cali_opr[i].pos_idnex);
                        }

                        if (INCUBATION_POOL_NEW == pool_type) {
                            LOG("incubation pool new.\n");
                            move_func = catcher_auto_check_pos_y_incubation;
                            offest_steps = cali_opr[i].extra_offest_y_2;
                            cali_opr[i].y_dir = 1;
                        }
                    }

                    pos_axi = &src_p[i].pos.y;
                    break;
                case 2:
                    motor_id = MOTOR_CATCHER_X;
                    move_func = cali_opr[i].move_func_x;
                    offest_steps = cali_opr[i].extra_offest_x;
                    pos_axi = &src_p[i].pos.x;
                    break;
                case 3:
                    motor_id = MOTOR_CATCHER_Z;
                    move_func = cali_opr[i].move_func_z_mir;
                    offest_steps = cali_opr[i].extra_offest_z;
                    pos_axi = &src_p[i].pos.z;
                    break;
            }

            if (NULL != move_func) {
                ret = move_func(cali_opr[i].pos_aera, cali_opr[i].pos_idnex, cali_opr[i].y_dir);
                if (-1 == ret) {
                    LOG("POS:%d-%d move_func failed\n", i, j);
                    return (ENG_DEBUG_GRIP == cali_id) ? -1 : eng_pos_idx;
                } else {
                    /* 只有Y轴可能涉及到反向 */
                    offest_steps = (1 == j && 0 == cali_opr[i].y_dir) ? -offest_steps : offest_steps;
                    LOG("index:%d-%d offest_steps:%d\n", i, j, offest_steps);

                    /* 写入及更新 */
                    LOG("index:%d terminal:%d\n", motor_id, ret + offest_steps);

                    if (j && (ENG_DEBUG_GRIP == cali_id))
                        report_auto_calc_pos(cali_opr[i].index, (3 == j) ? grip_z_src[i] : *pos_axi, ret + offest_steps, j);

                    *pos_axi = ret + offest_steps;
                }
            }
        }

        /* 上位机   工程师标定结果上报 */
        if (ENG_DEBUG_GRIP1 == cali_id) {
            engineer_needle_pos_report(eng_module_idx, eng_pos_idx, src_p[i].pos.x, src_p[i].pos.y, src_p[i].pos.z, 0);
            LOG("pos report! %d-%d-%d\n", src_p[i].pos.x, src_p[i].pos.y, src_p[i].pos.z);
        }
    }
    return 0;
}
