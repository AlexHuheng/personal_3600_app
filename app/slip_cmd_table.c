#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <list.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <arpa/inet.h>

#include "slip/slip_node.h"
#include "slip/slip_port.h"
#include "slip/slip_msg.h"
#include "slip/slip_process.h"
#include "log.h"
#include "slip_cmd_table.h"
#include "work_queue.h"
#include "movement_config.h"
#include "module_common.h"
#include "thrift_service_software_interface.h"
#include "module_sampler_ctl.h"
#include "module_reagent_table.h"
#include "module_optical.h"
#include "module_liquid_detect.h"
#include "module_upgrade.h"
#include "module_liquied_circuit.h"
#include "device_status_count.h"

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 运动速度 */
    int speed;
}__attribute__((packed)) motor_speed_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 运动速度 */
    int speed;
    /* 加速度 */
    int acc;

    /* 速度模式下，最大运动步长 */
    int max_step;
    /* 速度模式 是否启用最大步长限制 */
    unsigned char speed_flag;
}__attribute__((packed)) motor_speed_ctl_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 移动位移量 */
    int position;
    /* 运动速度 */
    int speed;
}__attribute__((packed)) motor_step_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 移动位移量 */
    int position;
    /* 运动速度 */
    int speed;
    int down_speed;
}__attribute__((packed)) motor_liq_detect_move_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 移动位移量 */
    int position;
    /* 运动速度 */
    int speed;
    /* 加速度 */
    int acc;
}__attribute__((packed)) motor_step_ctl_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 移动位移量 */
    int position_x;
    /* 移动位移量 */
    int position_y;
    /* 运动速度 */
    int speed;
    /* 加速度 */
    int acc;
}__attribute__((packed)) motor_step_dual_ctl_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 复位类型 */
    unsigned char type;
}__attribute__((packed)) motor_reset_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 复位类型 */
    unsigned char type;
    /* 运动速度 */
    int speed;
    /* 加速度 */
    int acc;
    /* 缓慢运动脉冲 */
    int slow_step;
}__attribute__((packed)) motor_reset_ctl_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 执行结果 */
    unsigned char status;
}__attribute__((packed)) motor_result_t;

typedef struct
{
    /* 0: A9, 1: M7 */
    unsigned char type;
    /* 执行结果 */
    unsigned char status[MAX_MOTOR_NUM];
}__attribute__((packed)) motor_result_ex_t;

typedef struct
{
    unsigned char encoder_id;
    int encoder_value;
}__attribute__((packed)) encoder_result_t;

typedef struct
{
    /* GPIO序号 */
    unsigned short gpio_id;
    /* 执行结果 */
    unsigned char status;
}__attribute__((packed)) gpio_result_t;

typedef struct
{
    /* GPIO序号 */
    unsigned short gpio_id;
    /* 状态设置 */
    unsigned char status;
}__attribute__((packed)) gpio_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 该电机所挂光电的数量, 最大支持10个 */
    unsigned short pe_cnt;
    motor_pe_t pe[MOTOR_PE_MAX];
}__attribute__((packed)) motor_set_pe_t;

typedef struct
{
    /* 子板通讯ID */
    unsigned char board_id;
    /* 获取结果 */
    char version[64];
}__attribute__((packed)) firmware_result_t;

typedef struct
{
    /* 子板通讯ID */
    unsigned char board_id;
    /* 获取结果 */
    int R0;
    int R1;
    int R2;
    int R3;
    int R12;
    int LR;
    int PC;
    int PSR;
}__attribute__((packed)) board_error_result_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 执行结果 */
    int pos;
}__attribute__((packed)) motor_pos_result_t;

typedef struct
{
    /* 电机序号 */
    unsigned char motor_id;
    /* 混匀光电计数 */
    unsigned short circle;
}__attribute__((packed)) motor_mix_circle_result_t;

typedef struct {
    /* 电机序号 */
    unsigned char motor_id;
    /* 复位类型 */
    unsigned char type;
    /* 超时时间 */
    int msec;
}motor_reset_async_t;

typedef struct
{
    /* bldc序号 */
    unsigned char   index;
    /* 参数 */
    unsigned char   status;
}__attribute__((packed)) bldc_data_t;

typedef struct
{
    /* bldc序号 */
    unsigned char   index;
    /* 执行结果 */
    unsigned char   status;
}__attribute__((packed)) bldc_result_t;

typedef struct
{
    unsigned char   index;
    int             speed;
}__attribute__((packed)) bldc_speed_t;

typedef struct {
    const slip_port_t *port;
    slip_msg_t *msg;
    void (*fun)(const slip_port_t *port, slip_msg_t *msg);
    struct list_head sibling;
}waiting_node_t;

struct ctl_cmd_type {
    unsigned char type;
    unsigned char sub_type;
    void (*fun)(const slip_port_t *port, slip_msg_t *msg);
};

static pthread_mutex_t waiting_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t waiting_list_cond = PTHREAD_COND_INITIALIZER;
static struct list_head waiting_list;
static int waiting_list_cnt = 0;
//static int reagent_rotate_index_current = 0;

static void slip_event_report(const slip_port_t *port, slip_msg_t *msg);
static void slip_key_reag_report(const slip_port_t *port, slip_msg_t *msg);
static void slip_rack_pull_out_report(const slip_port_t *port, slip_msg_t *msg);
static void slip_rack_push_in_err_report(const slip_port_t *port, slip_msg_t *msg);
static void slip_eletro_err_report(const slip_port_t *port, slip_msg_t *msg);
static void slip_get_clot_data(const slip_port_t *port, slip_msg_t *msg);
static void slip_get_pressure_data(const slip_port_t *port, slip_msg_t *msg);

static struct ctl_cmd_type slip_cmd_table[] =
{
    {OTHER_TYPE, OTHER_LIQUID_DETECT_GET_SUBTYPE, slip_liquid_detect_get_addata_async}, /* 液面探测AD值 */
    {OTHER_TYPE, OTHER_LIQUID_DETECT_RESULT_GET_SUBTYPE, slip_liquid_detect_get_result_async},  /* 液面探测结果 */
    {OTHER_TYPE, OTHER_LIQUID_DETECT_ALL_DATA_SUBTYPE, slip_liquid_detect_get_all_data_async},  /* 数据上报 */

    {OTHER_TYPE, OTHER_OPTICAL_DATA_GET_SUBTYPE, slip_optical_get_data_async},   /* M0光学结果上报 */
    {OTHER_TYPE, OTHER_OPTICAL_BKDATA_GET_SUBTYPE, slip_optical_get_bkdata_async},  /* M0光学本底噪声上报 */
    {OTHER_TYPE, OTHER_OPTICAL_CURR_CALC_SUBTYPE, slip_optical_curr_calc_async},    /* M0光学校准电流上报 */
    {OTHER_TYPE, OTHER_OPTICAL_VERSION_GET_SUBTYPE, slip_optical_get_version_async}, /* M0光学版本号上报 */

