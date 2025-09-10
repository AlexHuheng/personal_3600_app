/******************************************************
仪器动作计数文件
******************************************************/
#include <jansson.h>
#include <stdint.h>
#include "module_common.h"
#include "module_temperate_ctl.h"
#include "device_status_count.h"
#include "h3600_maintain_utils.h"

static pthread_mutex_t ds_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t device_status_cnt_cur[DS_COUNT_MAX] = {0};
static uint32_t device_status_cnt_total[DS_COUNT_MAX] = {0};
static int device_status_reset_flag = 0; /* 是否正在执行重置。0：没有 1：进行中 */

void device_status_reset_flag_set(int data)
{
    device_status_reset_flag = data;
    usleep(1000*100); /* 延迟生效 */
}

void device_status_count_add(int index, uint32_t data)
{
    if (device_status_reset_flag == 0) {
        device_status_cur_count_set(index, device_status_cur_count_get(index) + data);
        device_status_total_count_set(index, device_status_total_count_get(index) + data);
    }
}

uint32_t device_status_cur_count_get(int index)
{
    return device_status_cnt_cur[index>=DS_COUNT_MAX ? 0 : index];
}

void device_status_cur_count_set(int index, uint32_t data)
{
    device_status_cnt_cur[index>=DS_COUNT_MAX ? 0 : index] = data;
}

uint32_t device_status_total_count_get(int index)
{
    return device_status_cnt_total[index>=DS_COUNT_MAX ? 0 : index];
}

void device_status_total_count_set(int index, uint32_t data)
{
    device_status_cnt_total[index>=DS_COUNT_MAX ? 0 : index] = data;
}

void device_status_count_scanner_check(int scan_type)
{
    int ds_index = -1;

    switch (scan_type) {
        case SCANNER_RACKS:
            ds_index = DS_SCAN_NENERAL_USED_COUNT;
            break;
        case SCANNER_REAGENT:
            ds_index = DS_SCAN_TALBE_USED_COUNT;
            break;
        default:
            break;
    }

    if (ds_index != -1) {
        device_status_count_add(ds_index, 1);
    }
}

void device_status_count_gpio_check(int gpio_index)
{
    int ds_index = -1;
    int ds_index1 = -1;

    switch (gpio_index) {
        /* 主控板上的GPIO */
        case VALVE_SV1:
            ds_index = DS_WALVE_SV1_USED_COUNT;
            break;
        case VALVE_SV2:
            ds_index = DS_WALVE_SV2_USED_COUNT;
            break;
        case VALVE_SV3:
            ds_index = DS_WALVE_SV3_USED_COUNT;
            break;
        case VALVE_SV4:
            ds_index = DS_WALVE_SV4_USED_COUNT;
            break;
        case VALVE_SV5:
            ds_index = DS_WALVE_SV5_USED_COUNT;
            break;
        case VALVE_SV6:
            ds_index = DS_WALVE_SV6_USED_COUNT;
            break;
        case VALVE_SV7:
            ds_index = DS_WALVE_SV7_USED_COUNT;
            break;
        case VALVE_SV8:
            ds_index = DS_WALVE_SV8_USED_COUNT;
            break;
        case VALVE_SV9:
            ds_index = DS_WALVE_SV9_USED_COUNT;
            break;
        case VALVE_SV10:
            ds_index = DS_WALVE_SV10_USED_COUNT;
            break;
        case VALVE_SV11:
            ds_index = DS_WALVE_SV11_USED_COUNT;
            break;
        case VALVE_SV12:
            ds_index = DS_WALVE_SV12_USED_COUNT;
            break;

        case DIAPHRAGM_PUMP_Q1:
            ds_index = DS_WALVE_Q1_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_Q2:
            ds_index = DS_WALVE_Q2_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_Q3:
            ds_index = DS_WALVE_Q3_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_Q4:
            ds_index = DS_WALVE_Q4_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_F1:
            ds_index = DS_WALVE_F1_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_F2:
            ds_index = DS_WALVE_F2_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_F3:
            ds_index = DS_WALVE_F3_USED_COUNT;
            break;
        case DIAPHRAGM_PUMP_F4:
            ds_index = DS_WALVE_F4_USED_COUNT;
            break;
        default:
            break;
    }

    if (ds_index != -1) {
        device_status_count_add(ds_index, 1);
    }

    if (ds_index1 != -1) {
        device_status_count_add(ds_index1, 1);
    }
}

