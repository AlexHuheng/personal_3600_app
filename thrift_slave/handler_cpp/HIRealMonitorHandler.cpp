#include "HIRealMonitorHandler.h"
#include "../thrift_handler.h"

#include <stdio.h>

#include "slip_cmd_table.h"
#include "work_queue.h"
#include "log.h"
#include "module_temperate_ctl.h"
#include "module_magnetic_bead.h"
#include "h3600_maintain_utils.h"
#include "module_liquied_circuit.h"
#include "module_reagent_table.h"
#include "module_catcher_rs485.h"
#include "module_catcher_motor.h"
#include "module_sampler_ctl.h"
#include "module_optical.h"
#include "module_cuvette_supply.h"
#include "module_auto_cal_needle.h"

HIRealMonitorHandler::HIRealMonitorHandler() {
    // Your initialization goes here
}

typedef struct
{
    int16_t index;
    int32_t status;
    int32_t iUserData;
}__attribute__((packed)) io_t;

typedef struct
{
    int16_t index;
    int32_t temp;
    int32_t iUserData;
}__attribute__((packed)) temp_t;

typedef struct pressure {
    int16_t index;
    float   value;
    int32_t iUserData;
} pressure_t;

typedef enum{
    PWM_DISABLE,
    PWM_ENABLE,
}pwm_status_t;

typedef struct
{
    int32_t no;
    int32_t color;
    int32_t blink;
    int32_t iUserData;
}ind_led_t;

typedef struct
{
    int32_t open;
    int32_t sound;
    int32_t iUserData;
}alarm_sound_t;

typedef struct
{
    int32_t sensor;
    int32_t iUserData;
}rack_lock_t;

