#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <ev.h>
#include <signal.h>
#include <sys/time.h>
#include <dirent.h>


#include "list.h"
#include "jansson.h"
#include "log.h"
#include "misc_log.h"

#include "slip/slip_node.h"
#include "slip/slip_process.h"
#include "work_queue.h"
#include "common.h"

#include "ipi_console.h"
#include "slip_cmd_table.h"
#include "soft_power.h"
#include "thrift_service_software_interface.h"
#include "module_cup_monitor.h"
#include "module_common.h"
#include "module_monitor.h"

#include "module_reagent_table.h"
#include "module_incubation.h"
#include "module_magnetic_bead.h"
#include "module_optical.h"
#include "module_temperate_ctl.h"
#include "module_cup_mix.h"
#include "module_liquied_circuit.h"
#include "module_liquid_detect.h"
#include "module_needle_s.h"
#include "module_needle_r2.h"
#include "module_catcher.h"
#include <module_upgrade.h>
#include "module_cuvette_supply.h"
#include "h3600_maintain_utils.h"
#include "module_auto_calc_pos.h"

void stop_handler(int signo)
{
    static int flag = 0;

    /* 为了既能捕获段错误信号，又能生成coredump，需要将段错误信号转发至默认处理 */
    if (signo == SIGSEGV) {
        signal(signo, SIG_DFL);  //必须放在第一行，否则coredump堆栈显示的地址会对应不上
    }

    LOG("oops! stop:%d, flag:%d!!!\n", signo, flag);
    FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);

    if (flag == 0) {
        flag = 1;
        usleep(1000*400);
        /* 当程序崩溃 或 强制退出时，为防止xx阀不能关闭，禁止其它地方操作各个阀，
          且emergency_stop只能使用gpio_set，不能使用受value_set_control控制的valve_set */
        value_set_control(0);
        emergency_stop();
        usleep(1000*400);
        flag = 0;

        if (signo != SIGSEGV) { /* 段错误不能主动退出，否则不能生成coredump */
            _exit(0);
        }
    }
}

void signal_init()
{
     struct sigaction sigIntHandler;
     sigIntHandler.sa_handler =  stop_handler;
     sigemptyset(&sigIntHandler.sa_mask);
     sigIntHandler.sa_flags = 0;
     sigaction(SIGINT,&sigIntHandler,NULL);
     sigaction(SIGQUIT,&sigIntHandler,NULL);
     sigaction(SIGTERM,&sigIntHandler,NULL);
     sigaction(SIGSEGV,&sigIntHandler,NULL);
     sigaction(SIGABRT,&sigIntHandler,NULL);
}

static void *coredump_detect_task(void *arg)
{
    char cmd_buff[256] = {0};
    FILE *fp = NULL;

    /* 设置coredump生成位置 */
    memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
    sprintf(cmd_buff, "echo %s%s > /proc/sys/kernel/core_pattern", LOG_DIR, COREDUMP_FILE);
    system(cmd_buff);

    /* 提前压缩coredump */
    fp = fopen(LOG_DIR COREDUMP_FILE, "r");
    if (fp != NULL) {
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        sprintf(cmd_buff, "cd %s && nice -n 19 tar -zcvf %s.tar.gz %s > /dev/null", LOG_DIR, COREDUMP_FILE, COREDUMP_FILE);
        system(cmd_buff);

        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        sprintf(cmd_buff, "cd %s && rm -rf %s > /dev/null", LOG_DIR, COREDUMP_FILE);
        system(cmd_buff);

        fflush(fp);

        fclose(fp);
    }

    return NULL;
}

