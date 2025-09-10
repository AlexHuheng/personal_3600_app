#include "module_sampler_ctl.h"
#include "thrift_handler.h"
#include "module_monitor.h"
#include "movement_config.h"
#include "device_status_count.h"

#include "module_reagent_table.h"

/* 上报样本缓存 */
static sample_info_t sample_infor[TUBE_NUM_MAX];
static uint8_t racks_hat[RACK_NUM_MAX][TUBE_NUM_MAX];
static uint8_t scan_sample_flag; /* 上位机下发的扫码标志 */
static float g_noise_data = 0;/* 记录下位机初始化后的压力本底噪声 */

void sampler_init(void)
{
    uint8_t i, j;

    LOG("sampler init!\n");

    for (i = 0; i < RACK_NUM_MAX; i++) {
        for (j = 0; j < TUBE_NUM_MAX; j++) {
            racks_hat[i][j] = 0;
        }
    }

    /* 重启程序时，解锁进样器电磁铁,并复位LED */
    if (-1 == slip_reinit_to_sampler_board()) {
        LOG("eletro unlock fail!\n");
    }

    scan_sample_flag = true;
}

static void check_sampler_alarm_report(char *alarm_dic)
{
    char alarm_message[FAULT_CODE_LEN] = {0};

    /* 非故障上报进样器部分信息 */
    fault_code_generate(alarm_message, MODULE_CLASS_FAULT_SAMPLER, alarm_dic);
    report_alarm_message(0, alarm_message);
}

void set_sample_scan_flag(uint8_t flag)
{
    scan_sample_flag = flag;
    LOG("set scan sample barcode flag:%d\n", flag);
}

/* 试管架信息上报 */
void slip_racks_infor_report(const slip_port_t *port, slip_msg_t *msg)
{
    uint8_t tx_data = 0;
    report_rack_t *rx_data = NULL;
    uint8_t i = 0;

    if (!msg->length) {
        LOG("param invalid!\n");
        return;
    }
    
    rx_data = (report_rack_t *)&msg->data[0];

    LOG("rack_id:%d\n", rx_data->rack_id);

    /* 取样本 */
    for (i = 0 ; i < TUBE_NUM_MAX ; i++) {
        memset(&sample_infor[i], 0, sizeof(sample_info_t));
        sample_infor[i].rack_index = rx_data->rack_id;
        sample_infor[i].sample_index = i;
        sample_infor[i].exist = (rx_data->tube_exist & (1UL << (i))) ? 1 : 0;
//        sample_infor[i].scan_status = rx_data->result_state;
        sample_infor[i].tube_type = rx_data->type[i];
        sample_infor[i].with_hat = (rx_data->hat & (1UL << (i))) ? 1 : 0;
        sample_infor[i].rack_number = rx_data->rack_number;

        /* 上位机是否下发扫码标志 */
        if (scan_sample_flag) {
            /* 存在样本，且是常规采血管，且条码无效时，才判定为扫码失败 */
            if (sample_infor[i].exist && (REPORT_TUBE_NORMAL == rx_data->type[i]) && (0 == strlen(rx_data->tube_bar[i]))) { /* 仅有样本，但无条码 */
                sample_infor[i].scan_status = false;
            } else if (sample_infor[i].exist && REPORT_TUBE_ADAPTER == rx_data->type[i]) {
                /* 适配器+日立杯时无法扫到样本条码，故需要下位机上报扫码失败 */
                sample_infor[i].scan_status = false;
            } else {
                sample_infor[i].scan_status = true;
                if (strlen(rx_data->tube_bar[i])) {
                    memcpy(sample_infor[i].barcode, rx_data->tube_bar[i], strlen(rx_data->tube_bar[i]));
                }
            }
        } else {
            sample_infor[i].scan_status = true;
        }
        racks_hat[rx_data->rack_number - 1][i] = sample_infor[i].with_hat;
        LOG("hat:%d type: %d barcode:%s\n", sample_infor[i].with_hat, sample_infor[i].tube_type, sample_infor[i].barcode);
    }

    /* 上报thrift */
    if (0 == get_power_off_stat()) {
        report_rack_info(&sample_infor[0]);
        LOG("racks param get success! response now.\n");
    } else {
        LOG("thrift link lost.\n");
    }

    slip_send_node(slip_node_id_get(),
               SLIP_NODE_SAMPLER,
               0x0, 
               SAMPLER_TYPE,
               SAMPLER_RACK_INFOR_RESPONSE_SUBTYPE, 
               1, 
               (void *)&tx_data);
    device_status_count_scanner_check(SCANNER_RACKS);
}

