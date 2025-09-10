#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <jansson.h>

#include "log.h"
#include "slip_cmd_table.h"
#include "module_common.h"
#include <module_liquid_detect.h>
#include <module_cuvette_supply.h>
#include "thrift_service_software_interface.h"

h3600_conf_t h3600_conf;
h3600_conf_t h3600_old_conf;

h3600_conf_t* h3600_conf_get()
{
    return &h3600_conf;
}

h3600_conf_t* h3600_old_conf_get()
{
    return &h3600_old_conf;
}

static int write_conf(void)
{
    json_t *root = NULL, *motor_attr = NULL, *motor = NULL, *pos = NULL;
    char motor_id_str[32] = {0};
    char cmd_buff[256] = {0};
    int i = 0, j = 0;

    LOG("backup config.\n");
    sprintf(cmd_buff, "cp %s %s", H3600_CONF, H3600_CONF_OLD);
    system(cmd_buff);

    LOG("write json config\n");
    root = json_object();

    /* 电机参数、标定位置参数 */
    motor_attr = json_array();
    json_object_set_new(root, "motor_attr", motor_attr);

    for (i = 1; i <= MAX_MOTOR_NUM; i++) {
        sprintf(motor_id_str, "%d", i);
        pos = json_array();
        for (j = 0; j < h3600_conf.motor_pos_cnt[i]; j++) {
            json_array_append(pos, json_integer(h3600_conf.motor_pos[i][j]));
        }
        motor = json_pack("{s:{s:{s:i,s:i},s:o}}", motor_id_str, "attr",
                                                   "acc", h3600_conf.motor[i].acc,
                                                   "speed", h3600_conf.motor[i].speed,
                                                   "pos", pos);
        json_array_append(motor_attr, motor);
    }

    /* 样本针/试剂针KB值参数 */
    motor_attr = json_array();
    json_object_set_new(root, "liquid_amount", motor_attr);
    for (i = 0; i < NEEDLE_TYPE_MAX; i++) {
        LOG("amount: %d k = %f, b = %f.\n", i, h3600_conf.liquid_amount[i].k_ratio, h3600_conf.liquid_amount[i].b_ratio);
        sprintf(motor_id_str, "needle_r%d", i);
        motor = json_pack("{s:{s:f,s:f}}",
                            motor_id_str,
                           "k_ratio", h3600_conf.liquid_amount[i].k_ratio,
                           "b_ratio", h3600_conf.liquid_amount[i].b_ratio);
        json_array_append(motor_attr, motor);
    }

    /* 磁珠检测通道 禁用参数 */
    pos = json_array();
    for (j=0; j<sizeof(h3600_conf.mag_pos_disable)/sizeof(h3600_conf.mag_pos_disable[0]); j++) {
        json_array_append(pos, json_integer(h3600_conf.mag_pos_disable[j]));
    }
    json_object_set_new(root, "mag_pos_disable", pos);

    /* 光学检测通道 禁用参数 */
    pos = json_array();
    for (j=0; j<sizeof(h3600_conf.optical_pos_disable)/sizeof(h3600_conf.optical_pos_disable[0]); j++) {
        json_array_append(pos, json_integer(h3600_conf.optical_pos_disable[j]));
    }
    json_object_set_new(root, "optical_pos_disable", pos);

    /* 上位机thrift的IP、PORT参数 */
    pos = json_pack("{s:s,s:i}", "ip", h3600_conf.thrift_master_server.ip, "port", h3600_conf.thrift_master_server.port);
    json_object_set_new(root, "thrift_master_server", pos);

    /* 下位机thrift的IP、PORT参数 */
    pos = json_pack("{s:s,s:i}", "ip", h3600_conf.thrift_slave_server.ip, "port", h3600_conf.thrift_slave_server.port);
    json_object_set_new(root, "thrift_slave_server", pos);

    /* 光学电流值参数 */
    pos = json_array();
    for (j = 0; j < 5; j++) {
        json_array_append(pos, json_integer(h3600_conf.optical_curr_data[j]));
    }
    json_object_set_new(root, "optical_led_curr", pos);

    /* 散热风扇占空比参数 */
    pos = json_array();
    for (j = 0; j < sizeof(h3600_conf.fan_pwm_duty)/sizeof(h3600_conf.fan_pwm_duty[0]); j++) {
        json_array_append(pos, json_integer(h3600_conf.fan_pwm_duty[j]));
    }
    json_object_set_new(root, "fan_pwm_duty", pos);

    /* 新加样算法KB值参数 */
    pos = json_array();
    for (j = 0; j < sizeof(h3600_conf.r0_liq_true_value)/sizeof(h3600_conf.r0_liq_true_value[0]); j++) {
        json_array_append(pos, json_real(h3600_conf.r0_liq_true_value[j]));
    }
    json_object_set_new(root, "liq_kb_value_r0", pos);
    pos = json_array();
    for (j = 0; j < sizeof(h3600_conf.r2_liq_true_value)/sizeof(h3600_conf.r2_liq_true_value[0]); j++) {
        json_array_append(pos, json_real(h3600_conf.r2_liq_true_value[j]));
    }
    json_object_set_new(root, "liq_kb_value_r2", pos);
    pos = json_array();
    for (j = 0; j < sizeof(h3600_conf.r3_liq_true_value)/sizeof(h3600_conf.r3_liq_true_value[0]); j++) {
        json_array_append(pos, json_real(h3600_conf.r3_liq_true_value[j]));
    }
    json_object_set_new(root, "liq_kb_value_r3", pos);
    pos = json_array();
    for (j = 0; j < sizeof(h3600_conf.r4_liq_true_value)/sizeof(h3600_conf.r4_liq_true_value[0]); j++) {
        json_array_append(pos, json_real(h3600_conf.r4_liq_true_value[j]));
    }
    json_object_set_new(root, "liq_kb_value_r4", pos);

    /* 样本/孵育试剂针针探测阈值 */
    pos = json_array();
    for (j = 0; j < ARRAY_SIZE(h3600_conf.s_threshold); j++) {
        json_array_append(pos, json_integer(h3600_conf.s_threshold[j]));
    }
    json_object_set_new(root, "s_threshold", pos);

    /* 启动试剂针探测阈值 */
    pos = json_array();
    for (j = 0; j < ARRAY_SIZE(h3600_conf.r2_threshold); j++) {
        json_array_append(pos, json_integer(h3600_conf.r2_threshold[j]));
    }
    json_object_set_new(root, "r2_threshold", pos);

    /* 扩展功能参数 */
    //json_object_set_new(root, "hil_support", json_integer(h3600_conf.hil_support));
    json_object_set_new(root, "pierce_support", json_integer(h3600_conf.pierce_support));
    json_object_set_new(root, "pierce_enable", json_integer(h3600_conf.pierce_enable));
    json_object_set_new(root, "throughput_mode", json_integer(h3600_conf.throughput_mode));
    json_object_set_new(root, "straight_release", json_integer(h3600_conf.straight_release));
    json_object_set_new(root, "clot_check", json_integer(h3600_conf.clot_check_switch));

    json_dump_file(root, H3600_CONF, JSON_INDENT(1));
    json_decref(root);

    return 0;
}

