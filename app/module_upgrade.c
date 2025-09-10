#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <module_upgrade.h>
#include <sys/time.h>
#include <crc.h>
#include <log.h>
#include <common.h>
#include <slip/slip_msg.h>
#include <slip/slip_node.h>
#include "thrift_handler.h"
#include <slip_cmd_table.h>
#include <sys/types.h>   
#include <dirent.h>
#include "module_optical.h"

struct file_update g_update_img;

static struct version_info version_table[] = 
{
    {1, SLIP_NODE_A9_LINUX, {0}, {0}},
    {2, SLIP_NODE_A9_RTOS, {0}, {0}},
    {3, SLIP_NODE_SAMPLER, {0}, {0}},
    {4, SLIP_NODE_OPTICAL_1, {0}, {0}},
    {6, SLIP_NODE_MAGNECTIC, {0}, {0}},
    {7, SLIP_NODE_TEMP_CTRL, {0}, {0}},
    {8, SLIP_NODE_LIQUID_DETECT_1, {0}, {0}},
    {9, SLIP_NODE_LIQUID_DETECT_2, {0}, {0}},
    {13, SLIP_NODE_FPGA_A9, {0}, {0}},
};
static const int ver_number = ARRAY_SIZE(version_table);

static struct firmware_info firmware_table[] = 
{
    {BOARD_TYPE_SAMPLER_MCU, "h3600_sample_handler_mcu", SLIP_NODE_SAMPLER},                   /* 进样器核心板 MCU */
    {BOARD_TYPE_OPTICAL_1, "h5000_optical_1", SLIP_NODE_OPTICAL_1},                            /* 光学1 */
    {BOARD_TYPE_MAGNECTIC, "h5000_magnetic_bead_samc21_mcu", SLIP_NODE_MAGNECTIC},             /* 磁珠 */
    {BOARD_TYPE_TEMP_CTRL, "h3600_temp_ctl_samc21_mcu", SLIP_NODE_TEMP_CTRL},                  /* 温控 */
    {BOARD_TYPE_LIQUID_DETECT_1, "h3600_liquid_detect_samc21_mcu", SLIP_NODE_LIQUID_DETECT_1}, /* 液面探测1：样本针 */
    {BOARD_TYPE_LIQUID_DETECT_2, "h3600_liquid_detect_samc21_mcu", SLIP_NODE_LIQUID_DETECT_2}, /* 液面探测1：试剂针2 */
};
        
static const int firmware_number = ARRAY_SIZE(firmware_table);
static uint8_t dst_cur = 0;
static uint8_t up_id;
static uint8_t other_cmd_flag = 0;

extern int slip_optical_set(uint8_t optical_sw);

struct version_info *find_version_tbl_item(int index, int node)
{
    uint8_t i = 0;

    for (i = 0; i < ver_number; i++) {
        if (node == version_table[i].node_id || index == version_table[i].index) {
            return &version_table[i];
        }
    }

    /* 默认返回下位机主程序版本号 */
    return &version_table[0];
}

int module_is_upgrading_now(uint8_t dst, uint8_t type)
{
    if (dst != dst_cur || other_cmd_flag) {
        return 0;
    }

    return (UPDATE_STATUS_IDLE != g_update_img.state && (UPGRADE_TYPE != type)) ? 1 : 0;
}

void upgrade_id_get(uint32_t id)
{
    up_id = id;
}

static int upgrade_get_board_id(const char *board_type)
{
    int i , node_id;

    for (i = 0 ; i < firmware_number ; i++) {
        if (!strcmp(firmware_table[i].board_name, board_type)) {
            if (!strcmp(BOARD_TYPE_LIQUID_DETECT_1, board_type)) {
                if (8 == up_id) {
                    node_id = SLIP_NODE_LIQUID_DETECT_1;
                } else if(9 == up_id) {
                    node_id = SLIP_NODE_LIQUID_DETECT_2;
                } else if(10 == up_id) {
                    node_id = SLIP_NODE_LIQUID_DETECT_3;
                }
            } else {
                node_id = firmware_table[i].node_id;
            }
            LOG_ERROR("get board slip node_id:%d\n", firmware_table[i].node_id);
            return node_id;
        }
    }
    LOG_ERROR("invalid board type:%s\n", board_type);
    return 0;
}