static int sampler_status_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint8_t *data = (uint8_t *)arg;
    uint8_t *result = (uint8_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }
    
    data[0] = result[0];
    data[1] = result[1];
    return 0;
}

/* 电磁铁上锁 0: 掉电 1：解锁 2：上锁*/
int slip_ele_lock_to_sampler(uint8_t index, uint8_t  status)
{
    uint8_t tx_data[2] = {0};
    uint8_t result[2] = {0};

    if (index >= RACK_CHANNEL_MAX) {
        LOG("Invalid rack index %d\n", index);
        return -1;
    }

    if (status == 1) {
        device_status_count_add(DS_SAMPLER_ELE1_COUNT+index, 1);
    }

    tx_data[0] = index;
    tx_data[1] = (1 == status) ? 2 : 1;

    LOG("ele lock: index %d status %d\n", index, status);

    slip_send_node(slip_node_id_get(),
                    SLIP_NODE_SAMPLER,
                    0x0, 
                    SAMPLER_TYPE, 
                    SAMPLER_ELE_LOCK_SYBTYPE,
                    sizeof(tx_data), 
                    &tx_data[0]);
    
    if (0 == result_timedwait(SAMPLER_TYPE, SAMPLER_ELE_LOCK_SYBTYPE, sampler_status_set_result, result, 5000)) {
        if (0 != result[1]) {
            sampler_fault_infor_report(index);
        }
        LOG("sampler ele set:%s\n", (0 == result[1]) ? "success" : "fail");
        return (0 == result[1]) ? 0 : -1;
    }
    return -1;
}

int ele_unlock_by_status(void)
{
    int ret = 0;
    int step = 0;

    for (int i = 0; i < RACK_NUM_MAX; i++) {
        step = slip_ele_lock_to_sampler(i, 0);

        if (!step) /* 解锁成功，上报 */
            report_io_state((output_io_t)(i + TEMP_ELE_CTL_START + 7), 0);

        ret += step;
    }
    LOG("ele all reinit:%s\n", (0 == ret) ? "success" : "fail");

    return (!ret) ? 0 : -1;
}

/* 通电自检 */
int sampler_power_on_func(void)
{
    uint8_t i, j;
    int ret = -1;
    char bar[SCANNER_BARCODE_LENGTH] = {0};

    /* 电磁铁 */
    for (i = 0; i < RACK_NUM_MAX; i++) {
        for (j = 0; j < 2; j++) {
            if (0 != slip_ele_lock_to_sampler(i, 1 - j)) /* 操作失败，退出 */
                goto out;

            usleep(500 * 1000);
        }
    }

    /* 样本架扫码 */
    if (0 == scanner_read_barcode_sync(SCANNER_RACKS, bar, SCANNER_BARCODE_LENGTH)) {
        if (0 != strncmp(bar, "BK", 2)) {
            LOG("scan barcode fail! channel:%d\n", SCANNER_RACKS);
            goto out;
        }
        ret = 0;
    }

out:
    return ret;
}

/* 设置LED状态 0: 关灯 1：红灯 2：绿灯*/
int slip_rack_tank_led_to_sampler(uint8_t index, uint8_t  status)
{
    uint8_t tx_data[2] = {0};
    uint8_t result[2] = {0};

    if (index >= RACK_CHANNEL_MAX) {
        LOG("Invalid rack index %d\n", index);
        return -1;
    }

    tx_data[0] = index;
    tx_data[1] = (1 == status) ? 2 : 1;

    LOG("led status: index %d status %d\n", index, status);

    slip_send_node(slip_node_id_get(),
                    SLIP_NODE_SAMPLER,
                    0x0, 
                    SAMPLER_TYPE,
                    SAMPLER_RACKS_TANK_LED_SUBTYPE, 
                    sizeof(tx_data), 
                    &tx_data[0]);
    
    if (0 == result_timedwait(SAMPLER_TYPE, SAMPLER_RACKS_TANK_LED_SUBTYPE, sampler_status_set_result, result, 5000)) {
        LOG("sampler led set:%s\n", (0 == result[1]) ? "success" : "fail");
        return (0 == result[1]) ? 0 : -1;
    }
    return -1;
}

