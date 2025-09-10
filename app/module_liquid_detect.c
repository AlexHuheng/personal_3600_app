#include "module_liquid_detect.h"

#define DETECT_LOG(format, ...) LOG("ld_log -> " format, ##__VA_ARGS__)

static struct list_head reag_info_list;     /* 记录试剂仓内试剂和稀释液相关信息 */
static struct list_head reag_remain_list;   /* 记录余量探测相关信息 */

static int fd_debug_uart = -1;
static int fd_dilu_scanner_uart = -1;
int g_detect_running = 0;

liquid_detect_misc_t g_misc_array[NEEDLE_TYPE_MAX] = {
    {
        .slip_id    = SLIP_NODE_LIQUID_DETECT_1,
        .result     = LIQ_INIT,
        .signo      = LEAVE_S_LIQUID,
        .m_lock     = PTHREAD_MUTEX_INITIALIZER,
    },
    {
        .slip_id    = SLIP_NODE_LIQUID_DETECT_1,
        .result     = LIQ_INIT,
        .signo      = LEAVE_S_LIQUID,
        .m_lock     = PTHREAD_MUTEX_INITIALIZER,
    },
    {
        .slip_id    = SLIP_NODE_LIQUID_DETECT_2,
        .result     = LIQ_INIT,
        .signo      = LEAVE_R2_LIQUID,
        .m_lock     = PTHREAD_MUTEX_INITIALIZER,
    },
    {
        .slip_id    = SLIP_NODE_LIQUID_DETECT_1,
        .result     = LIQ_INIT,
        .signo      = LEAVE_S_LIQUID,
        .m_lock     = PTHREAD_MUTEX_INITIALIZER,
    },
    {
        .slip_id    = SLIP_NODE_LIQUID_DETECT_1,
        .result     = LIQ_INIT,
        .signo      = LEAVE_S_LIQUID,
        .m_lock     = PTHREAD_MUTEX_INITIALIZER,
    }
};

static liquid_detect_step_ul_t s_dilu_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 0.665},
    {.step_left = 1503,     .vol_left = 1000,       .ratio = 1.326},
    {.step_left = 2257,     .vol_left = 2000,       .ratio = 2.747},
    {.step_left = 2621,     .vol_left = 3000,       .ratio = 2.288},
    {.step_left = 3058,     .vol_left = 4000,       .ratio = 2.123},
    {.step_left = 3529,     .vol_left = 5000,       .ratio = 1.952},
    {.step_left = 4541,     .vol_left = 7000,       .ratio = 1.877},
    {.step_left = 6090,     .vol_left = 10000,      .ratio = 1.842},
    {.step_left = 8803,     .vol_left = 15000,      .ratio = 1.838},
    {.step_left = 9347,     .vol_left = 16000,      .ratio = 1.880},
    {.step_left = 9879,     .vol_left = 17000,      .ratio = 1.876},
    {.step_left = 10412,    .vol_left = 18000,      .ratio = 1.916},
    {.step_left = 10934,    .vol_left = 19000,      .ratio = 2.320},
    {.step_left = 11365,    .vol_left = 20000,      .ratio = 2.370},
    {.step_left = 11787,    .vol_left = 21000,      .ratio = 1.976},
    {.step_left = 12293,    .vol_left = 22000,      .ratio = 2.247},
    {.step_left = 12738,    .vol_left = 23000,      .ratio = 1.157},
    {.step_left = 13602,    .vol_left = 24000,      .ratio = 0.509},
    {.step_left = 15566,    .vol_left = 25000,      .ratio = 0.387},
    {.step_left = 18150,    .vol_left = 26000,      .ratio = 0.437},
    {.step_left = 19293,    .vol_left = 26500,      .ratio = 0.437}
};

static liquid_detect_step_ul_t s_ep_cup_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 0.047},
    {.step_left = 2108,     .vol_left = 100,        .ratio = 0.074},
    {.step_left = 3463,     .vol_left = 200,        .ratio = 0.110},
    {.step_left = 4372,     .vol_left = 300,        .ratio = 0.112},
    {.step_left = 5262,     .vol_left = 400,        .ratio = 0.144},
    {.step_left = 5958,     .vol_left = 500,        .ratio = 0.173},
    {.step_left = 7693,     .vol_left = 800,        .ratio = 0.169},
    {.step_left = 8879,     .vol_left = 1000,       .ratio = 0.177},
    {.step_left = 11707,    .vol_left = 1500,       .ratio = 0.177}
};

static liquid_detect_step_ul_t s_stand_cup_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 0.049},
    {.step_left = 2040,     .vol_left = 100,        .ratio = 0.097},
    {.step_left = 3070,     .vol_left = 200,        .ratio = 0.140},
    {.step_left = 3785,     .vol_left = 300,        .ratio = 0.174},
    {.step_left = 4359,     .vol_left = 400,        .ratio = 0.234},
    {.step_left = 4787,     .vol_left = 500,        .ratio = 0.208},
    {.step_left = 6226,     .vol_left = 800,        .ratio = 0.213},
    {.step_left = 7165,     .vol_left = 1000,       .ratio = 0.226},
    {.step_left = 9381,     .vol_left = 1500,       .ratio = 0.308},
    {.step_left = 11005,    .vol_left = 2000,       .ratio = 0.482},
    {.step_left = 12042,    .vol_left = 2500,       .ratio = 0.482}
};

/* 非镀膜小瓶R1 */
static liquid_detect_step_ul_t s_7ml_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 0.830},
    {.step_left = 1205,     .vol_left = 1000,       .ratio = 0.855},
    {.step_left = 2374,     .vol_left = 2000,       .ratio = 0.795},
    {.step_left = 3632,     .vol_left = 3000,       .ratio = 0.819},
    {.step_left = 4853,     .vol_left = 4000,       .ratio = 0.812},
    {.step_left = 6085,     .vol_left = 5000,       .ratio = 0.787},
    {.step_left = 7356,     .vol_left = 6000,       .ratio = 0.945},
    {.step_left = 8414,     .vol_left = 7000,       .ratio = 0.533},
    {.step_left = 10290,    .vol_left = 8000,       .ratio = 0.251},
    {.step_left = 14273,    .vol_left = 9000,       .ratio = 0.251}
};

/* 镀膜小瓶R1 */
static liquid_detect_step_ul_t s_7ml_coated_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 0.614},
    {.step_left = 1628,     .vol_left = 1000,       .ratio = 1.129},
    {.step_left = 2514,     .vol_left = 2000,       .ratio = 0.842},
    {.step_left = 3701,     .vol_left = 3000,       .ratio = 0.807},
    {.step_left = 4940,     .vol_left = 4000,       .ratio = 0.806},
    {.step_left = 6181,     .vol_left = 5000,       .ratio = 0.799},
    {.step_left = 7433,     .vol_left = 6000,       .ratio = 0.824},
    {.step_left = 8646,     .vol_left = 7000,       .ratio = 0.897},
    {.step_left = 9761,     .vol_left = 8000,       .ratio = 0.268},
    {.step_left = 13486,    .vol_left = 9000,       .ratio = 0.268}
};

/* 非镀膜大瓶R1 */
static liquid_detect_step_ul_t s_15ml_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 1.140},
    {.step_left = 877,      .vol_left = 1000,       .ratio = 2.020},
    {.step_left = 1372,     .vol_left = 2000,       .ratio = 1.546},
    {.step_left = 2019,     .vol_left = 3000,       .ratio = 1.600},
    {.step_left = 2644,     .vol_left = 4000,       .ratio = 1.672},
    {.step_left = 3242,     .vol_left = 5000,       .ratio = 1.560},
    {.step_left = 3883,     .vol_left = 6000,       .ratio = 1.610},
    {.step_left = 4504,     .vol_left = 7000,       .ratio = 1.689},
    {.step_left = 5096,     .vol_left = 8000,       .ratio = 1.548},
    {.step_left = 5742,     .vol_left = 9000,       .ratio = 1.634},
    {.step_left = 6354,     .vol_left = 10000,      .ratio = 1.576},
    {.step_left = 9526,     .vol_left = 15000,      .ratio = 1.605},
    {.step_left = 10149,    .vol_left = 16000,      .ratio = 1.664},
    {.step_left = 10750,    .vol_left = 17000,      .ratio = 1.795},
    {.step_left = 11307,    .vol_left = 18000,      .ratio = 1.704},
    {.step_left = 11894,    .vol_left = 19000,      .ratio = 1.582},
    {.step_left = 12526,    .vol_left = 20000,      .ratio = 0.951},
    {.step_left = 13577,    .vol_left = 21000,      .ratio = 0.502},
    {.step_left = 15568,    .vol_left = 22000,      .ratio = 0.454},
    {.step_left = 17773,    .vol_left = 23000,      .ratio = 0.454}
};

/* 镀膜大瓶R1 */
static liquid_detect_step_ul_t s_15ml_coated_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 0.659},
    {.step_left = 1517,     .vol_left = 1000,       .ratio = 2.488},
    {.step_left = 1919,     .vol_left = 2000,       .ratio = 3.774},
    {.step_left = 2184,     .vol_left = 3000,       .ratio = 1.572},
    {.step_left = 2820,     .vol_left = 4000,       .ratio = 1.608},
    {.step_left = 3442,     .vol_left = 5000,       .ratio = 1.603},
    {.step_left = 4066,     .vol_left = 6000,       .ratio = 1.658},
    {.step_left = 4669,     .vol_left = 7000,       .ratio = 1.524},
    {.step_left = 5325,     .vol_left = 8000,       .ratio = 1.595},
    {.step_left = 5952,     .vol_left = 9000,       .ratio = 1.484},
    {.step_left = 6626,     .vol_left = 10000,      .ratio = 1.578},
    {.step_left = 9795,     .vol_left = 15000,      .ratio = 1.590},
    {.step_left = 10424,    .vol_left = 16000,      .ratio = 1.656},
    {.step_left = 11028,    .vol_left = 17000,      .ratio = 1.592},
    {.step_left = 11656,    .vol_left = 18000,      .ratio = 1.795},
    {.step_left = 12213,    .vol_left = 19000,      .ratio = 1.773},
    {.step_left = 12777,    .vol_left = 20000,      .ratio = 0.781},
    {.step_left = 14058,    .vol_left = 21000,      .ratio = 0.428},
    {.step_left = 16394,    .vol_left = 22000,      .ratio = 0.610},
    {.step_left = 18033,    .vol_left = 23000,      .ratio = 0.610}
};

/* 非镀膜小瓶R2 */
static liquid_detect_step_ul_t r2_7ml_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 2.959},
    {.step_left = 338,      .vol_left = 1000,       .ratio = 3.115},
    {.step_left = 659,      .vol_left = 2000,       .ratio = 3.086},
    {.step_left = 983,      .vol_left = 3000,       .ratio = 3.125},
    {.step_left = 1303,     .vol_left = 4000,       .ratio = 3.040},
    {.step_left = 1632,     .vol_left = 5000,       .ratio = 2.941},
    {.step_left = 1972,     .vol_left = 6000,       .ratio = 3.663},
    {.step_left = 2245,     .vol_left = 7000,       .ratio = 1.969},
    {.step_left = 2753,     .vol_left = 8000,       .ratio = 0.960},
    {.step_left = 3795,     .vol_left = 9000,       .ratio = 0.960}
};

/* 镀膜小瓶R2 */
static liquid_detect_step_ul_t r2_7ml_coated_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 2.278},
    {.step_left = 439,      .vol_left = 1000,       .ratio = 4.167},
    {.step_left = 679,      .vol_left = 2000,       .ratio = 3.145},
    {.step_left = 997,      .vol_left = 3000,       .ratio = 3.021},
    {.step_left = 1328,     .vol_left = 4000,       .ratio = 3.012},
    {.step_left = 1660,     .vol_left = 5000,       .ratio = 3.040},
    {.step_left = 1989,     .vol_left = 6000,       .ratio = 2.959},
    {.step_left = 2327,     .vol_left = 7000,       .ratio = 3.401},
    {.step_left = 2621,     .vol_left = 8000,       .ratio = 1.038},
    {.step_left = 3584,     .vol_left = 9000,       .ratio = 1.038}
};

/* 非镀膜大瓶R2 */
static liquid_detect_step_ul_t r2_15ml_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 4.149},
    {.step_left = 241,      .vol_left = 1000,       .ratio = 6.250},
    {.step_left = 401,      .vol_left = 2000,       .ratio = 6.536},
    {.step_left = 554,      .vol_left = 3000,       .ratio = 5.128},
    {.step_left = 749,      .vol_left = 4000,       .ratio = 6.289},
    {.step_left = 908,      .vol_left = 5000,       .ratio = 5.988},
    {.step_left = 1075,     .vol_left = 6000,       .ratio = 6.061},
    {.step_left = 1240,     .vol_left = 7000,       .ratio = 5.882},
    {.step_left = 1410,     .vol_left = 8000,       .ratio = 6.135},
    {.step_left = 1573,     .vol_left = 9000,       .ratio = 6.536},
    {.step_left = 1726,     .vol_left = 10000,      .ratio = 6.061},
    {.step_left = 2551,     .vol_left = 15000,      .ratio = 6.173},
    {.step_left = 2713,     .vol_left = 16000,      .ratio = 6.897},
    {.step_left = 2858,     .vol_left = 17000,      .ratio = 6.289},
    {.step_left = 3017,     .vol_left = 18000,      .ratio = 6.536},
    {.step_left = 3170,     .vol_left = 19000,      .ratio = 6.173},
    {.step_left = 3332,     .vol_left = 20000,      .ratio = 3.922},
    {.step_left = 3587,     .vol_left = 21000,      .ratio = 1.949},
    {.step_left = 4100,     .vol_left = 22000,      .ratio = 1.701},
    {.step_left = 4688,     .vol_left = 23000,      .ratio = 1.701}
};

