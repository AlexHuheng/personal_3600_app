#include "module_cuvette_supply.h"

#define CUVETTE_SUPPLY_DEBUG(format, ...) LOG("cuvette_supply_func: " format, ##__VA_ARGS__)

static int module_cuvette_fault = 0;
static sem_t sem_reaction_cup;

typedef enum {
    PRE_MONITOR = 0,
    RUNNING_MONITOR,
    FALLBACK_MONITOR
} bldc_monitor_t;

static cuvette_supply_para_t g_para = {
    .idx        = REACTION_CUP_INSIDE,
    .switch_io  = MICRO_SWITCH_BUCKLE,
    .gate_io    = MICRO_GATE_CUVETTE,
    .gate_open  = 0,
    .bldc_idx   = CUVETTE_BLDC_INDEX,
    .stime      = 0,
    .timeout    = BLDC_RESET_TIMEOUT,
    .state      = CUP_INIT,
    .report     = 0,
    .priority   = 0,
    .running    = 0
};

void cuvette_supply_notify(void)
{
    sem_post(&sem_reaction_cup);
}

static void cuvette_supply_wait(void)
{
    sem_wait(&sem_reaction_cup);
}

static int cuvette_supply_fault_get(void)
{
    return module_cuvette_fault;
}

static void cuvette_supply_fault_set(void)
{
    module_cuvette_fault = 1;
}

void cuvette_supply_fault_clear(void)
{
    module_cuvette_fault = 0;
}

static inline int bldc_rads_get(int idx)
{
    int data = 0;

    data = slip_bldc_rads_get(idx);

    return data;
}

static inline void bldc_control(int index, bldc_state_t state)
{
    slip_bldc_ctl_set(index, state);
}

static inline char *cuvette_supply_state_string(reaction_cup_state_t state)
{
    switch (state) {
        case CUP_INIT:
            return "Init";
            break;
        case CUP_READY:
            return "Ready";
            break;
        case CUP_RUNNING:
            return "Running";
            break;
        case CUP_GATE:
            return "Gate is opened";
            break;
        case CUP_BUCKLE:
            return "Buckle is opened";
            break;
        case CUP_ERROR:
            return "Supply is failed";
            break;
        default:
            break;
    }

    return "Unknown";
}

cuvette_supply_para_t *cuvette_supply_para_get(void)
{
    return &g_para;
}

static void cuvette_supply_status_reinit(void)
{
    cuvette_supply_para_t *para = &g_para;

    para->report = 0;
    para->mode = MAINTAIN_GET_MODE;
    para->state = CUP_INIT;
    para->led_blink = 0;
}