/* 设置试剂仓按键LED状态 0: 关灯 1：开灯 2：闪烁*/
int slip_button_reag_led_to_sampler(uint8_t status)
{
    uint8_t tx_data[2] = {0};
    uint8_t result[2] = {0};

    tx_data[0] = 0;
    tx_data[1] = status;

    LOG("reag led status: status %d\n", status);

    slip_send_node(slip_node_id_get(),
                    SLIP_NODE_SAMPLER,
                    0x0,
                    SAMPLER_TYPE,
                    SAMPLER_LED_REAG_CTL_SUBTYPE,
                    sizeof(tx_data),
                    &tx_data[0]);

    if (0 == result_timedwait(SAMPLER_TYPE, SAMPLER_LED_REAG_CTL_SUBTYPE, sampler_status_set_result, result, 5000)) {
        LOG("sampler led set:%s\n", (0 == result[1]) ? "success" : "fail");
        return (0 == result[1]) ? 0 : -1;
    }
    return -1;
}


/* 获取凝块数据 */
static int clot_data_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint8_t *data = (uint8_t *)arg;
    uint8_t *result = (uint8_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    data[0] = result[1];
    return 0;
}

int slip_set_clot_data_from_sampler(uint8_t check_switch, int orderno)
{
    uint8_t tx_data[5] = {0};
    uint8_t result = -1;
    int ret = -1;

    LOG("send to sampler orderno : %d.\n", orderno);
    tx_data[0] = check_switch;
    tx_data[1] = orderno >> 24;
    tx_data[2] = orderno >> 16;
    tx_data[3] = orderno >> 8;
    tx_data[4] = orderno;
    slip_send_node(slip_node_id_get(),
                    SLIP_NODE_SAMPLER,
                    0x0,
                    SAMPLER_TYPE,
                    SAMPLER_CLOT_GET_SUBTYPE,
                    sizeof(tx_data),
                    &tx_data);
    if (0 == result_timedwait(SAMPLER_TYPE, SAMPLER_CLOT_GET_SUBTYPE, clot_data_set_result, &result, 50)) {
        LOG("result success!!!\n");
        ret = 0;
    }
    return ret;
}

static int liq_noise_sign_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    float *result = (float *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    g_noise_data = *result;
    LOG("get sign pressure noise is %f.\n",g_noise_data);

    return 0;
}

int slip_liq_cir_noise_set(void)
{
    return -1;

    uint8_t tx_data = 0;
    int ret = -1;
    float result = 0;

    slip_send_node(slip_node_id_get(),
                    SLIP_NODE_SAMPLER,
                    0x0,
                    SAMPLER_TYPE,
                    SAMPLER_CLOT_NOISE_SUBTYPE,
                    sizeof(tx_data),
                    &tx_data);
    if (0 == result_timedwait(SAMPLER_TYPE, SAMPLER_CLOT_NOISE_SUBTYPE, liq_noise_sign_result, &result, 5000)) {
        LOG("set pressure data success.\n");
        ret = 0;
    }

    return ret;
}

/* 复位M7子板状态 */
int slip_reinit_to_sampler_board(void)
{
    uint8_t result[2] = {0};

    LOG("sub board reinit!\n");

    slip_send_node(slip_node_id_get(),
                    SLIP_NODE_SAMPLER,
                    0x0,
                    SAMPLER_TYPE,
                    SAMPLER_SOFT_REINIT_SUBTYPE,
                    0,
                    NULL);

    if (0 == result_timedwait(SAMPLER_TYPE, SAMPLER_SOFT_REINIT_SUBTYPE, sampler_status_set_result, result, 5000)) {
        LOG("sampler board reinit:%s\n", (0 == result[0]) ? "success" : "fail");
        return result[0];
    }
    return -1;
}

static int scanner_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    scanner_result_t *data = (scanner_result_t *)arg;
    scanner_result_t *result = (scanner_result_t *)msg->data;
    if (msg->length == 0) {
        return 1;
    }

    if (result->type == data->type) {
        data->status = result->status;
        data->data_len = result->data_len;
//        snprintf(data->data, data->data_len + 1, "%s", result->data);
        strncpy(data->data, result->data, result->data_len);
        return 0;
    }

    return -1;
}