void hex_dump(char *buf, int len, int addr) 
{
    return ; /* 以下代码段有崩溃可能，暂时屏蔽 */

    int i = 0;
    int j = 0;
    int k = 0;
    char binstr[80];
 
    for (i = 0 ; i < len ; i++) 
    {
        if (0 == (i % 16)) 
        {
            sprintf(binstr, "%08x -", i+addr);
            sprintf(binstr, "%s %02x", binstr, (unsigned char)buf[i]);
        } else if (15 == (i % 16)) {
            sprintf(binstr, "%s %02x", binstr, (unsigned char)buf[i]);
            sprintf(binstr, "%s  ", binstr);

            for (j = i - 15; j <= i; j++) 
            {
                sprintf(binstr, "%s%c", binstr, ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
            }

//            LOG("%s\n", binstr);
        } else {
            sprintf(binstr, "%s %02x", binstr, (unsigned char)buf[i]);
        }
    }

    if (0 != (i % 16)) 
    {
        k = 16 - (i % 16);

        for (j = 0; j < k; j++) {
            sprintf(binstr, "%s   ",binstr);
        }

        sprintf(binstr, "%s  ",binstr);
        k = 16-k;
        
        for (j = i - k; j < i; j++) {
            sprintf(binstr, "%s%c", binstr, ('!' < buf[j] && buf[j] <= '~') ? buf[j] : '.');
        }
//        LOG("%s\n", binstr);
    }
}

#ifdef UPGRADE_CHECK_HEAD
static void debug_print_file_head(update_file_head *p_file_head)
{
    LOG("--------------------\n");
    LOG("target:       \t%d\n", p_file_head->target);
    LOG("firmware ver :\t%s\n", p_file_head->firmware_version);
    LOG("firmware size:\t%d\n", p_file_head->firmware_size);
    LOG("reboot addr:  \t0x%x\n", p_file_head->reboot_address);
    LOG("board type:   \t%s\n", p_file_head->board_type);
    LOG("firmware crc: \t0x%x\n", p_file_head->firmware_checksum);
    LOG("data offset:  \t0x%x(%d)\n", p_file_head->data_offset, p_file_head->data_offset);
    LOG("head crc:     \t0x%x\n", p_file_head->head_checksum);
    
    LOG("head length:  \t%d\n", (int)FILE_HEAD_SIZE);
    LOG("head memory:\n");
    hex_dump((char *)p_file_head, FILE_HEAD_SIZE, (int)p_file_head);
    LOG("--------------------\n");

}

static int parse_file_head(FILE *fp)
{
    int ret = 0;
    unsigned int  head_crc = 0;
    
    fseek(fp, 0, SEEK_END);
    uint32_t file_size = ftell(fp);

    if (file_size <= FILE_HEAD_SIZE) {
        LOG_ERROR("invalid file size %d\n", file_size);
        ret = -1;
        goto end;
    }
    
    fseek(fp, 0, SEEK_SET);
    fread(&g_update_img.head, 1, FILE_HEAD_SIZE, fp);

    debug_print_file_head(&g_update_img.head);
    
    if (g_update_img.head.data_offset < FILE_HEAD_SIZE) {
        LOG_ERROR("invalid file offset %d\n", g_update_img.head.data_offset);
        ret = -1;
        goto end;
    }

    if ((g_update_img.head.target != DEVICE_MCU)
        && (g_update_img.head.target != DEVICE_FPGA)) {
        LOG_ERROR("invalid target:%d\n", g_update_img.head.target);
        ret = -1;
        goto end;
    }

    head_crc = uCRC_Compute(uCRC16_X25, (uint8_t *)&g_update_img.head, FILE_HEAD_SIZE - 4);
    head_crc = uCRC_ComputeComplete(uCRC16_X25, head_crc);
    if (head_crc != g_update_img.head.head_checksum) {
        LOG_ERROR("invalid head check 0x%x, bad:0x%x\n", 
                g_update_img.head.head_checksum, 
                head_crc);
        ret = -1;
        goto end;
    }

end:
    return ret;
}

static int parse_file_data(FILE *fp)
{
    int ret = 0;
    uint32_t  firmware_crc = 0;
    
    fseek(fp, 0, SEEK_END);
    uint32_t file_size = ftell(fp);

    if (g_update_img.head.data_offset + g_update_img.head.firmware_size > file_size) {
        LOG_ERROR("invalid data size (%d + %d > %d)\n", 
                g_update_img.head.data_offset, 
                g_update_img.head.firmware_size, 
                file_size);
        ret = -1;
        goto end;
    }

    g_update_img.buff = malloc(g_update_img.head.firmware_size);
    if (g_update_img.buff == NULL) {
        LOG_ERROR("malloc failed %d.\n", g_update_img.head.firmware_size);
        ret = -1;
        goto end;
    }

    memset(g_update_img.buff, 0x0, sizeof(g_update_img.head.firmware_size));
    
    fseek(fp, g_update_img.head.data_offset, SEEK_SET);
    fread(g_update_img.buff, 1, g_update_img.head.firmware_size, fp);
    
    firmware_crc = uCRC_Compute(uCRC16_X25, g_update_img.buff, g_update_img.head.firmware_size);
    firmware_crc = uCRC_ComputeComplete(uCRC16_X25, firmware_crc);

    if (firmware_crc != g_update_img.head.firmware_checksum) {
        LOG_ERROR("invalid CRC. (0x%x - 0x%x)\n", 
                g_update_img.head.firmware_checksum, 
                firmware_crc);
        ret = -1;
        goto end;
    }

    g_update_img.pack_offset = g_update_img.buff;
end:   
    return ret;
}

#else
static int parse_file_data(FILE *fp)
{
    int ret = 0;
    
    fseek(fp, 0, SEEK_END);
    uint32_t file_size = ftell(fp);

    LOG("file_size:%d\n", file_size);
    
    if (file_size > UPGRADE_MAX_FILE_SIZE) {
        LOG_ERROR("file is too large. (%d)\n", file_size);
        ret = -1;
        goto end;
    }
    
    g_update_img.buff = malloc(file_size);
    if (g_update_img.buff == NULL) {
        LOG_ERROR("malloc failed %d.\n", file_size);
        ret = -1;
        goto end;
    }

    memset(g_update_img.buff, 0x0, sizeof(file_size));
    
    fseek(fp, 0, SEEK_SET);
    fread(g_update_img.buff, 1, file_size, fp);

    g_update_img.pack_offset = g_update_img.buff;
    g_update_img.head.firmware_size = file_size;

end:
    return ret;
}

#endif

static int update_reset(unsigned char dst)
{
    typedef struct {
        int32_t order_id;
    }__attribute__((packed)) upgrade_reset_t;

    upgrade_reset_t reset_data;

    memset(&reset_data, 0x0, sizeof(upgrade_reset_t));

    if (g_update_img.state != UPDATE_STATUS_RESET) {
        LOG_ERROR("update status invalid.(%d)\n", g_update_img.state);
        return -1;
    }
    
    LOG("Tx update reset.(dst:%d)\n", dst);
    
    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE,
                    UPGRADE_RESET_SUBTYPE,
                    sizeof(upgrade_reset_t),
                    &reset_data);	
    if(dst == SLIP_NODE_SAMPLER){
//        slip_send_sampler_null_wait(UPGRADE_TYPE, UPGRADE_RESET_SUBTYPE);
    }
    return 0;
}