/* 镀膜大瓶R2 */
static liquid_detect_step_ul_t r2_15ml_coated_info[REMIAN_INFO_MAX] = {
    {.step_left = 0,        .vol_left = 0,          .ratio = 2.451},
    {.step_left = 408,      .vol_left = 1000,       .ratio = 8.772},
    {.step_left = 522,      .vol_left = 2000,       .ratio = 10.309},
    {.step_left = 619,      .vol_left = 3000,       .ratio = 6.098},
    {.step_left = 783,      .vol_left = 4000,       .ratio = 6.024},
    {.step_left = 949,      .vol_left = 5000,       .ratio = 6.173},
    {.step_left = 1111,     .vol_left = 6000,       .ratio = 6.250},
    {.step_left = 1271,     .vol_left = 7000,       .ratio = 5.747},
    {.step_left = 1445,     .vol_left = 8000,       .ratio = 6.250},
    {.step_left = 1605,     .vol_left = 9000,       .ratio = 5.714},
    {.step_left = 1780,     .vol_left = 10000,      .ratio = 5.959},
    {.step_left = 2619,     .vol_left = 15000,      .ratio = 6.098},
    {.step_left = 2783,     .vol_left = 16000,      .ratio = 6.849},
    {.step_left = 2929,     .vol_left = 17000,      .ratio = 6.211},
    {.step_left = 3090,     .vol_left = 18000,      .ratio = 6.944},
    {.step_left = 3234,     .vol_left = 19000,      .ratio = 6.135},
    {.step_left = 3397,     .vol_left = 20000,      .ratio = 3.704},
    {.step_left = 3667,     .vol_left = 21000,      .ratio = 1.621},
    {.step_left = 4284,     .vol_left = 22000,      .ratio = 2.786},
    {.step_left = 4643,     .vol_left = 23000,      .ratio = 2.786}
};

static inline int step_offset_min(needle_type_t type)
{
    return MIN_MULTIPLE * needle_z_1mm_steps(type);
}

static inline int step_offset_max(needle_type_t type)
{
    return MAX_MULTIPLE * needle_z_1mm_steps(type);
}

char *needle_type_string(needle_type_t needle)
{
    if (needle == NEEDLE_TYPE_S) {
        return "NEEDLE_S";
    } else if (needle == NEEDLE_TYPE_S_DILU) {
        return "NEEDLE_S for Dilu";
    } else if (needle == NEEDLE_TYPE_S_R1) {
        return "NEEDLE_S for R1";
    } else if (needle == NEEDLE_TYPE_R2) {
        return "NEEDLE_R2";
    }

    return "NEEDLE_UNKNOWN";
}

static inline char *liquid_detect_mode_string(liquid_detect_mode_t mode)
{
    if (mode == NORMAL_DETECT_MODE) {
        return "Normal";
    } else if (mode == REMAIN_DETECT_MODE) {
        return "Remain";
    } else if (mode == DEBUG_DETECT_MODE) {
        return "Debug";
    }

    return "Unknown";
}

static const char *liquid_detect_target_string(liquid_detect_arg_t arg)
{
    if (arg.needle == NEEDLE_TYPE_S) {
        if (arg.mode == NORMAL_DETECT_MODE && arg.hat_enable == ATTR_DISABLE &&
            (arg.tube == STANARD_CUP || arg.tube == EP_CUP)) {
            return "Sample(adp)";
        } else {
            return (arg.hat_enable ? "Sample(hat)" : "Sample");
        }
    } else if (arg.needle == NEEDLE_TYPE_S_DILU) {
        return "Dilu";
    } else if (arg.needle == NEEDLE_TYPE_S_R1 || arg.needle == NEEDLE_TYPE_R2) {
        if (arg.reag_idx % 2) {
            if (arg.needle == NEEDLE_TYPE_S_R1 && (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO)) {
                return (arg.bt == BT_TUBE_EP15 ? "Inside(EP)" : "Inside(stand)");
            } else {
                return (reagent_table_bottle_type_get(arg.reag_idx) ? "Inside(v15)" : "Inside(v7)");
            }
        } else {
            if (arg.needle == NEEDLE_TYPE_S_R1 && (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO)) {
                return (arg.bt == BT_TUBE_EP15 ? "Outside(EP)" : "Outside(stand)");
            } else {
                return (reagent_table_bottle_type_get(arg.reag_idx) ? "Outside(v15)" : "Outside(v7)");
            }
        }
    } else {
        return "Unknown";
    }
}

static void liquid_detect_thr_show(void)
{
    LOG("S/R1 take_reag(inside) threshold       = %d.\n", h3600_conf_get()->s_threshold[0]);
    LOG("S/R1 take_reag(outside) threshold      = %d.\n", h3600_conf_get()->s_threshold[1]);
    LOG("S/R1 take_sample threshold             = %d.\n", h3600_conf_get()->s_threshold[2]);
    LOG("S/R1 take_sample(with hat) threshold   = %d.\n", h3600_conf_get()->s_threshold[3]);
    LOG("S/R1 take_sample(with adp) threshold   = %d.\n", h3600_conf_get()->s_threshold[4]);
    LOG("S/R1 take_dilu threshold               = %d.\n", h3600_conf_get()->s_threshold[5]);
    LOG("S/R1 take_reag(mirco) threshold        = %d.\n", h3600_conf_get()->s_threshold[6]);

    LOG("R2 take_reag(inside) threshold         = %d.\n", h3600_conf_get()->r2_threshold[0]);
    LOG("R2 take_reag(outside) threshold        = %d.\n", h3600_conf_get()->r2_threshold[1]);
}

static void liquid_detect_err_count_clear(int type)
{
    liquid_detect_misc_t *misc_para = &g_misc_array[type == NEEDLE_TYPE_R2 ? NEEDLE_TYPE_R2 : NEEDLE_TYPE_S];

    misc_para->err_count = 0;
}

static void liquid_detect_err_count_set(int type)
{
    liquid_detect_misc_t *misc_para = &g_misc_array[type == NEEDLE_TYPE_R2 ? NEEDLE_TYPE_R2 : NEEDLE_TYPE_S];

    misc_para->err_count += 1;

    DETECT_LOG("%s detect err_count = %d.\n", needle_type_string(type), misc_para->err_count);
    if (misc_para->err_count >= 3) {
        if (type == NEEDLE_TYPE_S) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_NONE, (void*)MODULE_FAULT_NEEDLE_S_STOP);
        } else {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_R2_STOP);
        }
        liquid_detect_err_count_clear(type);
    }
}

void liquid_detect_err_count_all_clear(void)
{
    g_detect_running = 0;
    liquid_detect_err_count_clear(NEEDLE_TYPE_S);
    liquid_detect_err_count_clear(NEEDLE_TYPE_R2);
}

int liquid_detect_result_get(needle_type_t type)
{
    int result = g_misc_array[type].result;

    if (get_throughput_mode() && type == NEEDLE_TYPE_R2) {
        /* 通量模式下速度快，R2固定2s的out信号还未产生 */
        result = LIQ_LEAVE_OUT;
    }
    if (result != LIQ_LEAVE_OUT) {
        liquid_detect_err_count_set(type);
    }

    return result;
}

static void liquid_detect_remain_list_init(void)
{
    INIT_LIST_HEAD(&reag_remain_list);
}

void liquid_detect_remain_add_tail(int idx, int bt, int rc, int rx, int coated)
{
    liquid_detect_remain_arg_t *arg = NULL;

    arg = (liquid_detect_remain_arg_t *)calloc(1, sizeof(liquid_detect_remain_arg_t));
    if (!arg) {
        DETECT_LOG("calloc failed(%u).\n", errno);
        return;
    }
    arg->reag_idx = idx;
    arg->bottle_type = (bottle_type_t)bt;
    arg->reag_category = rc;
    arg->rx = rx;
    arg->coated = coated;
    list_add_tail(&arg->remain_sibling, &reag_remain_list);
}

void liquid_detect_remain_del_all(void)
{
    liquid_detect_remain_arg_t *n = NULL;
    liquid_detect_remain_arg_t *pos = NULL;

    list_for_each_entry_safe(pos, n, &reag_remain_list, remain_sibling) {
        list_del(&pos->remain_sibling);
        free(pos);
        pos = NULL;
    }
}

static int liquid_detect_rcd_list_init(void)
{
    int idx = 0;
    liquid_detect_rcd_t *ldr = NULL;

    INIT_LIST_HEAD(&reag_info_list);
    /* TBD  位置不够 */
    for (idx = POS_REAGENT_TABLE_I1; idx <= DILU_IDX_END; idx++) {
        ldr = (liquid_detect_rcd_t *)calloc(1, sizeof(liquid_detect_rcd_t));
        if (!ldr) {
            DETECT_LOG("calloc failed(%u).\n", errno);
            return -1;
        }
        ldr->needle = NEEDLE_TYPE_MAX;
        ldr->reag_idx = idx;
        list_add_tail(&ldr->info_sibling, &reag_info_list);
    }

    return 0;
}

/* 更新链表中记录的相关信息 */
static void liquid_detect_rcd_list_update(
    needle_type_t needle,
    int pos_idx,
    float take_ul,
    int detect_step,
    int max_step,
    bottle_type_t bt,
    int coated)
{
    liquid_detect_rcd_t *pos = NULL;
    liquid_detect_rcd_t *n = NULL;
    float ratio = 0;

    if (detect_step < EMAX) {
        return;
    }

    list_for_each_entry_safe(pos, n, &reag_info_list, info_sibling) {
        if (pos->reag_idx == pos_idx) {
            /* 更新表中的相关数据 */
            pos->bottle_type = (max_step == 0 ? pos->bottle_type : bt);         /* 非余量探测不更新该值 */
            pos->real_maxstep= (max_step == 0 ? pos->real_maxstep : max_step);  /* 非余量探测不更新该值 */
            pos->coated      = (max_step == 0 ? pos->coated : coated);          /* 非余量探测不更新该值 */
            pos->needle      = needle;
            if (max_step == 0) {
                /*
                    非余量探测(max_step为0)当本次实际探测脉冲detect_step由于一些原因探测略深但依旧在计算范围内，
                    如果将本次实际探测脉冲detect_step更新到链表，那么可能导致后续的探测值不满足计算的最小值而误报空探，
                    因此对记录进链表的数据进行适当修正(注意余量探测take_ul = 0后的第一次判断和试剂仓质控品特殊容器)。
                */
                if (pos->bottle_type == BT_TUBE_MICRO || pos->bottle_type == BT_TUBE_EP15) {
                    /* 试剂仓质控品瓶子特殊，不进行调整 */
                } else {
                    if (pos->take_ul == 0) {
                        ratio = 5;
                    } else {
                        ratio = 3;
                    }
                    if (detect_step - pos->detect_step > ratio * needle_z_1mm_steps(pos->needle)) {
                        DETECT_LOG("%s detect_pos = %d, detect_step = %d maybe too deep(last = %d) change.\n",
                            needle_type_string(pos->needle), pos_idx, detect_step, pos->detect_step);
                        detect_step = (pos->detect_step + detect_step) / 2;
                    }
                }
            }
            pos->detect_step = detect_step;
            pos->take_ul     = take_ul;
            break;
        }
    }
}

static int liquid_detect_result_report(needle_type_t needle, int ret)
{
    fault_type_t ftype = FAULT_NEEDLE_S;
    module_fault_stat_t flevel = MODULE_FAULT_LEVEL2;
    void *fid = NULL;

    if (needle == NEEDLE_TYPE_R2) {
        ftype = FAULT_NEEDLE_R2;
        flevel = MODULE_FAULT_LEVEL2;
    } else {
        ftype = FAULT_NEEDLE_S;
        flevel = MODULE_FAULT_LEVEL2;
    }

    switch (ret) {
        case EARG:
            fid = (needle == NEEDLE_TYPE_R2 ? MODULE_FAULT_NEEDLE_R2_Z_EARG : MODULE_FAULT_NEEDLE_S_Z_EARG);
            break;
        case ETIMEOUT:
            fid = (needle == NEEDLE_TYPE_R2 ? MODULE_FAULT_NEEDLE_R2_COMMUNICATE : MODULE_FAULT_NEEDLE_S_COMMUNICATE);
            break;
        case ECOLLSION:
            fid = (needle == NEEDLE_TYPE_R2 ? MODULE_FAULT_NEEDLE_R2_Z_COLLIDE : MODULE_FAULT_NEEDLE_S_Z_COLLIDE);
            break;
        case EMOVE:
            fid = (needle == NEEDLE_TYPE_R2 ? MODULE_FAULT_NEEDLE_R2_Z : MODULE_FAULT_NEEDLE_S_Z);
            break;
        default:
            break;
    }

    if (fid) FAULT_CHECK_DEAL(ftype, flevel, (void *)fid);
    return (fid ? -1 : 0);
}

static int slip_liquid_detect_set_result(const slip_port_t *port, slip_msg_t *msg, void *arg)
{
    slip_liquid_detect_result_t *data = (slip_liquid_detect_result_t *)arg;
    slip_liquid_detect_result_t *result = (slip_liquid_detect_result_t *)msg->data;

    if (msg->length == 0) {
        return 1;
    }

    if (result->board_id == data->board_id) {
        data->status = result->status;
        return 0;
    }

    return -1;
}

int slip_liquid_detect_thr_set(needle_type_t type, int thr)
{
    uint8_t slip_dst_id = 0;
    slip_liquid_detect_result_t result = {0};

    thr = thr / 100;
    if (type == NEEDLE_TYPE_R2) {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_DETECT_POS_SET_SUBTYPE, 1, (void *)&thr);

    result.board_id = slip_dst_id;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_DETECT_POS_SET_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return result.status;
    }

    return -1;
}

int slip_liquid_detect_type_set(needle_type_t needle)
{
    uint8_t send_data = 0;
    uint8_t slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    slip_liquid_detect_result_t result = {0};

    if (needle == NEEDLE_TYPE_R2) {
        send_data = 2;
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        send_data = 0;
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_DETECT_TYPE_SET_SUBTYPE, 1, (void *)&send_data);

    result.board_id = slip_dst_id;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_DETECT_TYPE_SET_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return result.status;
    }

    return -1;
}

/* 开启/关闭M0撞针信号屏蔽 */
int slip_liquid_detect_collsion_barrier_set(needle_type_t type, attr_enable_t enable)
{
    uint8_t slip_dst_id = 0;
    slip_liquid_detect_result_t result = {0};

    if (type == NEEDLE_TYPE_R2) {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_COLLSION_BARRIER_SUBTYPE, 1, (void *)&enable);

    result.board_id = slip_dst_id;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_COLLSION_BARRIER_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return result.status;
    }

    return -1;
}

/* 开启/关闭M0原始数据记录 */
int slip_liquid_detect_rcd_set(needle_type_t type, attr_enable_t enable)
{
    uint8_t slip_dst_id = 0;
    slip_liquid_detect_result_t result = {0};

    if (type == NEEDLE_TYPE_R2) {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_RCD_SUBTYPE, 1, (void *)&enable);

    result.board_id = slip_dst_id;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_RCD_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return result.status;
    }

    return -1;
}

