/******************************************************
定时关关机的实现文件
******************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <log.h>
#include <unistd.h>
#include <jansson.h>

#include "module_common.h"
#include "module_monitor.h"
#include "soft_power.h"
#include "wol.h"
#include "module_magnetic_bead.h"
#include "module_optical.h"
#include "module_temperate_ctl.h"

static user_config_t user_conf = {0};

pc_mac_t pc_macaddr_get()
{
    return user_conf.pc_macaddr;
}

/* 唤醒 PC机 */
int wake_up_pc()
{
    int i = 0;
    pc_mac_t pc_mac = pc_macaddr_get();

    for (i=0; i<PC_MAC_COUNT_MAX && strlen(pc_mac[i])>0; i++) {
        if (wol_message_send_with_mac(pc_mac[i]) == -1) {
            LOG("send wake up [%s] pc fail\n", pc_mac);
            return -1;
        }
    }

    return 0;
}

int reagent_gate_timeout_get()
{
    return user_conf.reagent_gate_timeout;
}

void reagent_gate_timeout_set(int timeout)
{
    user_conf.reagent_gate_timeout = timeout;
    soft_power_param_update_file();
}

int pc_macaddr_del_all()
{
    memset(user_conf.pc_macaddr, 0, sizeof(user_conf.pc_macaddr));
    return 0;
}

int pc_macaddr_update(int idx, const char* mac_str)
{
    memcpy(user_conf.pc_macaddr[idx], mac_str, strlen(mac_str));
    return 0;
}

/* 加载 定时开机的配置文件 */
static int soft_power_param_load_file()
{
    json_error_t jerror = {0};
    json_t *root = NULL, *boot_param = NULL, *pc_macaddr=NULL, *param = NULL, *obj = NULL;
    int i = 0, ret = 0;
    int week = 0, hour = 0, minute = 0, type = 0;

    root = json_load_file(USER_CONF, 0, &jerror);
    if (root == NULL) {
        LOG("json_load_file() fail, %s\n", jerror.text);
        return -1;
    }

    boot_param = json_object_get(root, "boot_param");
    json_array_foreach(boot_param, i, param) {
        ret = json_unpack_ex(param, &jerror, 0, "{s:i,s:i,s:i,s:i}",
               "week", &week, "hour", &hour, "minute", &minute, "type", &type);
        if (ret == -1) {
            LOG("json_unpack_ex() fail, %s\n", jerror.text);
            continue;
        }

        LOG("boot param: week:%d, hour:%d, min:%d, type:%d\n", week, hour, minute, type);
        soft_power_param_add(week, hour, minute, type);
    }

    pc_macaddr = json_object_get(root, "pc_macaddr");
    json_array_foreach(pc_macaddr, i, param) {
        sprintf(user_conf.pc_macaddr[i], "%s", json_string_value(param));
        LOG("idx:%d, pc_macaddr: %s\n", i, user_conf.pc_macaddr[i]);
    }

    obj = json_object_get(root, "reagent_gate_timeout");
    user_conf.reagent_gate_timeout =  json_integer_value(obj);
    LOG("reagent_gate_timeout: %d\n", user_conf.reagent_gate_timeout);

    return 0;
}

/* 保存(更新) 定时开机的配置文件 */
int soft_power_param_update_file()
{
    json_t *root = NULL, *boot_param = NULL, *pc_macaddr=NULL, *param = NULL;
    soft_power_param_t *node = NULL, *n = NULL;
    int i = 0;

    root = json_object();

    boot_param = json_array();
    json_object_set_new(root, "boot_param", boot_param);

    list_for_each_entry_safe(node, n, &user_conf.list_head, soft_power_param_list) {
        param = json_pack("{s:i,s:i,s:i,s:i}",
               "week", node->week, "hour", node->hour, "minute", node->minute, "type", node->type);
        json_array_append(boot_param, param);
    }

    pc_macaddr = json_array();
    json_object_set_new(root, "pc_macaddr", pc_macaddr);

    for (i=0; i<PC_MAC_COUNT_MAX && strlen(user_conf.pc_macaddr[i])>0; i++) {
        param = json_pack("s", user_conf.pc_macaddr[i]);
        json_array_append(pc_macaddr, param);
    }

    json_object_set_new(root, "reagent_gate_timeout", json_integer(user_conf.reagent_gate_timeout));

    json_dump_file(root, USER_CONF, JSON_INDENT(1));
    json_decref(root);

    return 0;
}