static int update_begin(unsigned char dst, struct file_update *image_info)
{
    if (image_info->state != UPDATE_STATUS_BEGIN) {
        LOG_ERROR("update status invalid.(%d)\n", image_info->state);
        return -1;
    }
    
    LOG("Tx update begin.\n");
    hex_dump((char *)&image_info->head, (FILE_HEAD_SIZE - 8), (int)&image_info->head);

    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE, 
                    UPGRADE_BEGIN_SUBTYPE,
                    (FILE_HEAD_SIZE - 8),
                    &image_info->head);	
    return 0;
}


static int update_working(unsigned char dst, uint32_t package_index, char *buff, uint32_t buff_len)
{
    update_data tx_data;
    
    if (g_update_img.state != UPDATE_STATUS_WORKING) {
        LOG_ERROR("update status invalid.(%d)\n", g_update_img.state);
        return -1;
    }

    memset(&tx_data, 0x0, sizeof(tx_data));
    tx_data.index = package_index;
    /* 最后一包数据，留余部分清零 */
    if(package_index == g_update_img.pack_number){
        buff_len = (g_update_img.head.firmware_size) % UPDATE_PACKAGE_SIZE;
    }
    memcpy(tx_data.data, buff, buff_len);
    
    LOG("tx pkt index:%d\n", tx_data.index);
    hex_dump((char *)&tx_data, sizeof(tx_data), (int)&tx_data);

    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE, 
                    UPGRADE_WORKING_SUBTYPE,
                    sizeof(tx_data),
                    &tx_data);
    return 0;
}

