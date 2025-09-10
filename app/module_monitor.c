#include <pthread.h>
#include <unistd.h>

#include "module_monitor.h"
#include "module_cup_monitor.h"
#include "module_common.h"
#include "module_incubation.h"
#include "log.h"
#include "slip_cmd_table.h"
#include "thrift_handler.h"
#include "device_status_count.h"

static module_start_cmd_t module_start_ctl_stat = MODULE_CMD_STOP;
static module_fault_stat_t main_fault_stat = MODULE_FAULT_NONE;
static struct timeval module_base_time;
static machine_stat_t machine_stat = MACHINE_STAT_STANDBY;
static sampler_stat_t sampler_add_stat = SAMPLER_ADD_START;
static int detect_period_flag = 0; /* 本次检测周期状态标志  0:本次检测周期结束(默认值)  1：本次检测周期开始 */
static int auto_cal_stop_flag = 0;

static pthread_mutex_t module_monitor_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t module_monitor_cond = PTHREAD_COND_INITIALIZER;

static ind_led_blink_stat_t ind_led_blink_stat;
static alarm_sound_stat_t alarm_sound_stat;

static ins_enable_io_t ins_enable_io = {0};

void ins_io_set(int gate_io, int reag_io, int waste_io, int reag_monitor, int reag_time)
{
    ins_enable_io.gate_io = gate_io;
    ins_enable_io.reag_io = reag_io;
    ins_enable_io.waste_io = waste_io;
    ins_enable_io.reag_monitor = reag_monitor;
    ins_enable_io.reag_time = reag_time;
}

ins_enable_io_t ins_io_get()
{
    return ins_enable_io;
}

void auto_cal_stop_flag_set(int v)
{
    LOG("auto_cal: needle stop flag = %d!\n", v);
    auto_cal_stop_flag = v;
}

int auto_cal_stop_flag_get(void)
{
    return auto_cal_stop_flag;
}

void set_machine_stat(machine_stat_t stat)
{
    machine_stat = stat;
}

machine_stat_t get_machine_stat(void)
{
    return machine_stat;
}

/* 模块同步基准时间 */
struct timeval *get_module_base_time(void)
{
    return &module_base_time;
}

void clear_module_base_time()
{
    module_base_time.tv_sec =0;
    module_base_time.tv_usec = 0;
}

int fault_code_generate(char *return_fault_code, char *main_fault_code, char *sub_fault_code)
{
    return snprintf(return_fault_code, FAULT_CODE_LEN, "%s-%s", main_fault_code, sub_fault_code);
}

/* 
若返回值大于等于0，则故障已消除 
若返回值小于0，则故障仍然存在 
*/
static int fault_common_handler(void *data)
{
    int flag = (int)data;
    
    LOG("handler:%d\n", flag);
    return -1;
}

static int fault_needle_s_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;
    
    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_NEEDLE_S, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_needle_r2_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;
    
    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_NEEDLE_R2, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_catcher_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;
    
    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_C, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_mix_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;
    
    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_MIX, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_incubation_module_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    fault_incubation_t* fault_data = (fault_incubation_t*)data;
    char *sub_fault_idx_str = fault_data->fault_str;
    int order_no = fault_data->order_no;
    
    LOG("order_no:%d, fault_str:%d, data:%p\n", order_no, sub_fault_idx_str); 

    if (sub_fault_idx_str != NULL) {
        report_order_state(order_no, OD_ERROR);
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_INCUBATION, sub_fault_idx_str);
        report_alarm_message(order_no, alarm_message);
    }
    return -1;
}

static int fault_magnetic_module_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;
    
    LOG("fault_data:%s\n", data);


    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_MAG, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_optical_module_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;
    
    LOG("fault_data:%s\n", data);
    
    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_OPTICAL, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_reaction_cup_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);
    
    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_CUVETTE_SUPPLY, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_liq_circuit_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_PUMP, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_sampler_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);


    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_SAMPLER, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_reagent_table_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_REAGENT_TABLE, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_complete_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_MACHINE, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}

static int fault_connect_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_MACHINE, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    /* 待机时，连接类异常不置下位机故障，仅上报 */
    return (MACHINE_STAT_STANDBY == get_machine_stat()) ? 0 : -1;
}

