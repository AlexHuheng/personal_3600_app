#include "module_temperate_ctl.h"

static slip_temperate_ctl_t temperate_ctl = {0};
static FILE *temperate_data_fd = NULL;

static void log_temp_data(const char *format, ...)
{
    static int fd = -1;
    static char log_diy_enable = 0;

    if (fd == -1) {
        fd = open("/tmp/log_temp", O_RDONLY | O_CREAT | O_TRUNC);
    }

    read(fd, &log_diy_enable, 1);
    lseek(fd, 0, SEEK_SET);

    if (log_diy_enable == '1') {
        va_list args;

        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

/*
功能：上位机获取温度数据
参数：
    index：温度通道索引
返回值：
    -1：失败
    其它：温度数据
*/
int thrift_temp_get(short index)
{
    int temp = 0;
    static int error_cnt[THRIFT_OPTICAL2+1] = {0};

    if (index == THRIFT_REAGENTCASE) {
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL3])/100);
    } else if (index == THRIFT_REAGENTPIPE) {
        uint16_t sensor_error_flag = 0;
        uint16_t sensor_temp_ch = 0;
        static time_t base_time = 0;
        static int maintence_state_back = 0;

        #if 0 /* 老温控板，PT100加热针 */
        sensor_error_flag = SENSOR_ERROR_FLAG5;
        sensor_temp_ch = SENSOR_TEMP_CHNNEL5;
        #else /* 新温控板，NTC加热针 */
        sensor_error_flag = SENSOR_ERROR_FLAG8;
        sensor_temp_ch = SENSOR_TEMP_CHNNEL8;
        #endif

        /* 为避免维护流程后，R2温度随意报警，当从 维护中 到 维护完成，延时20s时间让R2温度稳定 */
        if (maintence_state_back==1 && machine_maintence_state_get()==0) {
            base_time = time(NULL);
        }
        maintence_state_back = machine_maintence_state_get();

        /* 若处于维护流程、余量探测、自检流程，则加热针上报一个37度左右的随机值*/
        if ((machine_maintence_state_get() || time(NULL)-base_time<20) && (!(temperate_ctl.sensor_status & sensor_error_flag))) {
            temp = 370 + rand()%2;
        } else {
            temp = (int)((temperate_ctl.ic_temp_ad[sensor_temp_ch])/100);
        }
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL8])/100);
    } else if (index == THRIFT_INCUBATIONAREA) {
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL7])/100);
    } else if (index == THRIFT_MAGNETICBEAD) {
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL5])/100);
    } else if (index == THRIFT_ENVIRONMENTAREA) {
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL4])/100);
    } else if (index == THRIFT_REAGENTCASEGLASS) {
        /* temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL3])/100); */
        temp = 370 + rand()%5; /* 因试剂仓玻璃片的温度传感器已经去除，所以上报一个37度左右的随机值 */
    } else if (index == THRIFT_OPTICAL1) {
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL6])/100);
    } else if (index == THRIFT_OPTICAL2) {
        temp = (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL4])/100);
    } else {
        temp = -1;
    }

    if (((index==THRIFT_REAGENTCASE) && (temp>=121 || temp<=79)) ||
        ((index==THRIFT_INCUBATIONAREA || index==THRIFT_MAGNETICBEAD || index==THRIFT_OPTICAL1 || index==THRIFT_OPTICAL2) && (temp>=379 || temp<=361))) {
        if (error_cnt[index] == 0) {
            LOG("debug temp error start. idx:%d, cur:%d, env:%d, reagent:%d; run:%d, fan:0x%02X, temp:0x%02X, sensor:0x%02X\n", index, temp,
                (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL4])/100),  (int)((temperate_ctl.ic_temp_ad[SENSOR_TEMP_CHNNEL3])/100), 
                machine_maintence_state_get(), temperate_ctl.fan_status, temperate_ctl.temperate_status, temperate_ctl.sensor_status);
        }

        error_cnt[index]++;
    } else {
        if (error_cnt[index] > 0) {
            LOG("debug temp error resume. idx:%d, count:%d\n", index, error_cnt[index]);
        }
        error_cnt[index] = 0;
    }

    return temp;
}