    {UPGRADE_TYPE, UPGRADE_ACK_BEGIN_SUBTYPE, ack_update_result},
    {UPGRADE_TYPE, UPGRADE_ACK_WORKING_SUBTYPE, ack_update_working},
    {UPGRADE_TYPE, UPGRADE_ACK_STOP_SUBTYPE, ack_update_stop},

    {OTHER_TYPE, OTHER_EVENT_SUBTYPE, slip_event_report}, 	/* 事件上报 */

    {SAMPLER_TYPE, SAMPLER_RACK_INFOR_REPORT_SUBTYPE, slip_racks_infor_report}, /* 进架后试管架信息主动上报 */
    {SAMPLER_TYPE, SAMPLER_KEY_REAG_REPORT_SUBTYPE, slip_key_reag_report},
    {SAMPLER_TYPE, SAMPLER_RACK_PULL_OUT_SUBTYPE, slip_rack_pull_out_report},
    {SAMPLER_TYPE, SAMPLER_RACK_PUSH_IN_ERR_SUBTYPE, slip_rack_push_in_err_report},
    {SAMPLER_TYPE, SAMPLER_ELETRO_ERR_SUBTYPE, slip_eletro_err_report},
    {SAMPLER_TYPE, SAMPLER_CLOT_GET_REPONSE_SUBTYPE, slip_get_clot_data},
    {SAMPLER_TYPE, SAMPLER_PRESSURE_DATA_SUBTYPE, slip_get_pressure_data},
};

/* 清除 slip链表中 电机相关的信息 */
int clear_slip_list_motor()
{
    waiting_node_t *pos, *n;
    
    pthread_mutex_lock(&waiting_list_mutex);
    list_for_each_entry_safe(pos, n, &waiting_list, sibling) {
        if (pos->msg->cmd_type.type == MOTOR_TYPE) {
            waiting_list_cnt--;
            list_del(&pos->sibling);
            free(pos->msg);
            free(pos); 
        }
    }
    pthread_mutex_unlock(&waiting_list_mutex);

    return 0;
}

void wake_up_analyzer_work(void)
{
    LOG("send magic package to PC...\n");
}

///* 选转指定试剂架到窗口 pan_idx 1-6对应A-F */
int reagent_switch_rotate_ctl(int pan_idx, reagnet_mode_t mode)
{
    static int regent_rotate_flag = 0;
    unsigned char motor_z[2] = {MOTOR_NEEDLE_S_Z, MOTOR_NEEDLE_R2_Z};
    reag_table_cotl_t botton_rotate_ctl = {0};

    if (machine_maintence_state_get() == 1) {
        LOG("reagent table is using.\n");
        return -1;
    }
    if ((get_machine_stat() == MACHINE_STAT_STANDBY) && regent_rotate_flag == 0) {
        regent_rotate_flag = 1;
        machine_maintence_state_set(1);
        set_machine_stat(MACHINE_STAT_RUNNING);
        if ((gpio_get(PE_NEEDLE_S_Z) == 0) ||  (gpio_get(PE_NEEDLE_R2_Z) == 1)) {
            motor_move_async(MOTOR_NEEDLE_S_Z, CMD_MOTOR_RST, 0, 1);
            motor_move_async(MOTOR_NEEDLE_R2_Z, CMD_MOTOR_RST, 0, 1);
            if (0 != motors_timedwait(motor_z, ARRAY_SIZE(motor_z), MOTOR_DEFAULT_TIMEOUT)) {
                LOG("reset  s/r2 z timeout.\n");
                machine_maintence_state_set(0);
                set_machine_stat(MACHINE_STAT_STANDBY);
                regent_rotate_flag = 0;
                return -1;
            }
        }

        botton_rotate_ctl.table_move_type = TABLE_BOTTON_MOVE;
        botton_rotate_ctl.pan_idx = pan_idx;
        botton_rotate_ctl.button_mode = mode;
        reagent_table_move_interface(&botton_rotate_ctl);
        machine_maintence_state_set(0);
        set_machine_stat(MACHINE_STAT_STANDBY);
        regent_rotate_flag = 0;
    }
    return 0;
}

static void slip_event_report(const slip_port_t *port, slip_msg_t *msg)
{

}

static void slip_rack_pull_out_report(const slip_port_t *port, slip_msg_t *msg)
{
    uint8_t index = 0;

    index = msg->data[0];
    LOG("rack report: pull out. channel:%d\n", index);

    if (0 == get_power_off_stat()) {
        report_pull_out_rack(index + 1);
    }
}

static void slip_rack_push_in_err_report(const slip_port_t *port, slip_msg_t *msg)
{
    uint8_t index = 0;
    uint8_t error_code = 0;

    index = msg->data[0];
    error_code = msg->data[1];
    LOG("rack report: push in error. channel:%d\n", index);

    if (0 == get_power_off_stat()) {
        report_rack_push_in_error(index + 1, error_code);
        device_status_count_scanner_check(SCANNER_RACKS);
    }
}

static void slip_eletro_err_report(const slip_port_t *port, slip_msg_t *msg)
{
    uint8_t index = 0;
    uint8_t error_code = 0;

    index = msg->data[0];
    error_code = msg->data[1];

    LOG("board report: eletro ctl error. channel:%d\n", index);

    if (error_code) {
        sampler_fault_infor_report(index);
    }
}

static void slip_key_reag_report(const slip_port_t *port, slip_msg_t *msg)
{
    LOG("button_event: reagent spin ack.\n");

    if (0 == reagent_switch_rotate_ctl(0 , MODE_BUTTON)) {
//        report_button_bracket_rotating(reagent_rotate_index_current);
    }
}

static void slip_cmd_table_callback(void *arg)
{
    waiting_node_t *node = (waiting_node_t *)arg;

    node->fun(node->port, node->msg);
    free(node->msg);
    free(node);
}

static void slip_get_clot_data(const slip_port_t *port, slip_msg_t *msg)
{
    clot_para_t *result = (clot_para_t *)msg->data;

    set_clot_data(result);
}

static void slip_get_pressure_data(const slip_port_t *port, slip_msg_t *msg)
{
    int i = 0, orderno = 0;
    uint16_t data = 0;
    static int count;
    static float press[2000];

    for (i=0; i<100; i++) {
        data = 0;
        if (msg->data[i*2] == 0xff && msg->data[i*2+1] == 0xff) {
            data = (msg->data[200]) << 8;
            data += (msg->data[201]);
            orderno = data << 16;
            data = 0;
            data = (msg->data[203]) << 8;
            data += (msg->data[202]);//此处增加订单号，该订单号来源于下发时同传至进样器的订单号，该号存于最后增加的两个数据中
            orderno += data;
            LOG("get orderno : %d, data count %d.\n", orderno, count);
            press_data_add_in_list(count, press, orderno);
            count = 0;
            memset(press, 0, sizeof(float)*2000);
            break;
        }
        data = (msg->data[i*2+1]) << 8;
        data += msg->data[i*2];
        if (data == 0) continue;//数据过滤
        press[count] = 1.831*data - 15000;
        count++;
        if (count == 2000) {
            count = 0;
            memset(press, 0, sizeof(float)*2000);
        }
    }
}