int slip_liquid_detect_state_set(needle_type_t type, attr_enable_t data)
{
    uint8_t slip_dst_id = 0;
    uint8_t send_data = 0;
    slip_liquid_detect_result_t result = {0};

    if (type == NEEDLE_TYPE_R2) {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    if (data == ATTR_ENABLE) {
        send_data = 1;
    } else {
        send_data = 0;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_DETECT_STATE_SET_SUBTYPE, 1, (void *)&send_data);

    result.board_id = slip_dst_id;
    result.status = -1;
    if (0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_DETECT_STATE_SET_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return result.status;
    }

    return -1;
}

static void liquid_detect_data_record(void *arg)
{
    int idx = 0;
    int *data = (int *)arg;
    int type = data[0];
    int ad_data = data[1];
    int thr = data[2];
    int turn = data[3];
    int step = data[4];
    char s[10] = {0};
    char str[20480] = {0};

    for (idx = 5; idx < LIQUID_DETECT_DATA_MAX; idx++) {
        if ((data[idx] == SLIP_NODE_LIQUID_DETECT_1) ||
            (data[idx] == SLIP_NODE_LIQUID_DETECT_2)) {
            break;
        } else {
            snprintf(s, sizeof(s), "%d ", data[idx]);
            strncat(str, s, sizeof(s));
        }
    }

    misc_log(type, "%s", str);
    misc_log(type, "\n%s: data = %d, thr = %d, turn = %d, count = %d, step = %d.\n",
        log_get_time(), ad_data, thr, turn, idx, step);
}

void slip_liquid_detect_get_all_data_async(const slip_port_t *port, slip_msg_t *msg)
{
    int idx = 0;
    int type = -1;
    liquid_detect_misc_t *log_para = NULL;
    slip_liquid_detect_all_data_t *result = (slip_liquid_detect_all_data_t *)msg->data;

    if (!msg->length) {
        return;
    }

    if (msg->src == SLIP_NODE_LIQUID_DETECT_1) {
        type = NEEDLE_TYPE_S;
    } else if (msg->src == SLIP_NODE_LIQUID_DETECT_2) {
        type = NEEDLE_TYPE_R2;
    } else {
        return;
    }

    log_para = &g_misc_array[type];
    for (idx = 0; idx < SEND_DATA_MAX; idx++) {
        if (log_para->idx == LIQUID_DETECT_DATA_MAX) {
            log_para->detect_data[LIQUID_DETECT_DATA_MAX - 1] = result->data[idx];
            memmove(log_para->detect_data, &log_para->detect_data[1], sizeof(int) * (LIQUID_DETECT_DATA_MAX - 1));
        } else {
            log_para->detect_data[log_para->idx++] = result->data[idx];
        }

        /* 最后一组数据 */
        if ((result->data[0] == SLIP_NODE_LIQUID_DETECT_1) ||
            (result->data[0] == SLIP_NODE_LIQUID_DETECT_2)) {
            /* data end */
            log_para->detect_data[0] = type;
            log_para->detect_data[1] = result->data[1];
            log_para->detect_data[2] = result->data[2];
            log_para->detect_data[3] = result->data[3];
            log_para->detect_data[4] = log_para->detect_step;
            log_para->idx = 0;
            work_queue_add(liquid_detect_data_record, (void *)log_para->detect_data);
            break;
        }
    }
}

/* 匿名科创 软件 */
static void uart_ano_int(uint8_t id, int32_t* p_data, int32_t size)
{
    uint8_t _cnt = 0;
    uint8_t sum = 0, i = 0;
    uint8_t buff[256] = {0};

    buff[_cnt++] = 0xAA;
    buff[_cnt++] = 0xAA;
    buff[_cnt++] = id;
    buff[_cnt++] = 0;

    for (i=0; i<size; i++) {
        buff[_cnt++] = BYTE3(*(p_data+i));
        buff[_cnt++] = BYTE2(*(p_data+i));
        buff[_cnt++] = BYTE1(*(p_data+i));
        buff[_cnt++] = BYTE0(*(p_data+i));
    }

    buff[3] = _cnt - 4;
    for (i = 0; i < _cnt; i++) {
        sum += buff[i];
    }
    buff[_cnt++] = sum;

    if(fd_debug_uart > 0){
        write(fd_debug_uart, buff, _cnt);
    }
}

void slip_liquid_detect_get_addata_async(const slip_port_t *port, slip_msg_t *msg)
{
    slip_liquid_detect_addata_t *result = (slip_liquid_detect_addata_t *)msg->data;

    if (msg->length != 16) {
        return ;
    }

    uart_ano_int(0xf1, result->ad_data, 4);
}

void slip_liquid_detect_get_result_async(const slip_port_t *port, slip_msg_t *msg)
{
    needle_type_t type = NEEDLE_TYPE_S;
    liquid_detect_misc_t *misc_para = NULL;
    slip_liquid_detect_get_result_t * result = (slip_liquid_detect_get_result_t *)msg->data;

    if (msg->length == 0) {
        goto out;
    }

    if (result->board_id == SLIP_NODE_LIQUID_DETECT_1) {
        type = NEEDLE_TYPE_S;
    } else if (result->board_id == SLIP_NODE_LIQUID_DETECT_2) {
        type = NEEDLE_TYPE_R2;
    } else {
        DETECT_LOG("board_id = %d invalid.\n", result->board_id);
        goto out;
    }
    misc_para = &g_misc_array[type];
    pthread_mutex_lock(&misc_para->m_lock);
    if (result->type == LIQ_LEAVE_IN) { /* 探测到液面 */
        DETECT_LOG("%s liquid in, cur result = %d.\n", needle_type_string(type), misc_para->result);
        if (misc_para->result != LIQ_REACH_MAX && misc_para->result != LIQ_COLLSION_IN) {
            /* 20240620 产线丝杆问题，导致探测成功时同时触发撞针，防止信号被post两次 */
            misc_para->result = result->type;
            leave_singal_send(misc_para->signo);
        }
    } else if (result->type == LIQ_LEAVE_OUT) { /* 离开液面 */
        DETECT_LOG("%s liquid out.\n", needle_type_string(type));
        misc_para->result = result->type;
    } else if (result->type == LIQ_COLLSION_IN) { /* 检测到撞针 */
        auto_cal_stop_flag_set(1); /* 自动标定撞针停止 */
        DETECT_LOG("%s collsion in, cur result = %d.\n", needle_type_string(type), misc_para->result);
        if (g_detect_running && misc_para->result != LIQ_REACH_MAX && misc_para->result != LIQ_LEAVE_IN) {
            /*
                20240620 产线丝杆问题，导致探测成功时同时触发撞针，防止信号被post两次
                20240718 非探测过程中的撞针不再发送信号，待机状态下拨动撞针，会导致后续正常流程第一次探测直接捕获该条件变量导致空探
            */
            leave_singal_send(misc_para->signo);
        }
        misc_para->result = result->type;
        liquid_detect_result_report(type, ECOLLSION);
    } else if (result->type == LIQ_COLLSION_OUT) { /* 撞针解除 */
        DETECT_LOG("%s collsion out.\n", needle_type_string(type));
        misc_para->result = result->type;
    } else {
        DETECT_LOG("%s invalid.\n", needle_type_string(type));
    }
    pthread_mutex_unlock(&misc_para->m_lock);

out:
    return;
}

int slip_liq_out_monitor_set(needle_type_t type, attr_enable_t data)
{
    uint8_t slip_dst_id = 0;
    uint8_t send_data = 0;
    slip_liquid_detect_result_t result = {0};

    if (type == NEEDLE_TYPE_R2) {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    if (data == ATTR_ENABLE) {
        send_data = 1;
    } else {
        send_data = 0;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_OUT_MONITOR_SUBTYPE, 1, (void *)&send_data);

    result.board_id = slip_dst_id;
    result.status = -1;
    if(0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_OUT_MONITOR_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return  result.status;
    }

    return -1;
}

int slip_liquid_detect_debug_set(needle_type_t type, uint8_t data)
{
    uint8_t slip_dst_id = 0;
    slip_liquid_detect_result_t result = {0};

    if (type == NEEDLE_TYPE_R2) {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_2;
    } else {
        slip_dst_id = SLIP_NODE_LIQUID_DETECT_1;
    }

    slip_send_node(slip_node_id_get(), slip_dst_id, 0x0, OTHER_TYPE,
        OTHER_LIQUID_DETECT_DEBUG_SET_SUBTYPE, 1, (void *)&data);

    result.board_id = slip_dst_id;
    result.status = -1;
    if(0 == result_timedwait(OTHER_TYPE, OTHER_LIQUID_DETECT_DEBUG_SET_SUBTYPE,
        slip_liquid_detect_set_result, (void *)&result, SLIP_TIMEOUT_DEFAULT)) {
        return  result.status;
    }

    return -1;
}

static void uart_debug_data_deal(char* data, uint8_t len)
{
    /* header+cmd+value1+value2...+end */
    if (data[0] == UART_DEBUG_FLAG_START) {
        char cmd = data[1];
        char val1 = data[2];
        char val2 = data[3];

        val1 = val1;
        val2 = val2;
        if (cmd == UART_DEBUG_CMD_ENABLE) {
            slip_liquid_detect_debug_set(val1, val2);
            DETECT_LOG("uart debug board: %d, enable: %d.\n", val1, val2);
        }
    } else {
        DETECT_LOG("[%s] debug recv wrong cmd.\n", __func__);
    }
}

static void* app_uart_debug_task(void *arg)
{
    return NULL;
    int cnt = 0;
    uint8_t rxdata = 0;
    char data_to_rec[30] = {0};
    uint8_t rec_count = 0;
    long long recv_time_last = 0;

    while (1) {
        cnt = read(fd_debug_uart, &rxdata, 1); /* 阻塞式读数据 */
        if (cnt > 0) {
            if (get_time() - recv_time_last > UART_DEBUG_RECV_TIMEOUT) { /* 接收数据, 1000ms超时 */
                rec_count = 0;
                memset(data_to_rec, 0, sizeof(data_to_rec));
                DETECT_LOG("[%s] clear last recv\n", __func__);
            }

            if (rec_count > sizeof(data_to_rec)-1) { /* 接收长度超限处理 */
                rec_count = 0;
                memset(data_to_rec,0,sizeof(data_to_rec));
            }
            recv_time_last = get_time();
            data_to_rec[rec_count++] = rxdata;
            if (rxdata == UART_DEBUG_FLAG_END) {
                uart_debug_data_deal(data_to_rec, rec_count);
                rec_count = 0;
                memset(data_to_rec, 0, sizeof(data_to_rec));
            }
        }
    }

    return 0;
}

void uart_debug_dev_init(void)
{
    pthread_t thread = 0;

    fd_debug_uart = ll_uart_open_nonblock(DEV_UART_DEBUG_PATH, 115200, 0, 8, 1, 'N');
    fd_dilu_scanner_uart = ll_uart_open_nonblock(DEV_UART_DILU_SCANNER_PATH, 115200, 0, 8, 1, 'N');

    if (0 != pthread_create(&thread, NULL, app_uart_debug_task, NULL)) {
        DETECT_LOG("thread create failed!, %s\n", strerror(errno));
        return ;
    }
}

/* 液面探测到达最大步长的处理 */
void liquid_detect_maxstep_task(void *arg)
{
    int cur_step = 0;
    liquid_detect_misc_t *para = (liquid_detect_misc_t *)arg;

    if (!para) {
        DETECT_LOG("param is null\n");
        goto out;
    }

    while (1) {
        if (para->stop_flag == 1) {
            break;
        }
        cur_step = motor_current_pos_timedwait(para->motor_id, para->timeout);
        if (para->reverse ? cur_step <= para->maxstep : cur_step >= para->maxstep) {
            /* 抵达最大步长 */
            DETECT_LOG("warnning, motor = %d, cur_step = %d, max_step = %d.\n", para->motor_id, cur_step, para->maxstep);
            pthread_mutex_lock(&para->m_lock);
            if (para->result != LIQ_LEAVE_IN && para->result != LIQ_COLLSION_IN) {
                /* 先探测成功，立即触发最大步长，防止信号被send两次 */
                para->result = LIQ_REACH_MAX;
                leave_singal_send(para->signo);
            }
            pthread_mutex_unlock(&para->m_lock);
            break;
        }

        if (para->auto_cal == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
            /* 检测到故障后，强制退出液面检测，避免误报通信超时 */
            DETECT_LOG("detect fault, force break.\n");
            if (para->result != LIQ_LEAVE_IN && para->result != LIQ_COLLSION_IN && para->result != LIQ_REACH_MAX) {
                leave_singal_send(para->signo);
            }
            break;
        }
        usleep(10 * 1000);
    }

out:
    return;
}

static int liquid_detect_step_parse(int detect_step, liquid_detect_arg_t arg, liquid_detect_para_t para)
{
    int ret = detect_step;
    int min_step = 0, max_step = 0;
    liquid_detect_rcd_t *pos = NULL;
    liquid_detect_rcd_t *n = NULL;

    if (ret < EMAX || !arg.order_no) {
        goto out;
    }

    if (arg.needle == NEEDLE_TYPE_S) {
        goto out;
    }

    list_for_each_entry_safe(pos, n, &reag_info_list, info_sibling) {
        if (pos->reag_idx == arg.reag_idx) {
            /* 余量探测完的第一次表中take_ul = 0, 最大值再放宽(因为余量和正常探测速度不同) */
            min_step = (int)step_offset_min(arg.needle);
            min_step = pos->detect_step - min_step;
            max_step = (int)step_offset_max(arg.needle);
            max_step = pos->detect_step +
                ((pos->take_ul == 0 || pos->bottle_type == BT_TUBE_EP15 || pos->bottle_type == BT_TUBE_MICRO) ? 2 * max_step : max_step);
            if (detect_step >= min_step && detect_step <= max_step) {
                /* 本次探测脉冲正确(在计算理论值范围内) */
                liquid_detect_rcd_list_update(arg.needle, arg.reag_idx, arg.take_ul, detect_step, 0, BT_UNKNOWN, 0);
            } else {
                ret = (detect_step > max_step ? EMAXSTEP : ENOTHING);
                DETECT_LOG("%s detect_pos = %d, detect_step = %d out of range(min = %d, max = %d)!\n",
                    needle_type_string(arg.needle), arg.reag_idx, detect_step, min_step, max_step);
            }
            break;
        }
    }

out:
    return ret;
}

static inline void liquid_detect_log_info_init(liquid_detect_misc_t *info, liquid_detect_para_t para)
{
    info->idx = 0;
    info->detect_step = 0;
    info->stop_flag = 0;
    info->reverse = 0;
    info->motor_id = para.motor_id;
    info->timeout = para.timeout;
    info->maxstep = para.dyna_maxstep;
    info->result = LIQ_INIT;
}

static inline cup_pos_t liquid_detect_circle_get(needle_type_t needle, needle_pos_t reag_idx)
{
    cup_pos_t pos = POS_INVALID;

    if (reag_idx > POS_REAGENT_TABLE_NONE && reag_idx < POS_REAGENT_DILU_1) {
        if (needle == NEEDLE_TYPE_S) {
        } else if (needle == NEEDLE_TYPE_S_DILU) {
        } else if (needle == NEEDLE_TYPE_S_R1) {
            pos = (reag_idx % 2 ? POS_REAGENT_TABLE_S_IN : POS_REAGENT_TABLE_S_OUT);
        } else {
            pos = (reag_idx % 2 ? POS_REAGENT_TABLE_R2_IN : POS_REAGENT_TABLE_R2_OUT);
        }
    }

    return pos;
}

/* 正常探测过程，根据余量探测后保存的试剂信息获取镀膜与否 */
static int liquid_detect_coated_get(int pos_idx)
{
    liquid_detect_rcd_t *pos = NULL;
    liquid_detect_rcd_t *n = NULL;
    int coated = 0;

    if (pos_idx > POS_REAGENT_TABLE_NONE && pos_idx < POS_REAGENT_DILU_1) {
        list_for_each_entry_safe(pos, n, &reag_info_list, info_sibling) {
            if (pos->reag_idx == pos_idx) {
                coated = pos->coated;
                break;
            }
        }
    }

    return coated;
}

/* 正常探测过程，根据余量探测后保存的试剂信息获取瓶型 */
static int liquid_detect_bt_get(int pos_idx)
{
    liquid_detect_rcd_t *pos = NULL;
    liquid_detect_rcd_t *n = NULL;
    bottle_type_t bt = BT_UNKNOWN;

    if (pos_idx > POS_REAGENT_TABLE_NONE && pos_idx <= POS_REAGENT_DILU_2) {
        list_for_each_entry_safe(pos, n, &reag_info_list, info_sibling) {
            if (pos->reag_idx == pos_idx) {
                bt = pos->bottle_type;
                break;
            }
        }
    }

    return bt;
}

static int liquid_detect_calibrate_step_get(liquid_detect_arg_t *arg, liquid_detect_cs_t *info)
{
    int to_top = 0, to_bottom = 0, top_diff = 0, bottom_diff = 0;
    move_pos_t mp = MOVE_S_SAMPLE_NOR;
    cup_pos_t cp = POS_INVALID;
    pos_t pos = {0};

    if (arg->mode == REMAIN_DETECT_MODE) {
        /* 余量探测使用上位机下发的标识 */
    } else if (arg->mode == NORMAL_DETECT_MODE) {
        /* 正常模式下查询试剂信息，并记录用于后续切瓶高低等计算 */
        arg->coated = liquid_detect_coated_get(arg->reag_idx);
        if (arg->needle != NEEDLE_TYPE_S) {
            arg->bt = liquid_detect_bt_get(arg->reag_idx);
        }
    } else {
        arg->coated = 0;    /* 调试模式默认非镀膜；瓶型根据位置计算V7或V15，无法使用ep和stand */
    }

    switch (arg->needle) {
        case NEEDLE_TYPE_S:
            mp = MOVE_S_SAMPLE_NOR;
            cp = arg->reag_idx;
            if ((arg->mode == NORMAL_DETECT_MODE || arg->mode == DEBUG_DETECT_MODE) &&
                arg->hat_enable == ATTR_DISABLE && (arg->tube == STANARD_CUP || arg->tube == EP_CUP)) {
                to_top = S_TO_MIRCO_TOP;
                to_bottom = (arg->tube == EP_CUP ? S_TO_EP_BOTTOM : S_TO_STAND_BOTTOM);
            } else {
                to_top = S_TO_TOP;
                to_bottom = S_TO_BOTTOM;
                if (arg->mode == NORMAL_DETECT_MODE) {
                    /* 正常流程中，采血管预留10mm高度防止吸取到血细胞 */
                    to_bottom -= 10 * needle_z_1mm_steps(NEEDLE_TYPE_S);
                }
            }
            break;
        case NEEDLE_TYPE_S_DILU:
            mp = MOVE_S_DILU;
            cp = arg->reag_idx - DILU_IDX_START;
            to_top = S_DILU_TO_TOP;
            to_bottom = (arg->bt == BT_TUBE_EP15 ? S_DULU_TO_BOTTOM1 : S_DILU_TO_BOTTOM);
            break;
        case NEEDLE_TYPE_S_R1:
            mp = MOVE_S_ADD_REAGENT;
            cp = liquid_detect_circle_get(arg->needle, arg->reag_idx);
            if (cp == POS_INVALID) {
                DETECT_LOG("%s get cs_step failed, pos = %d invalid.\n", needle_type_string(arg->needle), arg->reag_idx);
                return -1;
            }
            if (arg->bt == BT_TUBE_EP15 || arg->bt == BT_TUBE_MICRO) {
                to_top = S_MICRO_TO_TOP;
                if (cp == POS_REAGENT_TABLE_S_IN) {
                    to_bottom = (arg->bt == BT_TUBE_EP15 ? S_IN_EP_BTM : S_IN_STAND_BTM);
                } else {
                    to_bottom = (arg->bt == BT_TUBE_EP15 ? S_OUT_EP_BTM : S_OUT_STAND_BTM);
                }
            } else {
                to_top = S_V7_TO_TOP;
                if (cp == POS_REAGENT_TABLE_S_IN) {
                    if (reagent_table_bottle_type_get(arg->reag_idx) == 1) {
                        to_bottom = S_IN_V15_BTM;
                        top_diff = S_V15_TOP_DIFF;
                        bottom_diff = (arg->coated ? S_V15_BTM_DIFF : 0);
                    } else {
                        to_bottom = S_IN_V7_BTM;
                        top_diff = 0;
                        bottom_diff = (arg->coated ? S_V7_BTM_DIFF : 0);
                    }
                } else {
                    if (reagent_table_bottle_type_get(arg->reag_idx) == 1) {
                        to_bottom = S_OUT_V15_BTM;
                        top_diff = S_V15_TOP_DIFF;
                        bottom_diff = (arg->coated ? S_V15_BTM_DIFF : 0);
                    } else {
                        to_bottom = S_OUT_V7_BTM;
                        top_diff = 0;
                        bottom_diff = (arg->coated ? S_V7_BTM_DIFF : 0);
                    }
                }
            }
            break;
        case NEEDLE_TYPE_R2:
            mp = MOVE_R2_REAGENT;
            cp = liquid_detect_circle_get(arg->needle, arg->reag_idx);
            if (cp == POS_INVALID) {
                DETECT_LOG("%s get cs_step failed, pos = %d invalid.\n", needle_type_string(arg->needle), arg->reag_idx);
                return -1;
            }
            to_top = R2_V7_TO_TOP;
            if (cp == POS_REAGENT_TABLE_R2_IN) {
                if (reagent_table_bottle_type_get(arg->reag_idx) == 1) {
                    to_bottom = R2_IN_V15_BTM;
                    top_diff = R2_V15_TOP_DIFF;
                    bottom_diff = (arg->coated ? R2_V15_BTM_DIFF : 0);
                } else {
                    to_bottom = R2_IN_V7_BTM;
                    top_diff = 0;
                    bottom_diff = (arg->coated ? R2_V7_BTM_DIFF : 0);
                }
            } else {
                if (reagent_table_bottle_type_get(arg->reag_idx) == 1) {
                    to_bottom = R2_OUT_V15_BTM;
                    top_diff = R2_V15_TOP_DIFF;
                    bottom_diff = (arg->coated ? R2_V15_BTM_DIFF : 0);
                } else {
                    to_bottom = R2_OUT_V7_BTM;
                    top_diff = 0;
                    bottom_diff = (arg->coated ? R2_V7_BTM_DIFF : 0);
                }
            }
            break;
        default:
            DETECT_LOG("get cs_step failed, No such needle(%d).\n", arg->needle);
            return -1;
    }
    get_special_pos(mp, cp, &pos, FLAG_POS_UNLOCK);
    //DETECT_LOG("%s detect_pos = %d(bt = %d, coated = %d), cs.z = %d, to_top = %d, to_bottom = %d, top_diff = %d, bottom_diff = %d.\n",
    //    needle_type_string(arg->needle), arg->reag_idx, arg->bt, arg->coated, pos.z, to_top, to_bottom, top_diff, bottom_diff);
    info->z_step = pos.z;
    info->top_step = pos.z + to_top + top_diff;
    info->real_maxstep = pos.z + to_bottom + bottom_diff;
    if (info->real_maxstep - info->top_step <= 0) {
        DETECT_LOG("%s get cs_step failed, step invalid.\n", needle_type_string(arg->needle));
        return -1;
    }

    return 0;
}

/* 计算针a/2减速停需要的脉冲 */
static int liquid_detect_stop_step_get(needle_type_t needle)
{
    int v0 = 0, acc = 0, step = R2_1MM_STEPS, motor = MOTOR_NEEDLE_R2_Z;
    float t = 0;

    if (needle == NEEDLE_TYPE_S) {
        v0 = S_CONTACT_V;
        motor = MOTOR_NEEDLE_S_Z;
    } else if (needle == NEEDLE_TYPE_R2) {
        v0 = R2_CONTACT_V;
        motor = MOTOR_NEEDLE_R2_Z;
    } else {
        v0 = S_CONTACT_V_SLOW;
        motor = MOTOR_NEEDLE_S_Z;
    }
    acc  = h3600_conf_get()->motor[motor].acc / 2;
    t    = (float)(v0 - 0) / (float)acc;
    step = (v0 + 0) * t / 2;

    return step;
}

static int liquid_detect_dv_step_get(liquid_detect_arg_t arg, int real_maxstep)
{
    int dv_step = 0;

    if (arg.needle == NEEDLE_TYPE_S_DILU) {
        /* 稀释液瓶按照需求预留2ml */
        dv_step = real_maxstep - 3 * needle_z_1mm_steps(arg.needle);
    } else if (arg.needle == NEEDLE_TYPE_S_R1) {
        if (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO) {
            /*
                试剂仓微量管和样本架上微量管不同：EP管和日立杯底部高度基本一致，样本架上EP管底部低3mm左右
                且试剂仓质控品不存在切瓶概念，因此本死腔切瓶线无实际意义
            */
            dv_step = real_maxstep - (arg.bt == BT_TUBE_EP15 ? 2 : 1) * needle_z_1mm_steps(arg.needle);
        } else {
            if (reagent_table_bottle_type_get(arg.reag_idx) == 1) {
                if (arg.coated) {
                    /* 注意：镀膜15ML试剂瓶，低液量时液体形态特殊 */
                    dv_step = real_maxstep - 1 * needle_z_1mm_steps(arg.needle);
                } else {
                    /* 非镀膜15ML试剂瓶，到达切瓶线时还能继续吸取 */
                    dv_step = real_maxstep + 0.5 * needle_z_1mm_steps(arg.needle);
                }
            } else {
                if (arg.coated) {
                    /* 注意：镀膜7ML试剂瓶，低液量时液体形态特殊 */
                    dv_step = real_maxstep - 0.6 * needle_z_1mm_steps(arg.needle);
                } else {
                    /* 非镀膜7ML试剂瓶，到达切瓶线 */
                    dv_step = real_maxstep;
                }
            }
        }
    } else if (arg.needle == NEEDLE_TYPE_S) {
        /* 样本不存在切瓶概念，因此本死腔切瓶线无实际意义 */
        dv_step = real_maxstep - 0.5 * needle_z_1mm_steps(arg.needle);
    } else {
        /* R2 */
        if (reagent_table_bottle_type_get(arg.reag_idx) == 1) {
            dv_step = real_maxstep - (arg.coated ? 2.8 : 1.2) * needle_z_1mm_steps(arg.needle);
        } else {
            dv_step = real_maxstep - (arg.coated ? 3 : 1.5) * needle_z_1mm_steps(arg.needle);
        }
    }

    return dv_step;
}

static int liquid_detect_last_step_get(liquid_detect_arg_t arg, liquid_detect_para_t *para)
{
    int last_step = 0;
    liquid_detect_rcd_t *n = NULL;
    liquid_detect_rcd_t *pos = NULL;

    if (arg.mode == NORMAL_DETECT_MODE) {
        if (arg.needle == NEEDLE_TYPE_S) {
            /* 吸取样本无动态脉冲，不用获取上次探测脉冲 */
            last_step = para->cal_step.top_step;
        } else {
            list_for_each_entry_safe(pos, n, &reag_info_list, info_sibling) {
                if (pos->reag_idx == arg.reag_idx) {
                    if (pos->needle != arg.needle) {
                        DETECT_LOG("%s detect_pos = %d, last_step needle not match(rcd: %s).\n",
                            needle_type_string(arg.needle), arg.reag_idx, needle_type_string(pos->needle));
                    } else {
                        last_step = pos->detect_step;
                    }
                    break;
                }
            }
        }
    } else {
        /* 其他模式无需获取上次探测脉冲 */
        last_step = para->cal_step.top_step;
    }

    return last_step;
}

/* dyna_maxstep: 动态底部位置，作用在于限制本次Z下降的高度 */
static int liquid_detect_dyna_maxstep_get(liquid_detect_arg_t arg, liquid_detect_para_t *para)
{
    int dyna_max = 0, offset = 0, comp_step = 0;
    liquid_detect_rcd_t *n = NULL;
    liquid_detect_rcd_t *pos = NULL;

    if (arg.mode == REMAIN_DETECT_MODE) {
        /* 余量探测速度慢，统一留0.5mm防止撞针 */
        dyna_max = para->cal_step.real_maxstep - 0.5 * needle_z_1mm_steps(arg.needle);
    } else if (arg.mode == DEBUG_DETECT_MODE) {
        /* 工程师或service模式下，未经过余量探测且速度正常 */
        dyna_max = para->cal_step.real_maxstep - 1 * needle_z_1mm_steps(arg.needle);
    }else {
        if (arg.needle == NEEDLE_TYPE_S) {
            /* 吸取样本，速度快，默认留3mm防止撞针 */
            dyna_max = para->cal_step.real_maxstep - 3 * needle_z_1mm_steps(arg.needle);
        } else {
            /* 将记录的上次探测脉冲+补偿作为本次探测所使用动态底部脉冲 */
            list_for_each_entry_safe(pos, n, &reag_info_list, info_sibling) {
                if (pos->reag_idx == arg.reag_idx) {
                    if (pos->needle != arg.needle) {
                        dyna_max = 0;
                        DETECT_LOG("%s detect_pos = %d, dyna_maxstep calc needle(rcd = %s) not match.\n",
                            needle_type_string(arg.needle), arg.reag_idx, needle_type_string(pos->needle));
                    } else {
                        /* 余量探测后的第一次和试剂仓质控品放宽 */
                        offset = ((pos->take_ul == 0 || pos->bottle_type == BT_TUBE_EP15 || pos->bottle_type == BT_TUBE_MICRO) ?
                                2 * step_offset_max(arg.needle) : step_offset_max(arg.needle));
                        dyna_max = pos->detect_step + offset;
                        /* 防止撞针，不能使用真实底部位置去比较 */
                        comp_step = para->cal_step.real_maxstep -
                            (arg.needle != NEEDLE_TYPE_R2 ? 2 : 0.5) * needle_z_1mm_steps(arg.needle);
                        dyna_max = (dyna_max < comp_step ? dyna_max : comp_step);
                    }
                    break;
                }
            }
        }
    }

    return dyna_max;
}

static int liquid_detect_thr_get(liquid_detect_arg_t arg)
{
    if (arg.needle == NEEDLE_TYPE_S) {
        if ((arg.mode == NORMAL_DETECT_MODE || arg.mode == DEBUG_DETECT_MODE) &&
            arg.hat_enable == ATTR_DISABLE &&
            (arg.tube == STANARD_CUP || arg.tube == EP_CUP)) {
            return h3600_conf_get()->s_threshold[S_SAMPLE_ADP];
        } else {
            return h3600_conf_get()->s_threshold[(arg.hat_enable ? S_SAMPLE_HAT : S_SAMPLE)];
        }
    } else if (arg.needle == NEEDLE_TYPE_S_DILU) {
        if (arg.bt == BT_TUBE_EP15) {
            return (h3600_conf_get()->s_threshold[S_MIRCO] == 0 ? MIRCO_THR_DEFAULT : h3600_conf_get()->s_threshold[S_MIRCO]);
        } else {
            return h3600_conf_get()->s_threshold[S_DILU];
        }
    } else if (arg.needle == NEEDLE_TYPE_S_R1) {
        if (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO) {
            return (h3600_conf_get()->s_threshold[S_MIRCO] == 0 ? MIRCO_THR_DEFAULT : h3600_conf_get()->s_threshold[S_MIRCO]);
        } else {
            return h3600_conf_get()->s_threshold[arg.reag_idx % 2 ? REAG_TABLE_IN : REAG_TABLE_OUT];
        }
    } else if (arg.needle == NEEDLE_TYPE_R2) {
        return h3600_conf_get()->r2_threshold[arg.reag_idx % 2 ? REAG_TABLE_IN : REAG_TABLE_OUT];
    }

    return 0;
}

static int liquid_detect_contact_speed_get(liquid_detect_arg_t arg)
{
    int speed = 0;

    if (arg.needle == NEEDLE_TYPE_S) {
        if (arg.mode == REMAIN_DETECT_MODE) {
            speed = S_REMAIN_CONTACT_V;
        } else {
            speed = S_CONTACT_V;
        }
    } else if (arg.needle == NEEDLE_TYPE_R2) {
        if (arg.mode == REMAIN_DETECT_MODE) {
            speed = R2_REMAIN_CONTACT_V;
        } else {
            speed = R2_CONTACT_V;
        }
    } else {
        if (arg.mode == REMAIN_DETECT_MODE) {
            speed = S_REMAIN_CONTACT_V;
        } else {
            speed = S_CONTACT_V_SLOW;
        }
    }

    return speed;
}

/* 动态调整步长和速度模式脉冲 */
static void liquid_detect_dyna_step_calc(liquid_detect_arg_t arg, liquid_detect_para_t *para)
{
    int cur_step = arg.s_cur_step;
    int last_step = para->last_step;
    int top_step = para->cal_step.top_step;
    int real_maxstep = para->cal_step.real_maxstep;
    int diff_step = DETECT_DYNA_STEP_MM * needle_z_1mm_steps(arg.needle);

    /* 默认值 */
    para->step_step = top_step - cur_step;
    if (arg.mode == NORMAL_DETECT_MODE && arg.needle != NEEDLE_TYPE_S) {
        /* 正常探测模式且非样本针取样时才能动态调整 */
        para->step_step = (last_step == 0 ? para->step_step :
            (last_step - top_step > diff_step ? (last_step - diff_step - cur_step) : para->step_step));
    }
    para->speed_step = real_maxstep - para->step_step - cur_step;

    return;
}

/* 根据针类型和探测的具体位置获取相应参数(标定脉冲) */
static int liquid_detect_para_get(liquid_detect_arg_t *arg, liquid_detect_para_t *para)
{
    para->motor_id = (arg->needle == NEEDLE_TYPE_R2 ? MOTOR_NEEDLE_R2_Z : MOTOR_NEEDLE_S_Z);

    if (liquid_detect_calibrate_step_get(arg, &para->cal_step) < 0) {
        DETECT_LOG("%s detect_pos = %d, cal_steps get failed.\n", needle_type_string(arg->needle), arg->reag_idx);
        goto bad;
    }

    para->stop_step = liquid_detect_stop_step_get(arg->needle);
    para->dv_step = liquid_detect_dv_step_get(*arg, para->cal_step.real_maxstep);
    para->last_step = liquid_detect_last_step_get(*arg, para);
    if (para->last_step == 0) {
        DETECT_LOG("%s detect_pos = %d, last_step get failed.\n", needle_type_string(arg->needle), arg->reag_idx);
        goto bad;
    }

    para->dyna_maxstep = liquid_detect_dyna_maxstep_get(*arg, para);
    if (para->dyna_maxstep == 0) {
        DETECT_LOG("%s detect_pos = %d, dyna_maxstep get failed.\n", needle_type_string(arg->needle), arg->reag_idx);
        goto bad;
    }

    para->thr = liquid_detect_thr_get(*arg);
    if (para->thr == 0) {
        DETECT_LOG("%s detect_pos = %d, detect_thr get failed.\n", needle_type_string(arg->needle), arg->reag_idx);
        goto bad;
    }

    para->contact_speed = liquid_detect_contact_speed_get(*arg);
    if (para->contact_speed == 0) {
        DETECT_LOG("%s detect_pos = %d, contact_speed get failed.\n", needle_type_string(arg->needle), arg->reag_idx);
        goto bad;
    }

    /* 取样本和余量探测无法动态调整屏蔽位，因此必须使用三段式运动方式探测液面 */
    if (arg->mode == REMAIN_DETECT_MODE) {
        para->move_mode = CMD_MOTOR_MOVE_STEP;
        para->timeout = REMAIN_TIMEOUT;
    } else {
        para->move_mode = (arg->needle == NEEDLE_TYPE_S ? CMD_MOTOR_MOVE_STEP : CMD_MOTOR_LIQ_DETECT);
        para->timeout = NORMAL_TIMEOUT;
    }

    /* 动态屏蔽位调整 */
    liquid_detect_dyna_step_calc(*arg, para);

    #if 0
    LOG("---------------%s detect arg---------------\n", needle_type_string(arg->needle));
    LOG("   order:              %d.\n", arg->order_no);
    LOG("   move_mode:          %s.\n", para->move_mode == CMD_MOTOR_MOVE_STEP ? "Old" : "New");
    LOG("   contact_speed:      %d.\n", para->contact_speed);
    LOG("   target_liq:         %s.\n", liquid_detect_target_string(*arg));
    LOG("   threshold:          %d.\n", para->thr);
    LOG("   calibrate_z:        %d.\n", para->cal_step.z_step);
    LOG("   top_step:           %d.\n", para->cal_step.top_step);
    LOG("   step_step:          %d.\n", para->step_step);
    LOG("   speed_step:         %d.\n", para->speed_step);
    LOG("   dyna_maxstep:       %d.\n", para->dyna_maxstep);
    LOG("   dv_step:            %d.\n", para->dv_step);
    LOG("   real_max_step:      %d.\n", para->cal_step.real_maxstep);
    LOG("   last_step:          %d.\n", para->last_step);
    LOG("------------------------------------------------------\n");
    #endif

    return 0;

bad:
    return -1;
}

/*
液面探测存在2种方式:正常模式和余量探测模式
    余量探测模式为了确保准确性，速度低
正常模式液面探测存在2种运动方式:多段和一段式(新模式)
    多段：1针先步长模式运动到标定位(瓶口)，2再使用速度模式去接触界面，3探测成功后给电机下发停止指令(急停);
    一段：FPGA控制运动完步长脉冲后，不重新启动电机，直接进入给定的速度去探测液面,
            成功后使用a/2减速停止(因此包含一段减速脉冲).
余量探测和样本针取样本时使用多段模式
    1余量为了准确性且其不吸液，所以不需要减速停补偿，因此使用多段;
    2取样本使用多段原因:
        a样本吸样量少，a/2补偿约1000脉冲，微量杯样本少时会存在触底情况;
        b暂存(清洗池和筛查暂存)存在吸样量大的情况，a/的减速脉冲也不能保证足够的吸样量;
        c取样本无法动态调整屏蔽脉冲，为保证时间片，速度较快，减速停的脉冲太大无法控制.
其余情况均使用新模式探测
    1新模式优势:
        a针表现为一段连续的运动;
        b减速停能基本确保吸液量.
    2对于样本针(穿刺针)来说:
        由于结构的特殊性，针尖距离吸液孔约2.5mm，减速停仅能保证针孔位于液体平面下，
        依旧不能确保足够吸液量，因此也需要再次补偿
    3新模式动态调整步长(屏蔽)脉冲,因此可以减小探测液面的速度，可以优化接触液面和减速停后的一致性。
*/

int liquid_detect_start(liquid_detect_arg_t arg)
{
    int comp_step = 0, detect_step = 0, detect_ret = 0, parse_ret = 0;
    liquid_detect_misc_t *misc_para = NULL;     /* 记录日志需要使用的参数 */
    liquid_detect_para_t para = {0};            /* 探测需要使用的参数 */
    float ratio = 0;

    if (liquid_detect_para_get(&arg, &para) != 0) {
        DETECT_LOG("%s detect_pos = %d, para get failed.\n", needle_type_string(arg.needle), arg.reag_idx);
        return EARG;
    }

    DETECT_LOG("%s detect_pos = %d - %s start, mode = %s, order = %d, take_vol = %f, S.z = %d, S.hat = %d, S.tube = %d, thr = %d, "
        "speed = %d, cs.z = %d, top_step = %d, step_step = %d, speed_step = %d, dv_step = %d, dyna_maxstep = %d, real_maxstep = %d.\n",
        needle_type_string(arg.needle), arg.reag_idx, liquid_detect_target_string(arg), liquid_detect_mode_string(arg.mode),
        arg.order_no, arg.take_ul, arg.s_cur_step, arg.hat_enable, arg.tube, para.thr, para.contact_speed, para.cal_step.z_step,
        para.cal_step.top_step, para.step_step, para.speed_step, para.dv_step, para.dyna_maxstep, para.cal_step.real_maxstep);

    misc_para = &g_misc_array[arg.needle == NEEDLE_TYPE_R2 ? NEEDLE_TYPE_R2 : NEEDLE_TYPE_S];
    liquid_detect_log_info_init(misc_para, para);

    if (slip_liquid_detect_type_set(arg.needle) < 0) {
        detect_ret = ETIMEOUT;
        goto out;
    }

    if (arg.needle == NEEDLE_TYPE_R2 && arg.mode != NORMAL_DETECT_MODE) {
        slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_OFF, 0);
    }

    /* 设置阈值 */
    if (slip_liquid_detect_thr_set(arg.needle, para.thr) < 0) {
        detect_ret = ETIMEOUT;
        goto out;
    }
    g_detect_running = 1;

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (para.move_mode == CMD_MOTOR_MOVE_STEP) {
        /* 多段模式探测液面: 步长运动阶段 */
        if (para.step_step > 0 && motor_move_sync(para.motor_id, para.move_mode, para.step_step,
            h3600_conf_get()->motor[para.motor_id].speed, para.timeout) < 0) {
            DETECT_LOG("%s z move(mode = step) failed.\n", needle_type_string(arg.needle));
            detect_ret = EMOVE;
            goto out;
        }
        if (arg.needle == NEEDLE_TYPE_S && arg.hat_enable == ATTR_ENABLE) {
            /* 特殊情况：带帽穿刺需要再穿透帽子 */
            slip_liquid_detect_collsion_barrier_set(arg.needle, arg.hat_enable);
            if (motor_move_sync(para.motor_id, para.move_mode, PIERCE_HAT_STEP,
                h3600_conf_get()->motor[para.motor_id].speed, para.timeout) < 0) {
                DETECT_LOG("%s z move(%d) failed.\n", needle_type_string(arg.needle), PIERCE_HAT_STEP);
                detect_ret = EMOVE;
                goto out;
            }
        }
    } else {
        if (para.step_step <= 0) {
            DETECT_LOG("%s z step(%d) invalid.\n", needle_type_string(arg.needle), para.step_step);
            detect_ret = EARG;
            goto out;
        } else if (motor_ld_move_sync(para.motor_id, para.step_step,
            h3600_conf_get()->motor[para.motor_id].speed, para.contact_speed, para.timeout) < 0) {
            DETECT_LOG("%s z move(mode = liquid) failed.\n", needle_type_string(arg.needle));
            detect_ret = EMOVE;
            goto out;
        }
    }
    FAULT_CHECK_END();

    /* 开启原始数据记录 */
    if (slip_liquid_detect_rcd_set(arg.needle, ATTR_ENABLE) < 0) {
        detect_ret = ETIMEOUT;
        goto out;
    }

    /* 开启液面探测 */
    if (slip_liquid_detect_state_set(arg.needle, ATTR_ENABLE) < 0) {
        detect_ret = ETIMEOUT;
        goto out;
    }

    /* 多段模式探测液面: 速度运动阶段 */
    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (para.move_mode == CMD_MOTOR_MOVE_STEP) {
        if (motor_speed_ctl_timedwait(para.motor_id, para.contact_speed, h3600_conf_get()->motor[para.motor_id ].acc,
            para.speed_step, 1, para.timeout) < 0) {
            detect_ret = ETIMEOUT;
            goto out;
        }
    }
    FAULT_CHECK_END();

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        /* 其他模块产生错误或者急停 */
        motor_stop_timedwait(para.motor_id, NORMAL_TIMEOUT);
        detect_ret = EMOVE;
        goto out;
    }

    /* 液面探测是否到达最大步长的监控 */
    work_queue_add(liquid_detect_maxstep_task, (void *)misc_para);

    /* 等待液面探测结果 */
    if (leave_singal_timeout_wait(misc_para->signo, para.timeout) == 1) {
        DETECT_LOG("%s detect_pos = %d, timeout.\n", needle_type_string(arg.needle), arg.reag_idx);
        detect_ret = ETIMEOUT;
    } else {
        #if 0
        if (misc_para->result == LIQ_LEAVE_IN) {
            detect_step = motor_current_pos_timedwait(para.motor_id, NORMAL_TIMEOUT);
            if (detect_step < 0) {
                detect_ret = ETIMEOUT;
                goto out;
            }
            DETECT_LOG("%s detect_pos = %d, result = %d, step = %d.\n", needle_type_string(arg.needle), arg.reag_idx, misc_para->result, detect_step);
        }
        #endif
    }
    misc_para->stop_flag = 1;

    /* 关闭液面探测，停止电机 */
    motor_stop_timedwait(para.motor_id, NORMAL_TIMEOUT);
    usleep(10 * 1000);
    if (slip_liquid_detect_state_set(arg.needle, ATTR_DISABLE) < 0) {
        detect_ret = ETIMEOUT;
        goto out;
    }
    if (detect_ret != ENONE) goto out;

    /* 获取移动的脉冲 */
    if (para.move_mode == CMD_MOTOR_LIQ_DETECT) {
        /* a/2减速停fpga响应2个中断，需等待a/2减速动作执行完毕后才能获取到正确的脉冲 */
        usleep(200 * 1000);
    }
    detect_step = motor_current_pos_timedwait(para.motor_id, NORMAL_TIMEOUT);
    if (detect_step < 0) {
        detect_ret = ETIMEOUT;
        goto out;
    }
    detect_ret = misc_para->detect_step = detect_step;
    DETECT_LOG("%s detect_pos = %d, detect_step = %d, dyna_maxstep = %d, dv_step = %d, real_maxstep = %d.\n",
        needle_type_string(arg.needle), arg.reag_idx, detect_step, para.dyna_maxstep, para.dv_step, para.cal_step.real_maxstep);
    if (misc_para->result == LIQ_REACH_MAX || misc_para->result == LIQ_COLLSION_IN) {
        if (misc_para->result == LIQ_REACH_MAX) {
            detect_ret = EMAXSTEP;
            if (arg.mode == NORMAL_DETECT_MODE && arg.needle != NEEDLE_TYPE_S && (para.dv_step - para.last_step < 2 * needle_z_1mm_steps(arg.needle))) {
                /*
                    1 试剂瓶实际很少(余量探测后首次取该试剂时出现:余量探测能探，正常探测可能无法探测)此时切瓶
                    2 试剂瓶未放置到底，试剂被提前取完导致的无法探测，此时切瓶
                */
                if ((arg.needle == NEEDLE_TYPE_S_R1 || arg.needle == NEEDLE_TYPE_S_DILU) && (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO)) {
                    /* 试剂仓质控品和如抗xa校准使用稀释液微量容器时不存在切瓶 */
                    DETECT_LOG("%s detect_pos = %d, maxstep!\n", needle_type_string(arg.needle), arg.reag_idx);
                    detect_ret = EMAXSTEP;
                } else {
                    DETECT_LOG("%s detect_pos = %d, reag_switch 0!\n", needle_type_string(arg.needle), arg.reag_idx);
                    detect_ret = ESWITCH;
                }
            }
        } else {
            detect_ret = ECOLLSION;
        }
        goto out;
    }

    /* 和理论探测高度比较 */
    if (arg.mode == NORMAL_DETECT_MODE && detect_ret > EMAX && module_fault_stat_get() == MODULE_FAULT_NONE) {
        parse_ret = liquid_detect_step_parse(detect_step, arg, para);
        if (parse_ret < EMAX) {
            detect_ret = parse_ret;
            DETECT_LOG("%s detect_pos = %d, detect_step = %d Invalid.\n", needle_type_string(arg.needle), arg.reag_idx, detect_step);
            goto out;
        }
    }

    /* R2试剂针针孔在下，样本针取稀释液(非微量容器)和R1试剂速度较快(15000时减速停脉冲850，约2.2mm)，吸液无需补偿触发，切瓶线后直接切瓶 */
    if (arg.mode == NORMAL_DETECT_MODE && ((arg.needle == NEEDLE_TYPE_S_DILU && arg.bt != BT_TUBE_EP15)
        || (arg.needle == NEEDLE_TYPE_S_R1 && (arg.bt == BT_REAGENT_7_ML || arg.bt == BT_REAGENT_15_ML)) || arg.needle == NEEDLE_TYPE_R2)) {
        if (detect_step >= para.dv_step) {
            DETECT_LOG("%s detect_pos = %d, detect_step = %d, dv_step = %d, real_max = %d, reag_switch 1!\n",
                needle_type_string(arg.needle), arg.reag_idx, detect_step, para.dv_step, para.cal_step.real_maxstep);
            detect_ret = ESWITCH;
        }
    }

    /* 穿刺针特殊性，为保证吸取样本/试剂仓内质控品/抗xa校准等稀释液微量容器，进行单独补偿 */
    if (arg.mode == NORMAL_DETECT_MODE && detect_ret > EMAX && module_fault_stat_get() == MODULE_FAULT_NONE &&
        (arg.needle == NEEDLE_TYPE_S || ((arg.needle == NEEDLE_TYPE_S_R1 || arg.needle == NEEDLE_TYPE_S_DILU) && (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO)))) {
        if (arg.hat_enable == ATTR_ENABLE) {
            comp_step = (int)(arg.take_ul  * VACUTAINER_1UL_RATIO);
        } else {
            if ((arg.needle == NEEDLE_TYPE_S_R1 || arg.needle == NEEDLE_TYPE_S_DILU) && (arg.bt == BT_TUBE_EP15 || arg.bt == BT_TUBE_MICRO)) {
                /* 试剂仓内微量杯:EP管和日立杯在试剂仓内底部高度基本一致，EP管底部截面积小扩大系数 */
                if (para.cal_step.real_maxstep - detect_step <= ADP_LOW_LIMIT) {
                    ratio = (arg.bt == BT_TUBE_EP15 ? ADP_LOW_1UL_RATIO + 5 : ADP_LOW_1UL_RATIO);
                } else {
                    ratio = ADP_1UL_RATIO;
                }
            } else {
                if (arg.tube == STANARD_CUP || arg.tube == EP_CUP) {
                    /* 样本架上微量杯 */
                    if (para.cal_step.real_maxstep - detect_step <= ADP_LOW_LIMIT) {
                        ratio = ADP_LOW_1UL_RATIO;
                    } else {
                        ratio = ADP_1UL_RATIO;
                    }
                } else {
                    /* 样本架上采血管 */
                    ratio = VACUTAINER_1UL_RATIO;
                }
            }
            comp_step = (arg.take_ul == 0 ? 0 : (int)(arg.take_ul * ratio));
            if (comp_step && comp_step < S_COMP_STEP_MIN) {
                comp_step = S_COMP_STEP_MIN;
            }
        }
        DETECT_LOG("%s detect_pos = %d, take = %ful, ratio = %f, comp_step = %d(expect).\n",
            needle_type_string(arg.needle), arg.reag_idx, arg.take_ul, ratio, comp_step);

        if (comp_step > 0) {
            if ((para.cal_step.real_maxstep - detect_step) >= comp_step) {
                detect_ret += comp_step;
                /* 根据计算还可以补偿，但此时管内剩余量已不多，为保证吸取足够(补偿系数不够精准)，直接到最底部吸液 */
                if ((para.cal_step.real_maxstep - detect_step) <= 6 * S_1MM_STEPS && comp_step > S_COMP_STEP_MIN) {
                    comp_step = para.cal_step.real_maxstep - detect_step;
                }
                if (motor_move_ctl_sync(para.motor_id, CMD_MOTOR_MOVE_STEP, comp_step,
                    h3600_conf_get()->motor[para.motor_id].speed, h3600_conf_get()->motor[para.motor_id].acc, NORMAL_TIMEOUT)) {
                    detect_ret = EMOVE;
                }
            } else {
                /* 不够补偿脉冲，则按最大步长处理 */
                DETECT_LOG("%s detect_pos = %d, can not comps steps.\n", needle_type_string(arg.needle), arg.reag_idx);
                detect_ret = EMAXSTEP;
            }
        }
    }

out:
    g_detect_running = 0;
    if (misc_para->stop_flag == 0) misc_para->stop_flag = 1;
    if (arg.needle == NEEDLE_TYPE_R2 && arg.mode != NORMAL_DETECT_MODE) {
        usleep(200 * 1000);
        slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    }

    if (arg.mode == NORMAL_DETECT_MODE) {
        if (detect_ret < EMAX && detect_ret != ESWITCH) {
            liquid_detect_err_count_set(arg.needle == NEEDLE_TYPE_R2 ? NEEDLE_TYPE_R2 : NEEDLE_TYPE_S);
        } else {
            liquid_detect_err_count_clear(arg.needle == NEEDLE_TYPE_R2 ? NEEDLE_TYPE_R2 : NEEDLE_TYPE_S);
        }
    }

    if (module_fault_stat_get() != MODULE_FAULT_NONE) {
        detect_ret = EMOVE;
    }
    return detect_ret;
}

