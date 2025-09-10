#include <errno.h>
#include <pthread.h>
#include <stdint.h>

#include "work_queue.h"
#include "h3600_cup_param.h"
#include "module_monitor.h"
#include "module_cup_monitor.h"
#include "log.h"
#include "module_common.h"
#include "module_incubation.h"
#include "module_cup_mix.h"
#include "module_needle_s.h"

static pthread_mutex_t cup_list_mtx, module_brocast_mtx, slot_del_mtx;
static module_t module_work_flag = MODULE_WORK_ALL; /* 需要正常工作的模块 */
static module_t module_ready_flag = MODULE_NONE; /* 举手完成的模块 */

static struct list_head react_cup_list;
static struct list_head needle_s_work_cup_list;
static struct list_head needle_r2_work_cup_list;
static struct list_head catcher_work_cup_list;

static uint32_t next_cup_id = 1;
static int is_test_finish_flag = 1;    /* 周期完成自动停止的查询接口 */
static int virtual_dilu_order_no = 0;

uint32_t get_next_cup_id(void)
{
    return next_cup_id++;
}

void set_slot_del_lock()
{
    pthread_mutex_lock(&slot_del_mtx);
}

void set_slot_del_unlock()
{
    pthread_mutex_unlock(&slot_del_mtx);
}

void react_cup_list_init()
{
    INIT_LIST_HEAD(&react_cup_list);
    INIT_LIST_HEAD(&needle_s_work_cup_list);
    INIT_LIST_HEAD(&needle_r2_work_cup_list);
    INIT_LIST_HEAD(&catcher_work_cup_list);
    pthread_mutex_init(&cup_list_mtx, NULL);
    pthread_mutex_init(&module_brocast_mtx, NULL);
    pthread_mutex_init(&slot_del_mtx, NULL);

    //react_cup_list_test();
}

struct react_cup *new_a_cup()
{
    struct react_cup *cup = (struct react_cup *)malloc(sizeof(struct react_cup));
    if (NULL == cup) {
        LOG("new cup malloc, %s\n", strerror(errno));
        return NULL;
    }
    memset(cup, 0, sizeof(struct react_cup));
    return cup;
}

//static void del_cup(uint32_t cup_id)
void del_cup(uint32_t cup_id)
{
    struct react_cup *pos = NULL, *n = NULL;

    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_id == cup_id) {
            list_del(&pos->mainsibling);
            free(pos);
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
}

void clear_detach_cups(void)
{
    struct react_cup *pos = NULL, *n = NULL;
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_pos == POS_REACT_CUP_DETACH) { /* 丢杯位 */
            del_cup(pos->cup_id);
        }
    }
}

void init_module_work_flag()
{
    module_work_flag = MODULE_WORK_ALL;
    module_ready_flag = MODULE_NONE;
}

void set_is_test_finished(int flag)
{
    is_test_finish_flag = flag;
}

int get_is_test_finished()
{
    return is_test_finish_flag;
}

/* 生产模块同步信号发送程序 MODULE_THREAD_COUNT为生产模块数 */
int do_work_module_brocast(module_t module, int result)
{
    static long long start = 0, end = 0;

    pthread_mutex_lock(&module_brocast_mtx);
    if (result == 1) {
        module_ready_flag |= module;
    } else if(result == 0) {
        module_work_flag &= (~module);
    }

    LOG("[ BROCAST ] = module result:%d, ready:0x%04x work:0x%04x\n", result, module_ready_flag, module_work_flag);
    if (module_work_flag!=MODULE_NONE && (module_ready_flag == module_work_flag)) {
        end = get_time();
        LOG("[%s] all module finish time:%lld ms\n", __func__, end-start);
        set_detect_period_flag(0);
        init_module_work_flag();
        clear_detach_cups();
        if (get_exist_cup_in_test()) {
            if (0 != virtual_dilu_no_record_get()) {
                report_order_state(virtual_dilu_no_record_get(), OD_ERROR);
                virtual_dilu_no_record(0);
            }
            module_start_control(MODULE_CMD_STOP);
            set_is_test_finished(1);
        } else {
            set_is_test_finished(0);
        }
        work_queue_add(module_monitor_start, NULL);
        start = get_time();
    }
    pthread_mutex_unlock(&module_brocast_mtx);

    return 0;
}

void reset_cup_list(cup_list_t type)
{
    switch (type) {
    case REACT_CUP_LIST:
        INIT_LIST_HEAD(&react_cup_list);
        break;
    case NEEDLE_S_CUP_LIST:
        INIT_LIST_HEAD(&needle_s_work_cup_list);
        break;
    case NEEDLE_R2_CUP_LIST:
        INIT_LIST_HEAD(&needle_r2_work_cup_list);
        break;
    case CATCHER_CUP_LIST:
        INIT_LIST_HEAD(&catcher_work_cup_list);
        break;
    default:
        LOG("no such type list\n");
        return;
    }
}

void add_cup_head(struct react_cup *cup, cup_list_t type)
{
    switch (type) {
    case REACT_CUP_LIST:
        list_add_head(&cup->mainsibling, &react_cup_list);
        break;
    case NEEDLE_S_CUP_LIST:
        list_add_head(&cup->needle_s_sibling, &needle_s_work_cup_list);
        break;
    case NEEDLE_R2_CUP_LIST:
        list_add_head(&cup->needle_r2_sibling, &needle_r2_work_cup_list);
        break;
    case CATCHER_CUP_LIST:
        list_add_head(&cup->catcher_sibling, &catcher_work_cup_list);
        break;
    default:
        LOG("no such type list\n");
        return;
    }
}