/* 电机起始索引从1开始，不是0 */
static int load_default_conf(void)
{
    load_default_conf_from_common(&h3600_conf);
    write_conf();
    return 0;
}

static int read_conf(void)
{
    json_error_t jerror = {0};
    json_t *root = NULL, *motor_attr = NULL, *motor = NULL, *pos = NULL, *pos_value = NULL, *obj = NULL;
    char motor_id_str[32] = {0};
    int i = 0, j = 0;
    int ret = 0;
    int motor_id = 0;
    char *master_ip = NULL, *slave_ip = NULL;

    root = json_load_file(H3600_CONF, 0, &jerror);
    if (root == NULL) {
        LOG("json_load_file() fail, %s\n", jerror.text);
        return -1;
    }

    /* 电机参数、标定位置参数 */
    motor_attr = json_object_get(root, "motor_attr");
    json_array_foreach(motor_attr, i, motor) {
        motor_id = i + 1;
        sprintf(motor_id_str, "%d", motor_id);
        ret = json_unpack_ex(motor, &jerror, 0, "{s:{s:{s:i,s:i},s:o}}", motor_id_str, "attr",
                                                   "acc", &h3600_conf.motor[motor_id].acc,
                                                   "speed", &h3600_conf.motor[motor_id].speed,
                                                   "pos", &pos);
        if (ret == -1) {
            LOG("json_unpack_ex() fail, %s\n", jerror.text);
            continue;
        }

        h3600_conf.motor_pos_cnt[motor_id] = json_array_size(pos);
        json_array_foreach(pos, j, pos_value) {
            h3600_conf.motor_pos[motor_id][j] = json_integer_value(pos_value);
        }
    }

    /* 样本针/试剂针KB值参数 */
    i = 0;
    motor_attr = json_object_get(root, "liquid_amount");
    json_array_foreach(motor_attr, i, motor) {
        sprintf(motor_id_str, "needle_r%d", i);
        ret = json_unpack_ex(motor, &jerror, 0, "{s:{s:f,s:f}}",
                            motor_id_str,
                           "k_ratio", &h3600_conf.liquid_amount[i].k_ratio,
                           "b_ratio", &h3600_conf.liquid_amount[i].b_ratio);

        if (ret == -1) {
            LOG("json_unpack_ex() fail, %s\n", jerror.text);
            continue;
        }

        LOG("liquid_amount, idx:%d, k:%f, b:%f\n", i,
            h3600_conf.liquid_amount[i].k_ratio, h3600_conf.liquid_amount[i].b_ratio);
    }

    /* 磁珠检测通道 禁用参数 */
    i = 0;
    motor_attr = json_object_get(root, "mag_pos_disable");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.mag_pos_disable[i] = json_integer_value(pos_value);
        LOG("mag_pos_disable[%d] : %x\n", i, h3600_conf.mag_pos_disable[i]);
    }

    /* 光学检测通道 禁用参数 */
    i = 0;
    motor_attr = json_object_get(root, "optical_pos_disable");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.optical_pos_disable[i] = json_integer_value(pos_value);
        LOG("optical_pos_disable[%d] : %x\n", i, h3600_conf.optical_pos_disable[i]);
    }

    /* 上位机thrift的IP、PORT参数 */
    obj = json_object_get(root, "thrift_master_server");
    json_unpack_ex(obj, &jerror, 0, "{s:s,s:i}", "ip", &master_ip, "port", &h3600_conf.thrift_master_server.port);
    if (master_ip) {
        memset(h3600_conf.thrift_master_server.ip, 0, sizeof(h3600_conf.thrift_master_server.ip));
        strncpy(h3600_conf.thrift_master_server.ip, master_ip, strlen(master_ip)>31 ? 31 : strlen(master_ip));
    }

    /* 下位机thrift的IP、PORT参数 */
    obj = json_object_get(root, "thrift_slave_server");
    json_unpack_ex(obj, &jerror, 0, "{s:s,s:i}", "ip", &slave_ip, "port", &h3600_conf.thrift_slave_server.port);
    if (slave_ip) {
        memset(h3600_conf.thrift_slave_server.ip, 0, sizeof(h3600_conf.thrift_slave_server.ip));
        strncpy(h3600_conf.thrift_slave_server.ip, slave_ip, strlen(slave_ip)>31 ? 31 : strlen(slave_ip));
    }

    LOG("thrift_master_server ip:port{%s:%d} ;thrift_slave_server ip:port{%s:%d}\n", 
        h3600_conf.thrift_master_server.ip, h3600_conf.thrift_master_server.port,
        h3600_conf.thrift_slave_server.ip, h3600_conf.thrift_slave_server.port);

    /* 光学电流值参数 */
    i = 0;
    motor_attr = json_object_get(root, "optical_led_curr");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.optical_curr_data[i] = json_integer_value(pos_value);
        LOG("optical_led_curr[%d] : %x\n", i, h3600_conf.optical_curr_data[i]);
    }

    /* 散热风扇占空比参数 */
    i = 0;
    h3600_conf.fan_pwm_duty[0] = 60; /* 默认值 */
    h3600_conf.fan_pwm_duty[1] = 60;
    h3600_conf.fan_pwm_duty[2] = 20;
    h3600_conf.fan_pwm_duty[3] = 20;
    motor_attr = json_object_get(root, "fan_pwm_duty");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.fan_pwm_duty[i] = json_integer_value(pos_value);
        LOG("fan_pwm_duty[%d]: %d\n", i, h3600_conf.fan_pwm_duty[i]);
    }

    /* 新加样算法KB值参数 */
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r0");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.r0_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_conf.r0_liq_true_value[i]);
    }
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r2");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.r2_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_conf.r2_liq_true_value[i]);
    }
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r3");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.r3_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_conf.r3_liq_true_value[i]);
    }
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r4");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.r4_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_conf.r4_liq_true_value[i]);
    }

    /* 样本针/孵育试剂针探测阈值 */
    i = 0;
    motor_attr = json_object_get(root, "s_threshold");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.s_threshold[i] = json_integer_value(pos_value);
        LOG("s_threshold[%d] : %d\n", i, h3600_conf.s_threshold[i]);
    }

    /* 启动试剂针探测阈值 */
    i = 0;
    motor_attr = json_object_get(root, "r2_threshold");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_conf.r2_threshold[i] = json_integer_value(pos_value);
        LOG("r2_threshold[%d] : %d\n", i, h3600_conf.r2_threshold[i]);
    }

    /* 扩展功能参数 */
    obj = json_object_get(root, "pierce_support");
    h3600_conf.pierce_support =  json_integer_value(obj);
    obj = json_object_get(root, "pierce_enable");
    h3600_conf.pierce_enable =  json_integer_value(obj);
    obj = json_object_get(root, "throughput_mode");
    h3600_conf.throughput_mode =  json_integer_value(obj);
    obj = json_object_get(root, "straight_release");
    h3600_conf.straight_release =  json_integer_value(obj);
    obj = json_object_get(root, "clot_check");
    h3600_conf.clot_check_switch =  json_integer_value(obj);

    LOG("device capability: support_pierce = %d(enable = %d),  throughput = %d.\n",
        h3600_conf.pierce_support, h3600_conf.pierce_enable, h3600_conf.throughput_mode);
    json_decref(root);

    return 0;
}