static int update_stop(unsigned char dst)
{
    typedef struct {
        uint32_t reserved;
    }__attribute__((packed)) upgrade_stop_t;

    upgrade_stop_t data = {0};

    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE, 
                    UPGRADE_STOP_SUBTYPE, 
                    sizeof(data),
                    &data);
    return 0;
}

static int update_active(unsigned char dst, uint8_t target)
{
    typedef struct {
        uint8_t target;
    }__attribute__((packed)) upgrade_active_t;

    upgrade_active_t data;

    data.target = target;

    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE, 
                    UPGRADE_ACTIVE_SUBTYPE, 
                    sizeof(data),
                    &data);
     return 0;
}

#if 0
static int update_get_boot_info(unsigned char dst)
{
    typedef struct
    {
        uint32_t reserved;
    }__attribute__((packed)) get_boot_info_t;

    get_boot_info_t data = {0};

    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE, 
                    UPGRADE_GET_REBOOT_INFO_SUBTYPE, 
                    sizeof(data),
                    &data);
    return 0;
}


static int update_switch(unsigned char dst, uint32_t boot_index)
{
    typedef struct
    {
        uint8_t index;
    }__attribute__((packed)) switch_firmware_t;

    switch_firmware_t data = {0};

    data.index = boot_index;

    slip_send_node(slip_node_id_get(), 
                    dst, 
                    0x0, 
                    UPGRADE_TYPE, 
                    UPGRADE_SWITCH_FIRMWARE_SUBTYPE, 
                    sizeof(data),
                    &data);

     return 0;
}
#endif

void ack_update_result(const slip_port_t *port, slip_msg_t *msg)
{
    LOG("msg->cmd_type.type:%x msg->cmd_type.sub_type:%x\n",msg->cmd_type.type,msg->cmd_type.sub_type);
    sem_wait(&g_update_img.config_sem);
    if ((msg->cmd_type.type == UPGRADE_TYPE) 
        && (msg->cmd_type.sub_type == UPGRADE_ACK_BEGIN_SUBTYPE)) {
        g_update_img.state = UPDATE_STATUS_WORKING;
        LOG("received begin ack\n");
        
    } else if ((msg->cmd_type.type == UPGRADE_TYPE) 
        && (msg->cmd_type.sub_type == UPGRADE_ACK_ACTIVE_SUBTYPE)) {
        g_update_img.state = UPDATE_STATUS_STOP;
        LOG("received active ack\n");
    } else {
        g_update_img.state = UPDATE_STATUS_ERROR;
        LOG_ERROR("rx ack invalid.\n");
    }
    LOG("received begin ack\n");
    gettimeofday(&g_update_img.last_time, NULL);
    sem_post(&g_update_img.config_sem);
}

static int get_upgrade_result(void)
{
    int result = 0;
    
    sem_wait(&g_update_img.config_sem);
    result = g_update_img.state;
    sem_post(&g_update_img.config_sem);

    return result;
}