void liquid_detect_motor_init(
    int motor_x,
    int motor_y,
    int motor_z,
    motor_time_sync_attr_t *attr_x,
    motor_time_sync_attr_t *attr_y,
    motor_time_sync_attr_t *attr_z)
{
    if (motor_x) {
        attr_x->v0_speed = 100;
        attr_x->speed = h3600_conf_get()->motor[motor_x].speed;
        attr_x->vmax_speed = h3600_conf_get()->motor[motor_x].speed;
        attr_x->acc = h3600_conf_get()->motor[motor_x].acc;
        attr_x->max_acc = h3600_conf_get()->motor[motor_x].acc;
    }

    attr_y->v0_speed = 100;
    attr_y->speed = h3600_conf_get()->motor[motor_y].speed;
    attr_y->vmax_speed = h3600_conf_get()->motor[motor_y].speed;
    attr_y->acc = h3600_conf_get()->motor[motor_y].acc;
    attr_y->max_acc = h3600_conf_get()->motor[motor_y].acc;

    attr_z->v0_speed = (motor_z == MOTOR_NEEDLE_S_Z ? 1000 : 100);
    attr_z->speed = h3600_conf_get()->motor[motor_z].speed;
    attr_z->vmax_speed = h3600_conf_get()->motor[motor_z].speed;
    attr_z->acc = h3600_conf_get()->motor[motor_z].acc;
    attr_z->max_acc = h3600_conf_get()->motor[motor_z].acc;
}