void slip_process_api(const slip_port_t *port, slip_msg_t *msg)
{
    int i;

    for (i = 0; i < sizeof(slip_cmd_table) / sizeof(slip_cmd_table[0]); i++) {
        if (msg->cmd_type.type == slip_cmd_table[i].type && msg->cmd_type.sub_type == slip_cmd_table[i].sub_type) {
            if (slip_cmd_table[i].fun) {
                waiting_node_t *node = calloc(1, sizeof(waiting_node_t));
                node->port = port;
                node->msg = calloc(1, sizeof(slip_msg_t));
                memcpy(node->msg, msg, sizeof(slip_msg_t));
                node->fun = slip_cmd_table[i].fun;
                work_queue_add(slip_cmd_table_callback, node);
            }
            return;
        }
    }

    waiting_node_t *node = calloc(1, sizeof(waiting_node_t));
    node->port = port;
    node->msg = calloc(1, sizeof(slip_msg_t));
    memcpy(node->msg, msg, sizeof(slip_msg_t));

    pthread_mutex_lock(&waiting_list_mutex);
    list_add_tail(&node->sibling, &waiting_list);
    waiting_list_cnt++;
    pthread_cond_broadcast(&waiting_list_cond);
    //LOG("waiting list cnt %d\n", waiting_list_cnt);
    pthread_mutex_unlock(&waiting_list_mutex);
}

int result_timedwait(unsigned char type, unsigned char sub_type, waiting_list_callback cb, void *data, int timeout_ms)
{
    int ret = -1;
    int timeout = timeout_ms;
    long long ms = 0, ms1 = 0, ms2 = 0;
    struct timespec abstime;
    struct timeval now;

    ms1 = get_time();
    while (ret != 0) {
        ms2 = get_time();
        timeout = timeout_ms - (ms2 - ms1);
        if (timeout <= 0) {
            LOG("timeout, type: %x, sub_type: %x, %d, %lld\n", type, sub_type, timeout_ms, ms2 - ms1);
            return -1;
        }

        gettimeofday(&now, NULL);
        ms = now.tv_usec / 1000 + timeout;
        abstime.tv_sec = now.tv_sec + ms / 1000;
        abstime.tv_nsec = ms % 1000 * 1000000;

        pthread_mutex_lock(&waiting_list_mutex);
        while (waiting_list_cnt <= 0) {
            if (0 != pthread_cond_timedwait(&waiting_list_cond, &waiting_list_mutex, &abstime)) {
                pthread_mutex_unlock(&waiting_list_mutex);
                LOG("timeout, type: %x, sub_type: %x, %d, %lld\n", type, sub_type, timeout_ms, get_time() - ms1);
                return -1;
            }
        }

        waiting_node_t *pos, *n;
        list_for_each_entry_safe(pos, n, &waiting_list, sibling) {
            if (pos->msg->cmd_type.type == type && (pos->msg->cmd_type.sub_type == sub_type || sub_type == CMD_SUBTYPE_ANY)) {
                ret = cb(pos->port, pos->msg, data);
                if (ret != -1) {
                    waiting_list_cnt--;
                    list_del(&pos->sibling);
                    free(pos->msg);
                    free(pos);
                    if (ret == 0) {
                        break;
                    }
                }
            }
        }

        pthread_mutex_unlock(&waiting_list_mutex);
        usleep(2000);
    }

    return 0;
}

static unsigned char slip_dst_node_translate_by_motor_id(unsigned char motor_id)
{
    return SLIP_NODE_A9_RTOS;
}

static unsigned short slip_dst_node_translate_by_gpio_id(unsigned short gpio_id)
{
    return gpio_id < SAMPLER_PE_START ? SLIP_NODE_A9_RTOS : SLIP_NODE_SAMPLER;
}

static int motor_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    if (msg->length == 0) {
        return 1;
    }

    if (msg->cmd_type.sub_type == MOTOR_ALL_STOP_SUBTYPE || msg->cmd_type.sub_type == MOTOR_ALL_POWER_CTL_SUBTYPE) {
        motor_result_ex_t *data = (motor_result_ex_t *)arg;
        motor_result_ex_t *result = (motor_result_ex_t *)msg->data;
        if (data->type == result->type) {
            /* 取对端返回的实际长度 */
            memcpy(data->status, result->status, msg->length>MAX_MOTOR_NUM ? MAX_MOTOR_NUM : msg->length-1);
            return 0;
        }
    } else {
        motor_result_t *data = (motor_result_t *)arg;
        motor_result_t *result = (motor_result_t *)msg->data;
        if (result->motor_id == data->motor_id) {
            data->status = result->status;
            if (data->status != 0) {
                if (result->motor_id == MOTOR_MIX_1 || result->motor_id == MOTOR_MIX_2 || result->motor_id == MOTOR_MIX_3 ||
                    result->motor_id == MOTOR_NEEDLE_R2_PUMP || result->motor_id == MOTOR_NEEDLE_S_PUMP) {
                    /* 忽略柱塞泵和混匀电机 */
                    LOG("ERROR! RTOS move motor(%d) failed(%d)! ignore.\n", data->motor_id, data->status);
                    data->status = 0;
                } else {
                    LOG("ERROR! RTOS move motor(%d) failed(%d)!\n", data->motor_id, data->status);
                    data->status = -1;
                }
            }
            return data->status;
        }
    }

    return -1;
}

static int motor_current_pos_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    motor_pos_result_t *data = (motor_pos_result_t *)arg;
    motor_pos_result_t *result = (motor_pos_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->motor_id == data->motor_id) {
        data->pos = ntohl(result->pos);
        return 0;
    }

    return -1;
}

/* 获取电机当前绝对位置 */
int motor_current_pos_timedwait(unsigned char motor_id, int msecs)
{
    motor_pos_result_t result = {0};

    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_GET_POSITION_SUBTYPE, sizeof(motor_id), &motor_id);
    if (msecs) {
        result.motor_id = motor_id;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_GET_POSITION_SUBTYPE, motor_current_pos_result, &result, msecs)) {
            return result.pos;
        }
        return -1;
    }
    return 0;
}

