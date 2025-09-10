#include <stdio.h>
#include <math.h>
#include <unistd.h>

#include "log.h"
#include "common.h"
#include "slip/slip_msg.h"
#include "slip/slip_node.h"
#include "slip_cmd_table.h"
#include "work_queue.h"

#include "movement_config.h"
#include "module_common.h"
#include "module_monitor.h"
#include "thrift_service_software_interface.h"

static int value_set_ctl = 1; /* 控制value_set是否生效 flag：0不生效，1生效（默认） */
static int power_off_stat = 0; /* 标识电机下电状态 */

void set_power_off_stat(int stat)
{
    power_off_stat = stat;
}

int get_power_off_stat()
{
    return power_off_stat;
}


/* 控制value_set是否生效 flag：0不生效，1生效（默认） */
void value_set_control(int flag)
{
    value_set_ctl = flag;
}

/* 获取value_set是否生效 flag */
int value_get_control()
{
    return value_set_ctl;
}

/*
*   泵阀控制函数
*/
int valve_set(unsigned short index, unsigned char status)
{
    int ret = -1;

    if (status == ON && value_set_ctl) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        gpio_set(index, ON);
        FAULT_CHECK_END();
        ret = 1;
    } else {
        if (value_set_ctl) {
            gpio_set(index, OFF);
            ret = 1;
        } else {
            LOG("valve %d set falid value_set_ctl flag = %d \n", index, value_set_ctl);
        }
    }
    return ret;
}

/*
*   电机运动控制函数
*   非阻塞调用，调用后必须匹配motors_move_timewait调用
*   CMD_MOTOR_MOVE_STEP步长模式
*   CMD_MOTOR_RST复位
*   CMD_MOTOR_MOVE_SPEED速度模式
*   CMD_MOTOR_STOP停止
*/
int motor_move_async(char motor_id, char cmd, int step, int speed)
{
    int res = 0;

    if (cmd == CMD_MOTOR_MOVE || cmd == CMD_MOTOR_MOVE_STEP) {
        res = motor_step(motor_id, step, speed);
    } else if (cmd == CMD_MOTOR_RST) {
        res = motor_reset(motor_id, speed);
    } else if (cmd == CMD_MOTOR_MOVE_SPEED) {
        res = motor_speed(motor_id, speed);
    } else if (cmd == CMD_MOTOR_STOP) {
        res = motor_stop(motor_id);
    } else {
        LOG("motor cmd error!!!\n");
        res = -1;
    }

    return res;
}

/*
*   电机运动控制高级函数
*   非阻塞调用，调用后必须匹配motors_move_timewait调用
*   CMD_MOTOR_MOVE_STEP步长模式
*   CMD_MOTOR_RST复位
*   CMD_MOTOR_MOVE_SPEED速度模式
*   CMD_MOTOR_STOP停止
*   CMD_MOTOR_DUAL_MOVE双电机动作
*/
int motor_move_ctl_async(char motor_id, char cmd, int step, int speed, int acc)
{
    int res = 0;

    if (cmd == CMD_MOTOR_MOVE || cmd == CMD_MOTOR_MOVE_STEP) {
        res = motor_step_ctl(motor_id, step, speed, acc);
    } else if (cmd == CMD_MOTOR_RST) {
        res = motor_reset_ctl(motor_id, 0, speed, acc);
    } else if (cmd == CMD_MOTOR_MOVE_SPEED) {
        res = motor_speed_ctl(motor_id, speed, acc);
    } else if (cmd == CMD_MOTOR_STOP) {
        res = motor_stop(motor_id);
    } else {
        LOG("motor cmd error!!!\n");
        res = -1;
    }

    return res;
}

/*
*   电机运动控制高级函数
*   CMD_MOTOR_DUAL_MOVE双电机动作
*/
int motor_move_dual_ctl_async(char motor_id, char cmd, int step_x, int step_y, int speed, int acc, double cost_time)
{
    int res = 0;

    if (cmd == CMD_MOTOR_DUAL_MOVE) {
        res = motor_step_dual_ctl(motor_id, step_x, step_y, speed, acc, cost_time);
    } else if (cmd == CMD_MOTOR_GROUP_RESET) {
        res = slip_motor_reset_timedwait(motor_id, 2, 30000);
    } else {
        LOG("dual motor cmd error!!!\n");
        res = -1;
    }

    return res;
}