void add_cup_tail(struct react_cup *cup, cup_list_t type)
{
    switch (type) {
    case REACT_CUP_LIST:
        list_add_tail(&cup->mainsibling, &react_cup_list);
        break;
    case NEEDLE_S_CUP_LIST:
        list_add_tail(&cup->needle_s_sibling, &needle_s_work_cup_list);
        break;
    case NEEDLE_R2_CUP_LIST:
        list_add_tail(&cup->needle_r2_sibling, &needle_r2_work_cup_list);
        break;
    case CATCHER_CUP_LIST:
        list_add_tail(&cup->catcher_sibling, &catcher_work_cup_list);
        break;
    default:
        LOG("no such type list\n");
        return;
    }
}

/*插入两个节点之间*/
void add_cup_middle(struct react_cup *cup, struct react_cup *prev, struct react_cup *next, cup_list_t type)
{
    switch (type) {
    case REACT_CUP_LIST:
        __list_add(&cup->mainsibling, &prev->mainsibling, &next->mainsibling);
        break;
    case NEEDLE_S_CUP_LIST:
        __list_add(&cup->needle_s_sibling, &prev->needle_s_sibling, &next->needle_s_sibling);
        break;
    case NEEDLE_R2_CUP_LIST:
        __list_add(&cup->needle_r2_sibling, &prev->needle_r2_sibling, &next->needle_r2_sibling);
        break;
    case CATCHER_CUP_LIST:
        __list_add(&cup->catcher_sibling, &prev->catcher_sibling, &next->catcher_sibling);
        break;
    default:
        LOG("no such type list\n");
        return;
    }
}

/*
 * 删除反应杯：
 *    1. 上位机解锁样本架后会删除对应槽位样本待测订单，直接删除订单
 *    2. 反应杯在加样位或R1，置为未激活
 *    3. 其他位置不能删除
*/
int del_cup_by_slot_id(int32_t slot_id)
{
    int ret = -1, del_flag = 0;
    struct react_cup *pos, *n;

    set_slot_del_lock();
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (slot_id == pos->cup_sample_tube.slot_id) {
            if (pos->cup_pos > POS_CUVETTE_SUPPLY_INIT) {
                LOG("there is some cups working!\n");
                goto out;
            }
        }
    }

    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (slot_id == pos->cup_sample_tube.slot_id) {
            if (POS_CUVETTE_SUPPLY_INIT == pos->cup_pos) {
                /* 进杯盘中直接删除 */
                list_del(&pos->mainsibling);
                free(pos);
                del_flag = 1;
            }
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
    ret = 0;

out:
    set_slot_del_unlock();
    if (del_flag == 1) {
        report_order_remain_checktime(get_order_checktime());
    }
    return ret;
}