int read_old_conf(void)
{
    json_error_t jerror = {0};
    json_t *root = NULL, *motor_attr = NULL, *motor = NULL, *pos = NULL, *pos_value = NULL, *obj = NULL;
    char motor_id_str[32] = {0};
    int i = 0, j = 0;
    int ret = 0;
    int motor_id = 0;
    char *master_ip = NULL, *slave_ip = NULL;

    if (0 != access(H3600_CONF_OLD, F_OK)) {
        LOG("%s not exist.\n", H3600_CONF_OLD);
        root = json_load_file(H3600_CONF, 0, &jerror);
    } else {
        root = json_load_file(H3600_CONF_OLD, 0, &jerror);
    }
    if (root == NULL) {
        LOG("json_load_file() fail, %s\n", jerror.text);
        return -1;
    }

    /* 电机参数、标定位置参数 */
    motor_attr = json_object_get(root, "motor_attr");
    json_array_foreach(motor_attr, i, motor) {
        motor_id = i + 1;
        sprintf(motor_id_str, "%d", motor_id);
        ret = json_unpack_ex(motor, &jerror, 0, "{s:{s:{s:i,s:i},s:o}}", motor_id_str, "attr",
                                                   "acc", &h3600_old_conf.motor[motor_id].acc,
                                                   "speed", &h3600_old_conf.motor[motor_id].speed,
                                                   "pos", &pos);
        if (ret == -1) {
            LOG("json_unpack_ex() fail, %s\n", jerror.text);
            continue;
        }

        h3600_old_conf.motor_pos_cnt[motor_id] = json_array_size(pos);
        json_array_foreach(pos, j, pos_value) {
            h3600_old_conf.motor_pos[motor_id][j] = json_integer_value(pos_value);
        }
    }

    /* 样本针/试剂针KB值参数 */
    i = 0;
    motor_attr = json_object_get(root, "liquid_amount");
    json_array_foreach(motor_attr, i, motor) {
        sprintf(motor_id_str, "needle_r%d", i);
        ret = json_unpack_ex(motor, &jerror, 0, "{s:{s:f,s:f}}",
                            motor_id_str,
                           "k_ratio", &h3600_old_conf.liquid_amount[i].k_ratio,
                           "b_ratio", &h3600_old_conf.liquid_amount[i].b_ratio);

        if (ret == -1) {
            LOG("json_unpack_ex() fail, %s\n", jerror.text);
            continue;
        }

        LOG("liquid_amount, idx:%d, k:%f, b:%f\n", i,
            h3600_old_conf.liquid_amount[i].k_ratio, h3600_old_conf.liquid_amount[i].b_ratio);
    }

    /* 上位机thrift的IP、PORT参数 */
    obj = json_object_get(root, "thrift_master_server");
    json_unpack_ex(obj, &jerror, 0, "{s:s,s:i}", "ip", &master_ip, "port", &h3600_old_conf.thrift_master_server.port);
    if (master_ip) {
        memset(h3600_old_conf.thrift_master_server.ip, 0, sizeof(h3600_old_conf.thrift_master_server.ip));
        strncpy(h3600_old_conf.thrift_master_server.ip, master_ip, strlen(master_ip)>31 ? 31 : strlen(master_ip));
    }

    /* 下位机thrift的IP、PORT参数 */
    obj = json_object_get(root, "thrift_slave_server");
    json_unpack_ex(obj, &jerror, 0, "{s:s,s:i}", "ip", &slave_ip, "port", &h3600_old_conf.thrift_slave_server.port);
    if (slave_ip) {
        memset(h3600_old_conf.thrift_slave_server.ip, 0, sizeof(h3600_old_conf.thrift_slave_server.ip));
        strncpy(h3600_old_conf.thrift_slave_server.ip, slave_ip, strlen(slave_ip)>31 ? 31 : strlen(slave_ip));
    }

    LOG("thrift_master_server ip:port{%s:%d} ;thrift_slave_server ip:port{%s:%d}\n", 
        h3600_old_conf.thrift_master_server.ip, h3600_old_conf.thrift_master_server.port,
        h3600_old_conf.thrift_slave_server.ip, h3600_old_conf.thrift_slave_server.port);

    /* 光学电流值参数 */
    i = 0;
    motor_attr = json_object_get(root, "optical_led_curr");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.optical_curr_data[i] = json_integer_value(pos_value);
        LOG("optical_led_curr[%d] : %x\n", i, h3600_old_conf.optical_curr_data[i]);
    }

    /* 散热风扇占空比参数 */
    i = 0;
    h3600_old_conf.fan_pwm_duty[0] = 60; /* 默认值 */
    h3600_old_conf.fan_pwm_duty[1] = 60;
    h3600_old_conf.fan_pwm_duty[2] = 20;
    h3600_old_conf.fan_pwm_duty[3] = 20;
    motor_attr = json_object_get(root, "fan_pwm_duty");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.fan_pwm_duty[i] = json_integer_value(pos_value);
        LOG("fan_pwm_duty[%d]: %d\n", i, h3600_old_conf.fan_pwm_duty[i]);
    }

    /* 新加样算法KB值参数 */
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r0");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.r0_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_old_conf.r0_liq_true_value[i]);
    }
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r2");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.r2_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_old_conf.r2_liq_true_value[i]);
    }
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r3");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.r3_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_old_conf.r3_liq_true_value[i]);
    }
    i = 0;
    motor_attr = json_object_get(root, "liq_kb_value_r4");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.r4_liq_true_value[i] = json_real_value(pos_value);
        LOG("r0_liq_true_value[%d] : %lf\n", i, h3600_old_conf.r4_liq_true_value[i]);
    }

    /* 样本针/孵育试剂针探测阈值 */
    i = 0;
    motor_attr = json_object_get(root, "s_threshold");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.s_threshold[i] = json_integer_value(pos_value);
        LOG("s_threshold[%d] : %d\n", i, h3600_old_conf.s_threshold[i]);
    }

    /* 启动试剂针探测阈值 */
    i = 0;
    motor_attr = json_object_get(root, "r2_threshold");
    json_array_foreach(motor_attr, i, pos_value) {
        h3600_old_conf.r2_threshold[i] = json_integer_value(pos_value);
        LOG("r2_threshold[%d] : %d\n", i, h3600_old_conf.r2_threshold[i]);
    }

    /* 扩展功能参数 */
    obj = json_object_get(root, "pierce_support");
    h3600_old_conf.pierce_support =  json_integer_value(obj);
    obj = json_object_get(root, "pierce_enable");
    h3600_old_conf.pierce_enable =  json_integer_value(obj);
    obj = json_object_get(root, "throughput_mode");
    h3600_old_conf.throughput_mode =  json_integer_value(obj);
    obj = json_object_get(root, "straight_release");
    h3600_old_conf.straight_release =  json_integer_value(obj);
    obj = json_object_get(root, "clot_check");
    h3600_old_conf.clot_check_switch =  json_integer_value(obj);

    LOG("device capability: support_pierce = %d(enable = %d),  throughput = %d.\n",
        h3600_old_conf.pierce_support, h3600_old_conf.pierce_enable, h3600_old_conf.throughput_mode);
    json_decref(root);

    return 0;
}


