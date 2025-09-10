#ifndef H5000_MODULE_CUP_MONITOR_H
#define H5000_MODULE_CUP_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <list.h>
#include "h3600_cup_param.h"

#define MODULE_BASE_INDEX  (0x0001)
typedef enum{
    MODULE_NONE = 0,
    MODULE_NEEDLE_S = MODULE_BASE_INDEX<<0,                 /* 进样模块 */
    MODULE_NEEDLE_R2 = MODULE_BASE_INDEX<<1,           /* 样本前处理模块 */
    MODULE_CATCHER = MODULE_BASE_INDEX<<2,              /* 孵育模块 */
}module_t;

#define MODULE_WORK_ALL (MODULE_NEEDLE_S | MODULE_NEEDLE_R2 | MODULE_CATCHER)

#define UPDATE_QC           BIT(0)
#define UPDATE_FACTOR       BIT(1)
#define UPDATE_DILU         BIT(2)
#define UPDATE_R1           BIT(3)
#define UPDATE_R2           BIT(4)

#define UPDATE_MSK          (UPDATE_DILU | UPDATE_QC | UPDATE_R1 | UPDATE_FACTOR | UPDATE_R2)

typedef enum{
    REACT_CUP_LIST,             /* 反应杯链表 */
    NEEDLE_S_CUP_LIST,          /* 前处理模块反应杯链表 */
    NEEDLE_R2_CUP_LIST,         /* 孵育模块测试杯链表 */
    CATCHER_CUP_LIST,           /* 磁珠模块测试杯链表 */
}cup_list_t;

void react_cup_list_init();
struct react_cup *new_a_cup();
uint32_t get_next_cup_id(void);
void set_slot_del_lock(void);
void set_slot_del_unlock(void);
void del_cup(uint32_t cup_id);
void clear_detach_cups(void);
void init_module_work_flag(void);
void reset_cup_list(cup_list_t type);
int module_request_cups(module_t module, struct list_head **work_cups_list);
int module_response_cups(module_t module, int result);
int check_all_cup_from_list(void);
int catcher_cup_list_show();
int needle_r2_cup_list_show();
int needle_s_cup_list_show();
void react_cup_list_show();
int get_order_checktime();
int get_exist_cup_in_test();
void set_is_test_finished(int flag);
int get_is_test_finished();
void react_cup_del_all(void);
void add_cup_handler(struct react_cup *cup, cup_list_t type);
void react_cup_list_test(int orderno_idx, int cnt, int tube_id, needle_pos_t r1_pos, needle_pos_t r2_pos, int r2_clean_type, int r2_take_ul, int cup_pos);
void cal_sample_volume_by_order(uint32_t tube_id, double extra_vol);
int del_cup_by_slot_id(int32_t slot_id);
void virtual_a_detach_cup(cup_pos_t virtual_cup_pos);
void virtual_dilu_no_record(int order_no);
int virtual_dilu_no_record_get();
void liq_det_set_cup_detach(uint32_t order_no);
void liq_det_r1_set_delay_detach(uint32_t order_no);
void liq_det_r1_set_cup_detach(uint32_t order_no);
void delete_tube_order_by_ar(void *order_no);
void delete_tube_order(void *order_no);
void delete_order_by_order_no(uint32_t order_no);
int update_reag_pos_info(int which, int order, int pos1, int pos2);

#ifdef __cplusplus
}
#endif

#endif