void ack_update_working(const slip_port_t *port, slip_msg_t *msg)
{
    typedef struct {
//        int8_t   result;
        uint32_t package_index;
    }__attribute__((packed)) ack_update_working_t;

    ack_update_working_t *p_result = (ack_update_working_t *)msg->data;

    sem_wait(&g_update_img.config_sem);
    g_update_img.rx_ack = ntohl(p_result->package_index);

    gettimeofday(&g_update_img.last_time, NULL);
    //LOG("ack update working result:%d, index:%d\n", p_result->result, g_update_img.rx_ack);
    LOG("index:%d\n", g_update_img.rx_ack);
    sem_post(&g_update_img.config_sem);
    g_update_img.recv_time_last = get_time();  
    g_update_img.ack_work = 1;
}

void ack_update_stop(const slip_port_t *port, slip_msg_t *msg)
{
    typedef struct {
        uint32_t    checksum;
//        int8_t      result;
    }__attribute__((packed)) ack_update_stop_t;

    ack_update_stop_t *p_result = (ack_update_stop_t *)msg->data;

    sem_wait(&g_update_img.config_sem);
    g_update_img.state = UPDATE_STATUS_ACTIVE;
    gettimeofday(&g_update_img.last_time, NULL);
    //LOG("ack stop checksum:0x%x, result:%d\n", p_result->checksum, p_result->result);
    LOG("ack stop checksum:0x%x\n", p_result->checksum);
    sem_post(&g_update_img.config_sem);
}

int upgrade_firmware_start(const char *file_name)
{
    int ret = 0;
    
    FILE *fp = fopen(file_name, "rb");
    if (fp == NULL) {
        LOG_ERROR("file open failed %s.\n", file_name);
        goto end;
    }
    
#ifdef UPGRADE_CHECK_HEAD
    /* 解析升级包 */
    if (parse_file_head(fp)) {
        ret = -1;
        LOG_ERROR("parse file head failed:%s.\n", file_name);
        goto end;
    }
#endif

    if (parse_file_data(fp)) {
        ret = -1;
        LOG_ERROR("parse file data failed:%s.\n", file_name);
        goto end;
    }

    gettimeofday(&g_update_img.last_time, NULL);
    g_update_img.rx_ack = 1;
    g_update_img.pack_index = 1;
    g_update_img.state = UPDATE_STATUS_INIT;
    
    /* 发送更新命令 */ 
    LOG("Tx upgrade cmd.\n");
    sem_post(&g_update_img.upgrade_sem);
end:
    if (fp != NULL) {
        fclose(fp);
    }
    return ret;
    
}