int h3600_conf_init(void)
{
    if (0 != access(H3600_CONF, F_OK)) {
        LOG("load default json\n");
        load_default_conf();
    } else {
        LOG("load custorm json\n");
        read_conf();
        pos_all_init(&h3600_conf, 1);
    }

    return 0;
}

int get_straight_release_para(void)
{
    return !!h3600_conf.straight_release;
}

void set_straight_release_para(int val)
{
    h3600_conf.straight_release = !!val;
    write_conf();
}

void set_clot_check_flag(int flag)
{
    h3600_conf.clot_check_switch = flag;
}

int get_clot_check_flag(void)
{
    return !!h3600_conf.clot_check_switch;
}

/* 设置指定电机速度及加速度 */
int thrift_motor_para_set(int motor_id, const thrift_motor_para_t *motor_para)
{
    LOG("motor id:%d, acc:%d, speed:%d\n", motor_id, motor_para->acc, motor_para->speed);
    memcpy(&h3600_conf.motor[motor_id], motor_para, sizeof(thrift_motor_para_t));
    write_conf();
    return 0;
}

/* 获取指定电机速度及加速度 */
int thrift_motor_para_get(int motor_id, thrift_motor_para_t *motor_para)
{
    memcpy(motor_para, &h3600_conf.motor[motor_id], sizeof(thrift_motor_para_t));
    return 0;
}