static void get_io(void *arg)
{
    io_t *io = (io_t *)arg;
    int exeState = 1;
    async_return_t async_return;

    if (io->index >= MOTOR_MIX_CIRCLE_START && io->index <= MOTOR_MIX_CIRCLE_END) {
        /* 混匀电机的光电计数，复用get   _io指令 */
        uint8_t motor_mix[] = {MOTOR_MIX_1 , MOTOR_MIX_2, MOTOR_MIX_3};
        io->status = motor_mix_circle_get_timedwait(motor_mix[io->index - MOTOR_MIX_CIRCLE_START], 10000);
    } else if (io->index >= TEMPERATURE_CTL_ENABLE_START && io->index <= TEMPERATURE_CTL_ENABLE_END) {
        /* TODO */
    } else if (io->index >= TEMPERATURE_CTL_GOAL_START && io->index <= TEMPERATURE_CTL_GOAL_END) {
        /* TODO */
    } else if (io->index >= MAG_DRIVER_LEVEL_START && io->index <= MAG_DRIVER_LEVEL_END) {
        io->status = slip_magnetic_pwm_driver_level_get(io->index - MAG_DRIVER_LEVEL_START);
    } else if (io->index >= BLDC_IDX_START && io->index <= BLDC_IDX_STOP) {
        io->status = -1;
        if (io->index == BLDC_IDX_STOP) {
            int data = 0;
            data = slip_bldc_rads_get(MIX_BLDC_INDEX);
            if (data > 0) {
                data = 10000 / (data * 3);
                io->status = data;
            }
        }
        /* TODO */
    } else if (io->index >= TEMPERATURE_CTL_GPIO_START && io->index <= TEMPERATURE_CTL_GPIO_END) {
        /* TODO */
    } else if (io->index >= MAG_DATA_START && io->index <= MAG_DATA_END) {
        io->status = thrift_mag_data_get((magnetic_pos_t)(io->index - MAG_DATA_START));
    } else if (io->index >= OPTICAL_DATA_START && io->index <= OPTICAL_DATA_END) {
        io->status = thrift_optical_data_get(OPTICAL_WAVE_405, (optical_pos_t)(io->index - OPTICAL_DATA_START));
    } else if (io->index >= TEMP_SENSOR_CALI_START && io->index <= TEMP_SENSOR_CALI_END) {
        io->status = slip_temperate_ctl_sensor_cali_get(io->index - TEMP_SENSOR_CALI_START);
    } else if (io->index >= TEMP_GOAL_CALI_START && io->index <= TEMP_GOAL_CALI_END) {
        io->status = slip_temperate_ctl_goal_cali_get(io->index - TEMP_GOAL_CALI_START);
    } else if (io->index >= TEMP_SENSOR_TYPE_START && io->index <= TEMP_SENSOR_TYPE_END) {
        /* TODO */
    } else if (io->index >= TEMP_GOAL_MODE_START && io->index <= TEMP_GOAL_MODE_END) {
        /* TODO */
    } else if (io->index >= MAG_PERIOD_LEVEL_START && io->index <= MAG_PERIOD_LEVEL_END) {
        io->status = slip_magnetic_pwm_period_get();
    } else if (io->index == CATCHER_SIM_IO) {
        io->status = check_catcher_status();
    } else if (io->index == CATCHER_ENCODER_SIM_IO) {
        io->status = catcher_motor_get_encoder();
    } else if (io->index >= TEMP_ELE_CTL_START && io->index < TEMP_ELE_CTL_CHANNEL_END) {
        io->status = (2 == gpio_get(io->index)) ? 1 : 0;
    } else if (io->index >= TEMP_LED_STAT_START && io->index < TEMP_LED_STAT_CHANNEL_END) {
        io->status = (2 == gpio_get(io->index)) ? 1 : 0;
    } else if (io->index >= OPTICAL_CURR_CALC_START && io->index <= OPTICAL_CURR_CALC_END) {
        /* TODO */
    } else if (io->index >= OPTICAL_CURR_CALC_WAVE_START && io->index <= OPTICAL_CURR_CALC_WAVE_END) {
        io->status = thrift_optical_led_calc_get(io->index - OPTICAL_CURR_CALC_WAVE_START);
    } else if (io->index >= DEVICE_STATUS_COUNT_START && io->index <= DEVICE_STATUS_COUNT_END) {
        /* TODO */
    } else if (io->index == TEMP_CLOT_PRESSUARE_DIFF_VALUE) {
        if (slip_liq_cir_noise_set()==0) {
            io->status = (int32_t)g_presure_noise_get();
        }
    } else {
        io->status = gpio_get(io->index);

        /* 对于 仪器仓盖光电、外部反应杯盘仓位限位光电、
            内部反应杯盘仓位限位光电、试剂仓仓盖光电、稀释液仓盖光电，
            与上位机约定：1代表开盖、0代表闭合 */
        switch (io->index) {
        case PE_REGENT_TABLE_GATE:
            if (ins_io_get().reag_io) {
                io->status = (io->status == 1 ? 1 : 0);
            } else {
                io->status = 0;
            }
            break;
        case OVERFLOW_BOT_IDX:
            /* 溢流瓶 */
            io->status = (io->status == 1 ? 1 : 0);
            break;
        case BUBBLESENSOR_NORM_IDX:
             /* 普通清洗液气泡传感器 */
            io->status = (io->status == 1 ? 1 : 0);
            break;
        case BUBBLESENSOR_SPCL_IDX:
             /* 特殊清洗液气泡传感器 */
            io->status = (io->status == 1 ? 1 : 0);
            break;

        case WASTESENSOR_IDX:
            /* 废液桶浮子 */
            io->status = (io->status == 1 ? 0 : 1);
            break;
        case WASHSENSOR_IDX:
            /* 普通清洗液浮子 */
            io->status = (io->status == 1 ? 0 : 1);
            break;
        case SPCL_CLEAR_IDX:
            /* 特殊清洗液微动开关 */
            io->status = (io->status == 1 ? 1 : 0);
            break;
            /* 反应杯检测光电 */
        case PE_REACT_CUP_CHECK:
            io->status = (io->status == 1 ? 1 : 0);
            break;
            /* 导向轮光电 */
        case MICRO_SWITCH_BUCKLE:
            io->status = (io->status == 1 ? 1 : 0);
            break;
            /* 杯盘到位光电 */
        case MICRO_GATE_CUVETTE:
            io->status = (io->status == 1 ? 1 : 0);
            break;
            /* 稀释瓶1到位光电 */
        case PE_DILU_1:
            io->status = (io->status == 1 ? 0 : 1);
            break;
            /* 稀释瓶2到位光电 */
        case PE_DILU_2:
            io->status = (io->status == 1 ? 0 : 1);
            break;
            /* 仪器仓盖 */
        case PE_UP_CAP:
            if (ins_io_get().gate_io) {
                io->status = (io->status == 1 ? 1 : 0);
            } else {
                io->status = 1;
            }
            break;
            /* 垃圾桶满 */
        case PE_WASTE_FULL:
            if (ins_io_get().waste_io) {
                io->status = (io->status == 1 ? 1 : 0);
            } else {
                io->status = 0;
            }
            break;
            /* 垃圾桶有无 */
        case PE_WASTE_CHECK:
            io->status = (io->status == 1 ? 1 : 0);
            break;

        default:
            break;
        }
    }

    if (io->status != -1) {
        exeState = 0;
    }
    //LOG("get io: %d, status: %d, exeState: %d\n", io->index, io->status, exeState);

    async_return.return_type = RETURN_INT;
    async_return.return_int = io->status;
    report_asnyc_invoke_result(io->iUserData, exeState == 0 ? 0 : 1, &async_return);
    free(io);
}