static int fault_temperate_handler(void *data)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    char *sub_fault_idx_str = (char *)data;

    LOG("fault_data:%s\n", data);

    if (sub_fault_idx_str != NULL) {
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_TEMPERATE, sub_fault_idx_str);
        report_alarm_message(0, alarm_message);
    }
    return -1;
}


const static fault_node_t fault_deal_table[] =
{
    {FAULT_COMMON, fault_common_handler},
    
    {FAULT_CATCHER, fault_catcher_handler},
    
    {FAULT_NEEDLE_S, fault_needle_s_handler},
    {FAULT_NEEDLE_R2, fault_needle_r2_handler},

    {FAULT_MIX, fault_mix_handler},
    {FAULT_INCUBATION_MODULE, fault_incubation_module_handler},
    {FAULT_MAGNETIC_MODULE, fault_magnetic_module_handler},

    {FAULT_OPTICAL_MODULE, fault_optical_module_handler},

    {FAULT_CUVETTE_MODULE, fault_reaction_cup_handler},

    {FAULT_LIQ_CIRCUIT_MODULE, fault_liq_circuit_handler},

    {FAULT_SAMPLER, fault_sampler_handler},
    {FAULT_REAGENT_TABLE, fault_reagent_table_handler},
    
    {FAULT_COMPLETE_MACHINE, fault_complete_handler},
    
    {FAULT_CONNECT, fault_connect_handler},
    {FAULT_TEMPRARTE, fault_temperate_handler},
};

int module_fault_stat_clear(void)
{
    main_fault_stat = MODULE_FAULT_NONE;
    return 0;
}

/* 故障状态设置函数 */
int module_fault_stat_set(module_fault_stat_t stat)
{
    main_fault_stat = stat;
    
    return 0;
}

void module_start_control(module_start_cmd_t module_ctl_cmd)
{
    module_start_ctl_stat = module_ctl_cmd;
}

int module_start_stat_get(void)
{
    return module_start_ctl_stat;
}

/* 
若返回值大于等于0，则故障已消除 
若返回值小于0，则故障仍然存在 
*/
int fault_deal_add(fault_type_t fault_type, int priority, void *data)
{
    int i = 0;
    int ret = -1;

    for (i=0; i<sizeof(fault_deal_table)/sizeof(fault_deal_table[0]); i++) {
        if (fault_deal_table[i].type == fault_type) {
            if (fault_deal_table[i].hander) {
                ret = fault_deal_table[i].hander(data);
            }
        }
    }

    return ret;
}

module_fault_stat_t module_fault_stat_get(void)
{
    return main_fault_stat;
}

void set_detect_period_flag(int flag)
{
    detect_period_flag = flag;
}

/* 等待当前检测周期完成 */
int wait_detect_period_finish(void)
{
    int count = 0;

    while (1) {
        /* 20s超时 */
        if (count++ > 20 ) {
            LOG("wait timeout\n");
            return -1;
        }

        if (detect_period_flag == 0) {
            LOG("wait ok\n");
            return 0;
        }
        sleep(1);
    }

    return 0;
}

int module_monitor_wait(void)
{
    pthread_mutex_lock(&module_monitor_mutex);
    pthread_cond_wait(&module_monitor_cond, &module_monitor_mutex);
    pthread_mutex_unlock(&module_monitor_mutex);
    LOG("[ wait ] successed.\n");

    return 0;
}

void module_monitor_start(void *arg)
{
    static int cnt = 0;
    usleep(50000);  /* 等待所有生产线程开始wait */
    if (module_start_ctl_stat == MODULE_CMD_START) {
        leave_singal_init(1);
        set_detect_period_flag(1);
        pthread_mutex_lock(&module_monitor_mutex);
        gettimeofday(&module_base_time, NULL);
        pthread_cond_broadcast(&module_monitor_cond);
        pthread_mutex_unlock(&module_monitor_mutex);
        LOG("[ broadcast ] : %d !!\n", ++cnt);
    }
}

void module_sampler_add_set(sampler_stat_t stat)
{
    sampler_add_stat = stat;
}