/* 设置指定电机指定标定参数步数 */
int thrift_motor_pos_set(int motor_id, int pos, int step)
{
    int pos_cnt = h3600_conf.motor_pos_cnt[motor_id];

    LOG("motor id:%d, pos:%d, step:%d\n", motor_id, pos, step);

    if (pos >= pos_cnt) {
        return -1;
    }
    h3600_conf.motor_pos[motor_id][pos] = step;
    write_conf();
    return 0;
}

/* 抓手自动标定后，设置指定电机指定标定参数步数 */
int thrift_motor_pos_grip_set(int motor_id, int pos, int step)
{
    int pos_cnt = h3600_conf.motor_pos_cnt[motor_id];

    LOG("motor id:%d, pos:%d, step:%d\n", motor_id, pos, step);

    if (pos >= pos_cnt) {
        return -1;
    }
    h3600_conf.motor_pos[motor_id][pos] = step;

    return 0;
}

/* 保存抓手标定的值 */
int save_grip_cali_value(void)
{
    write_conf();
    return 0;
}

/* 获取指定电机的相关标定参数步数 */
int thrift_motor_pos_get(int motor_id, int *pos, int pos_size)
{
    int pos_cnt = h3600_conf.motor_pos_cnt[motor_id];
    int cnt = pos_cnt > pos_size ? pos_size : pos_cnt;

    memcpy(pos, h3600_conf.motor_pos[motor_id], cnt * sizeof(int));
    return pos_cnt;
}