static void set_io(void *arg)
{
    io_t *io = (io_t *)arg;
    int exeState = 1;
    async_return_t async_return;

    if (io->index >= MOTOR_MIX_CIRCLE_START && io->index <= MOTOR_MIX_CIRCLE_END) {
        /* TODO */
    } else if (io->index >= TEMPERATURE_CTL_ENABLE_START && io->index <= TEMPERATURE_CTL_ENABLE_END) {
        if (io->index == TEMPERATURE_CTL_ENABLE_REAGENTCOOL) {
            slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER1, (uint8_t)io->status, 0);
            if (-1 == slip_temperate_ctl_pwm_set(TEMP_REAGENT_CONTAINER2, (uint8_t)io->status, 0)) {
                exeState = -1;
            } else {
                exeState = 0;
            }
        } else {
            if (-1 == slip_temperate_ctl_pwm_set((temperate_target_t)(io->index - TEMPERATURE_CTL_ENABLE_START), (uint8_t)io->status, 0)) {
                exeState = -1;
            } else {
                exeState = 0;
            }
        }
    } else if (io->index >= TEMPERATURE_CTL_GOAL_START && io->index <= TEMPERATURE_CTL_GOAL_END) {
        if (io->index == TEMPERATURE_CTL_ENABLE_REAGENTCOOL) {
            slip_temperate_ctl_goal_set(TEMP_REAGENT_CONTAINER1, io->status);
            if (-1 == slip_temperate_ctl_goal_set(TEMP_REAGENT_CONTAINER2, io->status)) {
                exeState = -1;
            } else {
                exeState = 0;
            }
        } else {
            if (-1 == slip_temperate_ctl_goal_set((temperate_target_t)(io->index - TEMPERATURE_CTL_GOAL_START), io->status)) {
                exeState = -1;
            } else {
                exeState = 0;
            }
        }
    } else if (io->index >= MAG_DRIVER_LEVEL_START && io->index <= MAG_DRIVER_LEVEL_END) {
        if (-1 == slip_magnetic_pwm_driver_level_set(io->index - MAG_DRIVER_LEVEL_START, io->status)) {
            exeState = -1;
        } else {
            exeState = 0;
        }
    } else if (io->index >= BLDC_IDX_START && io->index <= BLDC_TEST_MAX) {
        if (io->index == BLDC_IDX_MOVE) {
            slip_bldc_ctl_set(BLDC_MAX, REAGENT_MIX_SPEED_NORMAL);
            if (-1 == slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_REVERSE)) {
                exeState = -1;
            } else {
                exeState = 0;
            }
        } else if (io->index == BLDC_IDX_STOP) {
            if (-1 == slip_bldc_ctl_set(MIX_BLDC_INDEX, BLDC_STOP)) {
                exeState = -1;
            } else {
                exeState = 0;
            }
        } else if (io->index == BLDC_TEST || io->index == BLDC_FORWARD_TEST ||
            io->index == BLDC_REVERSE_TEST || io->index == BLDC_STOP_TEST) {
            exeState = thrift_cuvette_supply_func(io->index);
        }
    } else if (io->index >= MAG_DATA_START && io->index <= MAG_DATA_END) {
        /* TODO */
    } else if (io->index >= OPTICAL_DATA_START && io->index <= OPTICAL_DATA_END) {
        /* TODO */
    } else if (io->index >= TEMP_SENSOR_CALI_START && io->index <= TEMP_SENSOR_CALI_END) {
       if (-1 == slip_temperate_ctl_sensor_cali_set(io->index - TEMP_SENSOR_CALI_START, io->status)) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index >= TEMP_GOAL_CALI_START && io->index <= TEMP_GOAL_CALI_END) {
       if (-1 == slip_temperate_ctl_goal_cali_set(io->index - TEMP_GOAL_CALI_START, io->status)) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index >= TEMP_SENSOR_TYPE_START && io->index <= TEMP_SENSOR_TYPE_END) {
       if (-1 == slip_temperate_ctl_sensor_type_set(io->status)) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index >= TEMP_GOAL_MODE_START && io->index <= TEMP_GOAL_MODE_END) {
       if (-1 == slip_temperate_ctl_goal_mode_set(io->status)) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index >= MAG_PERIOD_LEVEL_START && io->index <= MAG_PERIOD_LEVEL_END) {
       if (-1 == slip_magnetic_pwm_period_set(io->status)) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index == (CATCHER_SIM_IO - 1)) {
        catcher_rs485_init();
        exeState = 0;
    } else if (io->index == CATCHER_SIM_IO) {
        if (io->status == 1) {
            catcher_ctl(CATCHER_OPEN);
        } else {
            catcher_ctl(CATCHER_CLOSE);
        }
        exeState = 0;
    } else if (io->index == (CATCHER_ENCODER_SIM_IO - 1)) {
        catcher_motor_init();
        exeState = 0;
    } else if (io->index == (CATCHER_ENCODER_SIM_IO)) {
        if (io->status == 1) {
            catcher_motor_ctl(CATCHER_MOTOR_OPEN);
        } else {
            catcher_motor_ctl(CATCHER_MOTOR_CLOSE);
        }
        exeState = 0;
    } else if (io->index >= TEMP_ELE_CTL_START && io->index < TEMP_ELE_CTL_CHANNEL_END) {
        if (-1 == slip_ele_lock_to_sampler(io->index - TEMP_ELE_CTL_START, io->status)) {
            exeState = -1;
        } else {
            exeState = 0;
            report_io_state(output_io_t(io->index + 7), io->status);
        }
    } else if (io->index >= TEMP_LED_STAT_START && io->index < TEMP_LED_STAT_CHANNEL_END) {
        if (-1 == slip_rack_tank_led_to_sampler(io->index - TEMP_LED_STAT_START, io->status)) {
            exeState = -1;
        } else {
            exeState = 0;
        }
    } else if (io->index >= OPTICAL_CURR_CALC_START && io->index <= OPTICAL_CURR_CALC_END) {
       if (-1 == thrift_optical_led_calc_start()) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index >= OPTICAL_CURR_CALC_WAVE_START && io->index <= OPTICAL_CURR_CALC_WAVE_END) {
       if (-1 == thrift_optical_led_calc_set(io->index - OPTICAL_CURR_CALC_WAVE_START, io->status)) {
           exeState = -1;
       } else {
           exeState = 0;
       }
    } else if (io->index >= DEVICE_STATUS_COUNT_START && io->index <= DEVICE_STATUS_COUNT_END) {
        /* TODO */
    } else if (io->index >= LED_CUVETTE_IN_R && io->index <= LED_CTL_STATUS_R) {
        if (io->status == 0) {
            exeState = gpio_set(io->index, 1);
        } else {
            exeState = gpio_set(io->index, 0);
        }
    } else if (io->index >= AUTO_CAL_IDX && io->index <= AUTO_CAL_ALL) {
        /* 自动标定 */
        if (io->index == AUTO_CAL_IDX) {
            exeState = thrift_auto_cal_single_func((auto_cal_idx_t)io->status);
        } else if (io->index == AUTO_CAL_ALL) {
            exeState = thrift_auto_cal_func();
        }
    } else {
        exeState = gpio_set(io->index, io->status);
    }

    LOG("set io: %d, status: %d, exeState: %d\n", io->index, io->status, exeState);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(io->iUserData, exeState == 0 ? 0 : 1, &async_return);
    free(io);
}