sampler_stat_t module_sampler_add_get(void)
{
    return sampler_add_stat;
}

void machine_led_set(int led_id, int color, int blink_ctl)
{
    if (blink_ctl == 1) {
        if (led_id == 1) {
            ind_led_blink_stat.machine_stat_blink = IND_LED_BLINK_OFF;
        } else if (led_id == 2) {
            ind_led_blink_stat.cuvette_blink = IND_LED_BLINK_OFF;
        }
    } else if (blink_ctl == 2) {
        if (led_id == 1) {
            ind_led_blink_stat.machine_stat_blink = IND_LED_BLINK_ON;
            ind_led_blink_stat.machine_stat_color = color;
        } else if (led_id == 2) {
            ind_led_blink_stat.cuvette_blink = IND_LED_BLINK_ON;
            ind_led_blink_stat.cuvette_color = color;
        }
    }
}

static void *ind_led_task(void *arg)
{
    ind_led_blink_stat.machine_stat_blink = IND_LED_BLINK_OFF;
    ind_led_blink_stat.cuvette_blink = IND_LED_BLINK_OFF;
    ind_led_blink_stat.machine_stat_color = 0;
    ind_led_blink_stat.cuvette_color = 0;

    while (1) {
        if (ind_led_blink_stat.machine_stat_blink == IND_LED_BLINK_ON) {
            if (ind_led_blink_stat.machine_stat_color == 1) {
                gpio_set(LED_CTL_STATUS_G, CPLD_LED_ON);
            } else if (ind_led_blink_stat.machine_stat_color == 2) {
                gpio_set(LED_CTL_STATUS_Y, CPLD_LED_ON);
            } else if (ind_led_blink_stat.machine_stat_color == 3) {
                gpio_set(LED_CTL_STATUS_R, CPLD_LED_ON);
            }
        }
        if (ind_led_blink_stat.cuvette_blink == IND_LED_BLINK_ON) {
            if (ind_led_blink_stat.cuvette_color == 1) {
                gpio_set(LED_CUVETTE_IN_G, CPLD_LED_ON);
            } else if (ind_led_blink_stat.cuvette_color == 2) {
                gpio_set(LED_CUVETTE_IN_Y, CPLD_LED_ON);
            } else if (ind_led_blink_stat.cuvette_color == 3) {
                gpio_set(LED_CUVETTE_IN_R, CPLD_LED_ON);
            }
        }
        usleep(1200*1000);
        if (ind_led_blink_stat.machine_stat_blink == IND_LED_BLINK_ON) {
            gpio_set(LED_CTL_STATUS_R, CPLD_LED_OFF);
            gpio_set(LED_CTL_STATUS_G, CPLD_LED_OFF);
            gpio_set(LED_CTL_STATUS_Y, CPLD_LED_OFF);
        }
        if (ind_led_blink_stat.cuvette_blink == IND_LED_BLINK_ON) {
            gpio_set(LED_CUVETTE_IN_R, CPLD_LED_OFF);
            gpio_set(LED_CUVETTE_IN_G, CPLD_LED_OFF);
            gpio_set(LED_CUVETTE_IN_Y, CPLD_LED_OFF);
        }
        usleep(600*1000);
    }
    return NULL;
}