int scanner_version(unsigned char type, char *version, int len)
{
    scanner_result_t result = {0};

    slip_send_node(slip_node_id_get(), 
                   SLIP_NODE_SAMPLER, 
                   0x0, 
                   SCANNER_TYPE,
                   SCANNER_VERSION_SUBTYPE, 
                   sizeof(type),
                   &type);

    result.type = type;
    result.status = -1;
    if (0 == result_timedwait(SCANNER_TYPE, SCANNER_VERSION_SUBTYPE, scanner_result, &result, 2000)) {
        strncpy(version, result.data, strlen(result.data));
        return result.status;
    }
    return -1;
}

/* 扫码 */
int scanner_read_barcode_async(unsigned char type , int pt_dd_debug, char *barcode)
{
    barcode_async_t scanner_t;
    scanner_result_t result = {0};

    scanner_t.type = type;
    scanner_t.pt_dd_mode = (unsigned char)pt_dd_debug;

    slip_send_node(slip_node_id_get(), 
                    SLIP_NODE_SAMPLER, 
                    0x0, 
                    SCANNER_TYPE, 
                    SCANNER_READ_BARCODE_SUBTYPE, 
                    sizeof(scanner_t), 
                    (void *)&scanner_t);

    memset(&result, 0, sizeof(scanner_result_t));
    result.type = type;
    result.status = -1;
    if (0 == result_timedwait(SCANNER_TYPE, SCANNER_READ_BARCODE_SUBTYPE, scanner_result, &result, 4000)) {
        if(0 == result.status) {
            strncpy(barcode, result.data, strlen(result.data));
            LOG("get bar is %s len is %d.\n", barcode, strlen(barcode));
        }
        return (0 == result.status) ? 0 : -1;
    }
    return -1;
}

/* 扫码上层接口 */
int scanner_read_barcode_sync(unsigned char type, char *barcode, int len)
{
    int ret = -1;

    LOG("scanner start!  type:%d\n", type);
    device_status_count_scanner_check(type);
    ret = scanner_read_barcode_async(type , 0, barcode);
    return ret;
}

static int compensate_scan_response_get(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    scanner_result_t *data = (scanner_result_t *)arg;
    scanner_result_t *result = (scanner_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->type == SCANNER_REAGENT_CONT) {
        LOG("cmd send successed\n");
        return 0;
    } else if (result->type == CONT_RESULT_GET) {
        if (result->status == 0) {
            if (strlen(result->data) > result->data_len) { /* 回传的内容有乱码 */
                memset(&result->data[result->data_len], 0, strlen(result->data) - result->data_len);
            }
            strncpy(data->data, result->data, strlen(result->data));
            LOG("compensate scan barcode successed. bar:%s\n", result->data);
        } else {
            LOG("compensate scan barcode faild.\n");
        }
        return 0;
    }

    return -1;
}

int scanner_compensate_read_barcode(char *barcode, int len, int stage)
{
    uint8_t scanner_cmd_set = 0;
    scanner_result_t result = {0};

    if (stage) {
        slip_send_node(slip_node_id_get(), 
                       SLIP_NODE_SAMPLER, 
                       0x0, 
                       SCANNER_TYPE, 
                       SCANNER_READ_COMPENSATE_BARCODE_SUBTYPE, 
                       sizeof(scanner_cmd_set),
                       (void *)&scanner_cmd_set);
        if (0 == result_timedwait(SCANNER_TYPE, SCANNER_READ_COMPENSATE_BARCODE_SUBTYPE, compensate_scan_response_get, &result, 4000)) {
        }
    } else {
        slip_send_node(slip_node_id_get(), 
                        SLIP_NODE_SAMPLER,
                        0x0,
                        SCANNER_TYPE,
                        SAMPLER_READ_BARCODE_RESPONSE,
                        sizeof(scanner_cmd_set),
                        (void *)&scanner_cmd_set);
        if (0 == result_timedwait(SCANNER_TYPE, SAMPLER_READ_BARCODE_RESPONSE, compensate_scan_response_get, &result, 4000)) {
            if(0 == result.status) {
                strncpy(barcode, result.data, strlen(result.data));
            }
            return (0 == result.status) ? 0 : -1;
        }
    }

    return 0;
}