/* 电机复位 */
int thrift_motor_reset(int motor_id, int is_first)
{
    LOG("thrift motor reset motor_id:%d, is_first:%d\n", motor_id, is_first);
    return slip_motor_reset_timedwait(motor_id, is_first, 40000);
}

/* 电机正向（步数为正数）或反向（步数为负数）移动 */
int thrift_motor_move(int motor_id, int step)
{
    int ret;
    int step_x, step_y;

    if (MOTOR_NEEDLE_S_X == motor_id || MOTOR_NEEDLE_S_Y == motor_id) { /* 样本针 */
        if (MOTOR_NEEDLE_S_X == motor_id) {
            step_x = 0;
            step_y = step;
        } else {
            step_x = step;
            step_y = 0;
        }
        ret = slip_motor_dual_step_timedwait(MOTOR_NEEDLE_S_Y, step_x, step_y, 10000) == -1 ? -1 : 0;
    } else if (MOTOR_CATCHER_X == motor_id || MOTOR_CATCHER_Y == motor_id) { /* 抓手 */
        if (MOTOR_CATCHER_X == motor_id) {
            step_x = step;
            step_y = 0;
        } else {
            step_x = 0;
            step_y = step;
        }
        ret = slip_motor_dual_step_timedwait(MOTOR_CATCHER_X, step_x, step_y, 10000) == -1 ? -1 : 0;
    } else { /* 其他 */
        ret = slip_motor_step_timedwait(motor_id, step, 10000) == -1 ? -1 : 0;
    }

    LOG("ret: %d\n", ret);
    return ret;
}

/* 电机移动到 */
int thrift_motor_move_to(int motor_id, int step)
{
    int current_step = 0;

    current_step = motor_current_pos_timedwait(motor_id, 2000);

    return thrift_motor_move(motor_id, step - current_step);
}

/* 扫描一维码或二维码 1：常规扫码，2：急诊扫码，3：试剂仓扫码 4: 稀释液扫码 */
int thrift_read_barcode(int type, char *barcode, int barcode_size)
{
    int ret = 0, i = 0;

    for (i = 0 ; i < 1 ; i++) {
        ret = scanner_read_barcode_sync(type, barcode, barcode_size);
        if (0 == ret) {
            LOG("scan barcode success! channel:%d barcode: %s\n", type, barcode);
            return ret;
        }
        usleep(200*1000);
    }
    LOG("scan barcode fail! channel:%d\n", type);
    return ret;
}

/* 液面探测 1：样本针，2：R1，3：R2，返回值为液面探测实际运动步数，当探测失败时，返回-1 */
int thrift_liquid_detect(int type)
{
    needle_type_t needle_type= NEEDLE_TYPE_MAX;

    if (type == 1) {
        needle_type = NEEDLE_TYPE_S;
    } else if (type == 2) {
        needle_type = NEEDLE_TYPE_S_R1;
    } else if (type == 3) {
        needle_type = NEEDLE_TYPE_R2;
    } else if (type == 4) {
        needle_type = NEEDLE_TYPE_S_DILU;
    } else if (type == 5) {
        needle_type = NEEDLE_TYPE_S_BOTH;
    } else {
        return -1;
    }

    return thrift_liquid_detect_start(needle_type);
}