/*反应杯添加到链表处理函数，对三种不同订单进行判断和处理*/
void add_cup_handler(struct react_cup *cup, cup_list_t type)
{
    /*找出需要插入的位置，急诊优先, 复检其次*/
    struct react_cup *pos, *prev_pos, *next_pos, *n;
    uint8_t count = 0;

    if (!list_empty(&react_cup_list)) {
        switch (cup->priority) {
            case TUBE_EMERG: /*急诊订单*/
                list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
                    count++;
                    prev_pos = next_pos;
                    next_pos = pos;

                    if (count != 1) {/*找出前后两个节点的样本号不一样且不是急诊订单时插入中间*/
                        if ((prev_pos->cup_sample_tube.sample_tube_id != next_pos->cup_sample_tube.sample_tube_id) && \
                            (next_pos->priority != TUBE_EMERG) && \
                            (next_pos->order_no != cup->order_no) && \
                            (next_pos->cup_pos == POS_CUVETTE_SUPPLY_INIT)) {
                            LOG("sample_tube_id:%d , sample_tube_id:%d , count:%d\n", \
                                prev_pos->cup_sample_tube.sample_tube_id, next_pos->cup_sample_tube.sample_tube_id, count);
                            add_cup_middle(cup, prev_pos, next_pos, REACT_CUP_LIST);
                            LOG("emerg_order_handler 1\n");
                            break;
                        } else if (list_is_last(&next_pos->mainsibling, &react_cup_list)) {/*遍历到最后一个节点时直接插链表*/
                            add_cup_tail(cup, type);
                            LOG("emerg_order_handler 2\n");
                            break;
                        }
                    } else if (list_is_last(&pos->mainsibling, &react_cup_list)) {/*链表只有一个节点时*/
                        add_cup_tail(cup, type);
                        LOG("emerg_order_handler 3\n");
                        break;
                    }
                }
                break;
            case TUBE_REVIEW: /*复查订单*/
                list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
                    count++;
                    prev_pos = next_pos;
                    next_pos = pos;

                    if (count != 1) {/*找出前后两个节点的样本号不一样且不是急诊订单、且不是复查订单时插入中间*/
                        if ((prev_pos->cup_sample_tube.sample_tube_id != next_pos->cup_sample_tube.sample_tube_id) && \
                            (next_pos->priority != TUBE_EMERG) && \
                            (next_pos->priority != TUBE_REVIEW) && \
                            (next_pos->order_no != cup->order_no) && \
                            (next_pos->cup_pos == POS_CUVETTE_SUPPLY_INIT)) {
                            LOG("sample_tube_id:%d , sample_tube_id:%d , count:%d\n", \
                                prev_pos->cup_sample_tube.sample_tube_id, next_pos->cup_sample_tube.sample_tube_id, count);
                            add_cup_middle(cup, prev_pos, next_pos, REACT_CUP_LIST);
                            LOG("review_order_handler 1\n");
                            break;
                        } else if (list_is_last(&next_pos->mainsibling, &react_cup_list)) {/*遍历到最后一个节点时直接插链表*/
                            add_cup_tail(cup, type);
                            LOG("review_order_handler 2\n");
                            break;
                        }
                    } else if (list_is_last(&pos->mainsibling, &react_cup_list)) {/*链表只有一个节点时*/
                        add_cup_tail(cup, type);
                        LOG("review_order_handler 3\n");
                        break;
                    }
                }
                break;
            case TUBE_QC: /*质控订单*/
                list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
                    count++;
                    prev_pos = next_pos;
                    next_pos = pos;

                    if (count != 1) {/*找出前后两个节点的样本号不一样且不是急诊订单、且不是复查订单时插入中间*/
                        if ((prev_pos->cup_sample_tube.sample_tube_id != next_pos->cup_sample_tube.sample_tube_id) && \
                            (next_pos->priority != TUBE_EMERG) && \
                            (next_pos->priority != TUBE_REVIEW) && \
                            (next_pos->priority != TUBE_QC) && \
                            (next_pos->order_no != cup->order_no) && \
                            (next_pos->cup_pos == POS_CUVETTE_SUPPLY_INIT)) {
                            LOG("sample_tube_id:%d , sample_tube_id:%d , count:%d\n", \
                                prev_pos->cup_sample_tube.sample_tube_id, next_pos->cup_sample_tube.sample_tube_id, count);
                            add_cup_middle(cup, prev_pos, next_pos, REACT_CUP_LIST);
                            LOG("qc_order_handler 1\n");
                            break;
                        } else if (list_is_last(&next_pos->mainsibling, &react_cup_list)) {/*遍历到最后一个节点时直接插链表*/
                            add_cup_tail(cup, type);
                            LOG("qc_order_handler 2\n");
                            break;
                        }
                    } else if (list_is_last(&pos->mainsibling, &react_cup_list)) {/*链表只有一个节点时*/
                        add_cup_tail(cup, type);
                        LOG("qc_order_handler 3\n");
                        break;
                    }
                }
                break;
            case TUBE_NORMAL: /*常规订单*/
                LOG("Order_type:general order type\n");
                add_cup_tail(cup, type);
                break;
            default:
                LOG_WARN("error order type\n");
            break;
        }
    }else {
        LOG("cup list is empty, add cup\n");
        add_cup_tail(cup, type);
    }
}

/* 检查 整个测试表是否存在测试杯，避免测试杯残留在任意工作位上 */
int check_all_cup_from_list(void)
{
    struct react_cup *pos = NULL, *n = NULL;
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos) {
            LOG("remain some cup, order:%d, pos:%d\n", pos->order_no, pos->cup_pos);
            return 1;
        }
    }
    return 0;
}

void react_cup_list_show()
{
    struct react_cup *pos = NULL, *n = NULL;

    pthread_mutex_lock(&cup_list_mtx);
    LOG("---------------------------main_react-------------------------------\n");
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        LOG("|    id:%d, order no:%d, tube id:%d, rack:%d, sample:%d, active:%d, pos:%d, temp store:%d, temp take:%d, order_type:%d    |\n",
            pos->cup_id, pos->order_no, pos->cup_sample_tube.sample_tube_id, pos->cup_sample_tube.rack_idx, pos->cup_sample_tube.sample_index, pos->cup_active,
            pos->cup_pos, pos->sp_para.temp_store, pos->sp_para.temp_take, pos->rerun);
        if (pos->cup_type == TEST_CUP) {
            LOG("type: TEST_CUP, take_ul: %f, dilu_ul: %f\n", pos->cup_test_attr.needle_s.take_ul, pos->cup_test_attr.needle_s.take_dilu_ul);
        } else if (pos->cup_type == DILU_CUP) {
            LOG("type: DILU_CUP, take_ul: %f, dilu_ul: %f\n", pos->cup_dilu_attr.take_ul, pos->cup_dilu_attr.dilu_ul);
        }
    }
    LOG("---------------------------------------------------------------------\n");
    pthread_mutex_unlock(&cup_list_mtx);
}

int get_order_checktime()
{
    int cnt = 0;

    struct react_cup *pos = NULL, *n = NULL;
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos) {
            cnt++;
        }
    }
    return (cnt * 20 + 270);
}

int get_exist_cup_in_test()
{
    int cnt = 0;
    struct react_cup *pos = NULL, *n = NULL;

    if (SAMPLER_ADD_START == module_sampler_add_get()) {
        list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
            if (pos) {
                cnt++;
            }
        }
    } else {
        list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
            if (pos->cup_pos != POS_CUVETTE_SUPPLY_INIT) {
                cnt++;
            }
        }
    }
    if (cnt != 0) {
        LOG("there are some cup in test!\n");
        return 0;
    }
    LOG("there is no cup in test!\n");
    return 1;
}