static int liquid_detect_remain_motor_move(
    needle_type_t needle,
    liquid_detect_remain_dst_t pos,
    int motor_x,
    int motor_y,
    pos_t cur_pos,
    pos_t dst_pos,
    motor_time_sync_attr_t attr_x,
    motor_time_sync_attr_t attr_y)
{
    int succ = 0;
    int acc_x = 0;
    int acc_y = 0;
    unsigned char id[1] = {motor_y};

    FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
    if (needle == NEEDLE_TYPE_R2) {
        attr_y.step = abs(dst_pos.y - cur_pos.y);
        acc_y = calc_motor_move_in_time(&attr_y, STARTUP_TIMES_S_X);
        if (pos == POS_RESET) {
            motor_move_ctl_async(motor_y, CMD_MOTOR_RST, 0, attr_y.speed, acc_y);
        } else {
            motor_move_ctl_async(motor_y, CMD_MOTOR_MOVE_STEP, dst_pos.y - cur_pos.y, attr_y.speed, acc_y);
        }
        if(motors_move_timewait(id, ARRAY_SIZE(id), MOTOR_DEFAULT_TIMEOUT)) {
            DETECT_LOG("reag_remain_detect: r2.y move timeout!\n");
            succ = -1;
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Y);
        }
    } else {
        attr_x.step = abs(dst_pos.x - cur_pos.x);
        attr_y.step = abs(dst_pos.y - cur_pos.y);
        if (attr_x.step > attr_y.step) {
        } else {
            attr_x.step = abs((dst_pos.y - cur_pos.y) - (dst_pos.x - cur_pos.x));
        }
        acc_x = calc_motor_move_in_time(&attr_x, STARTUP_TIMES_S_X);
        if (motor_move_dual_ctl_sync(MOTOR_NEEDLE_S_Y, CMD_MOTOR_DUAL_MOVE, dst_pos.x - cur_pos.x,
            dst_pos.y - cur_pos.y, attr_x.speed, acc_x, MOTOR_DEFAULT_TIMEOUT, STARTUP_TIMES_S_X)) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_NEEDLE_S_XY);
            DETECT_LOG("reag_remain_detect: S.x_y move failed!\n");
        }
    }
    FAULT_CHECK_END();

    return succ;
}