int get_kb_value(int type, double *k_value, double *b_value)
{
    switch (type) {
    case EDI_SAMPLE_NORMAL_5UL:
        *k_value = h3600_conf.r0_liq_true_value[0];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_10UL:
        *k_value = h3600_conf.r0_liq_true_value[1];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_15UL:
        *k_value = h3600_conf.r0_liq_true_value[2];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_25UL:
        *k_value = h3600_conf.r0_liq_true_value[3];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_50UL:
        *k_value = h3600_conf.r0_liq_true_value[4];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_100UL:
        *k_value = h3600_conf.r0_liq_true_value[5];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_150UL:
        *k_value = h3600_conf.r0_liq_true_value[6];
        *b_value = 0;
        break;
    case EDI_SAMPLE_NORMAL_200UL:
        *k_value = h3600_conf.r0_liq_true_value[7];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R3_5UL:
        *k_value = h3600_conf.r3_liq_true_value[0];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R3_10UL:
        *k_value = h3600_conf.r3_liq_true_value[1];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R3_15UL:
        *k_value = h3600_conf.r3_liq_true_value[2];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R3_25UL:
        *k_value = h3600_conf.r3_liq_true_value[3];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R3_50UL:
        *k_value = h3600_conf.r3_liq_true_value[4];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R4_15UL:
        *k_value = h3600_conf.r4_liq_true_value[0];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R4_20UL:
        *k_value = h3600_conf.r4_liq_true_value[1];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R4_50UL:
        *k_value = h3600_conf.r4_liq_true_value[2];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R4_100UL:
        *k_value = h3600_conf.r4_liq_true_value[3];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R4_150UL:
        *k_value = h3600_conf.r4_liq_true_value[4];
        *b_value = 0;
        break;
    case EDI_SAMPLE_DILU_R4_200UL:
        *k_value = h3600_conf.r4_liq_true_value[5];
        *b_value = 0;
        break;
    case EDI_R2_ADDING_20UL:
        *k_value = h3600_conf.r2_liq_true_value[0];
        *b_value = 0;
        break;
    case EDI_R2_ADDING_50UL:
        *k_value = h3600_conf.r2_liq_true_value[1];
        *b_value = 0;
        break;
    case EDI_R2_ADDING_100UL:
        *k_value = h3600_conf.r2_liq_true_value[2];
        *b_value = 0;
        break;
    case EDI_R2_ADDING_150UL:
        *k_value = h3600_conf.r2_liq_true_value[3];
        *b_value = 0;
        break;
    case EDI_R2_ADDING_200UL:
        *k_value = h3600_conf.r2_liq_true_value[4];
        *b_value = 0;
        break;
    default:
        LOG("invalid type\n");
        break;
    }

    return 0;
}

int set_kb_value(int type, double k_value, double b_value)
{

    switch (type) {
        case EDI_SAMPLE_NORMAL_5UL:
            h3600_conf.r0_liq_true_value[0] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_10UL:
            h3600_conf.r0_liq_true_value[1] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_15UL:
            h3600_conf.r0_liq_true_value[2] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_25UL:
            h3600_conf.r0_liq_true_value[3] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_50UL:
             h3600_conf.r0_liq_true_value[4] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_100UL:
            h3600_conf.r0_liq_true_value[5] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_150UL:
            h3600_conf.r0_liq_true_value[6] = k_value;
            break;
        case EDI_SAMPLE_NORMAL_200UL:
            h3600_conf.r0_liq_true_value[7] = k_value;
            break;
        case EDI_SAMPLE_DILU_R3_5UL:
            h3600_conf.r3_liq_true_value[0] = k_value;
            break;
        case EDI_SAMPLE_DILU_R3_10UL:
            h3600_conf.r3_liq_true_value[1] = k_value;
            break;
        case EDI_SAMPLE_DILU_R3_15UL:
            h3600_conf.r3_liq_true_value[2] = k_value;
            break;
        case EDI_SAMPLE_DILU_R3_25UL:
            h3600_conf.r3_liq_true_value[3] = k_value;
            break;
        case EDI_SAMPLE_DILU_R3_50UL:
            h3600_conf.r3_liq_true_value[4] = k_value;
            break;
        case EDI_SAMPLE_DILU_R4_15UL:
            h3600_conf.r4_liq_true_value[0] = k_value;
            break;
        case EDI_SAMPLE_DILU_R4_20UL:
            h3600_conf.r4_liq_true_value[1] = k_value;
            break;
        case EDI_SAMPLE_DILU_R4_50UL:
            h3600_conf.r4_liq_true_value[2] = k_value;
            break;
        case EDI_SAMPLE_DILU_R4_100UL:
            h3600_conf.r4_liq_true_value[3] = k_value;
            break;
        case EDI_SAMPLE_DILU_R4_150UL:
            h3600_conf.r4_liq_true_value[4] = k_value;
            break;
        case EDI_SAMPLE_DILU_R4_200UL:
            h3600_conf.r4_liq_true_value[5] = k_value;
            break;
        case EDI_R2_ADDING_20UL:
            h3600_conf.r2_liq_true_value[0] = k_value;
            break;
        case EDI_R2_ADDING_50UL:
            h3600_conf.r2_liq_true_value[1] = k_value;
            break;
        case EDI_R2_ADDING_100UL:
            h3600_conf.r2_liq_true_value[2] = k_value;
            break;
        case EDI_R2_ADDING_150UL:
            h3600_conf.r2_liq_true_value[3] = k_value;
            break;
        case EDI_R2_ADDING_200UL:
            h3600_conf.r2_liq_true_value[4] = k_value;
            break;
        default:
            LOG("invalid type\n");
            break;
    }

    write_conf();

    return 0;
}