static int indicator_led_init(void)
{
    pthread_t ind_led_thread;

    indicator_led_set(LED_MACHINE_ID, LED_COLOR_GREEN, LED_OFF);
    indicator_led_set(LED_MACHINE_ID, LED_COLOR_YELLOW, LED_OFF);
    indicator_led_set(LED_MACHINE_ID, LED_COLOR_RED, LED_OFF);

    if (0 != pthread_create(&ind_led_thread, NULL, ind_led_task, NULL)) {
        LOG("indicator_led_init thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void set_alarm_mode(int open, int mode)
{
    alarm_sound_stat.open = open;

    if (open == SOUND_OFF) {
        alarm_sound_stat.mode = SOUND_TYPE_0;
    } else if (open == SOUND_ON && mode >= alarm_sound_stat.mode) {
        alarm_sound_stat.start_time = time(NULL);
        alarm_sound_stat.mode = mode;
    }
}

static void *alarm_sound_task(void *arg)
{
    while (1) {
        if (alarm_sound_stat.open == SOUND_OFF) {
            gpio_set(VALVE_BUZZER, 0);
        } else {
            if (alarm_sound_stat.mode == SOUND_TYPE_1) {
                gpio_set(VALVE_BUZZER, 1);
                usleep(800*1000);
                gpio_set(VALVE_BUZZER, 0);
                usleep(800*1000);
            } else if (alarm_sound_stat.mode == SOUND_TYPE_2) {
                gpio_set(VALVE_BUZZER, 1);
                usleep(500*1000);
                gpio_set(VALVE_BUZZER, 0);
                usleep(500*1000);
            } else if(alarm_sound_stat.mode == SOUND_TYPE_3) {
                gpio_set(VALVE_BUZZER, 1);
                usleep(300*1000);
                gpio_set(VALVE_BUZZER, 0);
                usleep(300*1000);
            }
        }
        usleep(200*1000);
    }
    return NULL;
}

static int alarm_sound_init(void)
{
    pthread_t alarm_sound_thread;
    gpio_set(VALVE_BUZZER, 0);
    if (0 != pthread_create(&alarm_sound_thread, NULL, alarm_sound_task, NULL)) {
        LOG("alarm_sound_init thread create failed!, %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void *gate_state_task(void *arg)
{
    char alarm_message[FAULT_CODE_LEN] = {0};
    int check_flag = 0, check_cnt = 0;;

    while (1) {
        if (ins_io_get().waste_io) {
            if (gpio_get(PE_WASTE_FULL)) {
                usleep(200*1000);
            if (gpio_get(PE_WASTE_FULL)) {/* 消抖两次防止误判 */
                usleep(200*1000);
            if (gpio_get(PE_WASTE_FULL)) {/* 消抖两次防止误判 */
                if ((get_machine_stat() == MACHINE_STAT_RUNNING || module_start_stat_get() == MODULE_CMD_START)
                    && module_sampler_add_get() == SAMPLER_ADD_START) {
                    LOG("dustbin is full!\n");
                    if (check_cnt <= 2) {/* 光电可能会误判，尝试在下一次继续判断 */
                        check_cnt++;
                    }
                    if (check_flag == 0 && check_cnt >= 2) {
                        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_MACHINE, MODULE_FAULT_PE_WASTE_FULL);
                        report_alarm_message(0, alarm_message);
                        check_flag = 1;
                    } else {
                        LOG("wait for another check in 10s\n");
                        sleep(10);
                    }
                } else {
                    LOG("dustbin is full, but not in process!\n");
                }
            }
            }
            } else {
                check_flag = 0;
                check_cnt = 0;
            }
        }
        if (gpio_get(PE_WASTE_CHECK)) {
            usleep(200*1000);
        if (gpio_get(PE_WASTE_CHECK)) {/* 消抖两次防止误判 */
            usleep(200*1000);
        if (gpio_get(PE_WASTE_CHECK)) {/* 消抖两次防止误判 */
            if ((get_machine_stat() == MACHINE_STAT_RUNNING || module_start_stat_get() == MODULE_CMD_START)) {
                LOG("dustbin is out!\n");
                if (check_flag == 0) {
                    fault_code_generate(alarm_message, MODULE_CLASS_FAULT_MACHINE, MODULE_FAULT_PE_WASTE_IS_OUT);
                    report_alarm_message(0, alarm_message);
                    check_flag = 1;
                }
            }
        }
        }
        } else {
            check_flag = 0;
        }
        sleep(2);
    }
    return NULL;
}

static int gate_state_monitor_init(void)
{
    pthread_t gate_state_thread;
    gpio_set(VALVE_BUZZER, 0);
    if (0 != pthread_create(&gate_state_thread, NULL, gate_state_task, NULL)) {
        LOG("gate_state_monitor_init thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}


int module_monitor_init(void)
{
    int ret = 0;

    kb_ul_init();
    ret += indicator_led_init();
    ret += alarm_sound_init();
    ret += gate_state_monitor_init();
    ret += boards_error_get();
    ret += device_status_count_init();

    return ret;
}