/* 电机控制非阻塞调用控制函数 */
int motors_move_timewait(unsigned char motor_ids[], int motor_count, long msecs)
{
    return motors_timedwait(motor_ids, motor_count, msecs);
}

/*
*   电机运动控制函数
*   阻塞调用
*   CMD_MOTOR_MOVE_STEP步长模式
*   CMD_MOTOR_RST复位
*   CMD_MOTOR_MOVE_SPEED速度模式
*   CMD_MOTOR_STOP停止
*/
int motor_move_sync(char motor_id, char cmd, int step, int speed, int timeout)
{
    int res = 0;

    if (cmd == CMD_MOTOR_MOVE || cmd == CMD_MOTOR_MOVE_STEP) {
        motor_step(motor_id, step, speed);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_RST) {
        motor_reset(motor_id, speed);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_MOVE_SPEED) {
        motor_speed(motor_id, speed);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_STOP) {
        motor_stop(motor_id);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else {
        LOG("motor cmd error!!!\n");
        res = -1;
    }
    return res;
}

/* 带前段缓慢脉冲的正常复位: 阻塞调用 */
int motor_slow_step_reset_sync(char motor_id, int slow_step, int speed, int acc, int timeout)
{
    int ret = 0;

    motor_slow_step_reset_ctl(motor_id, 0, speed, acc, slow_step);
    if (timeout) {
        ret = motor_timedwait(motor_id, timeout);
    }

    return ret;
}

/* 带前段缓慢脉冲的正常复位: 非阻塞调用 */
int motor_slow_step_reset_async(char motor_id, int slow_step, int speed, int acc)
{
    return motor_slow_step_reset_ctl(motor_id, 0, speed, acc, slow_step);
}


/*
*   电机运动控制高级函数
*   阻塞调用
*   CMD_MOTOR_MOVE_STEP步长模式
*   CMD_MOTOR_RST复位
*   CMD_MOTOR_MOVE_SPEED速度模式
*   CMD_MOTOR_STOP停止
*/
int motor_move_ctl_sync(char motor_id, char cmd, int step, int speed, int acc, int timeout)
{
    int res = 0;

    if (cmd == CMD_MOTOR_MOVE || cmd == CMD_MOTOR_MOVE_STEP) {
        motor_step_ctl(motor_id, step, speed, acc);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_RST) {
        motor_reset_ctl(motor_id, 0, speed, acc);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_MOVE_SPEED) {
        motor_speed_ctl(motor_id, speed, acc);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_STOP) {
        motor_stop(motor_id);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else {
        LOG("motor cmd error!!!\n");
        res = -1;
    }
    return res;
}

/*
*   CMD_MOTOR_DUAL_MOVE双电机动作
*/
int motor_move_dual_ctl_sync(char motor_id, char cmd, int step_x, int step_y, int speed, int acc, int timeout, double cost_time)
{
    int res = 0;

    if (cmd == CMD_MOTOR_DUAL_MOVE) {
        motor_step_dual_ctl(motor_id, step_x, step_y, speed, acc, cost_time);
        if (timeout) {
            res = motor_timedwait(motor_id, timeout);
        }
    } else if (cmd == CMD_MOTOR_GROUP_RESET) {
        if (step_x != 0 || step_y != 0) {
            res = slip_motor_reset_timedwait(motor_id, 2, timeout);
        }
    } else {
        LOG("dual motor cmd error!!!\n");
        res = -1;
    }
    return res;
}

int motors_step_attr_init(void)
{
    return 0;
}

/*********************************
控制 所有电机电源
enable: 0：电机下电 1：电机上电
********************************/
int all_motor_power_clt(uint8_t enable)
{
    int idx = 0;
    unsigned char ret[MAX_MOTOR_NUM] = {0};

    LOG("A9 motor power ctl:%d\n", enable);
    memset(ret, CONTROL_CMD_RESULT_FAILED, sizeof(ret));
    motor_power_ctl_all_timedwait(0, enable, ret, 3000);
    for (idx = 0; idx < MAX_MOTOR_NUM; idx++) {
        if (ret[idx] != CONTROL_CMD_RESULT_SUCCESSS) {
            LOG("A9 motor(%d) power on failed, retry!\n", idx+1);
            usleep(10000);
            motor_power_ctl_all_timedwait(0, enable, ret, 3000);
            if (ret[idx] != CONTROL_CMD_RESULT_SUCCESSS) {
                LOG("A9 motor(%d) power on failed, last!\n", idx+1);
                return -1;
            }
        }
    }

    return 0;
}

int reset_all_motors(void)
{
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);

    motor_reset(MOTOR_NEEDLE_S_Z, 1);
    motor_reset(MOTOR_NEEDLE_R2_Z, 1);
    motor_reset(MOTOR_CATCHER_Z, 1);


    /* 等待针的Z轴复位完成 */
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
        return -1;
    }
    /* 20240924更改挡片后样本针洗针位置 */
    if (motor_move_ctl_sync(MOTOR_NEEDLE_S_Z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS,
        h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed,
        h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("S.z move failed.\n");
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles R2.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
        return -1;
    }

    /* 等待抓手的Z轴复位完成 */
    if (0 != motor_timedwait(MOTOR_CATCHER_Z, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset graps A.z timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Z);
        return -1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_REAGENT_TABLE, 1);    /* 试剂盘上电复位耗时最长大约35s */
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CATCHER_Y, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset catcher.y timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Y);
        return -1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_CATCHER_X, 1);
    if (0 != motor_timedwait(MOTOR_CATCHER_X, MOTOR_DEFAULT_TIMEOUT*2)) {
        LOG("reset catcher.x timeout.\n");
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_C_Y);
        return -1;
    }
    FAULT_CHECK_END();

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_S_Y, 1);
    motor_reset(MOTOR_NEEDLE_R2_Y, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.x timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_X);
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_NEEDLE_R2_Y, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles R2.y timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_Y);
        return -1;
    }
    FAULT_CHECK_END();
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_NEEDLE_S_X, 1);
    if (0 != motor_timedwait(MOTOR_NEEDLE_S_X, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset needles S.y timeout.\n");
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_Y);
        return -1;
    }
    FAULT_CHECK_END();

    /* 复位混匀电机 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_reset(MOTOR_MIX_1, 1);
    motor_reset(MOTOR_MIX_2, 1);
    motor_reset(MOTOR_MIX_3, 1);
    if (0 != motor_timedwait(MOTOR_MIX_1, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset mix1 timeout.\n");
        FAULT_CHECK_DEAL(FAULT_MIX, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_MIX1);
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_MIX_2, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset mix2 timeout.\n");
        FAULT_CHECK_DEAL(FAULT_MIX, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_MIX2);
        return -1;
    }
    if (0 != motor_timedwait(MOTOR_MIX_3, MOTOR_DEFAULT_TIMEOUT)) {
        LOG("reset mix3 timeout.\n");
        FAULT_CHECK_DEAL(FAULT_MIX, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_MIX3);
        return -1;
    }
    FAULT_CHECK_END();
    
    /* 混匀复位结束后，需要再转1/4圈(200步)，以确保磁铁再最下方 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    motor_move_sync(MOTOR_MIX_1, CMD_MOTOR_MOVE_STEP, 200, 10000, MOTOR_DEFAULT_TIMEOUT);
    motor_move_sync(MOTOR_MIX_2, CMD_MOTOR_MOVE_STEP, 200, 10000, MOTOR_DEFAULT_TIMEOUT);
    motor_move_sync(MOTOR_MIX_3, CMD_MOTOR_MOVE_STEP, 200, 10000, MOTOR_DEFAULT_TIMEOUT);
    FAULT_CHECK_END();
    
    /* 等待试剂盘慢速复位完成 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (0 != motor_timedwait(MOTOR_REAGENT_TABLE, MOTOR_DEFAULT_TIMEOUT*2)) {/* 试剂盘上电复位耗时最长大约35s */
        LOG("reset reagent table timeout.\n");
        FAULT_CHECK_DEAL(FAULT_REAGENT_TABLE, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_REAGENT_TABLE);
        return -1;
    }
    FAULT_CHECK_END();
    
    LOG("reset all finish\n");

    return 0;
}