int upgrade_firmware_handle(void)
{
    int already_tx = 0 , work_rty;
    double progress;

#if 0
    int interval_ms = 0;

    struct timeval interval_time;
    struct timeval current_time;
#endif

    dst_cur = upgrade_get_board_id(g_update_img.head.board_type);

    while(1) {
        sem_wait(&g_update_img.config_sem);

#if 0        
        gettimeofday(&current_time, NULL);
        timersub(&current_time, &g_update_img.last_time, &interval_time);
        interval_ms = interval_time.tv_sec * 1000 + interval_time.tv_usec / 1000;
       
        if (interval_ms > UPDATE_TIME_OUT_MS)
        {
            g_update_img.state = UPDATE_STATUS_TIMEOUT;
            LOG_ERROR("upgrade time out.\n", g_update_img.state);
            sem_post(&g_update_img.config_sem);
            break;       
        }
#endif

        if ((g_update_img.state == UPDATE_STATUS_ERROR)
            || (g_update_img.state == UPDATE_STATUS_IDLE)) {
            LOG("upgrade complete.(%d)\n", g_update_img.state);
            
            progress = (float)(g_update_img.pack_index)/(float)(g_update_img.pack_number) * 100.0;
            
            g_update_img.progress_fun(g_update_img.head.board_type, 
                                      (int)progress,
                                      g_update_img.state);

            sem_post(&g_update_img.config_sem);
            break;
        }
    
        switch(g_update_img.state) {
            case UPDATE_STATUS_INIT:
                g_update_img.state = UPDATE_STATUS_RESET;
                break;

            case UPDATE_STATUS_RESET:
                dst_cur = upgrade_get_board_id(g_update_img.head.board_type);
                other_cmd_flag = 1;
                boards_firmware_version_get(dst_cur, find_version_tbl_item(0, dst_cur)->old_version, 0);
                other_cmd_flag = 0;
                LOG("old_version:%s\n", find_version_tbl_item(0, dst_cur)->old_version);

                if(SLIP_NODE_OPTICAL_1 == dst_cur){ /* 光学1 */
                    slip_optical_set(OPTICAL_1_RESET);
                } else if (SLIP_NODE_OPTICAL_2 == dst_cur){ /* 光学2 */
                    slip_optical_set(OPTICAL_2_RESET);
                } else if (SLIP_NODE_OPTICAL_HIL == dst_cur){ /* 光学HIL */
                    slip_optical_set(OPTICAL_HIL_RESET);
                } else {
                    update_reset(dst_cur);
                }
                LOG("reset board! The dst is %d\n", dst_cur);
                g_update_img.state = UPDATE_STATUS_BEGIN;
                sleep(4);
                break;

            case UPDATE_STATUS_BEGIN:
                /* 启动升级 */
                if (!already_tx) {
                    update_begin(dst_cur, &g_update_img);

                    g_update_img.pack_number = (g_update_img.head.firmware_size) / UPDATE_PACKAGE_SIZE;
                    LOG("The number of packets is %d\n", g_update_img.pack_number);
                    if ((g_update_img.head.firmware_size) % UPDATE_PACKAGE_SIZE) {
                        g_update_img.pack_number++;  
                    }

                    already_tx = 1;

                    LOG("The number of packets is %d\n", g_update_img.pack_number);
                }

                break;

            case UPDATE_STATUS_WORKING:
                /* 下发升级文件 */        
                if (g_update_img.pack_index > g_update_img.pack_number) {
                    LOG("Tx stop cmd.(packets number:%d).\n", g_update_img.pack_number);
                    
                    g_update_img.state = UPDATE_STATUS_STOP;    
                    sleep(1);
                    break;
                }

                if ((g_update_img.rx_ack == g_update_img.pack_index) || (g_update_img.pack_index == 1)) {
                    LOG("pack_index:%d, pack_number:%d,rx_ack:%d\n", 
                         g_update_img.pack_index,
                         g_update_img.pack_number,
                         g_update_img.rx_ack);

                    work_rty = 0;
                    g_update_img.ack_work = 0;
working_rty_jump:                    
                    update_working(dst_cur,
                               g_update_img.pack_index, 
                               (char *)g_update_img.pack_offset, 
                               UPDATE_PACKAGE_SIZE);
                    
                    sem_post(&g_update_img.config_sem);
                    g_update_img.recv_time_last = get_time(); 
                    while(work_rty < 3){
                        if(g_update_img.ack_work == 1){
                            break;
                        }
                        if(get_time() - g_update_img.recv_time_last >= RETRANSMIT_INTERVAL_MS / 1000){
                            work_rty += 1;   
                            LOG("pack_index send fail:%d, retry cnt:%d,\n", g_update_img.pack_index , work_rty);
                            goto working_rty_jump;
                        }
                        usleep(1000);
                    }
                    
                    sem_wait(&g_update_img.config_sem);            
                    g_update_img.pack_offset += UPDATE_PACKAGE_SIZE;
                    g_update_img.pack_index++;

                    if (!(g_update_img.pack_index % PROGRESS_REPORT_INTERVAL)
                        && (g_update_img.progress_fun)) {
                        progress = (float)(g_update_img.pack_index)/(float)(g_update_img.pack_number) * 100.0;
                        
                        g_update_img.progress_fun(g_update_img.head.board_type, 
                                                  (int)progress,
                                                  g_update_img.state);
                    }
                }
                    
                break;

            case UPDATE_STATUS_STOP:
                LOG("UPDATE_STATUS_STOP\n");
                update_stop(dst_cur);
                sleep(1);
                g_update_img.state = UPDATE_STATUS_ACTIVE;                 
                break;

            case UPDATE_STATUS_ACTIVE:
                LOG("UPDATE_STATUS_ACTIVE\n");
                update_active(dst_cur, g_update_img.head.target);
                sleep((SLIP_NODE_LIQUID_DETECT_1 == dst_cur || SLIP_NODE_LIQUID_DETECT_2 == dst_cur || SLIP_NODE_LIQUID_DETECT_3 == dst_cur) ? 5 : 10);

                sleep(20);
                other_cmd_flag = 1;
                boards_firmware_version_get(dst_cur, find_version_tbl_item(0, dst_cur)->new_version, 0);
                other_cmd_flag = 0;
                LOG("new_version:%s\n", find_version_tbl_item(0, dst_cur)->new_version);
                g_update_img.state = UPDATE_STATUS_IDLE;
                break;

            default:
                LOG_ERROR("invalid state:%d\n", g_update_img.state);
                g_update_img.state = UPDATE_STATUS_ERROR;

        }

        sem_post(&g_update_img.config_sem);
        usleep(10*1000);
    }

    return 0;
}