static int slip_temperate_ctl_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    slip_temperate_ctl_t *dst_data = (slip_temperate_ctl_t *)arg;
    slip_temperate_ctl_t *result = (slip_temperate_ctl_t *)msg->data;
    int i = 0;
    
    if (msg->length == 0) {
        return 1;
    }

    dst_data->fan_status = result->fan_status;
    dst_data->temperate_status = ntohs(result->temperate_status);
    dst_data->sensor_status = ntohs(result->sensor_status);

    for (i=0; i<sizeof(dst_data->ic_temp_ad)/sizeof(dst_data->ic_temp_ad[0]); i++) {
        dst_data->ic_temp_ad[i] = ntohl(result->ic_temp_ad[i]);
    }

    log_temp_data("fan_status:0x%02X, temp_status:0x%02X, sensor_status:0x%02X\n",
        dst_data->fan_status, dst_data->temperate_status, dst_data->sensor_status);

    log_temp_data("ic1 1~4:%f, %f, %f, %f\n", dst_data->ic_temp_ad[0]/1000.0, dst_data->ic_temp_ad[1]/1000.0,
        dst_data->ic_temp_ad[2]/1000.0, dst_data->ic_temp_ad[3]/1000.0);

    log_temp_data("ic2 1~4:%f, %f, %f, %f\n", dst_data->ic_temp_ad[4]/1000.0, dst_data->ic_temp_ad[5]/1000.0,
        dst_data->ic_temp_ad[6]/1000.0, dst_data->ic_temp_ad[7]/1000.0);

    log_temp_data("ic3 1~4:%f, %f, %f, %f\n", dst_data->ic_temp_ad[8]/1000.0, dst_data->ic_temp_ad[9]/1000.0,
            dst_data->ic_temp_ad[10]/1000.0, dst_data->ic_temp_ad[11]/1000.0);

    return 0;
}