int soft_power_param_del(soft_power_param_t *boot_param)
{
    pthread_mutex_lock(&user_conf.list_mutex);
    list_del(&boot_param->soft_power_param_list);
    pthread_mutex_unlock(&user_conf.list_mutex);

    free(boot_param);
    boot_param = NULL;

    return 0;
}

int soft_power_param_add(int32_t week, int32_t hour, int32_t minute, int32_t type)
{
    soft_power_param_t *boot_param = NULL;

    boot_param = (soft_power_param_t*)calloc(1, sizeof(soft_power_param_t));
    boot_param->week = week;
    boot_param->hour = hour;
    boot_param->minute = minute;
    boot_param->type = type;

    pthread_mutex_lock(&user_conf.list_mutex);
    list_add_tail(&boot_param->soft_power_param_list, &user_conf.list_head);
    pthread_mutex_unlock(&user_conf.list_mutex);

    return 0;
}

int soft_power_param_del_all()
{
    soft_power_param_t *node = NULL, *n = NULL;

    list_for_each_entry_safe(node, n, &user_conf.list_head, soft_power_param_list) {
        soft_power_param_del(node);
    }

    return 0;
}

/* 定时开机任务 */
static void *soft_power_task(void *arg)
{
    struct timeval tv = {0};
    struct tm tm = {0};
    int power_flag = 0;
    soft_power_param_t *node = NULL, *n = NULL;
    int week = 0;

    wake_up_pc(); /* 开机启动，默认唤醒PC机 */
    all_motor_power_clt(1); /* 开机启动，默认上电所有电机 */

    while (1) {
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm);
        power_flag = 0;

        /* linux中，tm_wday星期x的取值区间为[0,6]，其中0代表星期天，1代表星期一，以此类推 */
        week = (tm.tm_wday==0) ? 7 : tm.tm_wday;

        list_for_each_entry_safe(node, n, &user_conf.list_head, soft_power_param_list) {
            if (node->type==1 && node->week==week && node->hour==tm.tm_hour && node->minute==tm.tm_min) {
                power_flag = 1;
            }
        }

        if (power_flag == 1) {
            /* 已经在运行态，则不能触发定时开机 */
            if (get_machine_stat() != MACHINE_STAT_RUNNING) {
                LOG("send magic package to PC...\n");
                /* 开启 thrift服务（20241203版本后由上位机管控，下位机一直开启监听及连接） */
                /* thrift_slave_client_connect_ctl(1); */
                /* thrift_slave_server_connect_ctl(1); */

                /* 使能 磁珠驱动力 */
                if (all_magnetic_pwm_ctl(1) == -1) {
                    //TODO? 错误处理
                }

                /* 使能 光学检测位led灯 */
                if (all_optical_led_ctl(1) == -1) {
                    //TODO? 错误处理
                }

                /* 使能 所有温控 */
                if (all_temperate_ctl(1, 0) == -1) {
                    //TODO? 错误处理
                }

                /* 上电 所有电机 */
                if (all_motor_power_clt(1) == -1) {
                    //TODO? 错误处理
                }

                /* 唤醒PC机 */
                if (wake_up_pc() == -1) {
                    //TODO? 错误处理
                }
                slip_button_reag_led_to_sampler(REAG_BUTTON_LED_OPEN);
                set_power_off_stat(0);
            } else {
                LOG("it's time, but machine is running\n");
            }

            soft_power_param_update_file();
        }

        sleep(20);
    }

    return NULL;
}

/* 定时开机初始化 */
int soft_power_init()
{
    pthread_t soft_power_thread = {0};

    INIT_LIST_HEAD(&user_conf.list_head);
    pthread_mutex_init(&user_conf.list_mutex, NULL);

    if (0 != access(USER_CONF, F_OK)) {
        soft_power_param_update_file(); /* 生成空文件 */
    } else {
        soft_power_param_load_file();
    }

    if (0 != pthread_create(&soft_power_thread, NULL, soft_power_task, NULL)) {
        LOG("soft power thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    return 0;
}


