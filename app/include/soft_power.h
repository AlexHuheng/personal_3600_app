#ifndef H3600_SOFT_POWER_H
#define H3600_SOFT_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "list.h"

#define USER_CONF  "/root/maccura/h_etc/user_conf.json" /* 用户级别的配置文件 */

#define PC_MAC_COUNT_MAX 4
#define PC_MAC_ADDR_SIZE 64

typedef char(*pc_mac_t)[PC_MAC_ADDR_SIZE];

typedef struct
{
    struct list_head list_head; /* 定时开机配置 链表头 */
    pthread_mutex_t list_mutex;
    char pc_macaddr[PC_MAC_COUNT_MAX][PC_MAC_ADDR_SIZE]; /* PC机MAC地址(最多支持PC_MAC_COUNT_MAX个) */
    int reagent_gate_timeout;   /* 关闭仪器时，打开试剂仓仓盖的超时时间(单位 s) */
}user_config_t;

typedef struct
{
    int32_t week; /* 周几，1~7,1:星期一；2:星期二;...7:星期日  */
    int32_t hour; /* 开机时间，小时0~23 */
    int32_t minute; /* 分0~59 */
    int32_t type; /* 是否启用开机. 1:启用          0:不启用 */
    struct list_head soft_power_param_list;
}soft_power_param_t;

int soft_power_init();
int soft_power_param_add(int32_t week, int32_t hour, int32_t minute, int32_t type);
int soft_power_param_del(soft_power_param_t *boot_param);
int soft_power_param_del_all();
int soft_power_param_update_file();
int pc_macaddr_update(int idx, const char* mac_str);
int pc_macaddr_del_all();
pc_mac_t pc_macaddr_get();
int reagent_gate_timeout_get();
void reagent_gate_timeout_set(int timeout);

#ifdef __cplusplus
}
#endif

#endif