/* 
功能：获取温度数据
参数：
    result:温度数据 
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_get(slip_temperate_ctl_t *result)
{
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_GET_SUBTYPE, 0, NULL);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_GET_SUBTYPE, slip_temperate_ctl_get_result, (void*)result, 2000)) {
        return 0;
    }

    return -1;
}

static int slip_temperate_ctl_pwm_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：设置 温度控制开关及模式
参数：
    index:温控通道索引
    enable:使能标志（0:关闭温控 1:pid温控，0xfe：次全功率（带最大功率限制）, 0xff：全功率）
    times:使能时间ms(0:一直使能， 其它值：使能时间)
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_pwm_set(temperate_target_t index, uint8_t enable, uint16_t times)
{
    slip_temperate_ctl_pwm_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    if (TEMP_NEEDLE_R2 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT0;
    } else if (TEMP_MAGNETIC == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT1;
    } else if (TEMP_OPTICAL1 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT2;
    } else if (TEMP_OPTICAL2 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT3;
    } else if (TEMP_REAGENT_SCAN == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT4;
    } else if (TEMP_INCUBATION == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT5;
    } else if (TEMP_REAGENT_CONTAINER1 == index) {
        slip_data.index = PID_PARAM_INDEX_COOL0;
    } else if (TEMP_REAGENT_CONTAINER2 == index) {
        slip_data.index = PID_PARAM_INDEX_COOL1;
    }

    slip_data.enable = enable;
    slip_data.times = htons(times);
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_PWM_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_PWM_SET_SUBTYPE, slip_temperate_ctl_pwm_set_result, (void*)&result, 2000)) {
        return result.status;
    }

    return -1;
}

static int slip_temperate_ctl_goal_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：设置 温度控制的目标温度
参数：
    index:温控通道索引
    data:目标温度（实际值*10）
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_goal_set(temperate_target_t index, uint32_t data)
{
    slip_temperate_ctl_goal_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    if (TEMP_NEEDLE_R2 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT0;
    } else if (TEMP_MAGNETIC == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT1;
    } else if (TEMP_OPTICAL1 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT2;
    } else if (TEMP_OPTICAL2 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT3;
    } else if (TEMP_REAGENT_SCAN == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT4;
    } else if (TEMP_INCUBATION == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT5;
    } else if (TEMP_REAGENT_CONTAINER1 == index) {
        slip_data.index = PID_PARAM_INDEX_COOL0;
    } else if (TEMP_REAGENT_CONTAINER2 == index) {
        slip_data.index = PID_PARAM_INDEX_COOL1;
    }

    slip_data.goal = htonl(data);
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_SET_SUBTYPE, slip_temperate_ctl_goal_set_result, (void*)&result, 2000)) {
        return result.status;
    }

    return -1;
}

static int slip_temperate_ctl_maxpower_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：设置 温度控制的最大功率
参数：
    index:温控通道索引
    data:最大功率
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_maxpower_set(temperate_target_t index, uint8_t data)
{
    slip_temperate_ctl_maxpower_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    if (TEMP_NEEDLE_R2 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT0;
    } else if (TEMP_MAGNETIC == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT1;
    } else if (TEMP_OPTICAL1 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT2;
    } else if (TEMP_OPTICAL2 == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT3;
    } else if (TEMP_REAGENT_SCAN == index) {
        slip_data.index = PID_PARAM_INDEX_HEAT4;
    } else if (TEMP_INCUBATION == index){
        slip_data.index = PID_PARAM_INDEX_HEAT5;
    } else if (TEMP_REAGENT_CONTAINER1 == index) {
        slip_data.index = PID_PARAM_INDEX_COOL0;
    } else if (TEMP_REAGENT_CONTAINER2 == index) {
        slip_data.index = PID_PARAM_INDEX_COOL1;
    }

    slip_data.maxpower = data;
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_MAXPOWER_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_MAXPOWER_SET_SUBTYPE, slip_temperate_ctl_maxpower_set_result, (void*)&result, 2000)) {
        return result.status;
    }

    return -1;
}

static int slip_temperate_gpio_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    slip_temperate_ctl_gpio_t *data = (slip_temperate_ctl_gpio_t *)arg;
    slip_temperate_ctl_gpio_t *result = (slip_temperate_ctl_gpio_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->gpio_id == data->gpio_id) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/* 
功能：受限制的温控板gpio设置 
参数：
    index:温控通道索引
    status: gpio电平
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_gpio_set(uint8_t index, uint8_t status)
{
    slip_temperate_ctl_gpio_t data = {0};
    slip_temperate_ctl_gpio_t result = {0};

    if (value_get_control() == 0) {
        LOG("gpio set flag is clear, index:%d, status:%d\n", index, status);
        return 0;
    }

    data.gpio_id = index;
    data.status = status;

    slip_send_node(slip_node_id_get(), SLIP_NODE_TEMP_CTRL, 0x0, IO_TYPE, IO_SET_SUBTYPE, sizeof(data), &data);

    result.gpio_id = index;
    result.status = -1;
    if (0 == result_timedwait(IO_TYPE, IO_SET_SUBTYPE, slip_temperate_gpio_result, &result, 2000)) {
        return result.status;
    }
    return -1;
}

/* 
功能：不受限制的温控板gpio设置 
参数：
    index:温控通道索引
    status: gpio电平
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_gpio_set_direct(uint8_t index, uint8_t status)
{
    slip_temperate_ctl_gpio_t data = {0};
    slip_temperate_ctl_gpio_t result = {0};

    data.gpio_id = index;
    data.status = status;

    slip_send_node(slip_node_id_get(), SLIP_NODE_TEMP_CTRL, 0x0, IO_TYPE, IO_SET_SUBTYPE, sizeof(data), &data);

    result.gpio_id = index;
    result.status = -1;
    if (0 == result_timedwait(IO_TYPE, IO_SET_SUBTYPE, slip_temperate_gpio_result, &result, 2000)) {
        return result.status;
    }
    return -1;
}


static int slip_temperate_ctl_sensor_cali_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    slip_temperate_ctl_sensor_cali_t *data = (slip_temperate_ctl_sensor_cali_t *)arg;
    slip_temperate_ctl_sensor_cali_t *result = (slip_temperate_ctl_sensor_cali_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->sensor_cali = result->sensor_cali;
        return 0;
    }

    return -1;
}

/*
功能：获取 温度传感器的校准参数 
参数：
    index: 温度通道号
返回值：
    -1：失败
    其它：校准值（实际值*1000）
*/
int16_t slip_temperate_ctl_sensor_cali_get(uint8_t index)
{
    slip_temperate_ctl_sensor_cali_t result = {0};
    uint8_t slip_data = 0;

    slip_data = index;

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_SENSOR_CALI_GET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = index;
    result.sensor_cali = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_SENSOR_CALI_GET_SUBTYPE, slip_temperate_ctl_sensor_cali_get_result, (void*)&result, 2000)) {
        return ntohs(result.sensor_cali);
    }

    return -1;
}