static reaction_cup_state_t cuvette_supply_bldc_monitor(cuvette_supply_para_t *para, bldc_monitor_t m_state)
{
    int idx = 0;
    int max = 3;
    int rads = 0;
    reaction_cup_state_t state = CUP_INIT;
    int timeout = (m_state == PRE_MONITOR ? 3 : para->timeout);

    usleep(50 * 1000);
    rads = bldc_rads_get(para->bldc_idx);

    if (m_state == PRE_MONITOR) {
        if (rads > BLDC_RADS_LIMIT + 2) {       /* 测试得出转速会出现不稳定现象1->5->6->7->6导致到位误判 */
            for (idx = 0; idx < max; idx++) {   /* 消抖 */
                usleep(10 * 1000);
                rads = bldc_rads_get(para->bldc_idx);
                if (rads > BLDC_RADS_LIMIT + 2) {
                    break;
                }
            }
            if (rads > BLDC_RADS_LIMIT + 2) {
                state = CUP_RUNNING;
                CUVETTE_SUPPLY_DEBUG("pre_monitor: enter running state(rads = %d).\n", rads);
                goto out;
            }
        }
    } else if (m_state == RUNNING_MONITOR) {
        if (rads <= BLDC_RADS_LIMIT) {
            if (rads == 0) {
                for (idx = 0; idx < max; idx++) {
                    usleep(10 * 1000);
                    rads = bldc_rads_get(para->bldc_idx);
                    if (rads) {
                        break;
                    }
                }
            }
            state = CUP_READY;
            CUVETTE_SUPPLY_DEBUG("running_monitor: enter ready state(rads = %d).\n", rads);
            goto out;
        }
    } else if (m_state == FALLBACK_MONITOR) {
        if (rads > BLDC_RADS_LIMIT) {
            state = CUP_RUNNING;
            CUVETTE_SUPPLY_DEBUG("fallback_monitor: enter fallback state(rads = %d).\n", rads);
            goto out;
        }
    }

    if (sys_uptime_sec() - para->stime >= timeout) {
        state = CUP_ERROR;
        CUVETTE_SUPPLY_DEBUG("%s_stage: timeout(cur = %d, s = %d, timeout = %d), rads = %d.\n",
            m_state == PRE_MONITOR ? "Pre_monitor" :
            (m_state == RUNNING_MONITOR ? "Running_monitor" : "Fallback_monitor"),
            sys_uptime_sec(), para->stime, timeout, rads);
    } else if (para->gate_open == 1 && m_state != FALLBACK_MONITOR) {
        state = CUP_GATE;
        CUVETTE_SUPPLY_DEBUG("%s_stage gate is opened.\n",
            m_state == PRE_MONITOR ? "Pre_monitor" :
            (m_state == RUNNING_MONITOR ? "Running_monitor" : "Fallback_monitor"));
    }

out:
    return state;
}

/* 反转松开胶带确保抓杯 */
static inline reaction_cup_state_t bldc_fall_back(reaction_cup_state_t state, int ms)
{
    /* 通量模式下减小电机控制间隔时间 */
    int delay = (get_throughput_mode() ? 500 : ms);
    int interval = (get_throughput_mode() ? 500 : 500);
    reaction_cup_state_t m_state = CUP_INIT;
    cuvette_supply_para_t *para = &g_para;

    usleep(200 * 1000);
    bldc_control(para->bldc_idx, BLDC_STOP);
    usleep(interval * 1000);    /* 控制间隔 */
    bldc_control(para->bldc_idx, BLDC_FORWARD);
    /* 监控反转动作是否执行 */
    para->stime = sys_uptime_sec();
    para->timeout = 3;
    while (1) {
        m_state = cuvette_supply_bldc_monitor(para, FALLBACK_MONITOR);
        if (m_state == CUP_RUNNING) {
            m_state = state;
            usleep(delay * 1000);
            break;
        } else if (m_state == CUP_ERROR || m_state == CUP_GATE) {
            usleep(delay * 1000);
            break;
        }
    }
    bldc_control(para->bldc_idx, BLDC_STOP);

    return m_state;
}

static void cuvette_supply_result_analysis(reaction_cup_get_mode_t mode,
    reaction_cup_index_t index,
    maintain_mode_t m_mode,
    attr_enable_t flag)
{
    char *fid = NULL;
    cuvette_supply_para_t *para = &g_para;
    char alarm_message[FAULT_CODE_LEN] = {0};

    if (get_machine_stat() == MACHINE_STAT_STOP) {
        CUVETTE_SUPPLY_DEBUG("MACHINE_STAT_STOP, result_analysis ignore.\n");
        return;
    }

    /* 无杯盘可用的故障上报 */
    if (index == REACTION_CUP_NONE) {
        if (mode == MAINTAIN_GET_MODE) {
            /* 复位过程无杯盘可用 */
            if (m_mode == POWER_ON) {
                if (para->state == CUP_BUCKLE) {
                    fid = MODULE_FAULT_BUCKLE_ERROR;
                } else {
                    fid = MODULE_FAULT_CUVETTE_LOW;
                }
            }
        } else {
            /* 正常进杯过程无杯盘可用 */
            if (para->state == CUP_BUCKLE) {
                fid = MODULE_FAULT_BUCKLE_ERROR;
            } else {
                fid = MODULE_FAULT_CUVETTE_LOW;
            }
        }
        if (flag != 1) {
            /* 正在进杯过程的无杯可用不置耗材不可用错误 */
            cuvette_supply_fault_set();
        }
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_CUVETTE_SUPPLY, fid);
        report_alarm_message(0, alarm_message);
    }

    return;
}