/* 电机复位后，需要额外执行的移动操作 */
int motor_extra_steps_set(unsigned char motor_id)
{
    int i , ret = -1;
    extra_offest_t offest[0] = {
        
    };

    for (i = 0; i < sizeof(offest) / sizeof(extra_offest_t) ; i++) {
        if (offest[i].motor_idx == motor_id) {
            ret = slip_motor_move_timedwait(offest[i].motor_idx, offest[i].pos, 1000);
        }
    }
    return ret;
}

/* 电机复位, type=1: 上电复位, type=0: 常规复位 */
int motor_reset_timedwait(unsigned char motor_id, unsigned char type, int msecs)
{
    unsigned char status;
    motor_reset_t mo_reset;
    mo_reset.motor_id = motor_id;
    mo_reset.type = type;

    motor_result_t result = {0};
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_RESET_SUBTYPE, sizeof(mo_reset), &mo_reset);
    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_RESET_SUBTYPE, motor_result, &result, msecs)) {
            status = result.status;
            motor_extra_steps_set(motor_id);
            return status;
        }
        return -1;
    }
    return 0;
}

/* 电机复位, type=1: 上电复位, type=0: 常规复位 */
int motor_reset_ctl_timedwait(unsigned char motor_id, unsigned char type, int speed, int acc, int msecs, int slow_step)
{
    unsigned char status;
    motor_reset_ctl_t mo_reset;
    mo_reset.motor_id = motor_id;
    mo_reset.type = type;
    mo_reset.speed = htonl(speed);
    mo_reset.acc = htonl(acc);
    mo_reset.slow_step = htonl(slow_step);

    motor_result_t result = {0};
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_RESET_CTL_SUBTYPE, sizeof(mo_reset), &mo_reset);
    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_RESET_CTL_SUBTYPE, motor_result, &result, msecs)) {
            status = result.status;
            motor_extra_steps_set(motor_id);
            return status;
        }
        return -1;
    }
    return 0;
}

/* 电机复位, type=1: 上电复位, type=0: 常规复位 */
int motor_reset_steps_timedwait(unsigned char motor_id, unsigned char type, int speed, int acc, int msecs, int extra_step)
{
    unsigned char status;
    motor_reset_ctl_t mo_reset;
    mo_reset.motor_id = motor_id;
    mo_reset.type = type;
    mo_reset.speed = htonl(speed);
    mo_reset.acc = htonl(acc);
    mo_reset.slow_step = htonl(extra_step);

    motor_result_t result = {0};
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_RESET_STEPS_SUBTYPE, sizeof(mo_reset), &mo_reset);
    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_RESET_STEPS_SUBTYPE, motor_result, &result, msecs)) {
            status = result.status;
            return status;
        }
        return -1;
    }
    return 0;
}

/* 电机速度运动模式 */
int motor_speed_timedwait(unsigned char motor_id, int speed, int msecs)
{
    motor_speed_t data = {0};
    motor_result_t result = {0};

    data.motor_id = motor_id;
    data.speed = htonl(speed);
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_SPEED_SUBTYPE, sizeof(data), &data);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_SPEED_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        return -1;
    }

    return 0;
}

/* 电机速度运动控制模式 */
int motor_speed_ctl_timedwait(unsigned char motor_id, int speed, int acc, int max_step, unsigned char speed_flag, int msecs)
{
    motor_speed_ctl_t data = {0};
    motor_result_t result = {0};

    data.motor_id = motor_id;
    data.speed = htonl(speed);
    data.acc = htonl(acc);
    data.max_step = htonl(max_step);
    data.speed_flag = speed_flag;
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_SPEED_CTL_SUBTYPE, sizeof(data), &data);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_SPEED_CTL_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        return -1;
    }

    return 0;
}

/* 电机设置光电 */
int motor_set_pe_timedwait(unsigned char motor_id, motor_pe_t *pe, int pe_cnt, int msecs)
{
    int i = 0;
    motor_set_pe_t motor_pe = {0};
    motor_result_t result = {0};
    int len;

    motor_pe.motor_id = motor_id;
    motor_pe.pe_cnt = htons(pe_cnt);

    for (i = 0; i < pe_cnt; i++) {
        motor_pe.pe[i].pe_id = htons(pe->pe_id);
        motor_pe.pe[i].speed = htonl(pe->speed);
    }

    len = 3 + pe_cnt * sizeof(motor_pe_t);
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_BINDING_PE_SUBTYPE, len, &motor_pe);
    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_BINDING_PE_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        return -1;
    }

    return 0;
}

/* 电机步长运动模式 */
int motor_step_timedwait(unsigned char motor_id, int step, int speed, int msecs)
{
    motor_step_t data = {0};
    motor_result_t result = {0};

    data.motor_id = motor_id;
    data.position = htonl(step);
    data.speed = htonl(speed);
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_STEP_SUBTYPE, sizeof(data), &data);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_STEP_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        LOG("timeout, motor: %d, step: %d, speed: %d, msecs: %d\n", motor_id, step, speed, msecs);
        return -1;
    }

    return 0;
}

/* 电机液面探测运动模式 */
int motor_ld_move_sync(unsigned char motor_id, int step, int speed, int c_speed, int msecs)
{
    motor_liq_detect_move_t data        = {0};
    motor_result_t          result      = {0};

    data.motor_id = motor_id;
    data.position = htonl(step);
    data.speed = htonl(speed);
    data.down_speed = htonl(c_speed);
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0,
        MOTOR_TYPE, MOTOR_LIQ_DETECT_SUBTYPE, sizeof(data), &data);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_LIQ_DETECT_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        LOG("timeout, motor: %d, step: %d, speed: %d, down_speed: %d, msecs: %d\n",
            motor_id, step, speed, c_speed, msecs);
        return -1;
    }

    return 0;
}

/* 电机步长运动控制模式 */
int motor_step_ctl_timedwait(unsigned char motor_id, int position, int speed, int acc, int msecs)
{
    motor_step_ctl_t data = {0};
    motor_result_t result = {0};

    data.motor_id = motor_id;
    data.position = htonl(position);
    data.speed = htonl(speed);
    data.acc = htonl(acc);
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_STEP_CTL_SUBTYPE, sizeof(data), &data);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_STEP_CTL_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        return -1;
    }

    return 0;
}

/* 双电机步长运动控制模式 */
int motor_step_dual_ctl_timedwait(unsigned char motor_id, int position_x, int position_y, int speed, int acc, int msecs, double cost_time)
{
    uint8_t data[21] = {0};
    motor_result_t result = {0};

    *(uint8_t *)&data[0] = motor_id;
    *(int *)&data[1] = htonl(position_x);
    *(int *)&data[5] = htonl(position_y);
    *(int *)&data[9] = htonl(speed);
    *(int *)&data[13] = htonl(acc);
    *(int *)&data[17] = htonl(cost_time*100);

    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_STEP_CTL_DUAL_SUBTYPE, sizeof(data), data);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_STEP_CTL_DUAL_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        return -1;
    }

    return 0;
}


