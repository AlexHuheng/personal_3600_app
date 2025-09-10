#include <iostream>
#include <thrift/transport/TSocket.h>

#include "HIOtherHandler.h"
#include <slip/slip_msg.h>
#include <slip/slip_node.h>

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#include <thrift_service_software_interface.h>
#include <log.h>
#include <thrift_handler.h>
#include <work_queue.h>
#include <module_monitor.h>
#include <module_upgrade.h>
#include <thrift_connect_pool.h>
#include <ctype.h>
#include <jansson.h>
#include "soft_power.h"
#include "slip_cmd_table.h"
#include <module_engineer_debug_weigh.h>
#include <module_engineer_debug_position.h>
#include <module_engineer_debug_cmd.h>
#include <module_engineer_debug_aging.h>
#include "module_auto_calc_pos.h"
#include "module_reagent_table.h"
#include "module_optical.h"
#include "module_magnetic_bead.h"
#include "module_auto_cal_needle.h"

using namespace std;
using namespace ::apache::thrift;
using namespace ::apache::thrift::transport;

typedef struct
{
    thrift_master_t master_server;  /* 上位机PC的ip、port */
    thrift_master_t slave_server;   /* 下位机仪器的ip、port */
    int32_t pierce_enable;          /* 是否启用穿刺 */
    int32_t userdata;
}thrift_config_param_t;

/* 下位机文件类型定义, 定义来源于thrift文件  */
#define SFT_SLAVE_LOG_FILE                      1        // 下位机日志文件
#define SFT_SLAVE_CONFIG_FILE                   2        // 下位机仪器标定参数文件

#define SEND_DATA_SIZE                          (1024*500)

#define H3600_CONF_DIR              "/root/maccura/h_etc/"
#define H3600_CONF_RECV_DIR         "/root/maccura/h_etc/recv/"
#define H3600_CONF_BACK_DIR         "/root/maccura/h_etc/back/"
#define H3600_CONF_FILE             "h3600_conf.json"
#define H3600_CONF_MD5_FILE         "h3600_conf.json.md5"
#define H3600_CONF_TAR_FILE         "h3600_conf.tar"
#define H3600_CONF_TAR_MD5_FILE     "h3600_conf.tar.md5"
#define H3600_CONF_RECV_DIFF_FILE   "/root/maccura/log/restore_conf_diff.log"

#define H3600_LOG_DIR               "/root/maccura/log/"
#define H3600_LOG_TAR_FILE          "log.tar"
#define H3600_LOG_TAR_MD5_FILE      "log.tar.md5"

/*  */
#define PATH_LEN 256
#define MD5_LEN 32

#define STR_VALUE(val) #val
#define STR(name) STR_VALUE(name)
#define MD5SUM_CMD_FMT "md5sum %." STR(PATH_LEN) "s 2>/dev/null"

/* 获取文件的MD5值  
返回值：
    0：失败
    1：成功
*/
int get_file_md5(const char *file_name, char *md5_sum)
{
    char cmd[PATH_LEN + sizeof (MD5SUM_CMD_FMT)] = {0};
    FILE *pFile = NULL;
    int i = 0, ch = 0;
    char *p = md5_sum;

    sprintf(cmd, MD5SUM_CMD_FMT, file_name);
    pFile = popen(cmd, "r");
    if (pFile == NULL) {
        LOG("open (%s) file fail\n", file_name);
        return 0;
    }

    for (i=0; i<MD5_LEN && isxdigit(ch = fgetc(pFile)); i++) {
        *md5_sum++ = ch;
    }

    *md5_sum = '\0';

    LOG("%s md5 is:(%s)\n", file_name, p);
    pclose(pFile);

    return i == MD5_LEN;
}