void cuvette_supply_led_ctrl(int index, int blink)
{
    cuvette_supply_para_t *para = &g_para;
    led_blink_t old = para->led_blink;

    /* 关闭主机后，禁止开灯 */
    if(thrift_slave_client_connect_ctl_get() == 0){
        return ;
    }

    if (index == REACTION_CUP_NONE) {
        para->led_blink = LED_NONE_BLINK;
    } else {
        para->led_blink = blink;
    }

    if (old != para->led_blink) indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_GREEN, para->led_blink);
}

void cuvette_supply_led_ctrl1(void)
{
    cuvette_supply_para_t *para= &g_para;

    if (para->available) {
        cuvette_supply_led_ctrl(REACTION_CUP_INSIDE, LED_BLINK);
    }
}

static reaction_cup_state_t cuvette_supply_timeout_mode(cuvette_supply_para_t *para)
{
    reaction_cup_state_t state = CUP_INIT;

    if (cuvette_supply_fault_get()) {
        CUVETTE_SUPPLY_DEBUG("module_cuvette_fault already, ignore.\n");
        return state;
    }
    slip_bldc_timeout_set(para->bldc_idx, para->timeout);
    para->rads = 0;
    para->stime = sys_uptime_sec();
    bldc_control(para->bldc_idx, BLDC_REVERSE);
    usleep(100 * 1000);
    while (1) {
        /* 监控电机静止到运动阶段CUP_INIT->CUP_RUNNING */
        state = cuvette_supply_bldc_monitor(para, PRE_MONITOR);
        if (state == CUP_RUNNING) {
            break;
        } else if (state == CUP_ERROR || state == CUP_GATE) {
            bldc_fall_back(state, 800);
            return state;
        }
    }

    usleep(100 * 1000);
    while (1) {
        /* 监控电机运动到进杯到位阶段 */
        state = cuvette_supply_bldc_monitor(para, RUNNING_MONITOR);
        if (state == CUP_READY) {
            break;
        } else if (state == CUP_ERROR || state == CUP_GATE) {
            bldc_fall_back(state, 800);
            return state;
        }
    }

    /* 反转800ms确保抓杯 */
    return bldc_fall_back(state, 1200);
}

static void cuvette_supply_func(void *arg)
{
    cuvette_supply_para_t *para = (cuvette_supply_para_t *)arg;

    if (para->running) {
        CUVETTE_SUPPLY_DEBUG("In running, ignore.\n");
        return;
    }
    para->running = 1;
    /* 检查舱门 */
    if (gpio_get(para->gate_io) == 1) {
        CUVETTE_SUPPLY_DEBUG("Gate is opened.\n");
        para->gate_open = 1;
        para->state = CUP_GATE;
        goto out;
    } else {
        para->gate_open = 0;
        if (!gpio_get(para->switch_io)) {
            /* 卡扣未到位 */
            CUVETTE_SUPPLY_DEBUG("Buckle is opened.\n");
            para->state = CUP_BUCKLE;
            goto out;
        }

        if (para->mode == NORMAL_GET_MODE) {
            if (para->available == 0) {
                CUVETTE_SUPPLY_DEBUG("Not available, ignore.\n");
                para->state = CUP_ERROR;
                goto out;
            } else {
                if (para->state == CUP_READY) {
                    CUVETTE_SUPPLY_DEBUG("Already ready, ignore.\n");
                    goto out;
                } else if (para->state == CUP_ERROR || para->state == CUP_GATE || para->state == CUP_BUCKLE) {
                    CUVETTE_SUPPLY_DEBUG("Error already(%s), ignore.\n", cuvette_supply_state_string(para->state));
                    goto out;
                }
            }
        }

        para->state = CUP_INIT;
        para->timeout = (para->mode == MAINTAIN_GET_MODE ? BLDC_RESET_TIMEOUT : BLDC_NORMAL_TIMEOUT);
        para->state = cuvette_supply_timeout_mode(para);
    }

out:
    if (para->re_close || para->typein) {
        if (para->re_close) {
            para->re_close = 0;
        } else {
            para->typein = 0;
        }
    }
    CUVETTE_SUPPLY_DEBUG("supply end, state = %s.\n", cuvette_supply_state_string(para->state));
    para->running = 0;
    return;
}