/* 电机停止 */
int motor_stop_timedwait(unsigned char motor_id, int msecs)
{
    motor_result_t result = {0};

    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_STOP_SUBTYPE, sizeof(motor_id), &motor_id);

    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_STOP_SUBTYPE, motor_result, &result, msecs)) {
            return result.status;
        }
        return -1;
    }
    return 0;
}

/* 电机所有电机停止 */
int motor_stop_all_timedwait(int type, unsigned char *status, int msecs)
{
    motor_result_ex_t result = {0};

    slip_send_node(slip_node_id_get(), type == 0 ? SLIP_NODE_A9_RTOS : SLIP_NODE_SAMPLER, 0x0, MOTOR_TYPE, MOTOR_ALL_STOP_SUBTYPE, 0, NULL);

    if (msecs) {
        result.type = type;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_ALL_STOP_SUBTYPE, motor_result, &result, msecs)) {
            memcpy(status, result.status, MAX_MOTOR_NUM);
            return 0;
        }
        return -1;
    }
    return 0;
}

static int encoder_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    encoder_result_t *data = (encoder_result_t *)arg;
    encoder_result_t *result = (encoder_result_t *)msg->data;
    if (msg->length == 0) {
        return 1;
    }

    if (result->encoder_id == data->encoder_id) {
        data->encoder_value = ntohl(result->encoder_value);
        return 0;
    }
    return -1;
}

static int encoder_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    encoder_result_t *data = (encoder_result_t *)arg;
    encoder_result_t *result = (encoder_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->encoder_id == data->encoder_id) {
        data->encoder_value = ntohs(result->encoder_value);
        return 0;
    }

    return -1;
}

/* 设置编码器的值 */
int encoder_set_value(unsigned char encoder_id, int value, int msecs)
{
    encoder_result_t data = {0};
    encoder_result_t result = {0};

    data.encoder_id = encoder_id;
    data.encoder_value = htonl(value);
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(encoder_id), 0x0, MOTOR_TYPE, MOTOR_SET_ENCODER_VALUE_SUBTYPE, sizeof(data), &data);
    LOG("set %d:%d\n", encoder_id, value);
    if (msecs) {
        result.encoder_id = encoder_id;
        result.encoder_value = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_SET_ENCODER_VALUE_SUBTYPE, encoder_set_result, &result, msecs)) {
            return result.encoder_value;
        }
        return -1;
    }

    return 0;
}

/* 获取编码器的值 */
int encoder_get_value(unsigned char encoder_id, int msecs)
{
    encoder_result_t data = {0};
    encoder_result_t result = {0};

    data.encoder_id = encoder_id;
    data.encoder_value = 0;
    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(encoder_id), 0x0, MOTOR_TYPE, MOTOR_GET_ENCODER_VALUE_SUBTYPE, sizeof(data), &data);

    if (msecs) {
        result.encoder_id = encoder_id;
        result.encoder_value = -1;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_GET_ENCODER_VALUE_SUBTYPE, encoder_get_result, &result, msecs)) {
            LOG("get %d:%d\n", encoder_id, result.encoder_value);
            return result.encoder_value;
        }
        return -1;
    }

    return 0;
}


/*
控制所有电机上电状态
type: 0：控制A9电机 1：控制进样器电机
enable: 0：电机下电 1：电机上电
status: 0：执行成功 1：执行失败
*/
int motor_power_ctl_all_timedwait(uint8_t type, uint8_t enable, unsigned char *status, int msecs)
{
    motor_result_ex_t result = {0};

    slip_send_node(slip_node_id_get(), type == 0 ? SLIP_NODE_A9_RTOS : SLIP_NODE_SAMPLER,
        0x0, MOTOR_TYPE, MOTOR_ALL_POWER_CTL_SUBTYPE, sizeof(enable), &enable);

    if (msecs) {
        result.type = type;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_ALL_POWER_CTL_SUBTYPE, motor_result, &result, msecs)) {
            memcpy(status, result.status, MAX_MOTOR_NUM);
            return 0;
        }
        return -1;
    }
    return 0;
}

static int motor_mix_circle_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    motor_mix_circle_result_t *data = (motor_mix_circle_result_t *)arg;
    motor_mix_circle_result_t *result = (motor_mix_circle_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->motor_id == data->motor_id) {
        data->circle = ntohs(result->circle);
        return 0;
    }

    return -1;
}

/* 获取混匀电机的光电计数 */
int motor_mix_circle_get_timedwait(unsigned char motor_id, int msecs)
{
    motor_mix_circle_result_t result = {0};

    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_motor_id(motor_id), 0x0, MOTOR_TYPE, MOTOR_GET_MIX_CIRCLE_SUBTYPE, sizeof(motor_id), &motor_id);
    if (msecs) {
        result.motor_id = motor_id;
        if (0 == result_timedwait(MOTOR_TYPE, MOTOR_GET_MIX_CIRCLE_SUBTYPE, motor_mix_circle_get_result, &result, msecs)) {
            return result.circle;
        }
        return -1;
    }
    return 0;
}

static int bldc_rads_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    bldc_speed_t *data = (bldc_speed_t *)arg;
    bldc_speed_t *result = (bldc_speed_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->speed = ntohl(result->speed);
        return 0;
    }

    return -1;
}

static int bldc_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    bldc_result_t *data = (bldc_result_t *)arg;
    bldc_result_t *result = (bldc_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

int slip_bldc_ctl_set(int index, int status)
{
    bldc_data_t data = {0};
    bldc_result_t result = {0};

    data.index = index;
    data.status = status;

    slip_send_node(slip_node_id_get(),
                    slip_dst_node_translate_by_gpio_id(index),
                    0x0, OTHER_TYPE, OTHER_PWM_SET_SUBTYPE, sizeof(data), (void *)&data);

    result.index = index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_PWM_SET_SUBTYPE, bldc_result, &result, 10000)) {
        return result.status;
    }
    return -1;
}

int slip_bldc_timeout_set(int index, int timeout)
{
    bldc_data_t data = {0};
    bldc_result_t result = {0};

    data.index = index;
    data.status = timeout;

    slip_send_node(slip_node_id_get(),
                    slip_dst_node_translate_by_gpio_id(index),
                    0x0, OTHER_TYPE, OTHER_BLDC_TIMEOUT_SET_SUBTYPE, sizeof(data), (void *)&data);

    result.index = index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_BLDC_TIMEOUT_SET_SUBTYPE, bldc_result, &result, 10000)) {
        return result.status;
    }
    return -1;
}

int slip_bldc_rads_get(int index)
{
    bldc_speed_t result = {0};

    slip_send_node(slip_node_id_get(),
                    slip_dst_node_translate_by_gpio_id(index),
                    0x0, OTHER_TYPE, OTHER_PWM_GET_SUBTYPE, sizeof(index), &index);

    result.index = index;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_PWM_GET_SUBTYPE, bldc_rads_result, &result, 2000)) {
        return result.speed;
    }
    return -1;
}