static int liquid_detect_remain_bt_check(needle_type_t needle, int pos, bottle_type_t bt)
{
    int bt_15ml = reagent_table_bottle_type_get(pos);

    if (needle != NEEDLE_TYPE_R2) {
        if (pos >= DILU_IDX_START) {
            if (bt != BT_REAGENT_25_ML && bt != BT_TUBE_EP15) {
                DETECT_LOG("reag_remain_detect: %s at pos = %d, bt not match(cur = %d, must = %d or %d).\n",
                    needle_type_string(needle), pos, bt, BT_REAGENT_25_ML, BT_TUBE_EP15);
                goto bad;
            }
        } else {
            if (bt_15ml == 1) {
                if (bt != BT_REAGENT_15_ML) {
                    DETECT_LOG("reag_remain_detect: %s at pos = %d, bt not match(cur = %d, must = %d).\n",
                        needle_type_string(needle), pos, bt, BT_REAGENT_15_ML);
                    goto bad;
                }
            } else {
                if (bt != BT_REAGENT_7_ML && bt != BT_TUBE_MICRO && bt != BT_TUBE_EP15) {
                    DETECT_LOG("reag_remain_detect: %s at pos = %d, bt not match(cur = %d, must = %d).\n",
                        needle_type_string(needle), pos, bt, BT_REAGENT_7_ML);
                    goto bad;
                }
            }
        }
    } else {
        if (bt_15ml == 1) {
            if (bt != BT_REAGENT_15_ML) {
                DETECT_LOG("reag_remain_detect: %s at pos = %d, bt not match(cur = %d, must = %d).\n",
                    needle_type_string(needle), pos, bt, BT_REAGENT_15_ML);
                goto bad;
            }
        } else {
            if (bt != BT_REAGENT_7_ML) {
                DETECT_LOG("reag_remain_detect: %s at pos = %d, bt not match(cur = %d, must = %d).\n",
                    needle_type_string(needle), pos, bt, BT_REAGENT_7_ML);
                goto bad;
            }
        }
    }
    return 0;

bad:
    return -1;
}