/* 电机同时到达的算法 */
int calc_motor_move_in_sync(motor_time_sync_attr_t *motor_a_attr, motor_time_sync_attr_t *motor_b_attr)
{
    double smax_a = 0.0, smax_b = 0.0, run_time_a = 0.0, run_time_b = 0.0, V1_speed = 0.0, V2_speed = 0.0;
    motor_a_attr->v0_speed = MOTOR_V0_DEF_SPEED;
    motor_b_attr->v0_speed = MOTOR_V0_DEF_SPEED;
    /* Smax =( Vmax^2 – Vo^2)/a */
    smax_a = (pow(motor_a_attr->vmax_speed, 2) - pow(motor_a_attr->v0_speed, 2)) / motor_a_attr->acc;
    smax_b = (pow(motor_b_attr->vmax_speed, 2) - pow(motor_b_attr->v0_speed, 2)) / motor_b_attr->acc;

    /* 目标位移S ≤ Smax */
    if ((double)motor_a_attr->step <= smax_a) {
        /* T_run=2*(-Vo+√(〖Vo〗^2+S*a))/a */
        run_time_a = 2 * (-motor_a_attr->v0_speed + sqrt(pow(motor_a_attr->v0_speed, 2) + (motor_a_attr->step * motor_a_attr->acc)) / motor_a_attr->acc);
    } else {
        /* T_run=((Vmax-Vo)^2+S*a)/(a*Vmax) */
        run_time_a = (pow((motor_a_attr->vmax_speed - motor_a_attr->v0_speed), 2) + (motor_a_attr->step * motor_a_attr->acc)) / (motor_a_attr->acc * motor_a_attr->vmax_speed);
    }
    if ((double)motor_b_attr->step <= smax_b) {
        /* T_run=2*(-Vo+√(〖Vo〗^2+S*a))/a */
        run_time_b = 2 * (-motor_b_attr->v0_speed + sqrt(pow(motor_b_attr->v0_speed, 2) + (motor_b_attr->step * motor_b_attr->acc)) / motor_b_attr->acc);
    } else {
        /* T_run=((Vmax-Vo)^2+S*a)/(a*Vmax) */
        run_time_b = (pow((motor_b_attr->vmax_speed - motor_b_attr->v0_speed), 2) + (motor_b_attr->step * motor_b_attr->acc)) / (motor_b_attr->acc * motor_b_attr->vmax_speed);
    }

    if (run_time_a > run_time_b) {   /* B先到，修改B参数 */
        if (motor_b_attr->step <= smax_b) {
            /* a = 2*(S – Vo*T)/T */
            motor_b_attr->acc = 2 * (motor_b_attr->step - (motor_b_attr->v0_speed * run_time_b)) / run_time_b;
        } else {
            /* V1=(a*T+Vo)/2+√(〖(T*a+Vo〗^2)1/4-a*S)   */
            V1_speed = ((motor_b_attr->acc * run_time_b + motor_b_attr->v0_speed) / 2) + (sqrt((run_time_b * motor_b_attr->acc + pow(motor_b_attr->v0_speed, 2))/4-(motor_b_attr->acc * motor_b_attr->step)));
            if (V1_speed < motor_b_attr->vmax_speed) {
                motor_b_attr->vmax_speed = (uint32_t)V1_speed;
            } else {
                V2_speed = V1_speed;
            }
        }
    } else if (run_time_a < run_time_b) { /* A先到，修改A参数 */

    } else {

    }
    return V2_speed = V1_speed;
}