void cal_sample_volume_by_order(uint32_t tube_id, double extra_vol)
{
    struct react_cup *pos = NULL, *n = NULL;

    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_sample_tube.sample_tube_id == tube_id) {
            pos->cup_sample_tube.sample_volume = extra_vol;
        }
    }
    LOG("total take_ul: %lf\n", extra_vol);
    pthread_mutex_unlock(&cup_list_mtx);
}

void react_cup_del_all(void)
{
    struct react_cup *pos, *n;

    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        list_del(&pos->mainsibling);
        free(pos);
    }
    pthread_mutex_unlock(&cup_list_mtx);
}

int needle_s_cup_list_show()
{
    struct react_cup *pos = NULL, *n = NULL;

    LOG("----------------------------needle_s cup--------------------------------\n");
    list_for_each_entry_safe(pos, n, &needle_s_work_cup_list, needle_s_sibling) {
        LOG("|    id:%04d, order no:%04d, tube id:%04d, hashat:%d, type:%d, pos:%02d    |\n",
            pos->cup_id, pos->order_no, pos->cup_sample_tube.rack_idx, pos->cup_sample_tube.sp_hat, pos->cup_type, pos->cup_pos);
    }
    LOG("------------------------------------------------------------------------\n");

    return list_empty(&needle_s_work_cup_list);
}

int needle_r2_cup_list_show()
{
    struct react_cup *pos = NULL, *n = NULL;

    LOG("-----------------------------needle_r2 cup----------------------------\n");
    list_for_each_entry_safe(pos, n, &needle_r2_work_cup_list, needle_r2_sibling) {
        LOG("|    id:%04d, order no:%04d, tube id:%02d, hashat:%d, type:%d, pos:%02d    |\n",
            pos->cup_id, pos->order_no, pos->cup_sample_tube.rack_idx, pos->cup_sample_tube.sp_hat, pos->cup_type, pos->cup_pos);
    }
    LOG("----------------------------------------------------------------------\n");

    return list_empty(&needle_r2_work_cup_list);
}

int catcher_cup_list_show()
{
    struct react_cup *pos = NULL, *n = NULL;
    int cnt = 0;

    LOG("----------------------------catcher cup-------------------------------\n");
    list_for_each_entry_safe(pos, n, &catcher_work_cup_list, catcher_sibling) {
        if (pos->cup_pos == POS_CUVETTE_SUPPLY_INIT) {
            cnt++;
        } else {
            LOG("|    id:%04d, order no:%04d, tube id:%02d, hashat:%d, type:%d, pos:%02d    |\n",
                pos->cup_id, pos->order_no, pos->cup_sample_tube.rack_idx, pos->cup_sample_tube.sp_hat, pos->cup_type, pos->cup_pos);
        }
    }
    LOG("|    pos_init : %02d                                                   |\n", cnt);
    LOG("----------------------------------------------------------------------\n");

    return list_empty(&catcher_work_cup_list);
}

/* 虚拟一个需要丢弃的反应杯 */
void virtual_a_detach_cup(cup_pos_t virtual_cup_pos)
{
    struct react_cup *diluent_cup = NULL; /* 稀释杯 */

    diluent_cup = new_a_cup();
    diluent_cup->cup_id = 0;
    diluent_cup->order_no = 0;
    diluent_cup->rerun = 0;
    diluent_cup->cup_active = CUP_ACTIVE;
    diluent_cup->priority = TUBE_NORMAL;
    diluent_cup->cup_type = DILU_CUP;
    diluent_cup->cup_pos = virtual_cup_pos;
    diluent_cup->cup_dilu_attr.add_state = CUP_STAT_USED;
    diluent_cup->cup_dilu_attr.trans_state = CUP_STAT_USED;
    add_cup_handler(diluent_cup, REACT_CUP_LIST);
}

void virtual_dilu_no_record(int order_no)
{
    virtual_dilu_order_no = order_no;
}

int virtual_dilu_no_record_get()
{
    return virtual_dilu_order_no;
}

/* 样本针分杯稀释周期稀释液/R1液面探测失败设置延迟丢杯（延迟至抓手抓到孵育位时丢杯） */
void liq_det_r1_set_delay_detach(uint32_t order_no)
{
    struct react_cup *pos = NULL, *n = NULL;

    LOG("set inactive! order no:%d\n", order_no);
    report_order_state(order_no, OD_ERROR);
    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == order_no) {
            pos->cup_delay_active = CUP_INACTIVE;
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
}

/* 样本针稀释液/R1/R2液面探测失败设置丢杯 */
void liq_det_r1_set_cup_detach(uint32_t order_no)
{
    struct react_cup *pos = NULL, *n = NULL;

    LOG("set inactive! order no:%d\n", order_no);
    report_order_state(order_no, OD_ERROR);
    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == order_no) {
            pos->cup_active = CUP_INACTIVE;
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
}