int device_status_count_load_file()
{
    json_error_t jerror;
    json_t *root = NULL, *device_status_total = NULL, *device_status_cur = NULL, *node = NULL;
    int i = 0;
    int data = 0;

    root = json_load_file(DEVICE_STATUS_COUNT_LOG, 0, &jerror);
    if (root == NULL) {
        LOG("json_load_file() fail, to load back, %s\n", jerror.text);
        root = json_load_file(DEVICE_STATUS_COUNT_LOG_BACK, 0, &jerror);
        if (root == NULL) {
            LOG("json_load_file() load back retry fail, %s\n", jerror.text);
            return -1;
        }
    }

    device_status_total = json_object_get(root, "device_status_total");
    json_array_foreach(device_status_total, i, node) {
        data = json_integer_value(node);
        LOG("ds toatal idx:%d, data:%d\n", i , data);
        device_status_total_count_set(i, data);
    }

    device_status_cur = json_object_get(root, "device_status_cur");
    json_array_foreach(device_status_cur, i, node) {
        data = json_integer_value(node);
        LOG("ds cur idx:%d, data:%d\n", i , data);
        device_status_cur_count_set(i, data);
    }

    return 0;
}

int device_status_count_update_file()
{
    json_t *root = NULL, *device_status_total = NULL, *device_status_cur = NULL;
    int i = 0;

    pthread_mutex_lock(&ds_count_mutex);
    root = json_object();
    device_status_total = json_array();
    device_status_cur = json_array();
    for (i=0; i<DS_COUNT_MAX; i++) {
        json_array_append(device_status_total, json_integer(device_status_total_count_get(i)));
        json_array_append(device_status_cur, json_integer(device_status_cur_count_get(i)));
    }
    json_object_set_new(root, "device_status_total", device_status_total);
    json_object_set_new(root, "device_status_cur", device_status_cur);

    json_dump_file(root, DEVICE_STATUS_COUNT_LOG, JSON_INDENT(1));
    json_dump_file(root, DEVICE_STATUS_COUNT_LOG_BACK, JSON_INDENT(1));
    json_decref(root);
    pthread_mutex_unlock(&ds_count_mutex);

    return 0;
}

int device_status_reset()
{
    device_status_reset_flag_set(1);
    LOG("device_status_reset\n");

    memset(device_status_cnt_cur, 0, sizeof(device_status_cnt_cur));
    memset(device_status_cnt_total, 0, sizeof(device_status_cnt_total));
    device_status_count_update_file();

    device_status_reset_flag_set(0);

    return 0;
}

static void *device_status_count(void *arg)
{
    while (1) {
        /* 间隔1分钟计时 */
        sleep(60);

        if (get_power_off_stat() == 0) {
            device_status_count_add(DS_DEVICE_RUN_TIME, 1);
            device_status_count_add(DS_HEAT_INCUBATION_RUN_TIME, 1);
            device_status_count_add(DS_HEAT_MAGNETIC_RUN_TIME, 1);
            device_status_count_add(DS_HEAT_OPTICAL1_RUN_TIME, 1);
            device_status_count_add(DS_HEAT_R2_RUN_TIME, 1);
            device_status_count_add(DS_HEAT_REGENT_GLASS_RUN_TIME, 1);
            device_status_count_add(DS_OPTICAL_LED_RUN_TIME, 1);
        }

        if (get_power_off_stat() == 0 || (get_shutdown_temp_regent_flag() == 1)) {
            device_status_count_add(DS_COLD_REGENT_TABLE_RUN_TIME, 1); 
            device_status_count_add(DS_FAN_REGENT_TABLE_RUN_TIME, 1);
        }

        device_status_count_add(DS_FAN_DETECT_RUN_TIME, 1);
        device_status_count_add(DS_FAN_AIR_PUMP_RUN_TIME, 1);
        device_status_count_add(DS_FAN_MAIN_BOARD_RUN_TIME, 1);

        device_status_count_update_file();
    }

    return NULL;
}

int device_status_count_init()
{
    pthread_t pid;

    device_status_count_load_file();

    if (0 != pthread_create(&pid, NULL, device_status_count, NULL)) {
        LOG("air_pump_monitor thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