static int slip_temperate_ctl_sensor_cali_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/*
功能：设置 温度传感器的校准参数 
参数：
    index: 温度通道号
    data：校准值（实际值*1000）
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_sensor_cali_set(uint8_t index, int16_t data)
{
    slip_temperate_ctl_sensor_cali_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    slip_data.index = index;
    slip_data.sensor_cali = htons(data);

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_SENSOR_CALI_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_SENSOR_CALI_SET_SUBTYPE, slip_temperate_ctl_sensor_cali_set_result, (void*)&result, 2000)) {
        return result.status;
    }

    return -1;
}

static int slip_temperate_ctl_goal_cali_get_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    slip_temperate_ctl_sensor_cali_t *data = (slip_temperate_ctl_sensor_cali_t *)arg;
    slip_temperate_ctl_sensor_cali_t *result = (slip_temperate_ctl_sensor_cali_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->sensor_cali = result->sensor_cali;
        return 0;
    }

    return -1;
}

/*
功能：获取 目标温度的校准参数 
参数：
    index: pid通道号
返回值：
    -1：失败
    其它：校准值（实际值*1000）
*/
int16_t slip_temperate_ctl_goal_cali_get(uint8_t index)
{
    slip_temperate_ctl_sensor_cali_t result = {0};
    uint8_t slip_data = 0;

    slip_data = index;

    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_CALI_GET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = index;
    result.sensor_cali = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_CALI_GET_SUBTYPE, slip_temperate_ctl_goal_cali_get_result, (void*)&result, 2000)) {
        return ntohs(result.sensor_cali);
    }

    return -1;
}

static int slip_temperate_ctl_goal_cali_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

/*
功能：设置 目标温度的校准参数 
参数：
    index: pid通道号
    data：校准值（实际值*1000）
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_goal_cali_set(uint8_t index, int16_t data)
{
    slip_temperate_ctl_sensor_cali_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    slip_data.index = index;
    slip_data.sensor_cali = htons(data);
    
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_CALI_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    result.index = slip_data.index;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_CALI_SET_SUBTYPE, slip_temperate_ctl_goal_cali_set_result, (void*)&result, 2000)) {
        return result.status;
    }

    return -1;
}

static int slip_temperate_ctl_sensor_type_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint8_t *data = (uint8_t *)arg;
    uint8_t *result = (uint8_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    LOG("sensor_type_set, data:%d, result:%d\n", *data, *result);
    *data = *result;

    return 0;
}

/*
功能：设置获取温度数据类型 
参数：
    type: 0:液体温度(估算值)（默认） 1：传感器温度值
返回值：
    -1：失败
    0：成功
*/
int slip_temperate_ctl_sensor_type_set(uint8_t type)
{
    uint8_t slip_data = 0;
    uint8_t result = 0;

    slip_data = type;
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_SENSOR_TYPE_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_SENSOR_TYPE_SET_SUBTYPE, slip_temperate_ctl_sensor_type_set_result, (void*)&result, 2000)) {
        return 0;
    }

    return -1;
}