static int coredump_detect_init()
{
    pthread_t coredump_detect_thread;

    if(0 != pthread_create(&coredump_detect_thread, NULL, coredump_detect_task, NULL)) {
        LOG("coredump_detect thread create failed!, %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

#if H3600_DEBUG
static void help(char *process)
{
    printf("%s cmd\n", process);
    printf("cmd:\n");
    printf("\tio_get io_id\n");
    printf("\tio_set io_id status\n");
    printf("\tscanner_version type(1 | 2 | 3 | 4)\n");
    printf("\tscanner_scan type(1 | 2 | 3 | 4)\n");
    printf("\tmotor_reset motor_id type(0 | 1 | 2)\n");
    printf("\tmotor_move motor_id position\n");
    printf("\tmotor_move_to motor_id position\n");
    printf("\tmotor_step motor_id step\n");
    printf("\tmotor_dual_step motor_id stepx stepy\n");
    printf("\tmotor_dual_step_ctl motor_id stepx stepy speed acc\n");
    printf("\tmotors_step \"motor_id,step [motor_id,step ...]\"\n");
    printf("\tmotor_speed motor_id [speed]\n");
    printf("\tmotor_stop motor_id\n");
    printf("\tmotor_pos_get motor_id\n");
    printf("\tmotor_pos_set motor_id position step\n");
    printf("\tmotor_step_get motor_id\n");
    printf("\tupdate firmware\n");
    printf("\tboards_firmware node_id\n");
    printf("\treset_all_eletro\n");
    printf("\tauto_cali\n");
    printf("\treag_led\n");
    printf("\tscanner_jp index(0 | 1)\n");
}

static int h3600_debug(int argc, char *argv[])
{
    int motor_id;
    int i, io_id;
    int type, step, pos, speed;
    int status;
    int ret = 0;
    int timeout = 30000;

    if (argc == 1) {
        help(argv[0]);
        return -1;
    }

    motors_step_attr_init();
//    motors_speed_attr_init();
    slip_mainboard_init();
    h3600_conf_init();
    usleep(100000);

    if (0 == strcmp(argv[1], "motor_reset")) {
        motor_id = atoi(argv[2]);
        type = argv[3] ? atoi(argv[3]) : 0;
        ret = slip_motor_reset_timedwait(motor_id, type, timeout);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "io_get")) {
        io_id = atoi(argv[2]);
        ret = gpio_get(io_id);
        LOG("io %d result: %d\n", io_id, ret);
    } else if (0 == strcmp(argv[1], "io_set")) {
        io_id = atoi(argv[2]);
        status = atoi(argv[3]);
        ret = gpio_set(io_id, status);
        LOG("io %d result: %d\n", io_id, ret);
    } else if (0 == strcmp(argv[1], "scanner_version")) {
        char version[40] = {0};
        type = argv[2] ? atoi(argv[2]) : 1;
        ret = scanner_version(type, version, sizeof(version));
        LOG("scanner %d result: %s\n", type, version);
    } else if (0 == strcmp(argv[1], "scanner_scan")) {
        char barcode[64] = {0};
        type = argv[2] ? atoi(argv[2]) : 1;
        ret = scanner_read_barcode_sync(type, barcode, sizeof(barcode));
        LOG("scanner %d result: %s\n", type, barcode);
    } else if (0 == strcmp(argv[1], "motor_step")) {
        motor_id = atoi(argv[2]);
        step = atoi(argv[3]);
        ret = slip_motor_step_timedwait(motor_id, step, timeout);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "motor_dual_step")) {
        motor_id = atoi(argv[2]);
        step = atoi(argv[3]);
        ret = slip_motor_dual_step_timedwait(motor_id, atoi(argv[3]), atoi(argv[4]), timeout);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "motor_dual_step_ctl")) {
        motor_id = atoi(argv[2]);
        step = atoi(argv[3]);
        ret = slip_motor_dual_step_ctl_timedwait(motor_id, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), timeout);
    } else if (0 == strcmp(argv[1], "motor_speed")) {
        motor_id = atoi(argv[2]);
        if (argv[3]) {
            speed = atoi(argv[3]);
            ret = motor_speed_timedwait(motor_id, speed, timeout);
        } else {
            ret = slip_motor_speed_timedwait(motor_id, timeout);
        }
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "motor_move")) {
        motor_id = atoi(argv[2]);
        pos = atoi(argv[3]);
        ret = slip_motor_move_timedwait(motor_id, pos, timeout);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "motor_move_to")) {
        motor_id = atoi(argv[2]);
        pos = atoi(argv[3]);
        ret = slip_motor_move_to_timedwait(motor_id, pos, timeout);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "motors_step")) {
        unsigned char motors[32];
        int steps[32];
        int motor_cnt;

        char *str = argv[2];
        char *p;
        for (i = 0; NULL != (p = strtok(str, " ")) && i < 32; i++, str = NULL) {
            if (2 != (sscanf(p, "%hhd,%d", &motors[i], &steps[i]))) {
                help(argv[0]);
                return -1;
            }
            printf("motor_id: %d, step: %d\n", motors[i], steps[i]);
        }

        motor_cnt = i;
        for (i = 0; i < motor_cnt; i++) {
            ret = slip_motor_step(motors[i], steps[i]);
            if (ret == -1) {
                return -1;
            }
        }
        ret = motors_timedwait(motors, motor_cnt, 30000);
    }else if (0 == strcmp(argv[1], "motor_stop")) {
        motor_id = atoi(argv[2]);
        ret = motor_stop_timedwait(motor_id, 2000);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "motor_pos_get")) {
        int pos[100] = {0};
        motor_id = atoi(argv[2]);
        int cnt = thrift_motor_pos_get(motor_id, pos, sizeof(pos) / sizeof(pos[0]));
        for (i = 0; i < cnt; i++) {
            LOG("motor %d result: %d %d\n", motor_id, i, pos[i]);
        }
    } else if (0 == strcmp(argv[1], "motor_pos_set")) {
        motor_id = atoi(argv[2]);
        pos = atoi(argv[3]);
        step =  atoi(argv[4]);
        LOG("motor %d position set: %d %d\n", motor_id, pos, step);
        thrift_motor_pos_set(motor_id, pos, step);
    } else if (0 == strcmp(argv[1], "motor_step_get")) {
        motor_id = atoi(argv[2]);
        ret = motor_current_pos_timedwait(motor_id, 2000);
        LOG("motor %d result: %d\n", motor_id, ret);
    } else if (0 == strcmp(argv[1], "update_firmware")) {
        upgrade_all_firmware(FIRMWARE_PATH);
        LOG("update_firmware\n");
    } else if (0 == strcmp(argv[1], "boards_firmware")) {
        char firmware_version[64] = {0};
        type = argv[2] ? atoi(argv[2]) : 1;
        ret = boards_firmware_version_get(type, firmware_version, sizeof(firmware_version));
        LOG("board_id %d result: %s\n", type, firmware_version);
    } else if (0 == strcmp(argv[1], "reset_all_eletro")) {
        ret = slip_reinit_to_sampler_board();
        LOG("result: %d\n", ret);
    } else if (0 == strcmp(argv[1], "reag_led")) {
        status = atoi(argv[2]);
        ret = slip_button_reag_led_to_sampler(status);
        LOG("result: %d\n", ret);
    } else if (0 == strcmp(argv[1], "auto_cali")) {
        for (int i = 0 ; i < 1 ; i++) {
            LOG("------------------------------------------------------start: %d\n", i);
            ret = eng_gripper_auto_cali_func(ENG_DEBUG_GRIP);
            LOG("result: %d\n", ret);
        }
    } else if (0 == strcmp(argv[1], "scanner_jp")) {
        status = atoi(argv[2]);
        ret = scanner_jp_value_get(status);
        LOG("result: %d\n", ret);
    } else {
        help(argv[0]);
        return -1;
    }

    return ret == -1 ? -1 : 0;
}
#endif