int motor_timedwait(unsigned char motor_id, long msecs)
{
    motor_result_t result = {0};
    if (msecs) {
        result.motor_id = motor_id;
        result.status = -1;
//        LOG("motor_id:%d, %d\n", motor_id, msecs);
        if (0 == result_timedwait(MOTOR_TYPE, CMD_SUBTYPE_ANY, motor_result, &result, msecs)) {
            if (result.status != 0) {
                LOG("motor %d result status: %d\n", motor_id, result.status);
            }
            return result.status;
        }
    }
    return -1;
}

int motors_timedwait(unsigned char motor_ids[], int motor_count, long msecs)
{
    int i = 0;
    struct timeval tv, tv1, tv2;

    gettimeofday(&tv1, NULL);
    for (i = 0; i < motor_count && msecs; i++) {
        //LOG("motor %d wait %ld\n", motor_ids[i], msecs);
        if (0 != motor_timedwait(motor_ids[i], msecs)) {
            return -1;
        }
        gettimeofday(&tv2, NULL);
        timersub(&tv2, &tv1, &tv);
        msecs -= (tv.tv_sec * 1000 + tv.tv_usec / 1000);
        if (i < (motor_count - 1) && msecs <= 0) {
            return -1;
        }
        tv1 = tv2;
    }
    return 0;
}

static int gpio_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    gpio_result_t *data = (gpio_result_t *)arg;
    gpio_result_t *result = (gpio_result_t *)msg->data;
    unsigned short gpio_id = ntohs(result->gpio_id);

    if (msg->length == 0) {
        return 1;
    }

    if (gpio_id == data->gpio_id) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

int gpio_set(unsigned short index, unsigned char status)
{
    gpio_t data;
    gpio_result_t result;

    if (status == ON) {
        device_status_count_gpio_check(index);
    }

    data.gpio_id = htons(index);
    data.status = status;

    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_gpio_id(index), 0x0, IO_TYPE, IO_SET_SUBTYPE, sizeof(data), &data);

    result.gpio_id = index;
    result.status = -1;
    if (0 == result_timedwait(IO_TYPE, IO_SET_SUBTYPE, gpio_result, &result, 1500)) {
        return result.status;
    }
    return -1;
}

int gpio_get(unsigned short index)
{
    unsigned short gpio_id;
    gpio_result_t result = {0};

    gpio_id = htons(index);

    slip_send_node(slip_node_id_get(), slip_dst_node_translate_by_gpio_id(index), 0x0, IO_TYPE, IO_GET_SUBTYPE, sizeof(gpio_id), &gpio_id);

    result.gpio_id = index;
    result.status = -1;
    if (0 == result_timedwait(IO_TYPE, IO_GET_SUBTYPE, gpio_result, &result, 1500)) {
        return result.status;
    }
    return -1;
}

int indicator_led_set(int led_id, int color, int blink)
{
/*  led_id:指示灯编号，1:仪器状态指示灯 2:反应盘1指示灯 3:反应盘2指示灯;
    color指颜色,1:绿 2:黄 3:红;
    blink是长亮或闪烁 1:常亮 2:闪烁 3:关灯
*/
    LOG("%d, %d, %d\n", led_id, color, blink);
    if (blink == LED_NONE_BLINK) {
        machine_led_set(led_id, color, blink);
        switch (led_id) {
        case LED_MACHINE_ID:
            gpio_set(LED_CTL_STATUS_BREATH_R, 0);
            gpio_set(LED_CTL_STATUS_BREATH_G, 0);
            gpio_set(LED_CTL_STATUS_BREATH_Y, 0);
            gpio_set(LED_CTL_STATUS_R, CPLD_LED_OFF);
            gpio_set(LED_CTL_STATUS_G, CPLD_LED_OFF);
            gpio_set(LED_CTL_STATUS_Y, CPLD_LED_OFF);
            if (color == LED_COLOR_GREEN) {
                gpio_set(LED_CTL_STATUS_G, CPLD_LED_ON);
            } else if (color == LED_COLOR_YELLOW) {
                gpio_set(LED_CTL_STATUS_Y, CPLD_LED_ON);
            } else if (color == LED_COLOR_RED) {
                gpio_set(LED_CTL_STATUS_R, CPLD_LED_ON);
            }
            break;
        case LED_CUVETTE_INS_ID:
            gpio_set(LED_CUVETTE_IN_R, CPLD_LED_OFF);
            gpio_set(LED_CUVETTE_IN_G, CPLD_LED_OFF);
            gpio_set(LED_CUVETTE_IN_Y, CPLD_LED_OFF);
            if (color == LED_COLOR_GREEN) {
                gpio_set(LED_CUVETTE_IN_G, CPLD_LED_ON);
            } else if (color == LED_COLOR_YELLOW) {
                gpio_set(LED_CUVETTE_IN_Y, CPLD_LED_ON);
            } else if (color == LED_COLOR_RED) {
                gpio_set(LED_CUVETTE_IN_R, CPLD_LED_ON);
            }
            break;
        default:
            break;
        }
    } else if (blink == LED_BLINK) {
        if (led_id == LED_MACHINE_ID) {/* 仪器状态指示灯 呼吸模式 */
            gpio_set(LED_CTL_STATUS_BREATH_R, 0);
            gpio_set(LED_CTL_STATUS_BREATH_G, 0);
            gpio_set(LED_CTL_STATUS_BREATH_Y, 0);
            gpio_set(LED_CTL_STATUS_R, CPLD_LED_OFF);
            gpio_set(LED_CTL_STATUS_G, CPLD_LED_OFF);
            gpio_set(LED_CTL_STATUS_Y, CPLD_LED_OFF);
            if (color == LED_COLOR_GREEN) {
                gpio_set(LED_CTL_STATUS_BREATH_G, 1);
            } else if (color == LED_COLOR_YELLOW) {
                gpio_set(LED_CTL_STATUS_BREATH_Y, 1);
            } else if (color == LED_COLOR_RED) {
                gpio_set(LED_CTL_STATUS_BREATH_R, 1);
            }
        } else {
            machine_led_set(led_id, color, blink);
        }
    } else if (blink == LED_OFF) {
        machine_led_set(led_id, color, 1);
        if (led_id == LED_MACHINE_ID) {
            if (color == LED_COLOR_GREEN) {
                gpio_set(LED_CTL_STATUS_BREATH_G, 0);
                gpio_set(LED_CTL_STATUS_G, CPLD_LED_OFF);
            } else if (color == LED_COLOR_YELLOW) {
                gpio_set(LED_CTL_STATUS_BREATH_Y, 0);
                gpio_set(LED_CTL_STATUS_Y, CPLD_LED_OFF);
            } else if (color == LED_COLOR_RED) {
                gpio_set(LED_CTL_STATUS_BREATH_R, 0);
                gpio_set(LED_CTL_STATUS_R, CPLD_LED_OFF);
            }
        } else if (led_id == LED_CUVETTE_INS_ID) {
            if (color == LED_COLOR_GREEN) {
                gpio_set(LED_CUVETTE_IN_G, CPLD_LED_OFF);
            } else if (color == LED_COLOR_YELLOW) {
                gpio_set(LED_CUVETTE_IN_Y, CPLD_LED_OFF);
            } else if (color == LED_COLOR_RED) {
                gpio_set(LED_CUVETTE_IN_R, CPLD_LED_OFF);
            }
        }
    }
    return 0;
}
static int firmware_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    firmware_result_t *data = (firmware_result_t *)arg;
    firmware_result_t *result = (firmware_result_t *)msg->data;
    int version_len;

    if (msg->length == 0) {
        return 1;
    }

    version_len = (msg->cmd_type.sub_type == OTHER_FPGA_VERSION_GET_SUBTYPE) ? 30 : 14;

    if (result->board_id == data->board_id) {
        if (msg->cmd_type.sub_type == OTHER_FPGA_VERSION_GET_SUBTYPE) {
            strncpy(data->version, &result->version[16], version_len - 16);
        } else {
            strncpy(data->version, &result->version[1], version_len - 1);
        }
        LOG("obtain firmware version:%s  node_id:%d\n", data->version, result->board_id);
        return 0;
    }
    return -1;
}