static int slip_temperate_ctl_goal_mode_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    uint8_t *data = (uint8_t *)arg;
    uint8_t *result = (uint8_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    LOG("goal_mode_set, data:%d, result:%d\n", *data, *result);
    *data = *result;

    return 0;
}

/*
设置 目标温度模式
type: 0:自动调整目标温度，PID模式（默认） 1：手动模式
*/
int slip_temperate_ctl_goal_mode_set(uint8_t type)
{
    uint8_t slip_data = 0;
    uint8_t result = 0;

    slip_data = type;
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_MODE_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_GOAL_MODE_SET_SUBTYPE, slip_temperate_ctl_goal_mode_set_result, (void*)&result, 2000)) {
        return 0;
    }

    return -1;
}

static int slip_temperate_ctl_fan_enable_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }
    
    return 0;
}

/*
设置 风扇PWM使能开关
enable: 0：关闭，1：开启
*/
int slip_temperate_ctl_fan_enable_set(fan_pwm_index_t index, uint8_t enable)
{
    slip_temperate_ctl_fan_enable_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    slip_data.index = index;
    slip_data.enable = enable;
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_FAN_ENABLE_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_FAN_ENABLE_SET_SUBTYPE, slip_temperate_ctl_fan_enable_set_result, (void*)&result, 2000)) {
        return 0;
    }

    return -1;
}

static int slip_temperate_ctl_fan_duty_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    temperate_ctl_pwm_result_t *data = (temperate_ctl_pwm_result_t *)arg;
    temperate_ctl_pwm_result_t *result = (temperate_ctl_pwm_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->index == data->index) {
        data->status = result->status;
        return 0;
    }
    
    return 0;
}

/*
设置 风扇PWM模式和占空比
mode: 0：自动占空比，1：手动占空比
duty: 0~100
*/
int slip_temperate_ctl_fan_duty_set(fan_pwm_index_t index, uint8_t mode, uint8_t duty)
{
    slip_temperate_ctl_fan_duty_t slip_data = {0};
    temperate_ctl_pwm_result_t result = {0};

    slip_data.index = index;
    slip_data.mode = mode;
    slip_data.duty = duty;
    slip_send_node(slip_node_id_get(),
        SLIP_NODE_TEMP_CTRL, 0x0, OTHER_TYPE, OTHER_TEMPERATE_CTL_FAN_DUTY_SET_SUBTYPE, sizeof(slip_data), (void*)&slip_data);

    if (0 == result_timedwait(OTHER_TYPE, OTHER_TEMPERATE_CTL_FAN_DUTY_SET_SUBTYPE, slip_temperate_ctl_fan_duty_set_result, (void*)&result, 2000)) {
        return 0;
    }

    return -1;
}

/****************************************
控制 所有温度控制
enable: 0：关闭 1：使能
regent_table_flag: 0：更改试剂仓制冷状态 1：保持试剂仓制冷状态
*****************************************/
int all_temperate_ctl(uint8_t enable, int regent_table_flag)
{
    int i = 0;

    for (i=0; i<TEMP_MAX; i++) {
        /* 试剂仓玻璃片 跟随 试剂仓帕尔贴的开关状态 */
        if ((i==TEMP_REAGENT_CONTAINER1 || i==TEMP_REAGENT_CONTAINER2 || i==TEMP_REAGENT_SCAN) && regent_table_flag==1) {
            LOG("jump regent_table control\n");
            continue;
        }

        if (slip_temperate_ctl_pwm_set(i, enable, 0) == -1) {
            LOG("temperate ctl (%d) ch fail\n", i);
            return -1;
        }
    }

    for (i=0; i<FAN_PWM_MAX; i++) {
        if ((i==FAN_PWM_0 || i==FAN_PWM_1) && regent_table_flag==1) {
            LOG("jump regent_fan control\n");
            continue;
        }

        if (slip_temperate_ctl_fan_enable_set(i, enable) == -1) {
            LOG("fan ctl (%d) ch fail\n", i);
            return -1;
        }
    }

    return 0;
}