void easylogger_init()
{
    if (opendir(LOG_DIR) == NULL) {
        system("mkdir -p "LOG_DIR);
    }

    if (opendir(APP_DIR) == NULL) {
        system("mkdir -p "APP_DIR);
    }

    usleep(1000);
    /* close printf buffer */
    setbuf(stdout, NULL);

    /* initialize EasyLogger */
    elog_init();
    /* set EasyLogger log format */
    elog_set_fmt(ELOG_LVL_ASSERT, 0); /* 把ASSERT作为安静模式 */
    elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_TIME|ELOG_FMT_FUNC|ELOG_FMT_LINE);
    elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_TIME|ELOG_FMT_FUNC|ELOG_FMT_LINE);
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL|ELOG_FMT_TIME|ELOG_FMT_FUNC|ELOG_FMT_LINE);
    elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_TIME|ELOG_FMT_FUNC|ELOG_FMT_LINE);
    elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_TIME|ELOG_FMT_FUNC|ELOG_FMT_LINE);

    /* start EasyLogger */
    elog_start();
}

int main(int argc, char *argv[])
{
#if H3600_DEBUG
    easylogger_init();
    work_queue_init();
    upgrade_init();

    return h3600_debug(argc, argv);
#else
    //json_example();
    easylogger_init();
    coredump_detect_init();

    /* 加载RTOS */
    system(LOAD_IMAGE_FILE" "RTOS_FILE);
    sleep(3);/* 需等待RTOSDemo.bin加载完成后，再进行后续初始化(slip) */

    misc_log_init();

    /* 反应杯初始化 */
    react_cup_list_init();

    /* 初始化slip */
    slip_mainboard_init();

    h3600_conf_init();

    work_queue_init();

#if 0
    /* 加热针吐液温度的测试
    r2 debug
    app [xitu] [take_ul] [clean type] [loop_cnt] [special mode] [xi power] [normal clean power]  [special clean power] [predict clean]
    app 1 0 0 4 0 0 0 0 0 
    app 0 100.0 0 4 0 0 0 0 0
    app 0 100.0 0 4 3 20 20 30 0
    */
    module_monitor_init();
    leave_singal_init(0);
    liquid_circuit_init();
    module_reagent_table_init();
    module_liquid_detect_init();
    module_reagent_table_init();

    if (atoi(argv[1]) == 1) {
       needle_r2_heat_test(atof(argv[1]), 0, 0, atoi(argv[4]), 0, 0, 0, 0, 0);
    } else {
       needle_r2_heat_test(atof(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]), atoi(argv[8]), atoi(argv[9]));
    }
    sleep(2);
    value_set_control(0);
    emergency_stop(); 
    sleep(1);

    return 0;