static void heart_beart_async(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;

    if (thrift_slave_server_connect_get() == 1) {
        ret = 0;
        set_power_off_stat(0);
    } else {
        ret = 1;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
}

static void get_upload_backup_handler(void *arg)
{
    int32_t *param = (int32_t *)arg;
    int file_type = 0;
    int rand_no = 0;
    char data_buff[SEND_DATA_SIZE] = {0};
    char md5_buff[64] = {0};
    char cmd_buff[256] = {0};
    FILE *fp = NULL;
    int file_size = 0;
    int i = 0, j = 0, k = 0;

    file_type = param[0];
    rand_no = param[1];

    if (file_type == SFT_SLAVE_LOG_FILE) {
        /* 打包文件 */
        LOG("pack file\n");
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        sprintf(cmd_buff, "cd %s && nice -n 19 tar -cvf %s * --exclude %s", H3600_LOG_DIR, H3600_LOG_TAR_FILE, H3600_LOG_TAR_FILE);
        system(cmd_buff);

        /* 获取md5 */
        LOG("get md5\n");
        memset(md5_buff, 0, sizeof(md5_buff)/sizeof(md5_buff[0]));
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        get_file_md5(H3600_LOG_DIR H3600_LOG_TAR_FILE, md5_buff);
        sprintf(cmd_buff, "echo -n %s > %s", md5_buff, H3600_LOG_DIR H3600_LOG_TAR_MD5_FILE);
        system(cmd_buff);

        /* 上传文件数据至上位机 */
        fp = fopen(H3600_LOG_DIR H3600_LOG_TAR_FILE, "rb");
        if (fp == NULL) {
            LOG("open %s file\n", H3600_LOG_DIR H3600_LOG_TAR_FILE);
            upload_device_file(file_type, rand_no, data_buff, 0, 0, 1, md5_buff);
        } else {
            fseek(fp, 0, SEEK_END);
            file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            j = file_size / SEND_DATA_SIZE;
            k = file_size % SEND_DATA_SIZE;
            LOG("upload file. size:%d, j:%d, k:%d\n", file_size, j, k);
            for (i=0; i<j; i++) {
                memset(data_buff, 0, sizeof(data_buff)/sizeof(data_buff[0]));
                fread(data_buff, 1, SEND_DATA_SIZE, fp);
                upload_device_file(file_type, rand_no, data_buff, SEND_DATA_SIZE, i, 0, md5_buff);
            }

            memset(data_buff, 0, sizeof(data_buff)/sizeof(data_buff[0]));
            fread(data_buff, 1, k, fp);
            upload_device_file(file_type, rand_no, data_buff, k, i, 1, md5_buff);

            fclose(fp);

            /* 及时释放打包日志占用的缓存 */
            sync();
            memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
            sprintf(cmd_buff, "echo 1 > /proc/sys/vm/drop_caches");
            system(cmd_buff);
        }
    } else if (file_type == SFT_SLAVE_CONFIG_FILE) {
        /* 获取md5 */
        memset(md5_buff, 0, sizeof(md5_buff)/sizeof(md5_buff[0]));
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        get_file_md5(H3600_CONF_DIR H3600_CONF_FILE, md5_buff);
        sprintf(cmd_buff, "echo -n %s > %s", md5_buff, H3600_CONF_DIR H3600_CONF_MD5_FILE);
        system(cmd_buff);

        /* 打包文件 */
        LOG("pack file\n");
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        sprintf(cmd_buff, "cd %s && tar -cvf %s %s %s", H3600_CONF_DIR, H3600_CONF_TAR_FILE, H3600_CONF_FILE, H3600_CONF_MD5_FILE);
        system(cmd_buff);

        /* 获取md5 */
        LOG("get md5\n");
        memset(md5_buff, 0, sizeof(md5_buff)/sizeof(md5_buff[0]));
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        get_file_md5(H3600_CONF_DIR H3600_CONF_TAR_FILE, md5_buff);
        sprintf(cmd_buff, "echo -n  %s > %s", md5_buff, H3600_CONF_DIR H3600_CONF_TAR_MD5_FILE);
        system(cmd_buff);

        /* 上传文件数据至上位机 */
        fp = fopen(H3600_CONF_DIR H3600_CONF_TAR_FILE, "rb");
        if (fp == NULL) {
            LOG("open %s file\n", H3600_CONF_DIR H3600_CONF_TAR_FILE);
            upload_device_file(file_type, rand_no, data_buff, 0, 0, 1, md5_buff);
        } else {
            fseek(fp, 0, SEEK_END);
            file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            j = file_size / SEND_DATA_SIZE;
            k = file_size % SEND_DATA_SIZE;
            LOG("upload file. size:%d, j:%d, k:%d\n", file_size, j, k);
            for (i=0; i<j; i++) {
                memset(data_buff, 0, sizeof(data_buff)/sizeof(data_buff[0]));
                fread(data_buff, 1, SEND_DATA_SIZE, fp);
                upload_device_file(file_type, rand_no, data_buff, SEND_DATA_SIZE, i, 0, md5_buff);
            }

            memset(data_buff, 0, sizeof(data_buff)/sizeof(data_buff[0]));
            fread(data_buff, 1, k, fp);
            upload_device_file(file_type, rand_no, data_buff, k, i, 1, md5_buff);

            fclose(fp);
        }
    }

    LOG("upload file done\n");
    free(arg);
}

static void user_reboot_handler(void *arg)
{
    LOG("user reboot\n");

    sync();
    sleep(3);
    system("reboot&");
}

static void config_para_handler(void *arg)
{
    thrift_config_param_t *param = (thrift_config_param_t *)arg;
    char ip_cmd_buff[128] = {0};
    async_return_t async_return;

    /* 先应答上位机，再配置ip、port */
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(param->userdata, 0, &async_return);

    goto end;

    sleep(1);
    LOG("config para start\n");

    thrift_master_server_ipport_set(param->master_server.ip, param->master_server.port);
    thrift_slave_server_ipport_set(param->slave_server.ip, param->slave_server.port, param->pierce_enable);

    set_connect_ipport(param->master_server.ip, param->master_server.port);
    thrift_slave_server_restart(param->slave_server.ip, param->slave_server.port);

    memset(ip_cmd_buff, 0, sizeof(ip_cmd_buff));
    sprintf(ip_cmd_buff, "ifconfig eth0:1 %s", param->slave_server.ip);
    system(ip_cmd_buff);

    usleep(1000*300);
    memset(ip_cmd_buff, 0, sizeof(ip_cmd_buff));
    sprintf(ip_cmd_buff, "route add default gw 192.168.33.1");
    system(ip_cmd_buff);
end:
    free(param);
    LOG("config para done\n");
}

static void engineer_weighing_async_handler(void *arg)
{
    engineer_weighing_param_t *engineer_weighing_param = (engineer_weighing_param_t *)arg;
    int ret = 0;
    async_return_t async_return;
    int test_type = 0;
    int needle_take_ul = 0;
    int needle_dilu_ul = 5;
    static int running_flag = 0;
    machine_stat_t machine_state_bak = MACHINE_STAT_STANDBY;

    if (running_flag == 1) {
        LOG("last is runing\n");
        async_return.return_type = RETURN_VOID;
        report_asnyc_invoke_result(engineer_weighing_param->userdata, 1, &async_return);
        free(arg);
        return;
    }
    running_flag = 1;
    machine_state_bak = get_machine_stat();
    set_machine_stat(MACHINE_STAT_RUNNING);
    engineer_is_run_set(ENGINEER_IS_RUN);
    clear_slip_list_motor();
    module_fault_stat_clear();

    test_type = engineer_weighing_param->needle_type;
    needle_take_ul = engineer_weighing_param->sample_reagent_vol;
    needle_dilu_ul = engineer_weighing_param->diulent_vol;

    switch (test_type) {
        case NT_SAMPLE_NORMAL_ADDING_WITHOUT_PUNCTURE:
        case NT_SAMPLE_NORMAL_ADDING_WITH_PUNCTURE:
        case NT_R1_ADDING:
            /* S平头针普通流程 */
            LOG("needle S add test...\n");
            ret = needle_s_weigh(needle_take_ul);
            break;
        case NT_SAMPLE_DILUENT_ADDING_WITHOUT_PUNCTURE:
        case NT_SAMPLE_DILUENT_ADDING_WITH_PUNCTURE:
            /* S平头针稀释流程 */
            LOG("needle S dilu add test...\n");
            ret = needle_s_dilu_weigh(needle_dilu_ul, needle_take_ul);
            break;
        case NT_SAMPLE_TEMP_ADDING_WITH_PUNCTURE:
            /* S穿刺针暂存流程 */
            LOG("needle S_P temp add test...\n");
            ret = needle_s_sp_weigh(needle_take_ul);
            break;
        case NT_R2_ADDING:
            /* R2针普通流程 */
            LOG("needle R2 add test...\n");
            ret = needle_r2_weigh(needle_take_ul);
            break;
        case NT_SAMPLE_MUTI_NORMAL_ADDING_WITH_PUNCTURE:
            LOG("needle S muti add test...\n");
            ret = needle_s_muti_weigh(needle_take_ul);
            break;
        case NT_SAMPLE_MUTI_TEMP_ADDING_WITH_PUNCTURE:
            LOG("needle S_P muti temp add test...\n");
            ret = needle_s_sp_muti_weigh(needle_take_ul);
            break;
        case NT_R2_MUTI_ADDING:
            LOG("needle R2 muti add test...\n");
            ret = needle_r2_muti_weigh(needle_take_ul);
            break;
        default:
            LOG("no such type!\n");
            break;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(engineer_weighing_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("needle add done...\n");
    running_flag = 0;
    set_machine_stat(machine_state_bak);
}

static void engineer_position_async_handler(void *arg)
{
    struct ENGINEER_DEBUG_MOTOR_PARA_TT *engineer_position_param = (struct ENGINEER_DEBUG_MOTOR_PARA_TT *)arg;
    int ret = 0;
    async_return_t async_return;
    static int running_flag = 0;
    machine_stat_t machine_state_bak = MACHINE_STAT_STANDBY;

    if (running_flag == 1) {
        LOG("last is runing\n");
        async_return.return_type = RETURN_VOID;
        report_asnyc_invoke_result(engineer_position_param->userdata, 1, &async_return);
        free(arg);
        return ;
    }
    running_flag = 1;

    machine_state_bak = get_machine_stat();
    set_machine_stat(MACHINE_STAT_RUNNING);
    engineer_is_run_set(ENGINEER_IS_RUN);
    clear_slip_list_motor();
    module_fault_stat_clear();

    ret = engineer_debug_pos_run(engineer_position_param);

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(engineer_position_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("engineer_position done...\n");
    running_flag = 0;
    set_machine_stat(machine_state_bak);
}

static void engineer_run_cmd_async_handler(void *arg)
{
    struct ENGINEER_DEBUG_CMD_TT *engineer_cmd_param = (struct ENGINEER_DEBUG_CMD_TT *)arg;
    struct ENGINEER_DEBUG_RUN_RESULT_TT result = {0};
    int ret = 0;
    async_return_t async_return;
    static int running_flag = 0;
    machine_stat_t machine_state_bak = MACHINE_STAT_STANDBY;

    if (running_flag == 1) {
        LOG("last is runing\n");
        async_return.return_type = RETURN_VOID;
        report_asnyc_invoke_result(engineer_cmd_param->userdata, 1, &async_return);
        free(arg);
        return ;
    }
    running_flag = 1;

    machine_state_bak = get_machine_stat();
    set_machine_stat(MACHINE_STAT_RUNNING);
    engineer_is_run_set(ENGINEER_IS_RUN);
    memset(&result, 0, sizeof(result));
    clear_slip_list_motor();
    module_fault_stat_clear();

    ret = engineer_debug_cmd_run(engineer_cmd_param, &result);

    if (result.iRunResult > 0) { /* xx液面探测指令时，需要返回脉冲高度 */
        async_return.return_type = RETURN_INT;
        async_return.return_int = result.iRunResult;
    } else if (strlen(result.strBarcode) > 0) { /* xx扫码指令时，需要返回条码内容 */
        async_return.return_type = RETURN_STRING;
        memcpy(async_return.return_string, result.strBarcode, sizeof(result.strBarcode)/sizeof(result.strBarcode[0]));
    } else {
        async_return.return_type = RETURN_VOID;
    }

    report_asnyc_invoke_result(engineer_cmd_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("engineer_cmd done...ret:%d\n", ret);
    running_flag = 0;
    set_machine_stat(machine_state_bak);
}

static void engineer_aging_async_handler(void *arg)
{
    engineer_debug_aging_param_t *engineer_aging_param = (engineer_debug_aging_param_t *)arg;
    int ret = 0;
    async_return_t async_return;
    static int running_flag = 0;
    machine_stat_t machine_state_bak = MACHINE_STAT_STANDBY;

    if (running_flag == 1) {
        LOG("last is runing\n");
        async_return.return_type = RETURN_VOID;
        report_asnyc_invoke_result(engineer_aging_param->userdata, 1, &async_return);
        free(arg);
        return;
    }
    running_flag = 1;

    machine_state_bak = get_machine_stat();
    set_machine_stat(MACHINE_STAT_RUNNING);
    engineer_is_run_set(ENGINEER_IS_RUN);
    clear_slip_list_motor();
    module_fault_stat_clear();

    if (engineer_aging_param->aging.enable == 0) {
        engineer_aging_run_set(0);
    } else {
        engineer_aging_run_set(1);
        ret = engineer_aging_test(&engineer_aging_param->aging);
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(engineer_aging_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("engineer aging done...\n");
    running_flag = 0;
    set_machine_stat(machine_state_bak);
}


static void set_optical_led_handler(void *arg)
{
    int32_t *user_data = (int32_t *)arg;
    int ret = 0;
    async_return_t async_return;
    int count = 0;

    thrift_optical_led_calc_start();
    while (1) {
        /* 180s超时 */
        if (count++ > 180) {
            LOG("wait timeout\n");
            ret = -1;
            break;
        }

        if (optical_led_calc_flag_get() == 1 && optical_led_cmd_flag_get() == 0) {
            LOG("wait ok\n");
            if (thrift_optical_led_calc_get(OPTICAL_WAVE_405) == 0 ||
                thrift_optical_led_calc_get(OPTICAL_WAVE_660) == 0) {
                ret = -1;
            } else {
                ret = 0;
            }

            break;
        }
        sleep(1);
    }

    LOG("set_optical_led finish, ret: %d\n", ret);
    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(*user_data, ret == 0 ? 0 : 1, &async_return);
    free(arg);
}

HIOtherHandler::HIOtherHandler() {
    // Your initialization goes here
}

::EXE_STATE::type HIOtherHandler::SetSystemBaseData(const std::string& strJson)
{
    json_error_t jerror = {0};
    json_t *root = NULL, *obj = NULL;
    int ins_gate_io = 0, ins_reag_io = 0, ins_waste_io = 0, ins_off_reag = 0, ins_reag_time = 0;

    root = json_loads(strJson.c_str(), 0, &jerror);
    if (root == NULL) {
        LOG("json_error!\n");
        return EXE_STATE::FAIL;
    }

    obj = json_object_get(root, "EnableInstrumentGateIO");
    if (obj == NULL) {
        LOG("EnableInstrumentGateIO is NULL!\n");
    } else {
        ins_gate_io =  json_integer_value(obj);
    }
    obj = json_object_get(root, "EnableReagentCabinetIO");
    if (obj == NULL) {
        LOG("EnableReagentCabinetIO is NULL!\n");
    } else {
        ins_reag_io =  json_integer_value(obj);
    }
    obj = json_object_get(root, "EnableWasteBinIO");
    if (obj == NULL) {
        LOG("EnableWasteBinIO is NULL!\n");
    } else {
        ins_waste_io =  json_integer_value(obj);
    }
    obj = json_object_get(root, "EnableMonitorReagBin");
    if (obj == NULL) {
        LOG("EnableMonitorReagBin is NULL!\n");
    } else {
        ins_off_reag =  json_integer_value(obj);
    }
    obj = json_object_get(root, "MonitorReagBinOpenTime");
    if (obj == NULL) {
        LOG("MonitorReagBinOpenTime is NULL!\n");
    } else {
        ins_reag_time =  json_integer_value(obj);
    }
    LOG("gate_io : %d, reag_io : %d, waste_io : %d, off_reag : %d, reag_time %d\n", ins_gate_io, ins_reag_io, ins_waste_io, ins_off_reag, ins_reag_time);
    ins_io_set(ins_gate_io, ins_reag_io, ins_waste_io, ins_off_reag, ins_reag_time);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::ExecuteScriptAsync(const std::string& strFileNmae, const int32_t iUserData) { 
    return EXE_STATE::SUCCESS;
}

void HIOtherHandler::GetVersion(std::string& _return, const int32_t iType) {
    char firmware_version[64] = {0};
    uint8_t i_type_temp = SLIP_NODE_A9_LINUX + iType - 1;

    if (i_type_temp > SLIP_NODE_FPGA_M7 || i_type_temp < SLIP_NODE_A9_LINUX) {
        LOG("GetVersion: invalid iType:%d.\n", iType);
        goto end;
    }

    /* 获取子板固件 */
    if (-1 == boards_firmware_version_get(i_type_temp, firmware_version, sizeof(firmware_version))) {
        memset(firmware_version , 0, sizeof(firmware_version));
        goto end;
    }

    LOG("GetVersion success!\n");
end:
    _return = firmware_version;
}

::EXE_STATE::type HIOtherHandler::SetInstrumentNo(const std::string& strInstrumentNo) {
    // Your implementation goes here
    LOG("SetInstrumentNo\n");
    return EXE_STATE::SUCCESS;
}

void HIOtherHandler::GetInstrumentNo(std::string& _return) {
    // Your implementation goes here
    LOG("GetInstrumentNo\n");
}

::EXE_STATE::type HIOtherHandler::SetSystemTime(const  ::DATE_TIME_T& tDateTime) { 
    struct tm tm_time;
    struct timeval val_time;

    LOG("SetSystemTime: year=%d, month=%d, day=%d, hour=%d, min=%d, sec=%d, wday=%d\n",\
           tDateTime.iYear, tDateTime.iMonth, tDateTime.iDay, 
           tDateTime.iHour, tDateTime.iMinute, tDateTime.iSecond,
           tDateTime.iDayOfWeek);

    tm_time.tm_year  = tDateTime.iYear - 1900;
    tm_time.tm_mon   = tDateTime.iMonth - 1;
    tm_time.tm_mday  = tDateTime.iDay;
    tm_time.tm_hour  = tDateTime.iHour;
    tm_time.tm_min   = tDateTime.iMinute;
    tm_time.tm_sec   = tDateTime.iSecond;
    tm_time.tm_wday  = (tDateTime.iDayOfWeek==7 ? 0 : tDateTime.iDayOfWeek);

    /* 设置系统时间 */
    val_time.tv_sec  = mktime(&tm_time);
    val_time.tv_usec = 0;
    settimeofday(&val_time, NULL);

    /* 设置rtc时间 */  
    system("hwclock -w &");
    
    return EXE_STATE::SUCCESS;
}

void HIOtherHandler::GetSystemTime( ::DATE_TIME_T& _return) { 
    struct tm tm_time;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_time);
    
    _return.iYear = tm_time.tm_year + 1900;
    _return.iMonth = tm_time.tm_mon + 1;
    _return.iDay = tm_time.tm_mday;
    _return.iHour = tm_time.tm_hour; 
    _return.iMinute = tm_time.tm_min; 
    _return.iSecond = tm_time.tm_sec; 
    _return.iDayOfWeek = (tm_time.tm_wday==0 ? 7 : tm_time.tm_wday); 
    
    LOG("GetSystemTime: year=%d, month=%d, day=%d, hour=%d, min=%d, sec=%d, wday=%d\n",\
           _return.iYear, _return.iMonth, _return.iDay, 
           _return.iHour, _return.iMinute, _return.iSecond,
           _return.iDayOfWeek);
}

::EXE_STATE::type HIOtherHandler::SetBootStrategy(const std::vector< ::BOOT_PARAM_T> & lstcBootParams, const std::vector<std::string> & lstMAC) {
    uint32_t i = 0;

    LOG("SetBootStrategy, boot nums:%d, mac nums:%d\n", lstcBootParams.size(), lstMAC.size());

    pc_macaddr_del_all();
    for (i=0; i<lstMAC.size(); i++) {
        if (i >= PC_MAC_COUNT_MAX) {
            LOG("PC MAC param is too more,drop remain!\n");
            break;
        }
        pc_macaddr_update(i, lstMAC[i].c_str());
        LOG("idx:%d, PC MAC:%s\n", i, lstMAC[i].c_str());
    }

    soft_power_param_del_all();
    for (i=0; i<lstcBootParams.size(); i++) { 
        soft_power_param_add(lstcBootParams[i].iWeek, lstcBootParams[i].iHour, 
            lstcBootParams[i].iMinute, lstcBootParams[i].bEnable);
        
        LOG("idx:%d, boot param:iWeek:%d, iHour:%d, iMin:%d, iType:%d\n", i, 
            lstcBootParams[i].iWeek, lstcBootParams[i].iHour, 
            lstcBootParams[i].iMinute, lstcBootParams[i].bEnable);
    }
    soft_power_param_update_file();
    
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::HeartbeatAsync(const int32_t iUserData) {
    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));
    *userData = iUserData;

    work_queue_add(heart_beart_async, userData);
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::ThriftMotorParaSet(const  ::THRIFT_MOTOR_PARA_T& tMotorPara) {
    thrift_motor_para_t motor_para = {0};

    motor_para.speed = tMotorPara.iSpeed;
    motor_para.acc = tMotorPara.iAcc;
    return thrift_motor_para_set(tMotorPara.iMotorID, &motor_para) == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}

void HIOtherHandler::ThriftMotorParaGet( ::THRIFT_MOTOR_PARA_T& _return, const int32_t iMotorID) {
    thrift_motor_para_t motor_para = {0};

    if (-1 == thrift_motor_para_get(iMotorID, &motor_para)) {
        return;
    }
    
    _return.iMotorID = iMotorID;
    _return.iSpeed = motor_para.speed;
    _return.iAcc = motor_para.acc;
}

::EXE_STATE::type HIOtherHandler::ThriftMotorPosSet(const int32_t iMotorID, const int32_t iPos, const int32_t iStep) {
    return thrift_motor_pos_set(iMotorID, iPos, iStep) == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}

void HIOtherHandler::ThriftMotorPosGet(std::vector<int32_t> & _return, const int32_t iMotorID) {
    int poses[MAX_MOTOR_POS_NUM];
    int count = 0;
    int i;
    
    if (-1 == (count = thrift_motor_pos_get(iMotorID, poses, MAX_MOTOR_POS_NUM))) {
        return;
    }

    for (i = 0; i < count; i++) {
        _return.push_back(poses[i]);
    }
}

::EXE_STATE::type HIOtherHandler::ThriftMotorReset(const int32_t iMotorID, const int32_t iIsFirst) {
    return thrift_motor_reset(iMotorID, iIsFirst) == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}

::EXE_STATE::type HIOtherHandler::ThriftMotorMove(const int32_t iMotorID, const int32_t iStep) {
    return thrift_motor_move(iMotorID, iStep) == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}

::EXE_STATE::type HIOtherHandler::ThriftMotorMoveTo(const int32_t iMotorID, const int32_t iStep) {
    return thrift_motor_move_to(iMotorID, iStep) == 0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}

void HIOtherHandler::ThriftReadBarcode(std::string& _return, const int32_t iReaderID) {
    char barcode[SCANNER_BARCODE_LENGTH] = {0};
    
    if (-1 == thrift_read_barcode(iReaderID, barcode, sizeof(barcode))) {
        return;
    }

    _return = barcode;
}

int32_t HIOtherHandler::ThriftLiquidDetect(const int32_t iNeedleID) {
    LOG("Thrift liquid detect start, needle = %d.\n", iNeedleID);
    return thrift_liquid_detect(iNeedleID);
}

::EXE_STATE::type HIOtherHandler::RotatingReagentBin(const int32_t iReagentPos) {
    int ret = -1;

    ret = reagent_switch_rotate_ctl(iReagentPos, MODE_THRIFT);
    return ret==0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL;
}


::EXE_STATE::type HIOtherHandler::ThriftRackMoveIn() {
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::ThriftRackMoveOutHorizontal() {
    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::EngineerDebugPosSet(const  ::ENGINEER_DEBUG_MODULE_PARA_T& tModulePara) {
    struct ENGINEER_DEBUG_MODULE_PARA_TT engineer_module_param = {0};
    int ret = 0;

    LOG("tVirautlPosPara iModuleIndex:%d, iVirtualPosIndex:%d, enable rxyz:%d,%d,%d,%d step rxyz:%d,%d,%d,%d\n",
        tModulePara.tVirautlPosPara.iModuleIndex, tModulePara.tVirautlPosPara.iVirtualPosIndex, 
        tModulePara.tVirautlPosPara.iEnableR, tModulePara.tVirautlPosPara.iEnableX, tModulePara.tVirautlPosPara.iEnableY, tModulePara.tVirautlPosPara.iEnableZ,
        tModulePara.tVirautlPosPara.iR_Steps, tModulePara.tVirautlPosPara.iX_Steps, tModulePara.tVirautlPosPara.iY_Steps, tModulePara.tVirautlPosPara.iZ_Steps);

    LOG("iIsExistRelativeViraultPosPara:%d, iModuleIndex:%d, iVirtualPosIndex:%d, enable rxyz:%d,%d,%d,%d step rxyz:%d,%d,%d,%d\n",
        tModulePara.iIsExistRelativeViraultPosPara, tModulePara.tRelativeVirautlPosPara.iModuleIndex, tModulePara.tRelativeVirautlPosPara.iVirtualPosIndex, 
        tModulePara.tRelativeVirautlPosPara.iEnableR, tModulePara.tRelativeVirautlPosPara.iEnableX, tModulePara.tRelativeVirautlPosPara.iEnableY, tModulePara.tRelativeVirautlPosPara.iEnableZ,
        tModulePara.tRelativeVirautlPosPara.iR_Steps, tModulePara.tRelativeVirautlPosPara.iX_Steps, tModulePara.tRelativeVirautlPosPara.iY_Steps, tModulePara.tRelativeVirautlPosPara.iZ_Steps);

    /* 自身组件位置数据 */
    engineer_module_param.tVirautlPosPara.iModuleIndex = tModulePara.tVirautlPosPara.iModuleIndex;
    engineer_module_param.tVirautlPosPara.iVirtualPosIndex = tModulePara.tVirautlPosPara.iVirtualPosIndex;

    engineer_module_param.tVirautlPosPara.iEnableR = tModulePara.tVirautlPosPara.iEnableR;
    engineer_module_param.tVirautlPosPara.iEnableX = tModulePara.tVirautlPosPara.iEnableX;
    engineer_module_param.tVirautlPosPara.iEnableY = tModulePara.tVirautlPosPara.iEnableY;
    engineer_module_param.tVirautlPosPara.iEnableZ = tModulePara.tVirautlPosPara.iEnableZ;

    engineer_module_param.tVirautlPosPara.iR_Steps = tModulePara.tVirautlPosPara.iR_Steps;
    engineer_module_param.tVirautlPosPara.iX_Steps = tModulePara.tVirautlPosPara.iX_Steps;
    engineer_module_param.tVirautlPosPara.iY_Steps = tModulePara.tVirautlPosPara.iY_Steps;
    engineer_module_param.tVirautlPosPara.iZ_Steps = tModulePara.tVirautlPosPara.iZ_Steps;

    /* 关联组件位置数据 */
    engineer_module_param.iIsExistRelativeViraultPosPara = tModulePara.iIsExistRelativeViraultPosPara;

    engineer_module_param.tRelativeVirautlPosPara.iModuleIndex = tModulePara.tRelativeVirautlPosPara.iModuleIndex;
    engineer_module_param.tRelativeVirautlPosPara.iVirtualPosIndex = tModulePara.tRelativeVirautlPosPara.iVirtualPosIndex;

    engineer_module_param.tRelativeVirautlPosPara.iEnableR = tModulePara.tRelativeVirautlPosPara.iEnableR;
    engineer_module_param.tRelativeVirautlPosPara.iEnableX = tModulePara.tRelativeVirautlPosPara.iEnableX;
    engineer_module_param.tRelativeVirautlPosPara.iEnableY = tModulePara.tRelativeVirautlPosPara.iEnableY;
    engineer_module_param.tRelativeVirautlPosPara.iEnableZ = tModulePara.tRelativeVirautlPosPara.iEnableZ;

    engineer_module_param.tRelativeVirautlPosPara.iR_Steps = tModulePara.tRelativeVirautlPosPara.iR_Steps;
    engineer_module_param.tRelativeVirautlPosPara.iX_Steps = tModulePara.tRelativeVirautlPosPara.iX_Steps;
    engineer_module_param.tRelativeVirautlPosPara.iY_Steps = tModulePara.tRelativeVirautlPosPara.iY_Steps;
    engineer_module_param.tRelativeVirautlPosPara.iZ_Steps = tModulePara.tRelativeVirautlPosPara.iZ_Steps;

    ret = engineer_debug_pos_set(&engineer_module_param);
    if (ret == 0) {
        return EXE_STATE::SUCCESS;
    } else {
        return EXE_STATE::FAIL;
    }
}

void HIOtherHandler::EngineerDebugGetVirtualPosition(std::vector< ::ENGINEER_DEBUG_VIRTUAL_POSITION_T> & _return) {
    int idx = 0, idy = 0, id = 0, count = 0, motor = 0;
    struct ENGINEER_DEBUG_MODULE_PARA_TT *module_param = NULL;

    LOG("EngineerDebugGetVirtualPosition\n");
    read_old_conf();
    count = engineer_debug_pos_all_count();
    _return.resize(count);

    for (idx = ENG_DEBUG_NEEDLE_S; idx <= ENG_DEBUG_TABLE1; idx++) {
        count = engineer_debug_pos_count(idx);
        LOG("module_id = %d, count = %d.\n", idx, count);
        module_param = (struct ENGINEER_DEBUG_MODULE_PARA_TT *)calloc(1, count*sizeof(struct ENGINEER_DEBUG_MODULE_PARA_TT));
        engineer_debug_pos_get(idx, count, module_param);

        for (idy = 0; idy < count; idy++) {
            if (idx == ENG_DEBUG_NEEDLE_S && idy == 0) {
                id = 0;
            }
            LOG("report id = %d.\n", id);
            /* 旧位置 */
            _return[id].tOldVirautlPosPara.iModuleIndex = module_param[idy].tVirautlPosPara.iModuleIndex;
            _return[id].tOldVirautlPosPara.iVirtualPosIndex = module_param[idy].tVirautlPosPara.iVirtualPosIndex;
            _return[id].tOldVirautlPosPara.strVirtualPosName = module_param[idy].tVirautlPosPara.strVirtualPosName ? module_param[idy].tVirautlPosPara.strVirtualPosName : "";
            _return[id].tOldVirautlPosPara.iEnableR = module_param[idy].tVirautlPosPara.iEnableR;
            if (idx == ENG_DEBUG_NEEDLE_S && idy == 6) {
                /* 样本针试剂仓内 */
                _return[id].tOldVirautlPosPara.iR_Steps = h3600_old_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN];
            } else if (idx == ENG_DEBUG_NEEDLE_S && idy == 7) {
                /* 样本针试剂仓外 */
                _return[id].tOldVirautlPosPara.iR_Steps = h3600_old_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT];
            } else if (idx == ENG_DEBUG_NEEDLE_R2 && idy == 0) {
                /* 试剂针试剂仓内 */
                _return[id].tOldVirautlPosPara.iR_Steps = h3600_old_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN];
            } else if (idx == ENG_DEBUG_NEEDLE_R2 && idy == 1) {
                /* 试剂针试剂仓外 */
                _return[id].tOldVirautlPosPara.iR_Steps = h3600_old_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT];
            } else{
                _return[id].tOldVirautlPosPara.iR_Steps = module_param[idy].tVirautlPosPara.iR_Steps;
            }
            _return[id].tOldVirautlPosPara.iR_MaxSteps = module_param[idy].tVirautlPosPara.iR_MaxSteps;
            _return[id].tOldVirautlPosPara.iEnableX = module_param[idy].tVirautlPosPara.iEnableX;
            if (idx == ENG_DEBUG_NEEDLE_R2 || idx == ENG_DEBUG_TABLE1) {
                _return[id].tOldVirautlPosPara.iX_Steps = module_param[idy].tVirautlPosPara.iX_Steps;
            } else {
                if (idx == ENG_DEBUG_NEEDLE_S) {
                    motor = MOTOR_NEEDLE_S_X;
                } else if (idx == ENG_DEBUG_GRIP1) {
                    motor = MOTOR_CATCHER_X;
                }
                _return[id].tOldVirautlPosPara.iX_Steps = h3600_old_conf_get()->motor_pos[motor][idy];
            }
            _return[id].tOldVirautlPosPara.iX_MaxSteps = module_param[idy].tVirautlPosPara.iX_MaxSteps;
            _return[id].tOldVirautlPosPara.iEnableY = module_param[idy].tVirautlPosPara.iEnableY;
            if (idx == ENG_DEBUG_TABLE1) {
                _return[id].tOldVirautlPosPara.iY_Steps = module_param[idy].tVirautlPosPara.iY_Steps;
            } else {
                if (idx == ENG_DEBUG_NEEDLE_S) {
                    motor = MOTOR_NEEDLE_S_Y;
                } else if (idx == ENG_DEBUG_GRIP1) {
                    motor = MOTOR_CATCHER_Y;
                } else if (idx == ENG_DEBUG_NEEDLE_R2) {
                    motor = MOTOR_NEEDLE_R2_Y;
                }
                _return[id].tOldVirautlPosPara.iY_Steps = h3600_old_conf_get()->motor_pos[motor][idy];
            }
            _return[id].tOldVirautlPosPara.iY_MaxSteps = module_param[idy].tVirautlPosPara.iY_MaxSteps;
            _return[id].tOldVirautlPosPara.iEnableZ = module_param[idy].tVirautlPosPara.iEnableZ;
            if (idx == ENG_DEBUG_TABLE1) {
                _return[id].tOldVirautlPosPara.iZ_Steps = module_param[idy].tVirautlPosPara.iZ_Steps;
            } else {
                if (idx == ENG_DEBUG_NEEDLE_S) {
                    motor = MOTOR_NEEDLE_S_Z;
                } else if (idx == ENG_DEBUG_GRIP1) {
                    motor = MOTOR_CATCHER_Z;
                } else if (idx == ENG_DEBUG_NEEDLE_R2) {
                    motor = MOTOR_NEEDLE_R2_Z;
                }
                _return[id].tOldVirautlPosPara.iZ_Steps = h3600_old_conf_get()->motor_pos[motor][idy];
            }
            _return[id].tOldVirautlPosPara.iZ_MaxSteps = module_param[idy].tVirautlPosPara.iZ_MaxSteps;

            /* 新位置 */
            _return[id].tCurVirautlPosPara.iModuleIndex = module_param[idy].tVirautlPosPara.iModuleIndex;
            _return[id].tCurVirautlPosPara.iVirtualPosIndex = module_param[idy].tVirautlPosPara.iVirtualPosIndex;
            _return[id].tCurVirautlPosPara.strVirtualPosName = module_param[idy].tVirautlPosPara.strVirtualPosName ? module_param[idy].tVirautlPosPara.strVirtualPosName : "";
            _return[id].tCurVirautlPosPara.iEnableR = module_param[idy].tVirautlPosPara.iEnableR;
            if (idx == ENG_DEBUG_NEEDLE_S && idy == 6) {
                /* 样本针试剂仓内 */
                _return[id].tCurVirautlPosPara.iR_Steps = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN];
            } else if (idx == ENG_DEBUG_NEEDLE_S && idy == 7) {
                /* 样本针试剂仓外 */
                _return[id].tCurVirautlPosPara.iR_Steps = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT];
            } else if (idx == ENG_DEBUG_NEEDLE_R2 && idy == 0) {
                /* 试剂针试剂仓内 */
                _return[id].tCurVirautlPosPara.iR_Steps = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN];
            } else if (idx == ENG_DEBUG_NEEDLE_R2 && idy == 1) {
                /* 试剂针试剂仓外 */
                _return[id].tCurVirautlPosPara.iR_Steps = h3600_conf_get()->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT];
            } else{
                _return[id].tCurVirautlPosPara.iR_Steps = module_param[idy].tVirautlPosPara.iR_Steps;
            }
            _return[id].tCurVirautlPosPara.iR_MaxSteps = module_param[idy].tVirautlPosPara.iR_MaxSteps;
            _return[id].tCurVirautlPosPara.iEnableX = module_param[idy].tVirautlPosPara.iEnableX;
            if (idx == ENG_DEBUG_NEEDLE_R2 || idx == ENG_DEBUG_TABLE1) {
                _return[id].tCurVirautlPosPara.iX_Steps = module_param[idy].tVirautlPosPara.iX_Steps;
            } else {
                if (idx == ENG_DEBUG_NEEDLE_S) {
                    motor = MOTOR_NEEDLE_S_X;
                } else if (idx == ENG_DEBUG_GRIP1) {
                    motor = MOTOR_CATCHER_X;
                }
                _return[id].tCurVirautlPosPara.iX_Steps = h3600_conf_get()->motor_pos[motor][idy];
            }
            _return[id].tCurVirautlPosPara.iX_MaxSteps = module_param[idy].tVirautlPosPara.iX_MaxSteps;
            _return[id].tCurVirautlPosPara.iEnableY = module_param[idy].tVirautlPosPara.iEnableY;
            if (idx == ENG_DEBUG_TABLE1) {
                _return[id].tCurVirautlPosPara.iY_Steps = module_param[idy].tVirautlPosPara.iY_Steps;
            } else {
                if (idx == ENG_DEBUG_NEEDLE_S) {
                    motor = MOTOR_NEEDLE_S_Y;
                } else if (idx == ENG_DEBUG_GRIP1) {
                    motor = MOTOR_CATCHER_Y;
                } else if (idx == ENG_DEBUG_NEEDLE_R2) {
                    motor = MOTOR_NEEDLE_R2_Y;
                }
                _return[id].tCurVirautlPosPara.iY_Steps = h3600_conf_get()->motor_pos[motor][idy];
            }
            _return[id].tCurVirautlPosPara.iY_MaxSteps = module_param[idy].tVirautlPosPara.iY_MaxSteps;
            _return[id].tCurVirautlPosPara.iEnableZ = module_param[idy].tVirautlPosPara.iEnableZ;
            if (idx == ENG_DEBUG_TABLE1) {
                _return[id].tCurVirautlPosPara.iZ_Steps = module_param[idy].tVirautlPosPara.iZ_Steps;
            } else {
                if (idx == ENG_DEBUG_NEEDLE_S) {
                    motor = MOTOR_NEEDLE_S_Z;
                } else if (idx == ENG_DEBUG_GRIP1) {
                    motor = MOTOR_CATCHER_Z;
                } else if (idx == ENG_DEBUG_NEEDLE_R2) {
                    motor = MOTOR_NEEDLE_R2_Z;
                }
                _return[id].tCurVirautlPosPara.iZ_Steps = h3600_conf_get()->motor_pos[motor][idy];
            }
            _return[id].tCurVirautlPosPara.iZ_MaxSteps = module_param[idy].tVirautlPosPara.iZ_MaxSteps;
            id++;
        }
        free(module_param);
        module_param = NULL;
    }
}

void HIOtherHandler::EngineerDebugPosGet(std::vector< ::ENGINEER_DEBUG_MODULE_PARA_T> & _return, const int32_t iModuleIndex) {
    int i = 0;
    int count = 0;
    struct ENGINEER_DEBUG_MODULE_PARA_TT *module_param = NULL;

    count = engineer_debug_pos_count(iModuleIndex);
    LOG("EngineerDebugPosGet, iModuleIndex:%d, count:%d\n", iModuleIndex, count);

    _return.resize(count);
    module_param = (struct ENGINEER_DEBUG_MODULE_PARA_TT *)calloc(1, count*sizeof(struct ENGINEER_DEBUG_MODULE_PARA_TT));
    engineer_debug_pos_get(iModuleIndex, count, module_param);

    for (i=0; i<count; i++) {
        /* 自身组件位置数据 */
        _return[i].tVirautlPosPara.iModuleIndex = module_param[i].tVirautlPosPara.iModuleIndex;
        _return[i].tVirautlPosPara.iVirtualPosIndex = module_param[i].tVirautlPosPara.iVirtualPosIndex;
        _return[i].tVirautlPosPara.strVirtualPosName = module_param[i].tVirautlPosPara.strVirtualPosName ? module_param[i].tVirautlPosPara.strVirtualPosName : "";
        _return[i].tVirautlPosPara.iEnableR = module_param[i].tVirautlPosPara.iEnableR;
        _return[i].tVirautlPosPara.iR_Steps = module_param[i].tVirautlPosPara.iR_Steps;
        _return[i].tVirautlPosPara.iR_MaxSteps = module_param[i].tVirautlPosPara.iR_MaxSteps;
        _return[i].tVirautlPosPara.iEnableX = module_param[i].tVirautlPosPara.iEnableX;
        _return[i].tVirautlPosPara.iX_Steps = module_param[i].tVirautlPosPara.iX_Steps;
        _return[i].tVirautlPosPara.iX_MaxSteps = module_param[i].tVirautlPosPara.iX_MaxSteps;
        _return[i].tVirautlPosPara.iEnableY = module_param[i].tVirautlPosPara.iEnableY;
        _return[i].tVirautlPosPara.iY_Steps = module_param[i].tVirautlPosPara.iY_Steps;
        _return[i].tVirautlPosPara.iY_MaxSteps = module_param[i].tVirautlPosPara.iY_MaxSteps;
        _return[i].tVirautlPosPara.iEnableZ = module_param[i].tVirautlPosPara.iEnableZ;
        _return[i].tVirautlPosPara.iZ_Steps = module_param[i].tVirautlPosPara.iZ_Steps;
        _return[i].tVirautlPosPara.iZ_MaxSteps = module_param[i].tVirautlPosPara.iZ_MaxSteps;

        /* 关联组件位置数据 */
        _return[i].iIsExistRelativeViraultPosPara = module_param[i].iIsExistRelativeViraultPosPara;
        _return[i].tRelativeVirautlPosPara.iModuleIndex = module_param[i].tRelativeVirautlPosPara.iModuleIndex;
        _return[i].tRelativeVirautlPosPara.iVirtualPosIndex = module_param[i].tRelativeVirautlPosPara.iVirtualPosIndex;
        _return[i].tRelativeVirautlPosPara.strVirtualPosName = module_param[i].tRelativeVirautlPosPara.strVirtualPosName? module_param[i].tRelativeVirautlPosPara.strVirtualPosName : "";
        _return[i].tRelativeVirautlPosPara.iEnableR = module_param[i].tRelativeVirautlPosPara.iEnableR;
        _return[i].tRelativeVirautlPosPara.iR_Steps = module_param[i].tRelativeVirautlPosPara.iR_Steps;
        _return[i].tRelativeVirautlPosPara.iR_MaxSteps = module_param[i].tRelativeVirautlPosPara.iR_MaxSteps;
        _return[i].tRelativeVirautlPosPara.iEnableX = module_param[i].tRelativeVirautlPosPara.iEnableX;
        _return[i].tRelativeVirautlPosPara.iX_Steps = module_param[i].tRelativeVirautlPosPara.iX_Steps;
        _return[i].tRelativeVirautlPosPara.iX_MaxSteps = module_param[i].tRelativeVirautlPosPara.iX_MaxSteps;
        _return[i].tRelativeVirautlPosPara.iEnableY = module_param[i].tRelativeVirautlPosPara.iEnableY;
        _return[i].tRelativeVirautlPosPara.iY_Steps = module_param[i].tRelativeVirautlPosPara.iY_Steps;
        _return[i].tRelativeVirautlPosPara.iY_MaxSteps = module_param[i].tRelativeVirautlPosPara.iY_MaxSteps;
        _return[i].tRelativeVirautlPosPara.iEnableZ = module_param[i].tRelativeVirautlPosPara.iEnableZ;
        _return[i].tRelativeVirautlPosPara.iZ_Steps = module_param[i].tRelativeVirautlPosPara.iZ_Steps;
        _return[i].tRelativeVirautlPosPara.iZ_MaxSteps = module_param[i].tRelativeVirautlPosPara.iZ_MaxSteps;
    }

    free(module_param);
}

::EXE_STATE::type HIOtherHandler::EngineerDebugMotorActionExecuteAsync(const  ::ENGINEER_DEBUG_MOTOR_PARA_T& tMotorPara, const int32_t iUserData) {
    struct ENGINEER_DEBUG_MOTOR_PARA_TT *engineer_motor_param = NULL;

    LOG("iModuleIndex:%d, iVirtualPosIndex:%d, eAxisType:%d, eActionType:%d, iSteps:%d, iUserData:%d\n",
        tMotorPara.iModuleIndex, tMotorPara.iVirtualPosIndex, tMotorPara.eAxisType, tMotorPara.eActionType, tMotorPara.iSteps, iUserData);

    engineer_motor_param = (struct ENGINEER_DEBUG_MOTOR_PARA_TT *)calloc(1, sizeof(struct ENGINEER_DEBUG_MOTOR_PARA_TT));
    engineer_motor_param->iModuleIndex = tMotorPara.iModuleIndex;
    engineer_motor_param->iVirtualPosIndex = tMotorPara.iVirtualPosIndex;
    engineer_motor_param->eAxisType = (enum ENGINEER_DEBUG_AXIS_TYPE_TT)tMotorPara.eAxisType;
    engineer_motor_param->eActionType = (enum ENGINEER_DEBUG_ACTION_TYPE_TT)tMotorPara.eActionType;
    engineer_motor_param->iSteps = tMotorPara.iSteps;
    engineer_motor_param->userdata = iUserData;
    work_queue_add(engineer_position_async_handler, engineer_motor_param);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::EngineerDebugWeighingAsync(const int32_t iNeedType, const int32_t iSampleOrReagentVol, const int32_t iDiulentVol, const int32_t iCups, const int32_t iUserData) {
    LOG("needle_type:%d, sam_reag_vol:%d, dilu_vol:%d, cups:%d, userdata:%d\n", iNeedType, iSampleOrReagentVol, iDiulentVol, iCups, iUserData);

    engineer_weighing_param_t *engineer_weighing_param = NULL;
    engineer_weighing_param = (engineer_weighing_param_t *)calloc(1, sizeof(engineer_weighing_param_t));
    engineer_weighing_param->needle_type = iNeedType;
    engineer_weighing_param->sample_reagent_vol = iSampleOrReagentVol;
    engineer_weighing_param->diulent_vol = iDiulentVol;
    engineer_weighing_param->cups = iCups;
    engineer_weighing_param->userdata = iUserData;
    work_queue_add(engineer_weighing_async_handler, engineer_weighing_param);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::EngineerDebugInjectorKBSet(const  ::ENGINEER_DEBUG_INJECTOR_KB_T& tInjectorKB) {
    int type = 0;
    double k_value = 0.0;
    double b_value = 0.0;

    type = tInjectorKB.iInjectID;
    k_value = tInjectorKB.dK;
    b_value = tInjectorKB.dB;

    LOG("needle type:%d, k:%f, b:%f\n", type, k_value, b_value);
    LOG("needle type:%d, k:%f, b:%f\n", type, tInjectorKB.dK, tInjectorKB.dB);   
    set_kb_value(type, k_value, b_value);

    return EXE_STATE::SUCCESS;
}

void HIOtherHandler::EngineerDebugInjectorKBGet(std::vector< ::ENGINEER_DEBUG_INJECTOR_KB_T> & _return) {
    const char *type_name[EDI_ADDINGWITH_MAX] = {
        "样本针常规加样(5ul)", "样本针常规加样(10ul)", "样本针常规加样(15ul)", "样本针常规加样(25ul)",
        "样本针常规加样(50ul)","样本针常规加样(100ul)", "样本针常规加样(150ul)", "样本针常规加样(200ul)",
        "样本针稀释吸样(5ul)", "样本针稀释吸样(10ul)", "样本针稀释吸样(15ul)",
        "样本针稀释吸样(25ul)", "样本针稀释吸样(50ul)",
        "样本针稀释吐样(15ul)", "样本针稀释吐样(20ul)", "样本针稀释吐样(50ul)",
        "样本针稀释吐样(100ul)", "样本针稀释吐样(150ul)", "样本针稀释吐样(200ul)",
        "试剂针常规加样(20ul)", "试剂针常规加样(50ul)", "试剂针常规加样(100ul)",
        "试剂针常规加样(150ul)", "试剂针常规加样(200ul)"
    };
    double k_value = 0.0;
    double b_value = 0.0;
    uint32_t i = 0;
    char dst_name[128] = {0};

    _return.resize(EDI_ADDINGWITH_MAX);
    LOG("size:%d\n", _return.size());
    for (i=0; i<EDI_ADDINGWITH_MAX; i++) {
        k_value = 0.0;
        b_value = 0.0;

        _return[i].iInjectID = i;
        utf8togb2312(type_name[i], strlen(type_name[i]), dst_name, sizeof(dst_name)/sizeof(dst_name[0]));
        _return[i].strInjectName = dst_name;
        
        get_kb_value(i, &k_value, &b_value); 
        _return[i].dK = k_value;
        _return[i].dB = b_value;

        LOG("needle type:%d, k:%f, b:%f\n", i, k_value, b_value);
    }
}

::EXE_STATE::type HIOtherHandler::EngineerDebugRunAsync(const int32_t iModuleIndex, const int32_t iCmd, const int32_t iUserData) {
    struct ENGINEER_DEBUG_CMD_TT *engineer_cmd_param = NULL;
    LOG("iModuleIndex:%d, iCmd:%d, iUserData:%d\n", iModuleIndex, iCmd, iUserData);

    engineer_cmd_param = (struct ENGINEER_DEBUG_CMD_TT *)calloc(1, sizeof(struct ENGINEER_DEBUG_CMD_TT));
    engineer_cmd_param->iModuleIndex = iModuleIndex;
    engineer_cmd_param->iCmd = iCmd;
    engineer_cmd_param->userdata = iUserData;
    work_queue_add(engineer_run_cmd_async_handler, engineer_cmd_param);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::ThriftConfigPara(const  ::THRIFT_CONFIG_T& tThriftConfig, const int32_t iUserData) {
    LOG("master_server ip:port{%s:%d}, slave_server ip:port{%s:%d}\n", 
        tThriftConfig.strHostIP.c_str(), tThriftConfig.iHostPort, tThriftConfig.strSlaveIP.c_str(), tThriftConfig.iSlavePort);

    thrift_config_param_t *param = (thrift_config_param_t *)calloc(1, sizeof(thrift_config_param_t));

    memcpy(param->master_server.ip, tThriftConfig.strHostIP.c_str(), tThriftConfig.strHostIP.length());
    param->master_server.port = tThriftConfig.iHostPort;

    memcpy(param->slave_server.ip, tThriftConfig.strSlaveIP.c_str(), tThriftConfig.strSlaveIP.length());
    param->slave_server.port = tThriftConfig.iSlavePort;

    param->pierce_enable = tThriftConfig.iIsPunctureNeedle;
    param->userdata = iUserData;

    work_queue_add(config_para_handler, param);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::SetTimeOut(const int32_t iType, const int32_t iSeconds) {
    LOG("SetTimeOut. type:%d, time:%d\n", iType, iSeconds);

    switch (iType) {
    case 1:
        reagent_gate_timeout_set(iSeconds);
        break;
    default:
        LOG("not support type\n");
        break;
    }

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::GetUploadBackupFile(const int32_t iFileType, const int32_t iRandNo) {
    int32_t *param = (int32_t *)calloc(1, 2*sizeof(int32_t));

    LOG("iFileType:%d, iRandNo:%d\n", iFileType, iRandNo);
    param[0] = iFileType;
    param[1] = iRandNo;

    work_queue_add(get_upload_backup_handler, param);

    return EXE_STATE::SUCCESS;
}

::EXE_STATE::type HIOtherHandler::RestoreConfigFile(const std::string& strFileName, const int32_t iFileType, const std::string& hexConfigFile, const std::string& strMD5) {
    int ret = -1;
    char cmd_buff[256] = {0};
    char md5_buff[64] = {0};
    char data_buff[SEND_DATA_SIZE] = {0};
    char pcTime[128] = {0};
    time_t t = 0;
    FILE *fp = NULL;
    FILE *fp_md5 = NULL;
    json_error_t jerror = {0};
    json_t *root = NULL;

    LOG("strFileName:%s, iFileType:%d, strMD5:%s\n", strFileName.c_str(), iFileType, strMD5.c_str());
    memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
    sprintf(cmd_buff, "mkdir -p %s", H3600_CONF_RECV_DIR);
    system(cmd_buff);

    memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
    sprintf(cmd_buff, "mkdir -p %s", H3600_CONF_BACK_DIR);
    system(cmd_buff);

    if (iFileType == SFT_SLAVE_LOG_FILE) {
        ret = 0;
    } else if (iFileType == SFT_SLAVE_CONFIG_FILE) {
        /* 清空recv文件夹 */
        memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
        sprintf(cmd_buff, "cd %s && rm -rf *", H3600_CONF_RECV_DIR);
        system(cmd_buff);

        fp = fopen(H3600_CONF_RECV_DIR H3600_CONF_TAR_FILE, "wb");
        if (fp == NULL) {
            LOG("open %s file\n", H3600_CONF_RECV_DIR H3600_CONF_TAR_FILE);
            ret = -1;
        } else {
            fwrite(hexConfigFile.c_str(), 1, hexConfigFile.length(), fp);
            fflush(fp);

            /* 获取并校验md5 */
            memset(md5_buff, 0, sizeof(md5_buff)/sizeof(md5_buff[0]));
            get_file_md5(H3600_CONF_RECV_DIR H3600_CONF_TAR_FILE, md5_buff);
            if (strcmp(md5_buff, strMD5.c_str()) == 0) {
                LOG("tar md5 is equal\n");

                /* 解压缩并校验md5 */
                memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
                sprintf(cmd_buff, "cd %s && tar -xvf %s", H3600_CONF_RECV_DIR, H3600_CONF_TAR_FILE);
                system(cmd_buff);

                /* 校验json合法性 */
                root = json_load_file(H3600_CONF_RECV_DIR H3600_CONF_FILE, 0, &jerror);
                if (root == NULL) {
                    LOG("json_load_file() fail, %s\n", jerror.text);
                    ret = -1;
                } else {
                    json_decref(root);
  
                    /* 获取并校验md5 */
                    memset(md5_buff, 0, sizeof(md5_buff)/sizeof(md5_buff[0]));
                    get_file_md5(H3600_CONF_RECV_DIR H3600_CONF_FILE, md5_buff);
                    
                    /* 读取收到的md5文件内容 */        
                    fp_md5 = fopen(H3600_CONF_RECV_DIR H3600_CONF_MD5_FILE, "r");
                    if (fp_md5 == NULL) {
                        LOG("open %s file\n", H3600_CONF_RECV_DIR H3600_CONF_MD5_FILE);
                        ret = -1;
                    } else {
                        memset(data_buff, 0, sizeof(data_buff)/sizeof(data_buff[0]));
                        fread(data_buff, 1, 64, fp_md5);
                        if (strcmp(md5_buff, data_buff) == 0) {
                            LOG("conf md5 is equal\n");
                            /* 比较 接收配置文件与本地配置文件的差异 */
                            memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
                            sprintf(cmd_buff, "diff %s %s > %s", H3600_CONF_DIR H3600_CONF_FILE, H3600_CONF_RECV_DIR H3600_CONF_FILE, H3600_CONF_RECV_DIFF_FILE);
                            system(cmd_buff);
                            /* back目录保留最新的6个文件 */
                            memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
                            sprintf(cmd_buff, "cd %s && ls -t | sed -n '6,$p' | xargs -I {} rm  -rf {}", H3600_CONF_BACK_DIR);
                            system(cmd_buff);
                            /* 备份本地配置文件 */
                            t = time(NULL);
                            strftime(pcTime, sizeof(pcTime)/sizeof(pcTime[0]), "%Y-%m-%d_%H:%M:%S", localtime(&t));
                            memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
                            sprintf(cmd_buff, "cp -r %s %s.%s", H3600_CONF_DIR H3600_CONF_FILE, H3600_CONF_BACK_DIR H3600_CONF_FILE, pcTime);
                            system(cmd_buff);
                            /* 使用接收配置文件   覆盖本地配置文件 */
                            memset(cmd_buff, 0, sizeof(cmd_buff)/sizeof(cmd_buff[0]));
                            sprintf(cmd_buff, "cp -r %s %s", H3600_CONF_RECV_DIR H3600_CONF_FILE, H3600_CONF_DIR H3600_CONF_FILE);
                            system(cmd_buff);
                            /* 重启使新配置文件生效（需要给与回复上位机的时间，因此异步） */
                            work_queue_add(user_reboot_handler, NULL);
                            ret = 0;
                        } else {
                            LOG("conf md5 is not equal\n");
                            ret = -1;
                        }
                        fclose(fp_md5);
                    }
                }
            } else {
                LOG("tar md5 is not equal\n");
                ret = -1;
            }

            fclose(fp);
        }
    }

    return (ret==0 ? EXE_STATE::SUCCESS : EXE_STATE::FAIL);
}

static void core_upgrade_handler(void *arg)
{
    char fpga_up[] = "cd /root/maccura/h_etc && sh rbf_update_scri.sh";

    sync();
    sleep(3);
    system(fpga_up);

    LOG("core_upgrade reboot!\n");
    sleep(3);
    system("reboot&");
}

static void convert_img_file_from_upper(void *arg)
{
    upgrade_param_t *firm_file = (upgrade_param_t *)arg;
    async_return_t async_return;
    int ret = 0;
    char cmd[256] = {0};
    char md5_buff[64] = {0};
    FILE *fp = NULL;

    /* 进入工作目录，并打开、更新临时文件 */
    memset(cmd, 0, sizeof(cmd) / sizeof(cmd[0]));
    sprintf(cmd, "rm -rf %s/*", FIRMWARE_PATH);
    system(cmd);

    memset(cmd, 0, sizeof(cmd) / sizeof(cmd[0]));
    sprintf(cmd, "%s/%s", FIRMWARE_PATH, FIRMWARE_PACKET);

    fp = fopen(cmd, "wb+");
    if (NULL == fp) {
        LOG("upgrade file create or write fail.\n");
        goto out;
    }

    if (firm_file->program_len != fwrite(firm_file->program_file, 1, firm_file->program_len, fp)) {
        LOG("upgrade file write fail.\n");
        goto out;
    }
    fflush(fp);

    /* 获取并校验md5 */
    memset(md5_buff, 0, sizeof(md5_buff)/sizeof(md5_buff[0]));
    memset(cmd, 0, sizeof(cmd) / sizeof(cmd[0]));
    sprintf(cmd, "%s/%s", FIRMWARE_PATH, FIRMWARE_PACKET);
    get_file_md5(cmd, md5_buff);

    LOG("md5_buff:%s\n", md5_buff);
    LOG("firm_file->str_MD5:%s\n", firm_file->str_MD5);
    if (0 == strncmp(md5_buff, firm_file->str_MD5, MD5_LEN)) { /* 校验成功 */
        LOG("file check success! upgrade ready...");
        usleep(500 * 1000);

        /* 升级下发 */
        ret = upgrade_all_firmware(FIRMWARE_PATH);
    }

out:
    snprintf(async_return.return_string, \
             sizeof(async_return.return_string), \
             "%s->%s", \
             find_version_tbl_item(firm_file->program_no, 0)->old_version, \
             find_version_tbl_item(firm_file->program_no, 0)->new_version);
    LOG("%d++++ %s\n", firm_file->program_no, async_return.return_string);

    async_return.return_type = RETURN_STRING;
    report_asnyc_invoke_result(firm_file->userdata, ret == 0 ? 0 : 1, &async_return);

    /* 主控板程序，重启使新升级文件（需要给与回复上位机的时间，因此异步） */
    if ((0 == ret) && (1 == firm_file->program_no || 2 == firm_file->program_no || 13 == firm_file->program_no)) {
        work_queue_add(core_upgrade_handler, NULL);
    }
    free(arg);
}

::EXE_STATE::type HIOtherHandler::UpgradeSlaveProgramAsync(const  ::SLAVE_PROGRAM_T& tSlaveprogram, const int32_t iUserData) { 
    upgrade_param_t *gro_file = NULL;

    gro_file = (upgrade_param_t *)calloc(1, sizeof(upgrade_param_t));

    memset(gro_file, 0, sizeof(upgrade_param_t));

    gro_file->program_no = tSlaveprogram.iSlaveProgramNo;
    memcpy(gro_file->program_file_name, tSlaveprogram.strSlaveProgramFileName.c_str(), tSlaveprogram.strSlaveProgramFileName.size());
    memcpy(gro_file->program_file, tSlaveprogram.hexSlaveProgram.c_str(),tSlaveprogram.hexSlaveProgram.size());
    memcpy(gro_file->str_MD5, tSlaveprogram.strMD5.c_str(),tSlaveprogram.strMD5.size());
    gro_file->program_len = tSlaveprogram.iHexSlaveProgramLen;
    gro_file->userdata = iUserData;

    upgrade_id_get(gro_file->program_no);
    LOG("iSlaveProgramNo:%d\n", gro_file->program_no);
    LOG("strSlaveProgramFileName:%s\n", gro_file->program_file_name);
    LOG("strMD5:%s\n", gro_file->str_MD5);
    LOG("iHexSlaveProgramLen:%d\n", gro_file->program_len);
    work_queue_add(convert_img_file_from_upper, gro_file);

    return EXE_STATE::SUCCESS;
}

// 获取检测通道是否禁用信息
void HIOtherHandler::GetChannelStatus(std::vector< ::CHANNEL_STATUS_T> & _return) {
    int i = 0;

    LOG("GetChannelStatus\n");
    _return.resize(OPTICAL_MAX_WORK_STATIONS + MAGNETIC_CH_NUMBER);

    /* 光学通道 禁用情况 */
    for (i=0; i<OPTICAL_MAX_WORK_STATIONS; i++) {
        _return[i].iChannelType = 1;
        _return[i].iChannelNo = i;
        _return[i].iDisable = thrift_optical_pos_disable_get(i);;
        LOG("iChannelType:%d, iChannelNo:%d, iDisable:%d\n", _return[i].iChannelType, _return[i].iChannelNo, _return[i].iDisable);
    }

    /* 磁珠通道 禁用情况 */
    for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
        _return[i+OPTICAL_MAX_WORK_STATIONS].iChannelType = 0;
        _return[i+OPTICAL_MAX_WORK_STATIONS].iChannelNo = i;
        _return[i+OPTICAL_MAX_WORK_STATIONS].iDisable = thrift_mag_pos_disable_get(i);
        LOG("iChannelType:%d, iChannelNo:%d, iDisable:%d\n", 
            _return[i+OPTICAL_MAX_WORK_STATIONS].iChannelType, _return[i+OPTICAL_MAX_WORK_STATIONS].iChannelNo, _return[i+OPTICAL_MAX_WORK_STATIONS].iDisable);
    }
}

// 设置检测通道是否禁用信息
::EXE_STATE::type HIOtherHandler::SetChannelStatus(const std::vector< ::CHANNEL_STATUS_T> & lstChannelStatus) {
    uint32_t i = 0;

    LOG("SetChannelStatus, size:%d\n", lstChannelStatus.size());
    for (i=0; i<lstChannelStatus.size(); i++) {
        LOG("idx:%d, type:%d, ch:%d, disable:%d\n", i, lstChannelStatus[i].iChannelType, lstChannelStatus[i].iChannelNo, lstChannelStatus[i].iDisable);
        if (lstChannelStatus[i].iChannelType == 0) { /* 设置磁珠通道 */
            thrift_mag_pos_disable_set(lstChannelStatus[i].iChannelNo, lstChannelStatus[i].iDisable ? 1 : 0);
        } else if (lstChannelStatus[i].iChannelType == 1) { /* 设置光学通道 */
            thrift_optical_pos_disable_set(lstChannelStatus[i].iChannelNo, lstChannelStatus[i].iDisable ? 1 : 0);
        }
    }

    return EXE_STATE::SUCCESS;
}

// 启动光学通道校准
::EXE_STATE::type HIOtherHandler::StartAdjustChannelAsync(const int32_t iUserData) {
    int32_t *userData = (int32_t *)calloc(1, sizeof(int32_t));

    *userData = iUserData;
    LOG("StartAdjustChannelAsync, userdata:%d\n", iUserData);
    work_queue_add(set_optical_led_handler, userData);

    return EXE_STATE::SUCCESS;
}

// 控制光学LED灯开关，iOnOrOff：1：开，0：关， iWave：0：所有波长 340:340nm 405:405nm 570:570nm 660:660nm 800:800nm
::EXE_STATE::type HIOtherHandler::SetOpticalLED(const int32_t iOnOrOff, const int32_t iWave) {
    LOG("SetOpticalLED, iWave:%d, iOnOrOff:%d\n", iWave, iOnOrOff);

    if (iWave == 0) {
        if (iOnOrOff == 0) {
            slip_optical_set(OPTICAL_1_POWEROFF_AD);
        } else if (iOnOrOff == 1) {
            slip_optical_set(OPTICAL_1_POWERON);
        }
    }

    return EXE_STATE::SUCCESS;
}

// 获取所有通道AD
void HIOtherHandler::GetChannelAD(std::vector< ::CHANNEL_AD_T> & _return) {
    int i = 0;

    LOG("GetChannelAD\n");
    _return.resize(OPTICAL_MAX_WORK_STATIONS + MAGNETIC_CH_NUMBER);

    /* 光学信号值 */
    for (i=0; i<OPTICAL_MAX_WORK_STATIONS; i++) {
        _return[i].iChannelType = 1;
        _return[i].iChannelNo = i;
        _return[i].i340AD = thrift_optical_data_get(OPTICAL_WAVE_340, (optical_pos_t)i);
        _return[i].i405AD = thrift_optical_data_get(OPTICAL_WAVE_405, (optical_pos_t)i);
        _return[i].i570AD = thrift_optical_data_get(OPTICAL_WAVE_570, (optical_pos_t)i);
        _return[i].i660AD = thrift_optical_data_get(OPTICAL_WAVE_660, (optical_pos_t)i);
        _return[i].i800AD = thrift_optical_data_get(OPTICAL_WAVE_800, (optical_pos_t)i);
    }

    /* 磁珠信号值 */
    for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
        _return[i+OPTICAL_MAX_WORK_STATIONS].iChannelType = 0;
        _return[i+OPTICAL_MAX_WORK_STATIONS].iChannelNo = i;
        _return[i+OPTICAL_MAX_WORK_STATIONS].iReserved = thrift_mag_data_get((magnetic_pos_t)i);
    }
}

// 获取所有通道校准值
void HIOtherHandler::GetChannelGain( ::CHANNEL_GAIN_T& _return) {
    LOG("GetChannelGain\n");

    _return.i340Gain = thrift_optical_led_calc_get(OPTICAL_WAVE_340);
    _return.i405Gain = thrift_optical_led_calc_get(OPTICAL_WAVE_405);
    _return.i570Gain = thrift_optical_led_calc_get(OPTICAL_WAVE_570);
    _return.i660Gain = thrift_optical_led_calc_get(OPTICAL_WAVE_660);
    _return.i800Gain = thrift_optical_led_calc_get(OPTICAL_WAVE_800);
}

::EXE_STATE::type HIOtherHandler::EngineerAgingRunAsync(const  ::SLAVE_ASSEMBLY_AGING_PARA_T& tAssemblyAgingPara, const int32_t iUserData) {
    LOG("EngineerAgingRunAsync\n");
    engineer_debug_aging_param_t *engineer_aging_param = (engineer_debug_aging_param_t *)calloc(1, sizeof(engineer_debug_aging_param_t));

    engineer_aging_param->userdata = iUserData;
    engineer_aging_param->aging.enable = tAssemblyAgingPara.iIsOnOrOFF;
    engineer_aging_param->aging.loop_cnt = tAssemblyAgingPara.iLoopCounts;
    engineer_aging_param->aging.sampler_enable = tAssemblyAgingPara.iIsEnableSampler;
    engineer_aging_param->aging.reag_enable = tAssemblyAgingPara.iIsEnableReagentBin;
    engineer_aging_param->aging.catcher_enable = tAssemblyAgingPara.iIsEnableGripperA;
    engineer_aging_param->aging.needle_s_enable = tAssemblyAgingPara.iIsEnableSampleNeedle;
    engineer_aging_param->aging.needle_r2_enable = tAssemblyAgingPara.iIsEnableDetectionReagentNeedle;
    engineer_aging_param->aging.mix_enable = tAssemblyAgingPara.iIsEnableReactionCupMixing;
    engineer_aging_param->aging.cuvette_enable = tAssemblyAgingPara.iIsEnableReactionCupLoad;
    work_queue_add(engineer_aging_async_handler, engineer_aging_param);

    return EXE_STATE::SUCCESS;
}

HIOtherIf *HIOtherHandlerFactory::getHandler(const ::apache::thrift::TConnectionInfo& connInfo)
{
    stdcxx::shared_ptr<TSocket> sock = stdcxx::dynamic_pointer_cast<TSocket>(connInfo.transport);
    cout << "Incoming connection\n";
    cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
    cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
    cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
    cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";
    return new HIOtherHandler;
}

void HIOtherHandlerFactory::releaseHandler(HIOtherIf* handler) {
    delete handler;
}

int32_t HIOtherHandler::GetOtherPara(const int32_t iParaType) {
    int ret = 0;

    if (iParaType == IH_PT_DISCHARGING_WASTE_MODE) {
        ret = get_straight_release_para();
    }
    LOG("GetOtherPara type = %d, val = %d\n", iParaType, ret);

    return ret;
}

::EXE_STATE::type HIOtherHandler::SetOtherPara(const int32_t iParaType, const int32_t iParaVal) {
    LOG("SetOtherPara type = %d, val = %d\n", iParaType, iParaVal);
    if (iParaType == IH_PT_DISCHARGING_WASTE_MODE) {
        set_straight_release_para(iParaVal);
    }

    return EXE_STATE::SUCCESS;
}

static void engineer_auto_cali_async_handler(void *arg)
{
    auto_cali_param_t *engineer_cali_param = (auto_cali_param_t *)arg;
    int ret = 0, err_id = 1;
    async_return_t async_return;

    LOG("auto cali id = %d.\n", engineer_cali_param->i_cali_id);
    switch (engineer_cali_param->i_cali_id) {
        case ENG_DEBUG_NEEDLE: /* 针标定 */
            module_fault_stat_clear();
            auto_cal_stop_flag_set(0);
            ret = thrift_auto_cal_func();
            break;
        case ENG_DEBUG_GRIP: /* 抓手标定 service     */
        case ENG_DEBUG_GRIP1: /* 抓手标定 用户软件工程师调试              */
            module_fault_stat_clear();
            grip_auto_calc_stop_flag_set(0);
            ret = eng_gripper_auto_cali_func(engineer_cali_param->i_cali_id);
            break;
        case ENG_DEBUG_TABLE: /* 试剂仓标定 */
        case ENG_DEBUG_TABLE1:
            ret = reagent_table_scan_pos_auto_calibrate(engineer_cali_param->i_cali_id);
            break;
        case ENG_DEBUG_NEEDLE_S:
        case ENG_DEBUG_NEEDLE_R2:
            module_fault_stat_clear();
            auto_cal_stop_flag_set(0);
            ret = eng_auto_cal_func(engineer_cali_param->i_cali_id);
            break;
        default:
            LOG("no such type!\n");
            break;
    }

    LOG("return module_id = %d, pos_id = %d, err_id = %d.\n", engineer_cali_param->i_cali_id, ret, err_id);
    async_return.return_type = RETURN_STRING;
    snprintf(async_return.return_string, sizeof(async_return.return_string), "%d|%d|%d", engineer_cali_param->i_cali_id, ret, err_id);
    report_asnyc_invoke_result(engineer_cali_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("auto cali done...\n");
}

static void engineer_cali_write_async_handler(void *arg)
{
    auto_cali_param_t *engineer_cali_param = (auto_cali_param_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("auto cali write id = %d.\n", engineer_cali_param->i_cali_id);
    switch (engineer_cali_param->i_cali_id) {
        case ENG_DEBUG_NEEDLE: /* 针标定 */
            auto_cal_reinit_data();
            thrift_engineer_position_set();
            break;
        case ENG_DEBUG_GRIP: /* 抓手标定 service     */
        case ENG_DEBUG_GRIP1: /* 抓手标定 用户软件工程师调试              */
            ret = eng_gripper_write_auto_calc_pos();
            break;
        case ENG_DEBUG_TABLE: /* 试剂仓标定 */
        case ENG_DEBUG_TABLE1:
            ret = reagent_table_set_pos();
            break;
        case ENG_DEBUG_NEEDLE_S:
        case ENG_DEBUG_NEEDLE_R2:
            auto_cal_reinit_data_one(engineer_cali_param->i_cali_id == ENG_DEBUG_NEEDLE_S ? NEEDLE_TYPE_S : NEEDLE_TYPE_R2);
            thrift_engineer_position_set();
            break;
        default:
            LOG("no such type!\n");
            break;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(engineer_cali_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("auto cali write done...\n");
}

static void engineer_cali_stop_async_handler(void *arg)
{
    auto_cali_param_t *engineer_cali_param = (auto_cali_param_t *)arg;
    int ret = 0;
    async_return_t async_return;

    LOG("auto cali stop id = %d.\n", engineer_cali_param->i_cali_id);
    switch (engineer_cali_param->i_cali_id) {
        case ENG_DEBUG_NEEDLE: /* 针标定 */
        case ENG_DEBUG_NEEDLE_S:
        case ENG_DEBUG_NEEDLE_R2:
            auto_cal_stop_flag_set(engineer_cali_param->i_type);
            break;
        case ENG_DEBUG_GRIP: /* 抓手标定 service     */
        case ENG_DEBUG_GRIP1: /* 抓手标定 用户软件工程师调试              */
            grip_auto_calc_stop_flag_set(engineer_cali_param->i_type);
            break;
        case ENG_DEBUG_TABLE: /* 试剂仓标定 */
        case ENG_DEBUG_TABLE1:
            auto_calibrate_stop_set(engineer_cali_param->i_type);
            break;
        default:
            LOG("no such type!\n");
            break;
    }

    async_return.return_type = RETURN_VOID;
    report_asnyc_invoke_result(engineer_cali_param->userdata, ret == 0 ? 0 : 1, &async_return);
    free(arg);

    LOG("auto cali stop done...\n");
}

::EXE_STATE::type HIOtherHandler::EngineerDebugAutoCalibrationAsync(const int32_t iCalibID, const int32_t iType, const int32_t iUserData) {
    auto_cali_param_t *cali = NULL;

    cali = (auto_cali_param_t *)calloc(1, sizeof(auto_cali_param_t));

    LOG("iCalibID:%d iType:%d\n", iCalibID, iType);

    cali->i_cali_id = iCalibID;
    cali->i_type = iType;
    cali->userdata = iUserData;

    switch (cali->i_type) {
        case 0: /* 标定 */
            work_queue_add(engineer_auto_cali_async_handler, cali);
            break;
        case 1: /* 停止 */
            work_queue_add(engineer_cali_stop_async_handler, cali);
            break;
        case 2: /* 写入 */
            work_queue_add(engineer_cali_write_async_handler, cali);
            break;
    }

    return EXE_STATE::SUCCESS;
}