static void * cuvette_supply_thread(void *arg)
{
    cuvette_supply_para_t *para = &g_para;

    while (1) {
        cuvette_supply_wait();
        para->mode = NORMAL_GET_MODE;
        if (para->state == CUP_INIT) work_queue_add(cuvette_supply_func, para);
    }

    return NULL;
}

static void cuvette_supply_gate_check_func(void *arg)
{
    int idx = 0;
    int is_close = 0;
    int re_check = 5;
    int io_stat = 0;
    char alarm_message[FAULT_CODE_LEN] = {0};
    cuvette_supply_para_t *para = (cuvette_supply_para_t *)arg;

    para->gate_running = 1;
    io_stat = gpio_get(para->gate_io);
    if (io_stat == 1 && para->gate_open == 0) {
        for (idx = 0; idx < re_check; idx++) {
            usleep(20 * 1000);
            if (gpio_get(para->gate_io) == 0) {
                is_close += 1;
            }
        }
        if (is_close >= re_check / 2) {
            /* 认为误触发 */
            para->gate_running = 0;
            return;
        }
        /* 舱门中途被拉出，反转并停止电机 */
        CUVETTE_SUPPLY_DEBUG("Gate has been opened, available = %d, priority = %d, in_use = %d.\n",
            para->available, para->priority, para->in_use);
        para->gate_open = 1;
        para->state = CUP_GATE;
        if (get_machine_stat() == MACHINE_STAT_RUNNING) {
            cuvette_supply_fault_set();
            fault_code_generate(alarm_message, MODULE_CLASS_FAULT_CUVETTE_SUPPLY, MODULE_FAULT_CUVETTE_PULL_OUT);
            report_alarm_message(0, alarm_message);
        }
        indicator_led_set(LED_CUVETTE_INS_ID, LED_COLOR_GREEN, LED_NONE_BLINK);
        if (para->running) {
            for (idx = 0; idx < BLDC_NORMAL_TIMEOUT; idx++) {
                sleep(1);
                CUVETTE_SUPPLY_DEBUG("wait move finish(count = %d).\n", idx);
                if (!para->running) break;
            }
        }

        if (!para->running) {
            CUVETTE_SUPPLY_DEBUG("start fallback.\n");
            para->running = 1;
            bldc_fall_back(para->state, 500);
            para->running = 0;
        }
    } else if (io_stat == 0 && para->gate_open == 1) {
        /* 舱门中途推入，可能更换了新杯盘，BLDC_RESET_TIMEOUT超时模式进杯 */
        CUVETTE_SUPPLY_DEBUG("Gate re-closed, available = %d, priority = %d.\n", para->available, para->priority);
        cuvette_supply_fault_clear();
        cuvette_supply_status_reinit();
        para->gate_open = 0;
        para->re_close = 1;
        if (!gpio_get(para->switch_io)) {
            CUVETTE_SUPPLY_DEBUG("Buckle is opened.\n");
            para->state = CUP_BUCKLE;
            if (get_machine_stat() == MACHINE_STAT_RUNNING) {
                cuvette_supply_result_analysis(NORMAL_GET_MODE, REACTION_CUP_NONE, POWER_ON, 0);
            } else if (get_machine_stat() == MACHINE_STAT_STANDBY) {
                fault_code_generate(alarm_message, MODULE_CLASS_FAULT_CUVETTE_SUPPLY, MODULE_FAULT_BUCKLE_ERROR);
                report_alarm_message(0, alarm_message);
            }
        } else {
            if (para->available) {
                for (idx = 0; idx < BLDC_NORMAL_TIMEOUT; idx++) {
                    /* 如果反复推拉杯盘舱门，等待拉开舱门的反转执行完毕 */
                    if (!para->running) {
                        CUVETTE_SUPPLY_DEBUG("not running(count = %d), ready supply.\n", idx);
                        break;
                    }
                    sleep(1);
                }
                if (gpio_get(para->gate_io) == 0) {  //舱门可能又被拉出
                    CUVETTE_SUPPLY_DEBUG("not running, gate closed, start supply.\n");
                    work_queue_add(cuvette_supply_func, para);
                }
            }
        }
    }
    para->gate_running = 0;
}