/* 检查探测的结果，成功返回平均值，失败返回-1 */
static int liquid_detect_remain_step_check(needle_type_t needle, int *data, int size)
{
    int idx = 0, sum = 0, avg = 0, diff = 0;
    int diff_max = needle_z_1mm_steps(needle);

    /* 求平均值 */
    for (idx = 0; idx < size; idx++) {
        sum += data[idx];
    }
    avg = sum / size;

    /* 检查探测结果 */
    for (idx = 0; idx < size; idx++) {
        diff = abs(data[idx] - avg);
        if (diff > diff_max) {
            DETECT_LOG("detect_func: warning! step(%d) no macth(avg = %d).\n", data[idx], avg);
            return -1;
        }
    }

    return avg;
}

static int liquid_detect_remain_step_to_ul(needle_type_t needle, bottle_type_t bottle_type, int coated, int step_left)
{
    liquid_detect_step_ul_t *info = NULL;
    float ratio = 0;
    int i = 0, ul = 0, ul_max = 0, step_base = 0, vol_base = 0;

    if (bottle_type <= BT_UNKNOWN || bottle_type >= BT_TYPE_MAX) {
        DETECT_LOG("step_to_ul: unknown bottle_type(%d).\n", bottle_type);
        goto out;
    }
    if (step_left <= 0) {
        DETECT_LOG("step_to_ul: no step left(%d).\n", step_left);
        goto out;
    }

    if (needle == NEEDLE_TYPE_R2) {
        if (bottle_type == BT_REAGENT_7_ML) {
            info = (coated ? r2_7ml_coated_info : r2_7ml_info);
        } else if (bottle_type == BT_REAGENT_15_ML) {
            info = (coated ? r2_15ml_coated_info : r2_15ml_info);
        } else {
            DETECT_LOG("step_to_ul: Not support such bottle_type(%d).\n", bottle_type);
            goto out;
        }
        ul_max = (bottle_type == BT_REAGENT_7_ML ? BT_REAGENT_7_ML_MAX : BT_REAGENT_15_ML_MAX);
    } else {
        if (bottle_type == BT_REAGENT_7_ML) {
            info = (coated ? s_7ml_coated_info : s_7ml_info);
            ul_max = BT_REAGENT_7_ML_MAX;
        } else if (bottle_type == BT_REAGENT_15_ML) {
            info = (coated ? s_15ml_coated_info : s_15ml_info);
            ul_max = BT_REAGENT_15_ML_MAX;
        } else if (bottle_type == BT_REAGENT_25_ML) {
            info = s_dilu_info;
            ul_max = BT_REAGENT_25_ML_MAX;
        } else if (bottle_type == BT_TUBE_EP15) {
            info = s_ep_cup_info;
            ul_max = BT_EP_CUP_UL_MAX;
        } else if (bottle_type == BT_TUBE_MICRO) {
            info = s_stand_cup_info;
            ul_max = BT_STAND_CUP_UL_MAX;
        } else {
            DETECT_LOG("step_to_ul: Not support such bottle_type(%d).\n", bottle_type);
            goto out;
        }
    }

    ratio = info[0].ratio;
    vol_base = info[0].vol_left;
    step_base = info[0].step_left;
    for (i = 0; i < REMIAN_INFO_MAX - 1; i++) {
        if (step_left >= info[i].step_left && step_left < info[i + 1].step_left) {
            step_base = info[i].step_left;
            vol_base = info[i].vol_left;
            ratio = info[i].ratio;
            break;
        } else if ((i >= 1 && info[i].step_left == 0 && info[i - 1].step_left != 0) &&
            step_left >= info[i - 1].step_left) {
            /* 剩余脉冲 > 记录的最大数据，此时采用最后一组数据计算 */
            step_base = info[i - 1].step_left;
            vol_base = info[i - 1].vol_left;
            ratio = info[i - 1].ratio;
            break;
        }
    }
    ul = vol_base + (step_left - step_base) * ratio;
    ul = (ul > ul_max ? ul_max : ul);   /* 限制最大量 */
    DETECT_LOG("step_to_ul: calc, needle = %s, bt_type = %d, left = %d, step_base = %d, vol_base = %d, ratio = %f -> %dul(%d + (%d - %d) * %f).\n",
        needle_type_string(needle), bottle_type, step_left, step_base, vol_base, ratio, ul, vol_base, step_left, step_base, ratio);

out:
    return ul;
}

static void liquid_detect_remain_fault_handler(int motor_x, int motor_y, int motor_z)
{
    if (motor_x && motor_y) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_XY);
    } else if (motor_x) {
        FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_X);
    } else if (motor_y) {
        if (motor_y == MOTOR_NEEDLE_S_Y) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Y);
        } else if (motor_y == MOTOR_NEEDLE_R2_Y) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Y);
        }
    } else {
        if (motor_z == MOTOR_NEEDLE_S_Z) {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_S, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_S_Z);
        } else {
            FAULT_CHECK_DEAL(FAULT_NEEDLE_R2, MODULE_FAULT_LEVEL2, (void *)MODULE_FAULT_NEEDLE_R2_Z);
        }
    }
}

static int liquid_detect_remain_clean(int motor_z, motor_time_sync_attr_t *needle_attr_z)
{
    s_normal_outside_clean(1);
    if (motor_move_ctl_sync(motor_z, CMD_MOTOR_RST, 0,
        needle_attr_z->speed, needle_attr_z->acc, MOTOR_DEFAULT_TIMEOUT)) {
        DETECT_LOG("reag_remain_detect: S.z reset failed!\n");
        goto bad;
    }
    /* 20240924更改挡片后样本针洗针位置 */
    if (motor_z == MOTOR_NEEDLE_S_Z) {
        if (motor_move_ctl_sync(motor_z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS,
            h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].speed, h3600_conf_get()->motor[MOTOR_NEEDLE_S_Z].acc, MOTOR_DEFAULT_TIMEOUT)) {
            DETECT_LOG("reag_remain_detect: S.z move failed!\n");
            goto bad;
        }
    }
    s_normal_outside_clean(0);
    usleep(200 * 1000);
    s_normal_inside_clean();

    return 0;

bad:
    liquid_detect_remain_fault_handler(0, 0, motor_z);
    return -1;
}