/* 功能：初始化温度数据文件 */
static int temperate_data_file_init()
{
    if (temperate_data_fd == NULL) {
        temperate_data_fd = fopen(TEMEPRATE_DATA_FILE, "a");
        if (temperate_data_fd == NULL) {
            LOG("open temperate data file fail\n");
            return -1;
        }
    }

    return 0;
}

/* 功能：保存温度数据 */
static int save_temperate_data(float temperate_data[][TEMEPRATE_DATA_LINE_SIZE], int count)
{
    int i = 0;
    int num1 = 0, num2 = 0;
    int idx = 0;
    FILE *tmp_fp = NULL;
    struct timeval tv;
    struct tm tm;

    if (temperate_data_fd != NULL) {
        if (ftell(temperate_data_fd) > TEMEPRATE_DATA_FILE_MAX_SIZE) {
            fclose(temperate_data_fd);
            if ((tmp_fp = fopen(TEMEPRATE_DATA_FILE".0", "r")) != NULL) {
                fclose(tmp_fp);
                remove(TEMEPRATE_DATA_FILE".0");
            }
            LOG("over temperate data file, tmp_fp: %p\n", tmp_fp);
            rename(TEMEPRATE_DATA_FILE, TEMEPRATE_DATA_FILE".0");
            temperate_data_fd = fopen(TEMEPRATE_DATA_FILE, "w");
            if (temperate_data_fd == NULL) {
                LOG("reopen temperate data file fail\n");
                return -1;
            }
        }
        
        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm);

        /* 环境温度 */
        fprintf(temperate_data_fd, "==env,time:%02d-%02d %02d:%02d:%02d\t", 
            tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        idx = 0;
        num1 = count/TEMEPRATE_SAVE_FILE_DATA_SIZE;
        num2 = count%TEMEPRATE_SAVE_FILE_DATA_SIZE;
        for (i=0; i<num1; i++) {
            fprintf(temperate_data_fd, "%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t",
                temperate_data[0][idx], temperate_data[0][idx+1], temperate_data[0][idx+2],
                temperate_data[0][idx+3], temperate_data[0][idx+4], temperate_data[0][idx+5]);
                idx += TEMEPRATE_SAVE_FILE_DATA_SIZE;
        }

        for (i=0; i<num2; i++) {
            fprintf(temperate_data_fd, "%0.1f\t", temperate_data[0][idx]);
            idx++;
        }
        fprintf(temperate_data_fd, "\n");

        /* 试剂仓温度 */
        fprintf(temperate_data_fd, "==reg,time:%02d-%02d %02d:%02d:%02d\t", 
            tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        idx = 0;
        num1 = count/TEMEPRATE_SAVE_FILE_DATA_SIZE;
        num2 = count%TEMEPRATE_SAVE_FILE_DATA_SIZE;
        for (i=0; i<num1; i++) {
            fprintf(temperate_data_fd, "%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t",
                temperate_data[1][idx], temperate_data[1][idx+1], temperate_data[1][idx+2],
                temperate_data[1][idx+3], temperate_data[1][idx+4], temperate_data[1][idx+5]);
                idx += TEMEPRATE_SAVE_FILE_DATA_SIZE;
        }

        for (i=0; i<num2; i++) {
            fprintf(temperate_data_fd, "%0.1f\t", temperate_data[1][idx]);
            idx++;
        }
        fprintf(temperate_data_fd, "\n");

        fflush(temperate_data_fd);
    }

    return 0;
}

/* 温控主任务 */
static void *temperate_ctl_task(void *arg)
{
    int ret = -1;
    uint32_t temp_ready_count = 0;
    uint8_t temp_ready_flag = 0;
    uint8_t fan_duty_fisrt_flag = 0;
    uint32_t lost_comm = 0;
    int report_err_flag = 0;
    uint8_t fan_status_back = FUN_ERROR_FLAG0 | FUN_ERROR_FLAG1 | FUN_ERROR_FLAG4 |FUN_ERROR_FLAG5; /* h3600只有试剂仓的两个风扇支持异常检测 */
    uint16_t temperate_status_back = 0;
    uint16_t sensor_status_back = 0;
    int regent_fan_mode = 0;
    machine_stat_t machine_status_back = -1;
    int i = 0;
    uint8_t fan_error_talbe[FUN_NUMBER] = {
        FUN_ERROR_FLAG0, FUN_ERROR_FLAG1, FUN_ERROR_FLAG2, FUN_ERROR_FLAG3
    };
    char *fan_fault_talbe[FUN_NUMBER] = {
        MODULE_FAULT_FAN0, MODULE_FAULT_FAN1, MODULE_FAULT_FAN2, MODULE_FAULT_FAN3
    };
    float temperate_data[2][TEMEPRATE_DATA_LINE_SIZE] = {0};
    uint32_t temperate_cnt = 0;

    slip_temperate_ctl_fan_duty_set(FAN_PWM_0, 0, 0);
    slip_temperate_ctl_fan_duty_set(FAN_PWM_1, 0, 0);
    slip_temperate_ctl_fan_duty_set(FAN_PWM_2, 0, 0);
    slip_temperate_ctl_fan_duty_set(FAN_PWM_3, 0, 0);

    /* 重新触发试剂仓制冷温控 */
    usleep(1000);
    slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER1, TEMP_CTL_OFF, 0);
    slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER2, TEMP_CTL_OFF, 0);
    slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER1, TEMP_CTL_NORMAL_ON, 0);
    slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER2, TEMP_CTL_NORMAL_ON, 0);

    LOG("start\n");
    while (1) {
        ret = slip_temperate_ctl_get(&temperate_ctl);
        if (ret == -1) {
            if (lost_comm == 0) {
                LOG("connect error start\n");
            }

            if (module_is_upgrading_now(SLIP_NODE_TEMP_CTRL, OTHER_TEMPERATE_CTL_GET_SUBTYPE) == 0 && ++lost_comm >= TEMP_ERROR_COUNT_MAX) {
                if (thrift_salve_heartbeat_flag_get() == 1 && report_err_flag == 0) {
                    FAULT_CHECK_DEAL(FAULT_CONNECT, MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, (void *)MODULE_FAULT_TEMP_CONNECT);
                    report_err_flag = 1;
                }
            }
            sleep(GET_TEMPERATE_INTERVAL);
            continue;
        } else {
            if (lost_comm > 0) {
                LOG("connect error(count:%d, flag:%d) resume\n", lost_comm, report_err_flag);
            }

            lost_comm = 0;
            report_err_flag = 0;
        }

        /* 记录温度数据，10min保存一次 */
        temperate_data[0][temperate_cnt] = thrift_temp_get(THRIFT_ENVIRONMENTAREA)/10.0;
        temperate_data[1][temperate_cnt] = thrift_temp_get(THRIFT_REAGENTCASE)/10.0;
        if (++temperate_cnt >= TEMEPRATE_DATA_LINE_SIZE) {
            save_temperate_data(temperate_data, temperate_cnt);
            temperate_cnt = 0;
        }

        /* 关闭主机后，不再进行风扇的控制、监控等 */
        if (thrift_slave_client_connect_ctl_get() == 0) {
            if (regent_fan_mode == 0) {
                LOG("regent fan force to normal power fan duty, env:%f\n", thrift_temp_get(THRIFT_ENVIRONMENTAREA)/10.0);
                slip_temperate_ctl_fan_duty_set(FAN_PWM_2, 0, 0);
                slip_temperate_ctl_fan_duty_set(FAN_PWM_3, 0, 0);
            }
            regent_fan_mode = 1;
            sleep(GET_TEMPERATE_INTERVAL);
            continue;
        } else {
            regent_fan_mode = 0;
        }

        /* 非运行状态下，
            若试剂仓温度达到指定目标的（10.0度+1.0度）180s之后，则试剂仓风扇、检测室风扇降低占空比，以减少噪音。
            若在减少噪音期间，检查到试剂仓温度升温到13.0度以上，则强制恢复自动散热模式，以保障试剂仓里的试剂 */
        if (thrift_temp_get(THRIFT_REAGENTCASE) < 110) {
            if (temp_ready_flag==0 && temp_ready_count++>180) {
                temp_ready_flag = 1;
            }
        } else if (thrift_temp_get(THRIFT_REAGENTCASE) > 130) {
            temp_ready_count = 0;
        }

        if (temp_ready_flag == 1) {
            if (temp_ready_count == 0) {
                LOG("force to normal power fan duty, env:%f\n", thrift_temp_get(THRIFT_ENVIRONMENTAREA)/10.0);
                temp_ready_flag = 0;
                slip_temperate_ctl_fan_duty_set(FAN_PWM_0, 0, 0);
                slip_temperate_ctl_fan_duty_set(FAN_PWM_1, 0, 0);
                slip_temperate_ctl_fan_duty_set(FAN_PWM_2, 0, 0);
                slip_temperate_ctl_fan_duty_set(FAN_PWM_3, 0, 0);

                /* 重新触发试剂仓制冷温控 */
                usleep(1000);
                slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER1, TEMP_CTL_OFF, 0);
                slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER2, TEMP_CTL_OFF, 0);
                slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER1, TEMP_CTL_NORMAL_ON, 0);
                slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER2, TEMP_CTL_NORMAL_ON, 0);
            }else if (machine_status_back!=get_machine_stat() || fan_duty_fisrt_flag==0) {
                fan_duty_fisrt_flag = 1;
                if (get_machine_stat() != MACHINE_STAT_RUNNING) {
                    LOG("change to low power fan duty, env:%f\n", thrift_temp_get(THRIFT_ENVIRONMENTAREA)/10.0);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_0, 1, h3600_conf_get()->fan_pwm_duty[FAN_PWM_0]);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_1, 1, h3600_conf_get()->fan_pwm_duty[FAN_PWM_1]);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_2, 1, h3600_conf_get()->fan_pwm_duty[FAN_PWM_2]);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_3, 1, h3600_conf_get()->fan_pwm_duty[FAN_PWM_3]);
                } else {
                    LOG("change to normal power fan duty, env:%f\n", thrift_temp_get(THRIFT_ENVIRONMENTAREA)/10.0);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_0, 0, 0);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_1, 0, 0);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_2, 0, 0);
                    slip_temperate_ctl_fan_duty_set(FAN_PWM_3, 0, 0);
                }
            }
        }

        /* 散热风扇异常处理 */
        if (temperate_ctl.fan_status != fan_status_back || temperate_ctl.temperate_status!= temperate_status_back || 
            temperate_ctl.sensor_status != sensor_status_back) {
            LOG("dectet change: fan_status:0x%02X, temp_status:0x%02X, sensor_status:0x%02X\n",
                temperate_ctl.fan_status, temperate_ctl.temperate_status, temperate_ctl.sensor_status);
        }

        if (thrift_salve_heartbeat_flag_get() == 1) {
            for (i=0; i<FUN_NUMBER; i++) {
                 /*状态 由正常到异常，则上报异常告警 */
                 if ((fan_status_back & fan_error_talbe[i])==0 && (temperate_ctl.fan_status & fan_error_talbe[i])) {
                     LOG("FAN%d error\n", i);
                     FAULT_CHECK_DEAL(FAULT_TEMPRARTE, MODULE_FAULT_NONE, (void *)(fan_fault_talbe[i]));
                 }
            }

            fan_status_back = temperate_ctl.fan_status;
        }

        temperate_status_back = temperate_ctl.temperate_status;
        sensor_status_back = temperate_ctl.sensor_status;
        machine_status_back = get_machine_stat();

        sleep(GET_TEMPERATE_INTERVAL);
    }

    return NULL;
}

/* 初始化温控模块 */
int temperate_ctl_init(void)
{
    pthread_t temperate_ctl_thread = {0};

    temperate_data_file_init();

    if (0 != pthread_create(&temperate_ctl_thread, NULL, temperate_ctl_task, NULL)) {
        LOG("temperate_ctl thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