/* 样本针液面探测失败设置丢杯 */
void liq_det_set_cup_detach(uint32_t order_no)
{
    struct react_cup *pos = NULL, *n = NULL;
    int clear_testcup_flag = 0;

    LOG("set inactive! order no:%d\n", order_no);
    report_order_state(order_no, OD_ERROR);
    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == order_no) {
            pos->cup_active = CUP_INACTIVE;
            if (pos->cup_type == DILU_CUP) {
                /* 第一个稀释杯在加样位，需要删除在杯盘中的测试杯 */
                clear_testcup_flag = 1;
            }
        }
    }
    if (clear_testcup_flag == 1) {
        pos = NULL;
        n = NULL;
        list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
            if (pos->order_no == order_no && pos->cup_pos == POS_CUVETTE_SUPPLY_INIT) {
                list_del(&pos->mainsibling);
                free(pos);
            }
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
}

/*
*   删除当前订单所在样本管中所有的订单
*   用于抗凝比例筛查失败时删除该管所有订单
*/
void delete_tube_order_by_ar(void *order_no)
{
    struct react_cup *pos = NULL, *n = NULL;
    uint32_t sample_tube_id_tmp = 0;
    uint32_t orderno = (int)order_no;
    needle_s_cmd_t tmp_cmd = NEEDLE_S_NONE;

    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == orderno) {
            sample_tube_id_tmp = pos->cup_sample_tube.sample_tube_id;
            break;
        }
    }
    LOG("delete tube id = %d\n", sample_tube_id_tmp);
    pos = NULL;
    n = NULL;
    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_sample_tube.sample_tube_id == sample_tube_id_tmp) {
            if (pos->cup_pos == POS_CUVETTE_SUPPLY_INIT) {
                /* 进杯盘中直接删除 */
                report_order_state(pos->order_no, OD_ERROR);
                list_del(&pos->mainsibling);
                free(pos);
            } else if ((pos->cup_pos >= POS_PRE_PROCESSOR) && (pos->cup_pos <= POS_PRE_PROCESSOR_MIX2)) {
                tmp_cmd = get_needle_s_cmd();
                if (NEEDLE_S_DILU1_SAMPLE == tmp_cmd || NEEDLE_S_DILU2_R1 == tmp_cmd || NEEDLE_S_DILU2_R1_TWICE == tmp_cmd) {
                    /* 分杯稀释周期的反应杯不能直接设置丢杯，需要等待正常周期 */
                    pos->cup_delay_active = CUP_INACTIVE;
                } else {
                    /* 前处理上的反应杯直接设置为丢杯 */
                    pos->cup_active = CUP_INACTIVE;
                }
                report_order_state(pos->order_no, OD_ERROR);
                pos->cup_active = CUP_INACTIVE;
            }
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
    react_cup_list_show();
    report_order_remain_checktime(get_order_checktime());
}