static liquid_remain_detect_ret_t liquid_detect_remain_start(liquid_detect_remain_arg_t *arg)
{
    int motor_x = 0, motor_y = 0, motor_z = 0, ret = RET_OK, err_flag = 0, clean_ignore = 0;
    move_pos_t move_pos = 0, clean_pos = 0;
    cup_pos_t cup_pos = POS_INVALID;
    pos_t cur = {0}, dst = {0}, clean = {0};
    int idx = 0, step_avg = 0, step_left = 0, vol_left = 0, recheck = 0;
    int detect_step[LIQUID_DETECT_MAX] = {0};
    liquid_detect_arg_t ld_arg = {0};
    motor_time_sync_attr_t attr_x = {0};
    motor_time_sync_attr_t attr_y = {0};
    motor_time_sync_attr_t attr_z = {0};
    attr_enable_t reag_table_needed = ATTR_DISABLE;
    fault_type_t ftype = FAULT_COMMON;
    module_fault_stat_t flevel = MODULE_FAULT_NONE;
    reag_table_cotl_t rtc_arg = {0};
    liquid_detect_cs_t cs = {0};
    char *fcode = NULL, *fid = NULL, alarm_message[FAULT_CODE_LEN] = {0};

    DETECT_LOG("reag_remain_detect: pos = %d, bt = %d, rc = %d, rx = %d, coated = %d.\n",
        arg->reag_idx, arg->bottle_type, arg->reag_category, arg->rx, arg->coated);

    /* 根据参数信息填充探测参数 */
    ld_arg.mode = REMAIN_DETECT_MODE;
    ld_arg.tube = PP_1_8;
    ld_arg.reag_idx = arg->reag_idx;
    ld_arg.bt = arg->bottle_type;
    ld_arg.coated = arg->coated;
    if (arg->rx == 4) {
        ld_arg.needle = NEEDLE_TYPE_R2;
    } else {
        ld_arg.needle = NEEDLE_TYPE_S;
        ld_arg.s_cur_step = motor_current_pos_timedwait(MOTOR_NEEDLE_S_Z, NORMAL_TIMEOUT);
        if (arg->reag_category == 0) {
            /* 作为孵育试剂针探测试剂 */
            ld_arg.needle = NEEDLE_TYPE_S_R1;
            reag_table_needed = ATTR_ENABLE;
            move_pos = MOVE_S_ADD_REAGENT;
            cup_pos = (arg->reag_idx % 2 ? POS_REAGENT_TABLE_S_IN : POS_REAGENT_TABLE_S_OUT);
        } else if (arg->reag_category == 1) {
            /* TBD 作为孵育试剂针探测乏因子血浆，该瓶是否有特殊位置要求？ */
            ld_arg.needle = NEEDLE_TYPE_S_R1;
            reag_table_needed = ATTR_ENABLE;
            move_pos = MOVE_S_ADD_REAGENT;
            cup_pos = (arg->reag_idx % 2 ? POS_REAGENT_TABLE_S_IN : POS_REAGENT_TABLE_S_OUT);
        } else if (arg->reag_category == 2) {
            /* 作为样本针探测稀释液 */
            ld_arg.needle = NEEDLE_TYPE_S_DILU;
            move_pos = MOVE_S_DILU;
            cup_pos = arg->reag_idx - DILU_IDX_START;
        } else if (arg->reag_category == 4) {
            /* 作为孵育试剂针探测试剂仓内质控品 */
            ld_arg.needle = NEEDLE_TYPE_S_R1;
            reag_table_needed = ATTR_ENABLE;
            move_pos = MOVE_S_ADD_REAGENT;
            cup_pos = (arg->reag_idx % 2 ? POS_REAGENT_TABLE_S_IN : POS_REAGENT_TABLE_S_OUT);
        } else {
            DETECT_LOG("reag_remain_detect: dont support such reag_category(%d).\n", arg->reag_category);
            fid = MODULE_FAULT_NEEDLE_S_REMAIN;
            fault_code_generate(alarm_message, MODULE_CLASS_FAULT_NEEDLE_S, fid);
            report_alarm_message(0, alarm_message);
            ret = RET_IGNORE;
            clean_ignore = 1;
            goto out;
        }
    }

    /* 获取坐标信息，X/Y/试剂仓移动就位 */
    if (ld_arg.needle != NEEDLE_TYPE_R2) {
        motor_x = MOTOR_NEEDLE_S_X;
        motor_y = MOTOR_NEEDLE_S_Y;
        motor_z = MOTOR_NEEDLE_S_Z;
        ftype = FAULT_NEEDLE_S;
        flevel = MODULE_FAULT_LEVEL2;
        clean_pos = MOVE_S_CLEAN;
    } else {
        motor_y = MOTOR_NEEDLE_R2_Y;
        motor_z = MOTOR_NEEDLE_R2_Z;
        ftype = FAULT_NEEDLE_R2;
        flevel = MODULE_FAULT_LEVEL2;
        move_pos = MOVE_R2_REAGENT;
        clean_pos = MOVE_R2_CLEAN;
        reag_table_needed = ATTR_ENABLE;
        cup_pos = (arg->reag_idx % 2 ? POS_REAGENT_TABLE_R2_IN : POS_REAGENT_TABLE_R2_OUT);
    }

    /* 电机初始化 */
    liquid_detect_motor_init(motor_x, motor_y, motor_z, &attr_x, &attr_y, &attr_z);

    /* 检查瓶型 */
    if (liquid_detect_remain_bt_check(ld_arg.needle, arg->reag_idx, arg->bottle_type) < 0) {
        fid = MODULT_FAULT_REAGENT_BT_NOT_MATCH;
        fault_code_generate(alarm_message, MODULE_CLASS_FAULT_REAGENT_TABLE, fid);
        report_alarm_message(0, alarm_message);
        ret = RET_IGNORE;
        clean_ignore = 1;
        goto out;
    }

    /* 获取标定位置 */
    if (liquid_detect_calibrate_step_get(&ld_arg, &cs) < 0) {
        DETECT_LOG("reag_remain_detect: %s detect_pos = %d, cal_steps get failed.\n", needle_type_string(ld_arg.needle), arg->reag_idx);
        fid = (ld_arg.needle == NEEDLE_TYPE_R2 ? MODULE_FAULT_NEEDLE_R2_REMAIN : MODULE_FAULT_NEEDLE_S_REMAIN);
        fcode = (ld_arg.needle == NEEDLE_TYPE_R2 ? MODULE_CLASS_FAULT_NEEDLE_R2 : MODULE_CLASS_FAULT_NEEDLE_S);
        fault_code_generate(alarm_message, fcode, fid);
        report_alarm_message(0, alarm_message);
        ret = RET_IGNORE;
        clean_ignore = 1;
        goto out;
    }

    /* 试剂仓就位 */
    if (reag_table_needed == ATTR_ENABLE) {
        rtc_arg.table_move_type = TABLE_COMMON_MOVE;
        rtc_arg.table_dest_pos_idx = (needle_pos_t)arg->reag_idx;
        rtc_arg.req_pos_type = (ld_arg.needle == NEEDLE_TYPE_R2 ? NEEDLE_R2 : NEEDLE_S);
        rtc_arg.move_time = MOTOR_DEFAULT_TIMEOUT;
        if (reag_table_occupy_flag_get()) {
            DETECT_LOG("reag_remain_detect: reagent occupy get failed!\n");
            FAULT_CHECK_DEAL(ftype, flevel, (void*)MODULE_FAULT_NEEDLE_R2_GET_REAGENT);
            ret = RET_ERROR;
            clean_ignore = 1;
            goto out;
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL2);
        reag_table_occupy_flag_set(1);
        reagent_table_move_interface(&rtc_arg);
        FAULT_CHECK_END();
    }

    /* 针xy就位 */
    if (ld_arg.needle == NEEDLE_TYPE_R2) {
        set_pos(&cur, 0, 0, 0);
    } else {
        get_special_pos(MOVE_S_TEMP, 0, &clean, FLAG_POS_NONE);
        set_pos(&cur, clean.x, clean.y, 0);
    }
    get_special_pos(move_pos, cup_pos, &dst, FLAG_POS_UNLOCK); /* TBD */
    if (liquid_detect_remain_motor_move(ld_arg.needle, POS_DETECT,
        motor_x, motor_y, cur, dst, attr_x, attr_y) < 0) {
        DETECT_LOG("reag_remain_detect: %s motor move failed.\n", needle_type_string(ld_arg.needle));
        ret = RET_ERROR;
        clean_ignore = 1;
        goto out;
    }

again:
    for (idx = 0; idx < LIQUID_DETECT_MAX; idx++) {
        if (module_fault_stat_get() != MODULE_FAULT_NONE) {
            DETECT_LOG("reag_remain_detect: detect other error, force break.\n");
            ret = RET_ERROR;
            if (recheck == 0 && idx == 0) {
                clean_ignore = 1;
            }
            goto out;
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        detect_step[idx] = liquid_detect_start(ld_arg);
        slip_liquid_detect_rcd_set(ld_arg.needle, ATTR_DISABLE);
        if (detect_step[idx] == EMAXSTEP) {
            detect_step[idx] = cs.real_maxstep;
            DETECT_LOG("reag_remain_detect: reach maxstep.\n");
        } else {
            if (module_fault_stat_get() == MODULE_FAULT_NONE) {
                if (liquid_detect_result_report(ld_arg.needle, detect_step[idx]) < 0) {
                    DETECT_LOG("reag_remain_detect: result parse failed.\n");
                    ret = RET_ERROR;
                    goto out;
                }
            } else {
                DETECT_LOG("reag_remain_detect: detect other error, force break.\n");
                ret = RET_ERROR;
                goto out;
            }
        }
        FAULT_CHECK_END();

        if (motor_move_ctl_sync(motor_z, CMD_MOTOR_RST, 0, attr_z.speed, attr_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            liquid_detect_remain_fault_handler(0, 0, motor_z);
        }
        if (ld_arg.needle != NEEDLE_TYPE_R2) {
            ld_arg.s_cur_step = 0;
        }
        set_pos(&cur, dst.x, dst.y, 0);
        usleep(500 * 1000);
    }

    err_flag = 0;
    step_avg = liquid_detect_remain_step_check(ld_arg.needle, detect_step, ARRAY_SIZE(detect_step));
    if (step_avg < 0) {
        recheck++;
        DETECT_LOG("reag_remain_detect: warning! %s detect pos = %d, step not match, recheck %d times.\n",
            needle_type_string(ld_arg.needle), arg->reag_idx, recheck);
        if (recheck <= (LIQUID_DETECT_MAX - 1)) {
            goto again;
        } else {
            /* 多次尝试依然失败 */
            step_avg = cs.real_maxstep;
            err_flag = 1;
            fid = (ld_arg.needle == NEEDLE_TYPE_R2 ? MODULE_FAULT_NEEDLE_R2_REMAIN : MODULE_FAULT_NEEDLE_S_REMAIN);
            fcode = (ld_arg.needle == NEEDLE_TYPE_R2 ? MODULE_CLASS_FAULT_NEEDLE_R2 : MODULE_CLASS_FAULT_NEEDLE_S);
            fault_code_generate(alarm_message, fcode, fid);
            report_alarm_message(0, alarm_message);
            DETECT_LOG("reag_remain_detect: warning! %s detect pos = %d, step Not match.\n",
                needle_type_string(ld_arg.needle), arg->reag_idx);
        }
    }
    step_left = cs.real_maxstep - step_avg;
    vol_left = liquid_detect_remain_step_to_ul(ld_arg.needle, arg->bottle_type, arg->coated, step_left);
    DETECT_LOG("reag_remain_detect: avg = %d(%d - %d - %d), left = %d -> %dul(needle = %s, bt = %d, coated = %d, pos = %d).\n",
        step_avg, detect_step[0], detect_step[1], detect_step[2], step_left, vol_left,
        needle_type_string(ld_arg.needle), arg->bottle_type, arg->coated, arg->reag_idx);
    if (err_flag) {
        DETECT_LOG("reag_remain_detect: report %d remain = -1.\n", arg->reag_idx);
        report_reagent_remain(arg->reag_idx, -1, 0);
    } else {
        DETECT_LOG("reag_remain_detect: report %d remain = %dul.\n", arg->reag_idx, vol_left);
        report_reagent_remain(arg->reag_idx, vol_left, 0);
        liquid_detect_rcd_list_update(ld_arg.needle, arg->reag_idx, 0, step_avg, cs.real_maxstep, arg->bottle_type, arg->coated);
    }

    if (reag_table_needed == ATTR_ENABLE) {
        reag_table_occupy_flag_set(0);
    }

    get_special_pos(clean_pos, 0, &clean, FLAG_POS_UNLOCK);
    if (ld_arg.needle != NEEDLE_TYPE_R2) {
        if (liquid_detect_remain_motor_move(ld_arg.needle, POS_CLEAN,
            motor_x, motor_y, cur, clean, attr_x, attr_y) < 0) {
            DETECT_LOG("reag_remain_detect: %s motor move failed.\n", needle_type_string(ld_arg.needle));
            ret = RET_ERROR;
            goto out;
        }
        liquid_detect_remain_clean(motor_z, &attr_z);
    } else {
        /* 洗针位 */
        if (liquid_detect_remain_motor_move(ld_arg.needle, POS_CLEAN,
            motor_x, motor_y, cur, clean, attr_x, attr_y) < 0) {
            DETECT_LOG("reag_remain_detect: %s motor move failed.\n", needle_type_string(ld_arg.needle));
            ret = RET_ERROR;
            goto out;
        }
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(motor_z, CMD_MOTOR_MOVE_STEP, clean.z + NEEDLE_R2C_COMP_STEP - NEEDLE_R2_NOR_CLEAN_STEP,
            attr_z.speed, attr_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            DETECT_LOG("reag_remain_detect: %s z move failed.\n", needle_type_string(ld_arg.needle));
            liquid_detect_remain_fault_handler(0, 0, motor_z);
        }
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        r2_normal_clean();
        FAULT_CHECK_END();
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        if (motor_move_ctl_sync(motor_z, CMD_MOTOR_RST, 0, attr_z.speed, attr_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            DETECT_LOG("reag_remain_detect: %s z rst failed.\n", needle_type_string(ld_arg.needle));
            liquid_detect_remain_fault_handler(0, 0, motor_z);
        }
        FAULT_CHECK_END();
    }

    set_pos(&cur, clean.x, clean.y, 0);
    if (ld_arg.needle != NEEDLE_TYPE_R2) {
        /* 注意：S洗针需要在MOVE_S_CLEAN处，但余量探测开始时需要在MOVE_S_TEMP */
        get_special_pos(MOVE_S_TEMP, 0, &dst, FLAG_POS_NONE);
    } else {
        set_pos(&dst, 0, 0, 0);
    }
    if (liquid_detect_remain_motor_move(ld_arg.needle, POS_RESET,
        motor_x, motor_y, cur, dst, attr_x, attr_y) < 0) {
        DETECT_LOG("reag_remain_detect: %s motor move failed.\n", needle_type_string(ld_arg.needle));
        ret = RET_ERROR;
        goto out;
    }

    return 0;

out:
    DETECT_LOG("reag_remain_detect: %s remain_detect failed(%d).\n", needle_type_string(ld_arg.needle), ret);
    report_reagent_remain(arg->reag_idx, -1, 0);
    if (ld_arg.needle == NEEDLE_TYPE_R2) {
        slip_temperate_ctl_pwm_set(TEMP_NEEDLE_R2, TEMP_CTL_NORMAL_ON, 0);
    }
    /* 余量探测失败，也需要复位Z轴！！ */
    if (motor_move_ctl_sync(motor_z, CMD_MOTOR_RST, 0, attr_z.speed, attr_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
        DETECT_LOG("reag_remain_detect: %s z rst failed.\n", needle_type_string(ld_arg.needle));
        liquid_detect_remain_fault_handler(0, 0, motor_z);
        return RET_ERROR;
    }
    /* 更改挡片后，需要将针回退到NEEDLE_S_CLEAN_POS */
    if (ld_arg.needle != NEEDLE_TYPE_R2) {
        if (motor_move_ctl_sync(motor_z, CMD_MOTOR_MOVE_STEP, NEEDLE_S_CLEAN_POS, attr_z.speed, attr_z.acc, MOTOR_DEFAULT_TIMEOUT)) {
            liquid_detect_remain_fault_handler(0, 0, motor_z);
            return RET_ERROR;
        }
    }
    if (ld_arg.needle != NEEDLE_TYPE_R2 && clean_ignore == 0) {
        liquid_detect_remain_clean(motor_z, &attr_z);
    }
    return ret;
}

int liquid_detect_remain_func(void)
{
    int ret = 0;
    reag_table_cotl_t table_cotl = {0};
    liquid_detect_remain_arg_t *n = NULL;
    liquid_detect_remain_arg_t *pos = NULL;

    list_for_each_entry_safe(pos, n, &reag_remain_list, remain_sibling) {
        if (module_fault_stat_get() != MODULE_FAULT_NONE) {
            DETECT_LOG("reag_remain_detect: detect other error, force break.\n");
            ret = -1;
            break;
        }
        ret = liquid_detect_remain_start(pos);
        if (ret == RET_OK) {
            /* 探测成功，置成功标记 */
            reag_remain_detected_set(pos->reag_idx);
        } else if (ret == RET_IGNORE) {
            ret = RET_OK;
            DETECT_LOG("reag_remain_detect: pos %d remain detect ignore.\n", pos->reag_idx);
        } else {
            /* 探测失败，不再继续探测 */
            DETECT_LOG("reag_remain_detect: pos %d remain detect failed.\n", pos->reag_idx);
            ret = -1;
            break;
        }
    }

    if (ret == RET_OK) {
        FAULT_CHECK_START(MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL);
        table_cotl.table_move_type = TABLE_COMMON_RESET;
        reagent_table_move_interface(&table_cotl);
        FAULT_CHECK_END();
    }
    liquid_detect_remain_del_all();

    return ret;
}

void liquid_detect_remain_async(void *arg)
{
    int ret = -1;
    int userdata = *(int32_t *)arg;

    machine_maintence_state_set(1);
    init_module_work_flag();

    /* 自检部分 */
    ret = reset_all_motors();
    if (ret == 0) {
        ret = remain_detect_prepare();
        if(ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
            DETECT_LOG("reag_remain_detect: instrument_self_test_async successed, but stat != MODULE_FAULT_NONE.\n");
            ret = -1;
        }
    }

    if (ret == 0) {
        ret = liquid_detect_remain_func();
    }
    report_asnyc_invoke_result(userdata, ret == 0 ? 0 : 1, NULL);
    machine_maintence_state_set(0);
    free(arg);

    if(ret == -1) {
        FAULT_CHECK_DEAL(FAULT_COMMON, MODULE_FAULT_LEVEL1 | MODULE_FAULT_LEVEL2 | MODULE_FAULT_STOP_ALL, NULL);
    } else if (ret == 0 && module_fault_stat_get() != MODULE_FAULT_NONE) {
        ret = -1;
    } else if (ret == 0) {
        remain_detect_done();
    }
}

int thrift_liquid_detect_start(needle_type_t type)
{
    int ret = 0;
    long start = 0;
    liquid_detect_arg_t arg = {0};

    if (type == NEEDLE_TYPE_R2) {
        arg.needle = NEEDLE_TYPE_R2;
        arg.reag_idx = POS_REAGENT_TABLE_I1;
    } else if (type == NEEDLE_TYPE_S_R1) {
        /* 探测R1试剂 */
        arg.needle = NEEDLE_TYPE_S_R1;
        arg.reag_idx = POS_REAGENT_TABLE_I1;
    } else if (type == NEEDLE_TYPE_S_DILU) {
        /* 探测稀释液 */
        arg.needle = NEEDLE_TYPE_S_DILU;
        arg.reag_idx = POS_REAGENT_DILU_1;
    } else if (type == NEEDLE_TYPE_S_BOTH) {
        /* 探测适配器样本 */
        arg.needle = NEEDLE_TYPE_S;
        arg.reag_idx = 1;
        arg.tube = STANARD_CUP;
    } else {
        arg.needle = NEEDLE_TYPE_S;
        arg.reag_idx = 1;
        arg.tube = PP_1_8;
    }
    arg.mode = DEBUG_DETECT_MODE;
    start = sys_uptime_usec();
    ret = liquid_detect_start(arg);
    DETECT_LOG("%s detect pos = %d, cost(%d).\n", needle_type_string(type), arg.reag_idx, sys_uptime_usec() - start);
    slip_liquid_detect_rcd_set(arg.needle, ATTR_DISABLE);

    return (ret < EMAX ? -1 : ret);
}

int liquid_detect_connect_check(needle_type_t needle)
{
    if (slip_liquid_detect_type_set(needle) < 0) {
        DETECT_LOG("%s connect failed.\n", needle_type_string(needle));
        return -1;
    }
    return 0;
}

int module_liquid_detect_init(void)
{
    liquid_detect_thr_show();

    liquid_detect_remain_list_init();

    if (liquid_detect_rcd_list_init() < 0) {
        DETECT_LOG("module init failed.\n");
        return -1;
    }

    return 0;
}