static void *upgrade_thread(void *arg)
{       
    while (1) {
        sem_wait(&g_update_img.upgrade_sem);
        LOG("Rx upgrade cmd.\n");
        
        upgrade_firmware_handle();
    }
    
    return NULL;
}
static int tar_firmware(const char *firmware_path)
{
    int  status;
    char cmd[256];

    memset(cmd, 0x0, sizeof(cmd));
    snprintf(cmd, 
           sizeof(cmd), 
           "tar -xf %s/%s -C %s", 
           firmware_path, 
           FIRMWARE_PACKET,
           firmware_path);

    LOG("command:%s\n", cmd);
    status = system(cmd);
    if(status < 0) {
        LOG("cmd: %s\t error: %s", cmd, strerror(errno));
        return (-1);
    }

    if(WIFEXITED(status)) {
        //取得cmdstring执行结果
        LOG("normal termination, exit status = %d\n", WEXITSTATUS(status));  
    } else if(WIFSIGNALED(status)) {
        //如果cmdstring被信号中断，取得信号值
        LOG("abnormal termination,signal number =%d\n", WTERMSIG(status)); 
    } else if(WIFSTOPPED(status)) {
        //如果cmdstring被信号暂停执行，取得信号值
        LOG("process stopped, signal number =%d\n", WSTOPSIG(status)); 
    }

    return 0;
}

struct core_cmd_stru core_u_cmd[] =
{
    {"cd %s && cp -f %s %s.bak.%s", "cd %s && cp -f %s %s/", 0},
    {"cd %s && cp -f %s %s.bak.%s", "cd %s && cp -f %s %s/", 0},
    {"cd %s && cp -f %s %s.bak.%s", "cd %s && cp -f %s %s/", "cd /root/maccura/h_etc && sh rbf_update_scri.sh"}
};

static int copy_firmware(const char *firmware_name, uint8_t idx)
{
    char            cmd[256] = {0};
    int                  ret = 0;
    FILE                 *fp = NULL;
    char            date[10] = {0}; 
    int                    i = 0;

	fp = popen("date \"+%y%m%d\"", "r");
	fread(date, 1, 1024, fp);
	LOG("up file:%s read time %s\n", firmware_name , date);

    for (i = 0 ; i < 2 ; i++) {
        memset(cmd , 0, sizeof(cmd)); 

        switch(i) {
            case 0:
                /* 删除老旧版本备份 */
                memset(cmd , 0, sizeof(cmd));
                snprintf(cmd, sizeof(cmd), "cd %s && rm %s.bak.*", APP_PATH, firmware_name);
                ret += system(cmd);

                snprintf(cmd, sizeof(cmd), core_u_cmd[idx].back_cmd, APP_PATH, firmware_name, firmware_name, date);
                break;
            case 1:
                snprintf(cmd, sizeof(cmd), core_u_cmd[idx].load_cmd, FIRMWARE_PATH, firmware_name, APP_PATH);
                break;
            case 2:
                if(core_u_cmd[idx].extra_cmd) {
                    //snprintf(cmd, sizeof(cmd), core_u_cmd[idx].extra_cmd, FIRMWARE_PATH, firmware_name);
                    strcpy(cmd, core_u_cmd[idx].extra_cmd);
                } else { 
                    goto out;
                }
                break;
        }
        LOG("up cmd:%s\n", cmd);
        ret += system(cmd);
    }

out:
    return ret;
}