int set_optical_value(void)
{
    write_conf();
    return 0;
}

int thrift_master_server_ipport_set(const char* ip, int port)
{
    if (ip) {
        memset(h3600_conf.thrift_master_server.ip, 0, sizeof(h3600_conf.thrift_master_server.ip));
        strncpy(h3600_conf.thrift_master_server.ip, ip, strlen(ip)>31 ? 31 : strlen(ip));
    }
    h3600_conf.thrift_master_server.port = port;

    write_conf();
    return 0;
}

thrift_master_t* thrift_master_server_ipport_get()
{
    return &h3600_conf.thrift_master_server;
}

int thrift_slave_server_ipport_set(const char* ip, int port, int pierce_enable)
{
    if (ip) {
        memset(h3600_conf.thrift_slave_server.ip, 0, sizeof(h3600_conf.thrift_slave_server.ip));
        strncpy(h3600_conf.thrift_slave_server.ip, ip, strlen(ip)>31 ? 31 : strlen(ip));
    }
    h3600_conf.thrift_slave_server.port = port;
    h3600_conf.pierce_enable = pierce_enable;

    write_conf();
    return 0;
}

thrift_master_t* thrift_slave_server_ipport_get()
{
    return &h3600_conf.thrift_slave_server;
}

void thrift_engineer_position_set()
{
    write_conf();
//    pos_all_init(&h3600_conf, 0);
}

/* 工作模式 0常规模式，1通量模式 */
int get_throughput_mode(void)
{
    return h3600_conf_get()->throughput_mode;
}

void set_throughput_mode(void *arg)
{
    int ret = 0;
    throughput_mode_t *data = (throughput_mode_t *)arg;
    async_return_t async_return;

    h3600_conf_get()->throughput_mode = data->mode;
    write_conf();

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(data->iuserdata, !!ret, &async_return);
    free(data);
}

void thrift_optical_pos_disable_set(int ch, int disable)
{
    if (ch >= sizeof(h3600_conf.optical_pos_disable)/sizeof(h3600_conf.optical_pos_disable[0])) {
        LOG("overload max ch:%d\n", ch);
        return;
    }

    h3600_conf.optical_pos_disable[ch] = disable;
    write_conf();
}

int thrift_optical_pos_disable_get(int ch)
{
    if (ch >= sizeof(h3600_conf.optical_pos_disable)/sizeof(h3600_conf.optical_pos_disable[0])) {
        LOG("overload max ch:%d\n", ch);
        return 0;
    }

    return h3600_conf.optical_pos_disable[ch];
}

void thrift_mag_pos_disable_set(int ch, int disable)
{
    if (ch >= sizeof(h3600_conf.mag_pos_disable)/sizeof(h3600_conf.mag_pos_disable[0])) {
        LOG("overload max ch:%d\n", ch);
        return;
    }

    h3600_conf.mag_pos_disable[ch] = disable;
    write_conf();
}

int thrift_mag_pos_disable_get(int ch)
{
    if (ch >= sizeof(h3600_conf.mag_pos_disable)/sizeof(h3600_conf.mag_pos_disable[0])) {
        LOG("overload max ch:%d\n", ch);
        return 0;
    }

    return h3600_conf.mag_pos_disable[ch];
}