/* 获取管帽信息 */
sp_hat_t get_sp_is_carry_hat(struct sample_tube *tube)
{
    int ret;

//    if (PP_1_8 == tube->type || PP_2_7 == tube->type) {
//        ret = 0;
//    } else {
        ret = racks_hat[tube->slot_id - 1][tube->sample_index - 1];
//    }
    LOG("hat get! rack:%d, sample:%d hat:%d\n", tube->slot_id, tube->sample_index, ret);

    return ret;
}

//压力噪声
float g_presure_noise_get(void)
{
    return g_noise_data;
}

void sampler_fault_infor_report(uint8_t index)
{
    if (index >= RACK_NUM_MAX) {
        return;
    }

    switch (index) {
        case 0: check_sampler_alarm_report(MODULE_FAULT_ELETRO_1_CTL_FAILED); break;
        case 1: check_sampler_alarm_report(MODULE_FAULT_ELETRO_2_CTL_FAILED); break;
        case 2: check_sampler_alarm_report(MODULE_FAULT_ELETRO_3_CTL_FAILED); break;
        case 3: check_sampler_alarm_report(MODULE_FAULT_ELETRO_4_CTL_FAILED); break;
        case 4: check_sampler_alarm_report(MODULE_FAULT_ELETRO_5_CTL_FAILED); break;
        case 5: check_sampler_alarm_report(MODULE_FAULT_ELETRO_6_CTL_FAILED); break;
        default:
            break;
    }
}

static int scanner_restart_response(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    int *rx_msg = (int *)(msg->data);
    int *result = (int *)arg;

    if (msg->length == 0) {
        return -1;
    }

    if (*rx_msg != 0) {
        *result = 2;//重启后此标志用于再次扫码
    }

    return 0;
}

/* 扫码器重启 */
int scanner_restart(void)
{
    uint8_t scanner_cmd_set = 1;
    int result = -1;

    slip_send_node(slip_node_id_get(),
                   SLIP_NODE_SAMPLER,
                   0x0,
                   SCANNER_TYPE,
                   SCANNER_RSTART_SUBTYPE,
                   sizeof(scanner_cmd_set),
                   (void *)&scanner_cmd_set);

    if (0 == result_timedwait(SCANNER_TYPE, SCANNER_RSTART_SUBTYPE, scanner_restart_response, &result, 4000)) {
        LOG("waiting scanner restart.\n");
    }
    restart_sem_post();

    return result;
}

static int scanner_commicate_handle(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    int *result = (int *)arg;
    int *rx_msg = (int *)msg->data;

    if (msg->length == 0) {
        return -1;
    }
    *result = *rx_msg;
    LOG("reagent scanner connection %s.\n", (*result == 1) ? "connect" : "interrupt!");
    return 0;
}

/* 扫码器通信正常则表示正常工作，失败则表示挂机需进行重启 */
int scanner_get_communicate(void)
{
    int scanner_cmd_set = 1;
    int result = -1;

    slip_send_node(slip_node_id_get(),
                   SLIP_NODE_SAMPLER,
                   0x0,
                   SCANNER_TYPE,
                   SAMPLER_CANNER_COMMUNICATE_GET_SUBTYPE,
                   sizeof(scanner_cmd_set),
                   (void *)&scanner_cmd_set);
    if (0 == result_timedwait(SCANNER_TYPE, SAMPLER_CANNER_COMMUNICATE_GET_SUBTYPE, scanner_commicate_handle, &result, 4000)) {
        usleep(200*1000);
    }

    return result;
}

static int jp_value_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint8_t *data = (uint8_t *)arg;
    uint8_t *result = (uint8_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (data[0] == result[0]) {
        data[1] = result[1];
        return 0;
    }

    return -1;
}

int scanner_jp_value_get(uint8_t index)
{
    uint8_t result[2] = {0};

    slip_send_node(slip_node_id_get(), SLIP_NODE_SAMPLER, 0x0, SCANNER_TYPE, SCANNER_JP_PARAM_GET_SUBTYPE, sizeof(uint8_t), &index);

    result[0] = index;
    result[1] = 0;
    if (0 == result_timedwait(SCANNER_TYPE, SCANNER_JP_PARAM_GET_SUBTYPE, jp_value_result, &result, 1500)) {
        return result[1];
    }
    return -1;
}