static void *cuvette_supply_gate_check_thread(void *arg)
{
    cuvette_supply_para_t *para = &g_para;

    while (1) {
        if (!para->gate_running) work_queue_add(cuvette_supply_gate_check_func, para);

        /* 非运行状态下静默所有LED灯 */
        if (get_machine_stat() != MACHINE_STAT_RUNNING) {
            cuvette_supply_led_ctrl(REACTION_CUP_NONE, LED_NONE_BLINK);
        }

        usleep(100 * 1000);
    }

    return NULL;
}

int cuvette_supply_get(reaction_cup_get_mode_t mode)
{
    int idx = REACTION_CUP_NONE;
    cuvette_supply_para_t *para = &g_para;

    /* 恢复杯盘为初始状态，以便下次能正确进杯 */
    if (para->state == CUP_READY) {
        para->state = CUP_INIT;
        idx = REACTION_CUP_INSIDE;
        cuvette_supply_led_ctrl(REACTION_CUP_INSIDE, LED_BLINK);
        CUVETTE_SUPPLY_DEBUG("cup Ready.\n");
    } else {
        /*
            杯盘实际量和上位机剩余量不符时，杯盘使用完毕(上位机还有剩余)，进入加样停;
            更换新杯盘，立即点击开始，流程3s后会请求反应杯，如果新上的杯盘胶带过长，此时正处于进杯过程，返回无反应杯可用，再次进入加样停；
            此情况不再置杯盘耗材不可用cuvette_supply_fault_set，以便用户仅点击开始(无录入新杯盘信息或推拉舱门触发再次进杯)还能再次恢复。
        */
        CUVETTE_SUPPLY_DEBUG("cup Not ready(is_running = %d).\n", para->running);
    }
    cuvette_supply_result_analysis(mode, idx, POWER_ON, para->running);

    return idx;
}

int cuvette_supply_reset(maintain_mode_t mode)
{
    int idx = REACTION_CUP_NONE;
    int max = BLDC_RESET_TIMEOUT;
    struct timespec ts = {1, 0};
    cuvette_supply_para_t *para = &g_para;

    CUVETTE_SUPPLY_DEBUG("reset start.\n");
    if (para->running) {
        CUVETTE_SUPPLY_DEBUG("reset, In_running(%d) or enable_state(%d), ignore.\n",
            para->running, para->available);
    } else {
        if (para->state == CUP_INIT) {
            /* 如果上次异常停机电机还未来得及反转，拉紧状态，防止后续进杯转速获取错误，回退 */
            bldc_fall_back(para->state, 1000);
            work_queue_add(cuvette_supply_func, para);
        }
    }
    nanosleep(&ts, NULL);

again:
    if (max-- >= 0 && para->state < CUP_READY) {
        nanosleep(&ts, NULL);
        goto again;
    }

    if (para->state == CUP_READY) {
        idx = REACTION_CUP_INSIDE;
        cuvette_supply_led_ctrl(REACTION_CUP_INSIDE, LED_BLINK);
    }
    CUVETTE_SUPPLY_DEBUG("reset done, state = %s.\n", cuvette_supply_state_string(para->state));
    cuvette_supply_result_analysis(MAINTAIN_GET_MODE, idx, mode, 0);

    return idx;
}