static void set_ind_led(void *arg)
{
    async_return_t async_return;
    int exeState = 0;
    ind_led_t *ind_led = (ind_led_t *)arg;

    indicator_led_set(ind_led->no, ind_led->color, ind_led->blink);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(ind_led->iUserData, exeState == 0 ? 0 : 1, &async_return);
    free(ind_led);
}

static void set_alarm_sound(void *arg)
{
    async_return_t async_return;
    int exeState = 0;
    alarm_sound_t *alarm_sound = (alarm_sound_t *)arg;

    set_alarm_mode(alarm_sound->open, alarm_sound->sound);

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(alarm_sound->iUserData, exeState == 0 ? 0 : 1, &async_return);
    free(alarm_sound);
}

static void get_temp(void *arg)
{
    temp_t *data = (temp_t *)arg;
    int exeState = 1;
    async_return_t async_return;

    data->temp = thrift_temp_get(data->index);
    if (data->temp != -1) {
        exeState = 0;
    }

    async_return.return_type = RETURN_INT;
    async_return.return_int = data->temp;
    report_asnyc_invoke_result(data->iUserData, exeState == 0 ? 0 : 1, &async_return);
    free(data);
}

::EXE_STATE::type HIRealMonitorHandler::GetIOAsync(const  ::OUTPUT_IO::type sensor, const int32_t iUserData) {
    io_t *io = (io_t *)calloc(1, sizeof(io_t));

    io->index = sensor;
    io->iUserData = iUserData;
    work_queue_add(get_io, io);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::GetTemperatureAsync(const  ::TEMPERATURE_SENSOR::type sensor, const int32_t iUserData) {
    temp_t *data = (temp_t *)calloc(1, sizeof(temp_t));
    data->index = sensor;
    data->iUserData = iUserData;

    work_queue_add(get_temp, data);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::GetPressureAsync(const int32_t sensor, const int32_t iUserData) {
    pressure_t *data = (pressure_t *)calloc(1, sizeof(pressure_t));
    data->index = sensor;
    data->iUserData = iUserData;
    
//   work_queue_add(get_pressure, data);
    
    return EXE_STATE::SUCCESS;
}


::EXE_STATE::type HIRealMonitorHandler::SetIOAsync(const  ::INPUT_IO::type sensor, const int32_t iState, const int32_t iUserData) {
    io_t *io = (io_t *)calloc(1, sizeof(io_t));

    io->index = sensor;
    io->status = iState;
    io->iUserData = iUserData;
    work_queue_add(set_io, io);
    
    return EXE_STATE::SUCCESS;
}

static void set_sampler_rack_unlock(void *arg)
{
    async_return_t async_return;
    int exeState = 0;
    rack_lock_t *rack_sensor = (rack_lock_t *)arg;

    if (0 == del_cup_by_slot_id(rack_sensor->sensor - TEMP_ELE_CTL_START + 1)) {
        exeState = slip_ele_lock_to_sampler(rack_sensor->sensor - TEMP_ELE_CTL_START, 0);
        if (!exeState) { /* 解锁成功，上报 */
            report_io_state((output_io_t)(rack_sensor->sensor + 7), 0);
        }
        LOG("rack manual unlock result:%s\n", (!exeState) ? "success" : "fail");
    } else {
        exeState = -1;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(rack_sensor->iUserData, exeState == 0 ? 0 : 1, &async_return);
    free(rack_sensor);
}

::EXE_STATE::type HIRealMonitorHandler::ManualUnlockSlotAsync(const  ::INPUT_IO::type sensor, const int32_t iUserData) {
    rack_lock_t *ind_rack = (rack_lock_t *)calloc(1, sizeof(rack_lock_t));;

    LOG("ManualUnlockSlotAsync\n");

    ind_rack->sensor = sensor;
    ind_rack->iUserData = iUserData;
    work_queue_add(set_sampler_rack_unlock, (void *)ind_rack);
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::GetLightSignalAsync(const int32_t iWave, const int32_t iUserData) {
    // Your implementation goes here
    LOG("GetLightSignalAsync\n");
    async_return_t async_return;
    async_return.return_type = RETURN_INT;
    async_return.return_int = 0;
    
    report_asnyc_invoke_result(iUserData, 0, &async_return);
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::SetIndicatorLightAsync(const int32_t iIndicatorLightNo, const int32_t iColor, const int32_t iBlink, const int32_t iUserData) {
    // Your implementation goes here
    LOG("SetIndicatorLightAsync\n");
    ind_led_t *ind_led = (ind_led_t *)calloc(1, sizeof(ind_led_t));

    ind_led->no = iIndicatorLightNo;
    ind_led->color = iColor;
    ind_led->blink = iBlink;
    ind_led->iUserData = iUserData;

    work_queue_add(set_ind_led, ind_led);
    
    return EXE_STATE::SUCCESS;
}

// 报警声音控制 bOpen: 1:打开; 0:关闭  iSound:报警声音频率 1低频、2中频、3高频,此参数令bOpen为1时有效
::EXE_STATE::type HIRealMonitorHandler::SetAlarmSoundAsync(const  ::IBOOL bOpen, const int32_t iSound, const int32_t iUserData) {
    // Your implementation goes here
    LOG("SetAlarmSoundAsync, bOpen = %d, iSound = %d\n", bOpen, iSound);
    
    alarm_sound_t *alarm_sound = (alarm_sound_t *)calloc(1, sizeof(alarm_sound_t));

    alarm_sound->open = bOpen;
    alarm_sound->sound = iSound;
    alarm_sound->iUserData = iUserData;

    work_queue_add(set_alarm_sound, alarm_sound);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::ManualStopAsync(const int32_t iUserData) {
    // Your implementation goes here
    LOG("ManualStopAsync, state:%d\n", get_machine_stat());

    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    *userData = iUserData;

    work_queue_add(manual_stop_async_handler, userData);
    
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::SetCounterOrTimer(const int32_t iCounterID, const int32_t iNumberOfTimesOrElapsedTimes) {

    LOG("index:%d iNumberOfTimesOrElapsedTimes:%d\n", iCounterID, iNumberOfTimesOrElapsedTimes);

    if (iCounterID == DS_ALL) {
        device_status_reset();
    } else {
        device_status_reset_flag_set(1);
        device_status_cur_count_set(iCounterID, iNumberOfTimesOrElapsedTimes);
        device_status_count_update_file();
        device_status_reset_flag_set(0);
    }

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIRealMonitorHandler::SetTotalCounterOrTimer(const int32_t iCounterOrTimerID, const int32_t iTotalNumberOfTimesOrElapsedTime) {

    LOG("iCounterOrTimerID:%d iTotalNumberOfTimesOrElapsedTime:%d\n", iCounterOrTimerID, iTotalNumberOfTimesOrElapsedTime);
    if (iCounterOrTimerID == DS_ALL) {
        device_status_reset();
    } else {
        device_status_reset_flag_set(1);
        device_status_total_count_set(iCounterOrTimerID, iTotalNumberOfTimesOrElapsedTime);
        device_status_count_update_file();
        device_status_reset_flag_set(0);
    }
    return EXE_STATE::SUCCESS;
}

void HIRealMonitorHandler::GetAllCounterOrTimer(std::vector< ::SLAVE_COUNTEROR_TIMER_INFO_T> & _return) {

    int i = 0;
    for (i=1; i<DS_COUNT_MAX; i++) {
        if (i > DS_HEAT_REGENT_GLASS_RUN_TIME && i < DS_MAG1_USED_COUNT) {
            
        } else {
            ::SLAVE_COUNTEROR_TIMER_INFO_T tInfo;
            tInfo.iCounterOrTimerID = i;
            tInfo.iNumberOfTimesOrElapsedTime = device_status_cur_count_get(i);
            tInfo.iTotalNumberOfTimesOrElapsedTime = device_status_total_count_get(i);
            /* LOG("[%d]:{%d:%d}\n", tInfo.iCounterOrTimerID, tInfo.iNumberOfTimesOrElapsedTime, tInfo.iTotalNumberOfTimesOrElapsedTime); */
            _return.push_back(tInfo);
        }
    }

    return;
}

::EXE_STATE::type HIRealMonitorHandler::SetConsumablesInfo(const::CONSUMABLES_INFO_T& tConsumablesInfo) {
    consum_info_t           info;

    info.type       = tConsumablesInfo.iConsumableType;
    info.enable     = tConsumablesInfo.iEnable;
    info.index      = tConsumablesInfo.iIndex;
    info.priority   = tConsumablesInfo.iPriorityOfUse;
    info.serno      = tConsumablesInfo.iSerialNo;
    info.binit      = tConsumablesInfo.bInit;
    strncpy(info.strlotno, tConsumablesInfo.strLotNo.c_str(), strlen(tConsumablesInfo.strLotNo.c_str()) + 1);

    LOG("SetConsumablesInfo, iConsumableType:%d, iEnable = %d, iIndex = %d, iPriorityOfUse = %d, iSerialNo = %d, strLotNo = %s.\n",
       info.type, info.enable, info.index, info.priority, info.serno, info.strlotno);

    switch (info.type) {
        case CUP:
            cuvette_supply_para_set(&info);
            break;
        case WASTE_CUP:

            break;
        case WASH_A:
        case WASH_B:
        case WASTE_WATER:
            clean_liquid_para_set(&info);
            break;
        default:
            break;
    }

    return EXE_STATE::SUCCESS;
}

// GetInstrumentState返回值定义
// HS_WARM_UP                          = 0,        // 预热（下位机暂无此状态）
// HS_STAND_BY                         = 1,        // 待机
// HS_RUNNING                          = 2,        // 运行
// HS_SAMPLE_STOP                      = 3,        // 加样停（下位机暂无此状态）
// HS_STOP                             = 4,        // 停机
// HS_MAINTENANCE                      = 5,        // 维护
// HS_OFFLINE                          = 6,        // 脱机（下位机暂无此状态）
// HS_DISABLE                          = 7,        // 禁用（下位机暂无此状态）
// HS_MANUAL_SAMPLE_STOP               = 8         // 手动加样停   
int32_t HIRealMonitorHandler::GetCurrentInstrumentState()
{
    machine_stat_t machine_stat = get_machine_stat();
    int ret = 4;

    if (machine_stat == MACHINE_STAT_STANDBY) {
        ret = 1;
    } else if (machine_stat == MACHINE_STAT_RUNNING) {
        ret = 2;
    } else if (machine_stat == MACHINE_STAT_STOP) {
        ret = 4;
    }

    return ret;
}