/*
*   删除当前订单所在样本管中所有的订单
*   用于带帽穿刺液面探测失败时删除该管所有订单
*/
void delete_tube_order(void *order_no)
{
    struct react_cup *pos = NULL, *n = NULL;
    uint32_t sample_tube_id_tmp = 0;
    uint32_t orderno = (int)order_no;

    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == orderno) {
            sample_tube_id_tmp = pos->cup_sample_tube.sample_tube_id;
            break;
        }
    }
    LOG("delete tube id = %d\n", sample_tube_id_tmp);
    pos = NULL;
    n = NULL;
    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_sample_tube.sample_tube_id == sample_tube_id_tmp) {
            if (pos->cup_pos == POS_CUVETTE_SUPPLY_INIT) {
                /* 进杯盘中直接删除 */
                report_order_state(pos->order_no, OD_ERROR);
                list_del(&pos->mainsibling);
                free(pos);
            } else if ((pos->cup_pos >= POS_PRE_PROCESSOR) && (pos->cup_pos <= POS_PRE_PROCESSOR_MIX2)) {
                if (pos->cup_type == DILU_CUP) {
                    if (pos->cup_dilu_attr.add_state == CUP_STAT_UNUSED) {
                        /* 稀释杯因穿刺周期暂时被改为该位置，实际没有到达此位置，直接删除 */
                        report_order_state(pos->order_no, OD_ERROR);
                        list_del(&pos->mainsibling);
                        free(pos);
                    }
                } else if (pos->cup_type == TEST_CUP) {
                    if (pos->cup_test_attr.needle_s.r_x_add_stat == CUP_STAT_UNUSED) {
                        /* 测试杯因穿刺周期暂时被改为该位置，实际没有到达此位置，直接删除 */
                        report_order_state(pos->order_no, OD_ERROR);
                        list_del(&pos->mainsibling);
                        free(pos);
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
    react_cup_list_show();
    report_order_remain_checktime(get_order_checktime());
}

/* 根据订单号删除杯子信息 */
void delete_order_by_order_no(uint32_t order_no)
{
    struct react_cup *pos = NULL, *n = NULL;
    needle_s_cmd_t tmp_cmd = NEEDLE_S_NONE;

    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == order_no) {
            if (pos->cup_pos == POS_CUVETTE_SUPPLY_INIT) {
                /* 进杯盘中直接删除 */
                list_del(&pos->mainsibling);
                free(pos);
            } else if (pos->cup_pos >= POS_INCUBATION_WORK_1 && pos->cup_pos <= POS_INCUBATION_WORK_30) {
                /* 孵育位上的反应杯通知孵育模块需要丢弃 */
                incubation_inactive_by_order(order_no);
                /* 前处理未转运但位置改变为孵育位的设置丢杯状态 */
                pos->cup_active = CUP_INACTIVE;
            } else if (pos->cup_pos >= POS_PRE_PROCESSOR && pos->cup_pos <= POS_PRE_PROCESSOR_MIX2) {
                tmp_cmd = get_needle_s_cmd();
                if (NEEDLE_S_DILU1_SAMPLE == tmp_cmd || NEEDLE_S_DILU2_R1 == tmp_cmd || NEEDLE_S_DILU2_R1_TWICE == tmp_cmd) {
                    /* 分杯稀释周期的反应杯不能直接设置丢杯，需要等待正常周期 */
                    pos->cup_delay_active = CUP_INACTIVE;
                    pos->cup_active = CUP_INACTIVE;
                } else {
                    /* 前处理上的反应杯直接设置为丢杯 */
                    pos->cup_active = CUP_INACTIVE;
                }
            }
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);
    report_order_state(order_no, OD_ERROR);
}

/* 根据订单号更新杯子试剂信息 */
int update_reag_pos_info(int which, int order, int pos1, int pos2)
{
    struct react_cup * pos = NULL;
    struct react_cup * n = NULL;
    int old_pos1 = 0;
    int old_pos2 = 0;
    int new_pos1 = 0;
    int new_pos2 = 0;

    new_pos1 = pos1;
    new_pos2 = pos2;
    LOG("reag_switch: new_pos1 = %d(ori = %d), new_pos2 = %d(ori = %d).\n", new_pos1, pos1, new_pos2, pos2);
    pthread_mutex_lock(&cup_list_mtx);
    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->order_no == order) {
            if (which == UPDATE_DILU) {
                old_pos1 = pos->cup_sample_tube.diluent_pos;
                pos->cup_sample_tube.diluent_pos = pos1;
                LOG("reag_switch: UpdateOrder: UPDATE_DILU(order = %d, cup_type = %d, cup_pos = %d), %d -> %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, pos1);
            } else if (which == UPDATE_QC) {
                old_pos1 = pos->cup_sample_tube.sample_index;
                pos->cup_sample_tube.sample_index = pos1;
                LOG("reag_switch: UpdateOrder: UPDATE_QC(order = %d, cup_type = %d, cup_pos = %d), %d -> %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, pos1);
            } else if (which == UPDATE_R2) {
                old_pos1 = pos->cup_test_attr.needle_r2.needle_pos;
                pos->cup_test_attr.needle_r2.pos_idx = pos1;
                pos->cup_test_attr.needle_r2.needle_pos = new_pos1;
                LOG("reag_switch: UpdateOrder: UPDATE_R2(order = %d, cup_type = %d, cup_pos = %d),"
                    " %d -> %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, new_pos1);
            } else if (which == UPDATE_R1 || which == UPDATE_FACTOR) {
                old_pos1 = pos->cup_test_attr.needle_r1[0].needle_pos;
                pos->cup_test_attr.needle_r1[0].pos_idx = pos1;
                pos->cup_test_attr.needle_r1[0].needle_pos = new_pos1;
                LOG("reag_switch: UpdateOrder: UPDATE_R1 / UPDATE_FACTOR(order = %d, cup_type = %d, cup_pos = %d),"
                    " %d -> %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, new_pos1);
            } else if (which == UPDATE_FACTOR + UPDATE_R1) {
                old_pos1 = pos->cup_test_attr.needle_r1[1].needle_pos;
                pos->cup_test_attr.needle_r1[1].pos_idx = pos1;
                pos->cup_test_attr.needle_r1[1].needle_pos = new_pos1;
                LOG("reag_switch: UpdateOrder: UPDATE_R1(with factor)(order = %d, cup_type = %d, cup_pos = %d),"\
                    " %d -> %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, new_pos1);
            } else if (which == UPDATE_R1 + UPDATE_R2) {
                old_pos1 = pos->cup_test_attr.needle_r1[0].needle_pos;
                pos->cup_test_attr.needle_r1[0].pos_idx = pos1;
                pos->cup_test_attr.needle_r1[0].needle_pos = new_pos1;
                old_pos2 = pos->cup_test_attr.needle_r2.needle_pos;
                pos->cup_test_attr.needle_r2.pos_idx = pos2;
                pos->cup_test_attr.needle_r2.needle_pos = new_pos2;
                LOG("reag_switch: UpdateOrder: UPDATE_R1+R2(order = %d, cup_type = %d, cup_pos = %d),"
                    " %d + %d -> %d + %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, old_pos2, new_pos1, new_pos2);
            } else if (which == UPDATE_FACTOR + UPDATE_R1 + UPDATE_R2) {
                old_pos1 = pos->cup_test_attr.needle_r1[1].needle_pos;
                pos->cup_test_attr.needle_r1[1].pos_idx = pos1;
                pos->cup_test_attr.needle_r1[1].needle_pos = new_pos1;
                old_pos2 = pos->cup_test_attr.needle_r2.needle_pos;
                pos->cup_test_attr.needle_r2.pos_idx = pos2;
                pos->cup_test_attr.needle_r2.needle_pos = new_pos2;
                LOG("reag_switch: UpdateOrder: UPDATE_R1(with factor)+R2(order = %d, cup_type = %d, cup_pos = %d),"
                    "%d + %d -> %d + %d.\n",
                    order, pos->cup_type, pos->cup_pos, old_pos1, old_pos2, new_pos1, new_pos2);
            } else {
                LOG("reag_switch: UpdateOrder: error, please check.\n");
                pthread_mutex_unlock(&cup_list_mtx);
                return -1;
            }
        }
    }
    pthread_mutex_unlock(&cup_list_mtx);

    return 0;
}


/*
 *  样本针模块获取当前反应杯信息
 *  信息由参数work_cups_list携带返回
 *  返回值0表示成功获取新进杯，1表示当前无可用新反应杯，-1表示处理异常
 *  当返回1或-1时cup可能为NULL
 */
static int req_module_needle_s(struct list_head **work_cups_list)
{
    struct react_cup *pos = NULL, *n = NULL;

    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_pos >= POS_PRE_PROCESSOR && pos->cup_pos <= POS_PRE_PROCESSOR_MIX2) {
            add_cup_tail(pos, NEEDLE_S_CUP_LIST);
        }
    }

    *work_cups_list = &needle_s_work_cup_list;
    return 0;
}

/*
 *  试剂针模块获取当前反应杯信息
 *  信息由参数work_cups_list携带返回
 *  返回值0表示成功获取新进杯，1表示当前无可用新反应杯，-1表示处理异常
 *  当返回1或-1时cup可能为NULL
 */
static int req_module_needle_r2(struct list_head **work_cups_list)
{
    struct react_cup *pos = NULL, *n = NULL;

    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_pos >= POS_MAGNECTIC_WORK_1 && pos->cup_pos <= POS_OPTICAL_MIX) {
            add_cup_tail(pos, NEEDLE_R2_CUP_LIST);
        }
    }

    *work_cups_list = &needle_r2_work_cup_list;
    return 0;
}

/*
 *  抓手模块获取当前反应杯信息
 *  信息由参数work_cups_list携带返回
 *  返回值0表示成功获取新进杯，1表示当前无可用新反应杯，-1表示处理异常
 *  当返回1或-1时cup可能为NULL
 */
static int req_module_catcher(struct list_head **work_cups_list)
{
    struct react_cup *pos = NULL, *n = NULL;

    list_for_each_entry_safe(pos, n, &react_cup_list, mainsibling) {
        if (pos->cup_pos >= POS_CUVETTE_SUPPLY_INIT && pos->cup_pos <= POS_OPTICAL_WORK_8) {
            add_cup_tail(pos, CATCHER_CUP_LIST);
        }
    }
    *work_cups_list = &catcher_work_cup_list;
    return 0;
}


/*
 *  生产模块获取当前可用反应杯信息
 *  参数module表示具体生产模块
 *  信息由参数work_cups_list携带返回
 *  返回值0表示成功获取可用反应杯信息，1表示当前无可用反应杯，-1表示处理异常
 *  当返回1或-1时cup可能为NULL
 */
int module_request_cups(module_t module, struct list_head **work_cups_list)
{
    switch (module) {
    case MODULE_NEEDLE_S:
        return req_module_needle_s(work_cups_list);
    case MODULE_NEEDLE_R2:
        return req_module_needle_r2(work_cups_list);
    case MODULE_CATCHER:
        return req_module_catcher(work_cups_list);
    default:
        LOG("[%s %d]:no such module.\n", __FUNCTION__, __LINE__);
        return -1;
    }
    return 0;
}


/*
 *  样本前处理模块上报当前反应杯信息
 *  信息由参数work_cup携带
 *  返回值0表示成功获取新进杯，1表示当前无可用新反应杯，-1表示处理异常
 *  当返回1或-1时cup可能为NULL
 */
static int resp_module_needle_s(int result)
{
    do_work_module_brocast(MODULE_NEEDLE_S, result);
    return 0;
}

static int resp_module_needle_r2(int result)
{
    do_work_module_brocast(MODULE_NEEDLE_R2, result);
    return 0;
}

static int resp_module_catcher(int result)
{
    do_work_module_brocast(MODULE_CATCHER, result);
    return 0;
}

/*
 *  生产模块上报当前模块执行完后反应杯信息
 *  参数module表示具体生产模块
 *  信息由参数work_cup携带
 *  result:
        0:模块停止工作
        1:模块正常工作
 *  返回值0表示反应杯信息更新成功，1表示存在信息不符的反应杯，-1表示处理异常
 *  当返回1时cup信息出现错误
 */
int module_response_cups(module_t module, int result)
{
    switch (module) {
    case MODULE_NEEDLE_S:
        return resp_module_needle_s(result);
    case MODULE_NEEDLE_R2:
        return resp_module_needle_r2(result);
    case MODULE_CATCHER:
        return resp_module_catcher(result);
    default:
        LOG("[%s %d]:no such module.\n", __FUNCTION__, __LINE__);
        return -1;
    }
    return 0;
}

void react_cup_list_test(int orderno_idx, int cnt, int tube_id, needle_pos_t r1_pos, needle_pos_t r2_pos, int r2_clean_type, int r2_take_ul, int cup_pos)
{
    static int cup_idx = 1;
    struct react_cup *test_cup;
    int i = 0;

//    struct react_cup *dilu_cup;
    for(i=0;i<cnt;i++) {
//    dilu_cup = new_a_cup();
//    dilu_cup->order_no = orderno_idx+i+1;
//    dilu_cup->cup_id = cup_idx++;
//    dilu_cup->cup_type = DILU_CUP;
//    dilu_cup->cup_pos = POS_CUVETTE_SUPPLY_INIT;
//    dilu_cup->cup_sample_tube.rack_idx = tube_id;
//    dilu_cup->cup_sample_tube.rack_type = NORMAL_SAMPLE;
//    dilu_cup->cup_sample_tube.sample_tube_id = tube_id;
//    dilu_cup->cup_sample_tube.sp_hat = SP_NO_HAT;
//    dilu_cup->cup_sample_tube.sample_volume = 800;
//    dilu_cup->cup_sample_tube.test_cnt = cnt;
//    dilu_cup->cup_sample_tube.diluent_pos = POS_DILUENT_2;
//    dilu_cup->cup_dilu_attr.needle_pos = POS_REAGENT_TABLE_I3;
//    dilu_cup->cup_dilu_attr.take_dilu_ul = 120;
//    dilu_cup->cup_dilu_attr.take_ul = 80;
//    dilu_cup->cup_dilu_attr.add_state = CUP_STAT_UNUSED;
//    dilu_cup->cup_dilu_attr.trans_state = CUP_STAT_UNUSED;
//    add_cup_tail(dilu_cup, REACT_CUP_LIST);

    test_cup = new_a_cup();
    test_cup->order_no = orderno_idx+i+1;
    test_cup->cup_id = cup_idx++;
    test_cup->cup_type = TEST_CUP;
    test_cup->cup_pos = cup_pos;
    test_cup->cup_sample_tube.rack_idx = tube_id;
    test_cup->cup_sample_tube.rack_type = NORMAL_SAMPLE;
    test_cup->cup_sample_tube.sample_tube_id = tube_id;
    test_cup->cup_sample_tube.sp_hat = SP_NO_HAT;
    test_cup->cup_sample_tube.sample_volume = 800;
    test_cup->cup_sample_tube.test_cnt = cnt;
    test_cup->cup_sample_tube.diluent_pos = POS_DILUENT_2;
    test_cup->cup_test_attr.needle_s.r_x_add_stat = CUP_STAT_UNUSED;
    test_cup->cup_test_attr.needle_s.take_dilu_ul = 0;
    test_cup->cup_test_attr.needle_s.take_mix_ul = 0;
    test_cup->cup_test_attr.needle_s.take_ul = 50;
    test_cup->cup_test_attr.needle_s.needle_pos = POS_REAGENT_TABLE_O10;
    test_cup->cup_test_attr.needle_s.post_clean.type = NORMAL_CLEAN;
    test_cup->cup_test_attr.needle_r1[R1_ADD1].r_x_add_stat = CUP_STAT_UNUSED;
    test_cup->cup_test_attr.needle_r1[R1_ADD2].r_x_add_stat = CUP_STAT_USED;
    test_cup->cup_test_attr.needle_r1[R1_ADD1].needle_pos = r1_pos;
    test_cup->cup_test_attr.needle_r1[R1_ADD2].needle_pos = POS_REAGENT_TABLE_O1;
    test_cup->cup_test_attr.needle_r1[R1_ADD1].take_ul = 40;
    test_cup->cup_test_attr.needle_r1[R1_ADD2].take_ul = 0;
    test_cup->cup_test_attr.needle_r2.r_x_add_stat = CUP_STAT_UNUSED;
    test_cup->cup_test_attr.needle_r2.take_ul = r2_take_ul;
    test_cup->cup_test_attr.needle_r2.needle_pos = r2_pos;
    test_cup->cup_test_attr.needle_r2.post_clean.type = r2_clean_type;
    test_cup->cup_test_attr.test_cup_incubation.incubation_mix_enable = ATTR_DISABLE;
    test_cup->cup_test_attr.test_cup_incubation.mix_rate = 10000;
    test_cup->cup_test_attr.test_cup_incubation.mix_time = 20000;
    test_cup->cup_test_attr.test_cup_incubation.incubation_time = 30;

    test_cup->cup_test_attr.test_cup_magnectic.magnectic_enable = ATTR_ENABLE;
    test_cup->cup_test_attr.test_cup_magnectic.magnectic_power = 0;
    test_cup->cup_test_attr.test_cup_magnectic.magnectic_stat = CUP_STAT_UNUSED;
    test_cup->cup_test_attr.test_cup_magnectic.mag_beed_clot_percent = 50;
    test_cup->cup_test_attr.test_cup_magnectic.mag_beed_max_detect_seconds = 30;
    test_cup->cup_test_attr.test_cup_magnectic.mag_beed_min_detect_seconds = 3;

    test_cup->cup_test_attr.test_cup_optical.optical_enable = ATTR_DISABLE;
    test_cup->cup_test_attr.test_cup_optical.main_wave = 660;
    test_cup->cup_test_attr.test_cup_optical.optical_main_seconds = 40;
    test_cup->cup_test_attr.test_cup_optical.optical_mix_stat = CUP_STAT_UNUSED;
    test_cup->cup_test_attr.test_cup_optical.optical_mix_time = 4500;
    test_cup->cup_test_attr.test_cup_optical.optical_mix_rate = 10000;

    add_cup_tail(test_cup, REACT_CUP_LIST);
    }
}

