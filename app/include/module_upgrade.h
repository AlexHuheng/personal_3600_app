#ifndef _UPGRADE_H
#define _UPGRADE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <semaphore.h>
#include <stdint.h>
#include <slip/slip_msg.h>
#include <slip/slip_node.h>

#define APP_PATH                    "/root/maccura/app"
#define FIRMWARE_PATH               "/root/maccura/app/boards-firmware"

#define CORE_PACKET                 "h3600_app"
#define CORE_RTOS_PACKET            "RTOSDemo.bin"
#define CORE_FPGA_PACKET            "fpga_test_top.rbf"
#define FIRMWARE_PACKET             "h3600_firmware.tar.gz"
#define CORE_NEW_PACKET             "h3600_app"

#define BOARD_TYPE_A9_LINUX         "h3600_app_a9_linux"                 /* 主控板Linux测 */
#define BOARD_TYPE_A9_RTOS          "h3600_app_a9_rtos"                  /* 主控板Rtos测 */
#define BOARD_TYPE_SAMPLER_MCU      "h3600_sample_handler_mcu"           /* 进样器核心板 MCU */
#define BOARD_TYPE_OPTICAL_1        "h5000_optical_1"                    /* 光学1 */
#define BOARD_TYPE_MAGNECTIC        "h5000_magnetic_bead_samc21_mcu"    /* 磁珠 */
#define BOARD_TYPE_TEMP_CTRL        "h3600_temp_ctl_samc21_mcu"          /* 温控 */
#define BOARD_TYPE_LIQUID_DETECT_1  "h3600_liquid_detect_samc21_mcu"     /* 液面探测1：样本针 */
#define BOARD_TYPE_LIQUID_DETECT_2  "h3600_liquid_detect_samc21_mcu"     /* 液面探测2：试剂针2 */


/* 重传时间间隔*/
#define RETRANSMIT_INTERVAL_MS   (1000000)  //1000ms
#define UPDATE_TIME_OUT_MS       (10000)   //10s

#define UPGRADE_CHECK_HEAD          (1)
#define UPGRADE_MAX_FILE_SIZE       (5*1024*1024)

#define PROGRESS_REPORT_INTERVAL    (50) /*上报进度间隔*/

/* 升级类型 */
#define DEVICE_MCU                  (1)
#define DEVICE_FPGA                 (2)

/* 升级状态 */
#define UPDATE_STATUS_IDLE          0
#define UPDATE_STATUS_INIT          1
#define UPDATE_STATUS_RESET         2
#define UPDATE_STATUS_BEGIN         3
#define UPDATE_STATUS_WORKING       4
#define UPDATE_STATUS_STOP          5
#define UPDATE_STATUS_ACTIVE        6
#define UPDATE_STATUS_ERROR         8
#define UPDATE_STATUS_TIMEOUT       9


/* 更新包的大小 (字节)*/
#define UPDATE_PACKAGE_SIZE         128

struct version_info {
    int index;
    int node_id;
    char old_version[50];
    char new_version[50];
};

struct firmware_info{
    char *firmware_name;
    char *board_name;
    int node_id;
};

struct core_cmd_stru{
    char *back_cmd;
    char *load_cmd;
    char *extra_cmd;
};

typedef struct{
    uint16_t    reserved;
    uint32_t    index;
    uint8_t     data[UPDATE_PACKAGE_SIZE];
}__attribute__((packed)) update_data;

typedef struct{
    uint8_t     target;                 /* 1：MCU, 2：FPGA*/
    char        firmware_version[32];   
    uint32_t    firmware_size;
    uint32_t    reboot_address;
    char        board_type[32];
    uint32_t    firmware_checksum;
    uint32_t    data_offset;            /* firmware数据在文件中的偏移地址*/
    uint32_t    head_checksum;
}__attribute__((packed)) update_file_head;

#define FILE_HEAD_SIZE  (sizeof(update_file_head))

typedef struct
{
    int8_t  result;
}__attribute__((packed)) ack_result_t;

struct file_update
{
    update_file_head head;
    unsigned char   *buff;
    unsigned char   *pack_offset;
    int             rx_ack;
    uint32_t        pack_number;  /* 需要发送的总包数*/
    uint32_t        pack_index;   /* 包序号，从 1 开始 */
    int             state;        /* 状态机 */
    struct timeval  last_time;
    sem_t           upgrade_sem;  /* 更新固件通知信号量 */
    sem_t           config_sem;
    long long       recv_time_last;/* 存储时间 */
    unsigned char   ack_work;
    void            (*progress_fun)(char *name, int state, int result);
};

/* 升级下发参数 */
typedef struct
{
    uint32_t    program_no;                 /* 下位机子程序编号 */
    char        program_file_name[32];      /* 下位机子程序包名 */
    char        program_file[1024 * 1024 * 10];  /* 下位机子程序包 */
    char        str_MD5[32];                /* 下位机子程序包MD5 */
    uint32_t    program_len;                /* 下位机子程序包长度 */
    uint32_t    userdata;                   /* 下位机接口回复 */
}upgrade_param_t;

int upgrade_all_firmware(const char *firmware_path);
int upgrade_firmware_start(const char *file_name);
void ack_update_result(const slip_port_t *port, slip_msg_t *msg);
void ack_update_working(const slip_port_t *port, slip_msg_t *msg);
void ack_update_stop(const slip_port_t *port, slip_msg_t *msg);
int upgrade_init(void);
void hex_dump(char *buf, int len, int addr);
int module_is_upgrading_now(uint8_t dst, uint8_t type);
//extern void slip_send_sampler_null_wait(uint8_t type, uint8_t sub_type);
void upgrade_id_get(uint32_t id);
extern long long get_time();
struct version_info *find_version_tbl_item(int index, int node);

#ifdef __cplusplus
}
#endif

#endif