/* 每次启动上位机会自动下发所有杯盘参数 */
void cuvette_supply_para_set(consum_info_t *info)
{
    cuvette_supply_para_t *para = &g_para;

    CUVETTE_SUPPLY_DEBUG("cuvette_supply_para_set enable = %d, priority = %d, serno = %d, strno = %s.\n",
                        info->enable, info->priority, info->serno, info->strlotno);
    cuvette_supply_fault_clear();
    para->available = info->enable;
    para->priority  = info->priority;
    if (info->serno == para->serno && strcmp(para->strlotno, info->strlotno) == 0) {
        /* 反应杯盘批号+序列号相同 */
        CUVETTE_SUPPLY_DEBUG("Info not change, ignore.\n");
    } else {
        if (para->serno) CUVETTE_SUPPLY_DEBUG("old info strno = %s, serno = %d.\n", para->strlotno, para->serno);
        para->serno = info->serno;
        strcpy(para->strlotno, info->strlotno);
        /* 录入后也立即尝试检查杯盘是否可用，防止上位机计数为0置加样停后，重新录入杯盘但未拖拉舱门，点击继续检测提示杯盘不可用的错误 */
        if (para->available && !para->running && para->state != CUP_READY) {
            para->typein = 1;
            cuvette_supply_status_reinit();
            bldc_fall_back(para->state, 1000);
            work_queue_add(cuvette_supply_func, para);
        } else {
            CUVETTE_SUPPLY_DEBUG("typein, nothing todo(enable = %d, running = %d, state = %s).\n",
                para->available, para->running, cuvette_supply_state_string(para->state));
        }
    }
}

static void cuvette_supply_test(void)
{
    consum_info_t arg = {0};

    arg.enable = 1;
    arg.index = 0;
    arg.priority = 1;
    arg.serno = 888;
    strcpy(arg.strlotno, "test");
    cuvette_supply_para_set(&arg);
}

int thrift_cuvette_supply_func(int io_idx)
{
    cuvette_supply_para_t *para = &g_para;
    reaction_cup_get_mode_t mode = NORMAL_GET_MODE;
    int timeout = BLDC_NORMAL_TIMEOUT;
    int idx = 0;

    if (io_idx == BLDC_FORWARD_TEST || io_idx == BLDC_REVERSE_TEST || io_idx == BLDC_STOP_TEST) {
        return slip_bldc_ctl_set(para->bldc_idx, io_idx == BLDC_FORWARD_TEST ? BLDC_FORWARD :
                                (io_idx == BLDC_REVERSE_TEST ? BLDC_REVERSE : BLDC_STOP));
    } else {
        para->state = CUP_INIT;
        if (para->available == 0) {
            cuvette_supply_test();
            timeout = BLDC_RESET_TIMEOUT;
            mode = MAINTAIN_GET_MODE;
        } else {
            cuvette_supply_notify();
            timeout = BLDC_RESET_TIMEOUT;
        }
        for (idx = 0; idx < timeout; idx++) {
            sleep(1);
            if (para->state == CUP_READY || para->state == CUP_GATE ||
                para->state == CUP_BUCKLE || para->state == CUP_ERROR) {
                break;
            }
        }

        return cuvette_supply_get(mode);
    }
}

int module_cuvette_supply_init(void)
{
    pthread_t pid = 0, pid1 = 0;

    CUVETTE_SUPPLY_DEBUG("module init.\n");
    sem_init(&sem_reaction_cup, 0, 0);
    cuvette_supply_led_ctrl(REACTION_CUP_INSIDE, LED_NONE_BLINK);
    if (pthread_create(&pid, NULL, cuvette_supply_thread, NULL) != 0) {
        LOG("cuvette_supply_thread failed.\n");
        goto bad;
    }

    if (pthread_create(&pid1, NULL, cuvette_supply_gate_check_thread, NULL) != 0) {
        LOG("cuvette_supply_gate_check_thread failed.\n");
        goto bad;
    }

    pthread_detach(pid);
    pthread_detach(pid1);

    //cuvette_supply_test();

    return 0;
bad:
    return -1;
}