int boards_firmware_version_get(unsigned char board_id, char *version, int len)
{
    firmware_result_t result = {0};

    if (board_id > SLIP_NODE_FPGA_M7 || board_id < SLIP_NODE_A9_LINUX) {
        LOG("the node_id of input param is invalid. %d\n", board_id);
        return -1;
    }

    if (board_id == SLIP_NODE_A9_LINUX) {
        snprintf(result.version, sizeof(result.version), "%s.%s", INPUT_VERSION, INPUT_SHELL_VERSION);
        strncpy(version, result.version, strlen(result.version));
        LOG("obtain firmware version:%s  node_id:%d\n", version, board_id);
        return 0;
    } else if (board_id == SLIP_NODE_OPTICAL_1 || board_id == SLIP_NODE_OPTICAL_2 || board_id == SLIP_NODE_OPTICAL_HIL) {
        strncpy(version, optical_version_get(board_id), 64);
        return 0;
    } else if (board_id == SLIP_NODE_FPGA_A9 || board_id == SLIP_NODE_FPGA_M7) {
        board_id = (board_id == SLIP_NODE_FPGA_A9) ? SLIP_NODE_A9_RTOS : SLIP_NODE_SAMPLER;
        slip_send_node(slip_node_id_get(), board_id, 0x0, OTHER_TYPE, OTHER_FPGA_VERSION_GET_SUBTYPE, 0, NULL);
        result.board_id = board_id;
        if (0 == result_timedwait(OTHER_TYPE, OTHER_FPGA_VERSION_GET_SUBTYPE, firmware_result, &result, 2000)) {
            strncpy(version, result.version, strlen(result.version));
            return 0;
        }
    } else {
        slip_send_node(slip_node_id_get(), board_id, 0x0, OTHER_TYPE, OTHER_BOARDS_FIRMWARE_GET_SUBTYPE, 0, NULL);
        result.board_id = board_id;
        if (0 == result_timedwait(OTHER_TYPE, OTHER_BOARDS_FIRMWARE_GET_SUBTYPE, firmware_result, &result, 2000)) {
            strncpy(version, result.version, strlen(result.version));
            return 0;
        }
    }
    return -1;
}

static int slip_boards_error_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    board_error_result_t *result = (board_error_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    LOG("node_id:%d, R0:0x%x, R1:0x%x, R2:0x%x, R3:0x%x, R12:0x%x, LR:0x%x, PC:0x%x, PSR:0x%x\n", result->board_id, 
        result->R0, result->R1, result->R2, result->R3, result->R12, result->LR, result->PC, result->PSR);

    return 0;
}

int boards_error_get(void)
{
    int ret = 0;

    slip_send_node(slip_node_id_get(), SLIP_NODE_SAMPLER, 0x0, OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, 0, NULL);
    ret += result_timedwait(OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, slip_boards_error_get_result, NULL, 2000);

    slip_send_node(slip_node_id_get(), SLIP_NODE_MAGNECTIC, 0x0, OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, 0, NULL);
    ret += result_timedwait(OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, slip_boards_error_get_result, NULL, 2000);

    slip_send_node(slip_node_id_get(), SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, 0, NULL);
    ret += result_timedwait(OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, slip_boards_error_get_result, NULL, 2000);

    slip_send_node(slip_node_id_get(), SLIP_NODE_LIQUID_DETECT_1, 0x0, OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, 0, NULL);
    ret += result_timedwait(OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, slip_boards_error_get_result, NULL, 2000);

    slip_send_node(slip_node_id_get(), SLIP_NODE_LIQUID_DETECT_2, 0x0, OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, 0, NULL);
    ret += result_timedwait(OTHER_TYPE, OTHER_BOARD_ERROR_GET_SUBTYPE, slip_boards_error_get_result, NULL, 2000);

    if (ret == 0) {
        return 0;
    } else {
        return -1;
    }
}

int get_step_step(unsigned char motor_id)
{
    return h3600_conf_get()->motor_pos[motor_id][0];
}

int get_maxstep(unsigned char motor_id, int pos)
{
    return h3600_conf_get()->motor_pos[motor_id][pos] - h3600_conf_get()->motor_pos[motor_id][0];
}

/* 电机移动 */
int slip_motor_move_timedwait(unsigned char motor_id, int pos, int msec)
{
    int step, speed, acc;

    if (pos >= h3600_conf_get()->motor_pos_cnt[motor_id]) {
        LOG("Invalid motor %d pos %d\n", motor_id, pos);
        return -1;
    }

    step = h3600_conf_get()->motor_pos[motor_id][pos];
    speed = h3600_conf_get()->motor[motor_id].speed;
    acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d move pos %d, step %d\n", motor_id, pos, step);

    if (0 == motor_step_ctl_timedwait(motor_id, step, speed, acc, msec)) {
        return step;
    }

    return -1;
}

/* 电机移动到 */
int slip_motor_move_to_timedwait(unsigned char motor_id, int pos, int msec)
{
    int current_step, to_step, speed, acc, step;

    if (pos >= h3600_conf_get()->motor_pos_cnt[motor_id]) {
        LOG("Invalid motor %d pos %d\n", motor_id, pos);
        return -1;
    }

    current_step = motor_current_pos_timedwait(motor_id, 2000);
    to_step = h3600_conf_get()->motor_pos[motor_id][pos];
    step = to_step - current_step;
    speed = h3600_conf_get()->motor[motor_id].speed;
    acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d move to pos: %d, current_step: %d, to_step: %d, step %d\n", motor_id, pos, current_step, to_step, step);

    if (0 == motor_step_ctl_timedwait(motor_id, step, speed, acc, msec)) {
        return step;
    }

    return -1;
}