/* 电机时间控制的算法 */
int calc_motor_move_in_time(const motor_time_sync_attr_t *motor_attr, double move_time_s)
{
    double smax=0.0, trun=0.0, calc_acc=0.0, smax_temp=0.0;

    if (motor_attr->step == 0) {
        return (int)motor_attr->max_acc;
    }
//    motor_attr->v0_speed = MOTOR_V0_DEF_SPEED;

    /* Smax =( Vmax^2 – Vo^2)/Amax */
    smax = (pow(motor_attr->vmax_speed, 2) - pow(motor_attr->v0_speed, 2)) / motor_attr->max_acc;
    /* Tmax =2*( Vmax – Vo)/Amax */
//    tmax = 2 * (motor_attr->vmax_speed - motor_attr->v0_speed) / motor_attr->max_acc;

//    LOG("smax = %lf\n", smax);
    if (motor_attr->step <= smax) {
        /* T_run=2*(-Vo+√(〖Vo〗^2+S*Amax))/Amax */
        trun = 2 * ((uint64_t)(sqrt((uint64_t)pow(motor_attr->v0_speed, 2) + (uint64_t)((uint64_t)motor_attr->step *(uint64_t)motor_attr->max_acc))) - (uint64_t)motor_attr->v0_speed) / (uint64_t)motor_attr->max_acc;
//        LOG("motor_attr->step <= smax trun = %lf\n", trun);
        if (move_time_s > trun) {
            /* A=(4*(S-Vo*T))/(T^2) */
            calc_acc = 4 * (motor_attr->step - (motor_attr->v0_speed * move_time_s)) / pow(move_time_s, 2);
        } else if (move_time_s < trun) {
//            LOG("move_time_s < trun !!!\n");
            calc_acc = motor_attr->max_acc;
        } else {
//            LOG("move_time_s = trun !!!\n");
            calc_acc = motor_attr->max_acc;
        }
    } else {
        /* T_run=((Vmax-Vo)^2+S*Amax)/(Amax*Vmax) */
        //LOG("vmax = %lf, v0 = %lf, step = %lf, accmax = %lf\n", (double)motor_attr->vmax_speed, (double)motor_attr->v0_speed, (double)motor_attr->step, (double)motor_attr->max_acc);
       // LOG("pow = %lf\n",(double)(pow(motor_attr->vmax_speed - motor_attr->v0_speed, 2)));
       // LOG("S*acc = %lf\n", (double)(motor_attr->step * motor_attr->max_acc));
       // LOG("acc*vmax = %lld\n", ((long long)motor_attr->max_acc * (long long)motor_attr->vmax_speed));
        trun = (uint64_t)((uint64_t)pow((uint64_t)motor_attr->vmax_speed - (uint64_t)motor_attr->v0_speed, 2) + (uint64_t)((uint64_t)motor_attr->step * (uint64_t)motor_attr->max_acc)) / (uint64_t)((uint64_t)motor_attr->max_acc * (uint64_t)motor_attr->vmax_speed);
//        LOG("motor_attr->step > smax trun = %lf\n", trun);
        if (move_time_s < trun) {
//            LOG("move_time_s < trun !!!\n");
            calc_acc = motor_attr->max_acc;
        } else if (move_time_s > trun) {
            /* Smax_temp=((Vmax+Vo)*T)/2 */
            smax_temp = (motor_attr->vmax_speed + motor_attr->v0_speed) * move_time_s / 2;
            if (motor_attr->step <= smax_temp) {
                /* A=(4*(S-Vo*T))/(T^2) */
                calc_acc = 4 * (motor_attr->step - (motor_attr->v0_speed * move_time_s)) / pow(move_time_s, 2);
//            LOG("motor_attr->step <= smac_temp\n");
            } else {
                calc_acc = pow(motor_attr->vmax_speed - motor_attr->v0_speed, 2) / ((move_time_s * motor_attr->vmax_speed) - motor_attr->step);
//                LOG("condition2\n");
            }
        } else {
//            LOG("move_time_s = trun !!!\n");
            calc_acc = motor_attr->max_acc;
        }
    }
//    LOG("calc acc = %d\n",(int)calc_acc);
    if (calc_acc < 0 || calc_acc >= motor_attr->max_acc) {
        calc_acc = motor_attr->max_acc;
    } else if (calc_acc < 10) {
//        LOG("acc:%f, %lld\n", calc_acc, motor_attr->max_acc);
        calc_acc = 10.0;
    }
//    LOG("calc acc = %d\n", (int)calc_acc);
    return (int)calc_acc;
}