#endif

    /* 初始化thrift slave 服务端(上位机PC 连接 下位机仪器) */
    thrift_master_t thrift_slave_server = {THRIFT_SLAVE_SERVER_IP, THRIFT_SLAVE_SERVER_PORT};
    if (strlen(h3600_conf_get()->thrift_slave_server.ip) > 0 && h3600_conf_get()->thrift_slave_server.port != 0) {
        char ip_cmd_buff[64] = {0};

        memset(thrift_slave_server.ip, 0, sizeof(thrift_slave_server.ip));
        strncpy(thrift_slave_server.ip, h3600_conf_get()->thrift_slave_server.ip, 31);
        thrift_slave_server.port = h3600_conf_get()->thrift_slave_server.port;

        memset(ip_cmd_buff, 0, sizeof(ip_cmd_buff));
        sprintf(ip_cmd_buff, "ifconfig eth0:1 %s", thrift_slave_server.ip);
        system(ip_cmd_buff);

        usleep(1000*300);
        memset(ip_cmd_buff, 0, sizeof(ip_cmd_buff));
        sprintf(ip_cmd_buff, "route add default gw 192.168.33.1");
        system(ip_cmd_buff);
        sleep(1);
    }
    LOG("thrift_client_server connect ip:port {%s:%d}\n", thrift_slave_server.ip, thrift_slave_server.port);
    thrift_slave_server_init(&thrift_slave_server);

    /* 初始化thrift slave 客户端(下位机仪器 连接 上位机PC) */
    thrift_master_t thrift_master_server = {THRIFT_MASTER_SERVER_IP, THRIFT_MASTER_SERVER_PORT};
    if (strlen(h3600_conf_get()->thrift_master_server.ip) > 0 && h3600_conf_get()->thrift_master_server.port != 0) {
        memset(thrift_master_server.ip, 0, sizeof(thrift_master_server.ip));
        strncpy(thrift_master_server.ip, h3600_conf_get()->thrift_master_server.ip, 31);
        thrift_master_server.port = h3600_conf_get()->thrift_master_server.port;
    } else {
        if (argc == 2) {
            memset(thrift_master_server.ip, 0, sizeof(thrift_master_server.ip));
            strncpy(thrift_master_server.ip, argv[1], strlen(argv[1])>31 ? 31 : strlen(argv[1]));
        } else if(argc == 3) {
            memset(thrift_master_server.ip, 0, sizeof(thrift_master_server.ip));
            strncpy(thrift_master_server.ip, argv[1], strlen(argv[1])>31 ? 31 : strlen(argv[1]));
            thrift_master_server.port = atoi(argv[2]);
        }
    }
    LOG("thrift_master_server connect ip:port{%s:%d}\n", thrift_master_server.ip, thrift_master_server.port);
    thrift_slave_client_init(&thrift_master_server);

    signal_init();
    soft_power_init();
    leave_singal_init(0);
    module_monitor_init();

    /* 应用模块初始化 */
    module_liquid_detect_init();
    module_cuvette_supply_init();
    incubation_init();
    magnetic_bead_init();
    optical_init();
    temperate_ctl_init();
    cup_mix_init();
    module_reagent_table_init();
    sampler_init();
    liquid_circuit_init();
    needle_s_init();
    needle_r2_init();
    catcher_init();
    upgrade_init();

    /* 更新开机状态 */
    set_power_off_stat(1);

    while (1) {
        sleep(100);
    }
#endif

    return 0;
}