/* 电机移动到指定位置，并做offset偏移 */
int slip_motor_move_to_offset_timedwait(unsigned char motor_id, int pos, int offset, int msec)
{
    int current_step, to_step, speed, acc, step;

    if (pos >= h3600_conf_get()->motor_pos_cnt[motor_id]) {
        LOG("Invalid motor %d pos %d\n", motor_id, pos);
        return -1;
    }

    current_step = motor_current_pos_timedwait(motor_id, 2000);
    to_step = h3600_conf_get()->motor_pos[motor_id][pos];
    step = to_step - current_step + offset;
    speed = h3600_conf_get()->motor[motor_id].speed;
    acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d move offset pos: %d, current_step: %d, to_step: %d, offset: %d, step %d\n", motor_id, pos, current_step, to_step, offset, step);

    if (0 == motor_step_ctl_timedwait(motor_id, step, speed, acc, msec)) {
        return step;
    }

    return -1;
}

/* 电机移动指定步数 */
int slip_motor_step_timedwait(unsigned char motor_id, int step, int msec)
{
    int speed, acc;

    speed = h3600_conf_get()->motor[motor_id].speed;
    acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d move step %d\n", motor_id, step);

    if (0 == motor_step_ctl_timedwait(motor_id, step, speed, acc, msec)) {
        return step;
    }

    return -1;
}

/* 电机移动指定步数指定速度 */
int slip_motor_dual_step_ctl_timedwait(unsigned char motor_id, int stepx, int stepy, int speed, int acc, int msec)
{

    LOG("motor %d move stepx %d stepy %d speed %d acc %d\n", motor_id, stepx, stepy, speed, acc);

    if (0 == motor_step_dual_ctl_timedwait(motor_id, stepx, stepy, speed, acc, msec, 0.01)) {
        return stepx;
    }

    return -1;
}


/* 电机移动指定步数 */
int slip_motor_dual_step_timedwait(unsigned char motor_id, int stepx, int stepy, int msec)
{
    int speed, acc;

    speed = h3600_conf_get()->motor[motor_id].speed;
    acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d move stepx %d stepy %d\n", motor_id, stepx, stepy);

    if (0 == motor_step_dual_ctl_timedwait(motor_id, stepx, stepy, speed, acc, msec, 0.01)) {
        return stepx;
    }

    return -1;
}

int slip_motor_step(unsigned char motor_id, int step)
{
    int speed, acc;

    speed = h3600_conf_get()->motor[motor_id].speed;
    acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d move step %d\n", motor_id, step);

    if (0 == motor_step_ctl_timedwait(motor_id, step, speed, acc, 0)) {
        return step;
    }

    return -1;
}

int slip_motor_speed_timedwait(unsigned char motor_id, int msec)
{
    int speed = h3600_conf_get()->motor[motor_id].speed;
    int acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d speed %d\n", motor_id, speed);
    return motor_speed_ctl_timedwait(motor_id, speed, acc, 0, 0, msec);
}

int slip_trash_speed_timedwait(unsigned char motor_id, int msec)
{
    int speed = -h3600_conf_get()->motor[motor_id].speed / 2;
    int acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d speed %d\n", motor_id, speed);
    return motor_speed_ctl_timedwait(motor_id, speed, acc, 0, 0, msec);
}

int slip_motor_diy_speed_timedwait(unsigned char motor_id, int speed, int max_step, unsigned char speed_flag, int msec)
{
    int acc = h3600_conf_get()->motor[motor_id].acc;

    LOG("motor %d speed %d max_step %d speed_flag %d\n", motor_id, speed, max_step, speed_flag);
    return motor_speed_ctl_timedwait(motor_id, speed, acc, max_step, speed_flag, msec);
}

static void slip_motor_reset_async_callback(void *arg)
{
    motor_reset_async_t *r = (motor_reset_async_t *)arg;
    int speed = h3600_conf_get()->motor[r->motor_id].speed;
    int acc = h3600_conf_get()->motor[r->motor_id].acc;

    int slow_step = 0;

    motor_reset_ctl_timedwait(r->motor_id, r->type, speed, acc, r->msec, slow_step);
    free(arg);
}

int slip_motor_reset_async(unsigned char motor_id, unsigned char type, int msec)
{
    motor_reset_async_t *r = calloc(1, sizeof(motor_reset_async_t));
    r->motor_id = motor_id;
    r->type = type;
    r->msec = msec;
    LOG("motor %d reset async, type: %d\n", motor_id, type);
    return work_queue_add(slip_motor_reset_async_callback, r);
}

int slip_motor_reset_timedwait(unsigned char motor_id, unsigned char type, int msec)
{
    int speed = h3600_conf_get()->motor[motor_id].speed;
    int acc = h3600_conf_get()->motor[motor_id].acc;
    int slow_step = 0; 

    LOG("motor %d reset, type: %d\n", motor_id, type);
    return motor_reset_ctl_timedwait(motor_id, type, speed, acc, msec, slow_step);
}

void slip_mainboard_init(void)
{
    INIT_LIST_HEAD(&waiting_list);
    slip_node_id_set(SLIP_NODE_A9_LINUX);
    slip_node_register(SLIP_NODE_A9_LINUX, SLIP_NODE_TYPE_MASTER, NULL, PORT_TYPE_NULL);
    slip_node_register(SLIP_NODE_A9_RTOS, SLIP_NODE_TYPE_SLAVE, "/dev/ttyS1", PORT_TYPE_IPI);
    slip_node_register(SLIP_NODE_SAMPLER, SLIP_NODE_TYPE_SLAVE, "can0", PORT_TYPE_CAN);
    slip_node_register(SLIP_NODE_OPTICAL_1, SLIP_NODE_TYPE_SLAVE, "can1", PORT_TYPE_CAN);
    slip_node_register(SLIP_NODE_MAGNECTIC, SLIP_NODE_TYPE_SLAVE, "can0", PORT_TYPE_CAN);
    slip_node_register(SLIP_NODE_TEMP_CTRL, SLIP_NODE_TYPE_SLAVE, "can0", PORT_TYPE_CAN);
    slip_node_register(SLIP_NODE_LIQUID_DETECT_1, SLIP_NODE_TYPE_SLAVE, "/dev/ttyS3", PORT_TYPE_UART);
    slip_node_register(SLIP_NODE_LIQUID_DETECT_2, SLIP_NODE_TYPE_SLAVE, "/dev/ttyS2", PORT_TYPE_UART);
    slip_init();
}