int upgrade_all_firmware(const char *firmware_path)
{
    int                      i = 0;
    int                    ret = 0;
    int                 result = 0;
    char    firmware_name[256] = {0};
    uint8_t         is_main_up = 0;
    char           *up_file[3] = {CORE_PACKET, CORE_RTOS_PACKET, CORE_FPGA_PACKET};

    if (firmware_path == NULL)
        return (-1);
    
    /*解压固件升级包*/
    tar_firmware(firmware_path);
    sleep(5);

    /* 升级A9主控制器 */
    for (i = 0 ; i < 3 ; i++) {
        memset(firmware_name, 0x0, sizeof(firmware_name));
        snprintf(firmware_name, 
                sizeof(firmware_name), 
                "%s/%s", 
                firmware_path, 
                up_file[i]);
    
        if (!access(firmware_name, F_OK)) {
            /* 更新核心板固件 */
            ret = !!copy_firmware(up_file[i], i);
            is_main_up = 1;
        }
    }

    if (is_main_up) { /* 升级过主板程序 */
        LOG("upgrade file core firmware.\n");
        goto out;
    }

    /* 更新子板固件 */
    for (i = 0; i < firmware_number; i++) {
        memset(firmware_name, 0x0, sizeof(firmware_name));
        snprintf(firmware_name, 
                sizeof(firmware_name), 
                "%s/%s.img", 
                firmware_path, 
                firmware_table[i].firmware_name);

        if (access(firmware_name, F_OK)) {
            ret = -1;
            LOG("firmware file:%s not exist!\n", firmware_name);
            continue;
        }

        LOG("upgrade firmware:%s\n", firmware_name);

        ret = upgrade_firmware_start(firmware_name);
        if (ret != 0) {
            LOG("upgrade file fail.(ret = %d)\n", ret);
            goto out;
        }
        
        while(1) {
            result = get_upgrade_result();
            if ((result == UPDATE_STATUS_ERROR)
                || (result == UPDATE_STATUS_TIMEOUT)
                || (result == UPDATE_STATUS_IDLE)) {
                g_update_img.progress_fun(firmware_name, 100, result);
                ret = (result == UPDATE_STATUS_IDLE) ? 0 : -1;
                result = ((DEVICE_MCU == g_update_img.head.target) && (SLIP_NODE_SAMPLER == dst_cur)) ? 30 : 0;
                sleep(result); /* 延时，等待升级模块成功以后才开始下一模块升级 */
                LOG("firmware file %s upgrade %s result:%d\n", firmware_table[i].firmware_name, (0 == ret) ? "success" : "fail", result);
                goto out;
            }
            usleep(10*1000);
        }
    }

out:
    dst_cur = 0;
    g_update_img.state = UPDATE_STATUS_IDLE;
    return ret;
}

void upgrade_progress(char *name, int state, int result)
{
    LOG("upgrade board:%s, state:%d, result:%d\n", name, state, result);
}

int upgrade_init(void)
{
    int ret = 0;
    pthread_t tid;

    if (opendir(FIRMWARE_PATH) == NULL) {
        system("mkdir -p "FIRMWARE_PATH);
    }
    
    memset(&g_update_img, 0, sizeof(g_update_img));
    g_update_img.progress_fun = upgrade_progress;
    
    ret = sem_init(&g_update_img.upgrade_sem, 0, 0);
    if (ret) {
        LOG_ERROR("create upgrade_sem fail.(%d)\n", ret);
        return ret;
    }

    ret = sem_init(&g_update_img.config_sem, 0, 1);
    if (ret) {
        LOG_ERROR("create config_sem fail.(%d)\n", ret);
        return ret;
    }

    pthread_create(&tid, NULL, upgrade_thread, NULL);
    return 0;
}

