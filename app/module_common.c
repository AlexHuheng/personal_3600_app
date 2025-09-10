#include <stddef.h>
#include <log.h>
#include <common.h>
#include <timer.h>
#include <sys/time.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>

#include "module_common.h"
#include "movement_config.h"

static leave_param_t leave_param[LEAVE_MAX]; /* 信号量集合 */

/* 0:未占用， 1：已占用 */
static unsigned char pos_used_flag[POS_MAX] = {0};

int sys_uptime_sec(void)
{
    struct timespec ts = {0};

    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        goto out;
    }

    return ts.tv_sec;

out:
    return 0;
}

long sys_uptime_usec(void)
{
    struct timespec ts = {0};

    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        goto out;
    }

    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000 / 1000);

out:
    return 0;
}


const char *log_get_time()
{
    static char cur_system_time[64] = {0};
    struct timeval tv = {0};
    struct tm tm = {0};

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    snprintf(cur_system_time, sizeof(cur_system_time), "%02d-%02d %02d:%02d:%02d.%.03ld",
        tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec / 1000);

    return cur_system_time;
}

/* 毫秒 */
long long get_time()
{
    struct timeval tv = {0};
	
    gettimeofday(&tv,NULL);
    return ((long long)tv.tv_sec * 1000 + (long long)tv.tv_usec / 1000);
}

/* 微秒 */
long long get_time_us()
{
    struct timeval tv = {0};
	
    gettimeofday(&tv,NULL);
    return ((long long)tv.tv_sec * 1000000 + (long long)tv.tv_usec);
}

int module_sync_time(struct timeval *base_tv, int sync_time_ms)
{
    struct timeval now_tv = {0}, wait_tv = {0};

    gettimeofday(&now_tv, NULL);
    TIMER_MX_USEC_SUB(sync_time_ms, &now_tv, base_tv, &wait_tv);
//    LOG("base time %d s, %d us.\n", base_tv->tv_sec, base_tv->tv_usec);
//    LOG("now time %d s, %d us.\n", now_tv.tv_sec, now_tv.tv_usec);
//    LOG("wait time %d s, %d us.\n", wait_tv.tv_sec, wait_tv.tv_usec);
    return select(0, NULL, NULL, NULL, &wait_tv);
}

int utf8togb2312(const char *sourcebuf, size_t sourcelen, char *destbuf, size_t destlen)
{
    iconv_t cd;
    if ((cd = iconv_open("gb2312","utf-8")) == 0) {
        LOG("open fail\n");
        return -1;
    }

    memset(destbuf,0,destlen);
    const char **source = &sourcebuf;
    char **dest = &destbuf;
    if ((size_t)-1 == iconv(cd, (char**)(source), &sourcelen, dest, &destlen)) {
        LOG("iconv fail\n");
        iconv_close(cd);
        return -1;
    }

    iconv_close(cd);

    return 0;
}

void load_default_conf_from_common(h3600_conf_t *h3600_conf)
{
    /* 样本针S 位置点 */
    h3600_conf->motor[MOTOR_NEEDLE_S_X].acc = 164800;
    h3600_conf->motor[MOTOR_NEEDLE_S_X].speed = 32960;
    h3600_conf->motor[MOTOR_NEEDLE_S_Y].acc = 190400;
    h3600_conf->motor[MOTOR_NEEDLE_S_Y].speed = 38080;
    h3600_conf->motor[MOTOR_NEEDLE_S_Z].acc = 208000;
    h3600_conf->motor[MOTOR_NEEDLE_S_Z].speed = 41600;
    h3600_conf->motor[MOTOR_NEEDLE_S_PUMP].acc = 320000;
    h3600_conf->motor[MOTOR_NEEDLE_S_PUMP].speed = 32000;

    h3600_conf->motor_pos_cnt[MOTOR_NEEDLE_S_X] = 12;
    h3600_conf->motor_pos_cnt[MOTOR_NEEDLE_S_Y] = 12;
    h3600_conf->motor_pos_cnt[MOTOR_NEEDLE_S_Z] = 12;
    /* 常规取样位1 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_1] = 0;
    /* 常规取样位10 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_10] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_10] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_10] = 0;
    /* 常规取样位60 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_60] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_60] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_60] = 0;
    /* 常规加样位 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_PRE] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_PRE] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_PRE] = 0;
    /* 混匀加样位 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_MIX1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_MIX1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_MIX1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_MIX2] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_MIX2] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_MIX2] = 0;
    /* 试剂仓吸样位 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_REAGENT_TABLE_IN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_REAGENT_TABLE_IN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_REAGENT_TABLE_IN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_REAGENT_TABLE_OUT] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_REAGENT_TABLE_OUT] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_REAGENT_TABLE_OUT] = 0;
    /* 洗针位 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_CLEAN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_CLEAN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_CLEAN] = 0;
    /* 暂存位 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_TEMP] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_TEMP] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_TEMP] = 0;
    /* 取稀释液位1 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_DILU_1] = 0;
    /* 取稀释液位2 */
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_2] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_2] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_DILU_2] = 0;

    /* 试剂针R2 位置点 */
    h3600_conf->motor[MOTOR_NEEDLE_R2_Y].acc = 190400;
    h3600_conf->motor[MOTOR_NEEDLE_R2_Z].acc = 188800;
    h3600_conf->motor[MOTOR_NEEDLE_R2_PUMP].acc = 320000;

    h3600_conf->motor[MOTOR_NEEDLE_R2_Y].speed = 38080;
    h3600_conf->motor[MOTOR_NEEDLE_R2_Z].speed = 37760;
    h3600_conf->motor[MOTOR_NEEDLE_R2_PUMP].speed = 32000;

    h3600_conf->motor_pos_cnt[MOTOR_NEEDLE_R2_Y] = 6;
    h3600_conf->motor_pos_cnt[MOTOR_NEEDLE_R2_Z] = 6;

    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_IN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_OUT] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MIX_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MAG_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MAG_4] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN] = 0;

    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_REAGENT_IN] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_REAGENT_OUT] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MIX_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MAG_1] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MAG_4] = 0;
    h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN] = 0;

    /* 抓手 位置点 */
    h3600_conf->motor[MOTOR_CATCHER_X].acc = 128000;
    h3600_conf->motor[MOTOR_CATCHER_Y].acc = 72000;
    h3600_conf->motor[MOTOR_CATCHER_Z].acc = 265600;

    h3600_conf->motor[MOTOR_CATCHER_X].speed = 25600;
    h3600_conf->motor[MOTOR_CATCHER_Y].speed = 14400;
    h3600_conf->motor[MOTOR_CATCHER_Z].speed = 26560;

    h3600_conf->motor_pos_cnt[MOTOR_CATCHER_X] = 13;
    h3600_conf->motor_pos_cnt[MOTOR_CATCHER_Y] = 13;
    h3600_conf->motor_pos_cnt[MOTOR_CATCHER_Z] = 13;

    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_PRE] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MIX_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MIX_2] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_INCUBATION1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_INCUBATION10] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_INCUBATION30] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MAG_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MAG_4] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_OPTICAL_MIX] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_OPTICAL_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_OPTICAL_8] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_DETACH] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_CUVETTE] = 0;

    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_PRE] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MIX_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MIX_2] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_INCUBATION1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_INCUBATION10] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_INCUBATION30] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MAG_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MAG_4] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_OPTICAL_MIX] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_OPTICAL_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_OPTICAL_8] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_DETACH] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_CUVETTE] = 0;

    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_PRE] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MIX_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MIX_2] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_INCUBATION1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_INCUBATION10] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_INCUBATION30] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MAG_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MAG_4] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_OPTICAL_MIX] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_OPTICAL_1] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_OPTICAL_8] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_DETACH] = 0;
    h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_CUVETTE] = 0;

    /* 混匀电机 */
    h3600_conf->motor[MOTOR_MIX_1].acc = 13056;
    h3600_conf->motor[MOTOR_MIX_1].speed = 13056;

    h3600_conf->motor[MOTOR_MIX_2].acc = 13056;
    h3600_conf->motor[MOTOR_MIX_2].speed = 13056;

    h3600_conf->motor[MOTOR_MIX_3].acc = 13056;
    h3600_conf->motor[MOTOR_MIX_3].speed = 13056;

    h3600_conf->motor_pos_cnt[MOTOR_MIX_1] = 1;
    h3600_conf->motor_pos[MOTOR_MIX_1][0] = 10000;

    h3600_conf->motor_pos_cnt[MOTOR_MIX_2] = 1;
    h3600_conf->motor_pos[MOTOR_MIX_2][0] = 10000;

    h3600_conf->motor_pos_cnt[MOTOR_MIX_3] = 1;
    h3600_conf->motor_pos[MOTOR_MIX_3][0] = 10000;

    /* 试剂仓 */
    h3600_conf->motor[MOTOR_REAGENT_TABLE].acc = 20000;
    h3600_conf->motor[MOTOR_REAGENT_TABLE].speed = 32000;

    h3600_conf->motor_pos_cnt[MOTOR_REAGENT_TABLE] = 7;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN] = 9800;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT] = 10800;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN] = -4000;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT] = -4200;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_IN] = 3166;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_OUT] = 2499;
    h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_SCAN] = -8850;

    h3600_conf->liquid_amount[NEEDLE_TYPE_S].k_ratio = 128.91;
    h3600_conf->liquid_amount[NEEDLE_TYPE_S].b_ratio = 69.8;

    h3600_conf->liquid_amount[NEEDLE_TYPE_S_R1].k_ratio = 128.28;
    h3600_conf->liquid_amount[NEEDLE_TYPE_S_R1].b_ratio = -161.4;

    h3600_conf->liquid_amount[NEEDLE_TYPE_R2].k_ratio = 131.3;
    h3600_conf->liquid_amount[NEEDLE_TYPE_R2].b_ratio = 202.32;

    h3600_conf->liquid_amount[NEEDLE_TYPE_S_DILU].k_ratio = 128.0;
    h3600_conf->liquid_amount[NEEDLE_TYPE_S_DILU].b_ratio = 0.1;

    h3600_conf->liquid_amount[NEEDLE_TYPE_S_BOTH].k_ratio = 128.0;
    h3600_conf->liquid_amount[NEEDLE_TYPE_S_BOTH].b_ratio = 0.1;

    h3600_conf->pierce_support = 1;
    h3600_conf->throughput_mode = 0;
    h3600_conf->pierce_enable = 1;
    h3600_conf->clot_check_switch = 1;
    h3600_conf->straight_release = 0;

    h3600_conf->optical_curr_data[0] = 0x70;
    h3600_conf->optical_curr_data[1] = 0x56;
    h3600_conf->optical_curr_data[2] = 0x54;
    h3600_conf->optical_curr_data[3] = 0x83;
    h3600_conf->optical_curr_data[4] = 0x3f;

    h3600_conf->fan_pwm_duty[0] = 60;
    h3600_conf->fan_pwm_duty[1] = 60;
    h3600_conf->fan_pwm_duty[2] = 20;
    h3600_conf->fan_pwm_duty[3] = 20;

    h3600_conf->r0_liq_true_value[0] = 5;
    h3600_conf->r0_liq_true_value[1] = 10;
    h3600_conf->r0_liq_true_value[2] = 15;
    h3600_conf->r0_liq_true_value[3] = 25;
    h3600_conf->r0_liq_true_value[4] = 50;
    h3600_conf->r0_liq_true_value[5] = 100;
    h3600_conf->r0_liq_true_value[6] = 150;
    h3600_conf->r0_liq_true_value[7] = 200;

    h3600_conf->r2_liq_true_value[0] = 20;
    h3600_conf->r2_liq_true_value[1] = 50;
    h3600_conf->r2_liq_true_value[2] = 100;
    h3600_conf->r2_liq_true_value[3] = 150;
    h3600_conf->r2_liq_true_value[4] = 200;

    h3600_conf->r3_liq_true_value[0] = 5;
    h3600_conf->r3_liq_true_value[1] = 10;
    h3600_conf->r3_liq_true_value[2] = 15;
    h3600_conf->r3_liq_true_value[3] = 25;
    h3600_conf->r3_liq_true_value[4] = 50;

    h3600_conf->r4_liq_true_value[0] = 15;
    h3600_conf->r4_liq_true_value[1] = 20;
    h3600_conf->r4_liq_true_value[2] = 50;
    h3600_conf->r4_liq_true_value[3] = 100;
    h3600_conf->r4_liq_true_value[4] = 150;
    h3600_conf->r4_liq_true_value[5] = 200;

    h3600_conf->s_threshold[0] = 8000;
    h3600_conf->s_threshold[1] = 8000;
    h3600_conf->s_threshold[2] = 6000;
    h3600_conf->s_threshold[3] = 8000;
    h3600_conf->s_threshold[4] = 2300;
    h3600_conf->s_threshold[5] = 8000;
    h3600_conf->s_threshold[6] = 2300;

    h3600_conf->r2_threshold[0] = 8000;
    h3600_conf->r2_threshold[1] = 8000;
    h3600_conf->r2_threshold[2] = 8000;
    h3600_conf->r2_threshold[3] = 8000;
    h3600_conf->r2_threshold[4] = 8000;

    memset(h3600_conf->thrift_master_server.ip, 0, sizeof(h3600_conf->thrift_master_server.ip));
    strncpy(h3600_conf->thrift_master_server.ip, "192.168.33.100", strlen("192.168.33.100"));
    h3600_conf->thrift_master_server.port = 5000;

    memset(h3600_conf->thrift_slave_server.ip, 0, sizeof(h3600_conf->thrift_slave_server.ip));
    strncpy(h3600_conf->thrift_slave_server.ip, "192.168.33.200", strlen("192.168.33.100"));
    h3600_conf->thrift_slave_server.port = 6000;

}

static pos_t pos_record_catcher_pre[1] =
{
    [POS_0] = {350, 4820, 5070},
};

static pos_t pos_record_catcher_mix[2] =
{
    [POS_0] = {10, 380, 1330},
    [POS_1] = {10, 3170, 1330},
};

static pos_t pos_record_catcher_incubation[30] =
{
    [POS_0] = {4540, 310, 1110},
    [POS_1] = {4540, 310+((4070-310)/9)*1, 1110},
    [POS_2] = {4540, 310+((4070-310)/9)*2, 1110},
    [POS_3] = {4540, 310+((4070-310)/9)*3, 1110},
    [POS_4] = {4540, 310+((4070-310)/9)*4, 1110},
    [POS_5] = {4540, 310+((4070-310)/9)*5, 1110},
    [POS_6] = {4540, 310+((4070-310)/9)*6, 1110},
    [POS_7] = {4540, 310+((4070-310)/9)*7, 1110},
    [POS_8] = {4540, 310+((4070-310)/9)*8, 1110},
    [POS_9] = {4540, 310+((4070-310)/9)*9, 1110},
    [POS_10] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*0, 1110},
    [POS_11] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*1, 1110},
    [POS_12] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*2, 1110},
    [POS_13] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*3, 1110},
    [POS_14] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*4, 1110},
    [POS_15] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*5, 1110},
    [POS_16] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*6, 1110},
    [POS_17] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*7, 1110},
    [POS_18] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*8, 1110},
    [POS_19] = {4540+((6990-4540)/2)*1, 310+((4070-310)/9)*9, 1110},
    [POS_20] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*0, 1110},
    [POS_21] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*1, 1110},
    [POS_22] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*2, 1110},
    [POS_23] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*3, 1110},
    [POS_24] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*4, 1110},
    [POS_25] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*5, 1110},
    [POS_26] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*6, 1110},
    [POS_27] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*7, 1110},
    [POS_28] = {4540+((6990-4540)/2)*2, 310+((4070-310)/9)*8, 1110},
    [POS_29] = {6990, 4070, 1110},
};

static pos_t pos_record_catcher_magnetic[4] =
{
    [POS_0] = {6730, 360, 1070},
    [POS_1] = {6730+((6760-6730)/3)*1, 360+((4410-360)/3)*1, 1070+((1100-1070)/3)*1},
    [POS_2] = {6730+((6760-6730)/3)*2, 360+((4410-360)/3)*2, 1070+((1100-1070)/3)*2},
    [POS_3] = {6760, 4410, 1100},
};

static pos_t pos_record_catcher_optical[8] =
{
    [POS_0] = {7770, -110, 1050},
    [POS_1] = {7775, 571, 1050},
    [POS_2] = {7780, 1252, 1050},
    [POS_3] = {7785, 1933, 1050},
    [POS_4] = {7790, 2614, 1050},
    [POS_5] = {7795, 3295, 1050},
    [POS_6] = {7800, 3976, 1050},
    [POS_7] = {7810, 4660, 1050},
};

static pos_t pos_record_catcher_optical_mix[1] =
{
    [POS_0] = {10, 380, 1330},
};

static pos_t pos_record_catcher_detach[1] =
{
    [POS_0] = {10, 380, 1330},
};

static pos_t pos_record_catcher_newcup[1] =
{
    [POS_0] = {11530, 6710, 1150},
};

static pos_t pos_record_s_sample[60] =
{
    [POS_0] = {9400, 6910, 5000},   /* 常规采样位1-60 */
    [POS_1] = {9050, 6910, 5000},
    [POS_2] = {10050, 7890, 4000},
    [POS_3] = {10050, 7890, 4000},
    [POS_4] = {10050, 7890, 4000},
    [POS_5] = {10050, 10890, 4000},
    [POS_6] = {10050, 10890, 4000},

    [POS_10] = {9400, 6910, 5000},
    [POS_11] = {9400, 6910, 5000},
    [POS_12] = {9400, 6910, 5000},
    [POS_13] = {9400, 6910, 5000},
    [POS_14] = {9400, 6910, 5000},
    [POS_15] = {9400, 6910, 5000},
    [POS_16] = {9400, 6910, 5000},
    [POS_17] = {9400, 6910, 5000},
    [POS_18] = {9400, 6910, 5000},
    [POS_19] = {9400, 6910, 5000},
};

static pos_t pos_record_s_add_cup_pre[1] =
{
    [POS_0] = {570, 60, 2150},
};

pos_t pos_record_s_add_cup_mix[2] =
{
    [POS_0] = {570, 60, 2150},
    [POS_1] = {570, 60, 2150},
};

static pos_t pos_record_s_reagent_table[2] =
{
    [POS_0] = {3920, 1350, 2630},
    [POS_1] = {3920, 1350, 2630},
};

static pos_t pos_record_s_clean[1] =
{
    [POS_0] = {600, 840, 5150},
};

static pos_t pos_record_s_temp[1] =
{
    [POS_0] = {600, 840, 5150},
};

static pos_t pos_record_s_dilu[3] =
{
    [POS_0] = {1500, 2400, 7000},
    [POS_1] = {1500, 2400, 7000},
    [POS_2] = {1500, 2400, 7000},
};

static pos_t pos_record_r2_reagent_table[2] =
{
    [POS_0] = {0, 5950, 4450},  /* 外圈 */
    [POS_1] = {0, 4700, 4450},  /* 内圈 */
};

static pos_t pos_record_r2_mix[1] =
{
    [POS_0] = {0, 6720, 750},
};

static pos_t pos_record_r2_magnetic[4] =
{
    [POS_0] = {0, 6750, 500},
    [POS_1] = {0, 6750-((6750-3670)/3)*1, 500},
    [POS_2] = {0, 6750-((6750-3670)/3)*2, 500},
    [POS_3] = {0, 3670, 500},
};

static pos_t pos_record_r2_clean[1] =
{
    [POS_0] = {0, -80, 1830},
};

static pos_t pos_record_reagent_table_for_r2[2] =
{
    [POS_0] = {2500, 0, 0},  /* 外圈 */
    [POS_1] = {2500, 0, 0},  /* 内圈 */
};

static pos_t pos_record_reagent_table_for_s[2] =
{
    [POS_0] = {2500, 0, 0},  /* 外圈 */
    [POS_1] = {2500, 0, 0},  /* 内圈 */
};

static pos_t pos_record_reagent_table_for_mix[2] =
{
    [POS_0] = {2500, 0, 0}, /* 混匀内圈 */
    [POS_1] = {2500, 0, 0}, /* 混匀外圈 */
};

static pos_t pos_record_reagent_table_for_scan[1] =
{
    [POS_0] = {2500, 0, 0},
};

void pos_record_reag_table_for_r2_set(int which, int new_pos)
{
    LOG("auto_cal: force set r2_table-%d = %d.\n", which, new_pos);
    pos_record_reagent_table_for_r2[which].x = new_pos;
}

/*
flag:
    0：初始化条件变量 和 清零标志
    1：仅 清零标志
*/
void leave_singal_init(int flag)
{
    int i = 0;

    for (i=0; i<sizeof(leave_param)/sizeof(leave_param[0]); i++) {
        if (flag == 0) {
            pthread_mutex_init(&leave_param[i].mutex_leave, NULL);
            pthread_cond_init(&leave_param[i].cond_leave, NULL);
        }
    }
}

void leave_singal_one_clear(enum_leave_index_t index)
{
   leave_param[index].leave_flag = 0;
}

void leave_singal_wait(enum_leave_index_t index)
{
    pthread_mutex_lock(&leave_param[index].mutex_leave);
    if (leave_param[index].leave_flag == 0) {
//        LOG("[%s] lock: %d\n", __func__, index);
        pthread_cond_wait(&leave_param[index].cond_leave, &leave_param[index].mutex_leave);
//        LOG("[%s] unlock: %d\n", __func__, index);
    }
    leave_param[index].leave_flag = 0;
    pthread_mutex_unlock(&leave_param[index].mutex_leave);
}

int leave_singal_timeout_wait(enum_leave_index_t index, int timeout)
{
    struct timeval now = {0};
    struct timespec outtime = {0};
    int ret = 0;

    //LOG("[%s] lock: %d\n", __func__, index);
    pthread_mutex_lock(&leave_param[index].mutex_leave);
    if (leave_param[index].leave_flag == 0) {
//        LOG("[%s] lock: %d\n", __func__, index);
        gettimeofday(&now, NULL);
        outtime.tv_sec = now.tv_sec + timeout/1000;
        outtime.tv_nsec = now.tv_usec * 1000 + (timeout%1000)*1000000;

        if (pthread_cond_timedwait(&leave_param[index].cond_leave, &leave_param[index].mutex_leave, &outtime) == ETIMEDOUT) {
            ret = 1;
        }
//        LOG("[%s] unlock: %d\n", __func__, index);
    }
    leave_param[index].leave_flag = 0;
    pthread_mutex_unlock(&leave_param[index].mutex_leave);

    return ret;
}

void leave_singal_send(enum_leave_index_t index)
{
//    LOG("[%s] send leave singal: %d\n", __func__, index);
    pthread_mutex_lock(&leave_param[index].mutex_leave);
    pthread_cond_signal(&leave_param[index].cond_leave);
    leave_param[index].leave_flag = 1;
    pthread_mutex_unlock(&leave_param[index].mutex_leave);
}

void set_pos(pos_t* pos, int x, int y, int z)
{
    pos->x = x;
    pos->y = y;
    pos->z = z;
}

void cup_pos_used_clear(void)
{
    int i = 0;

    for (i=0; i<POS_MAX; i++) {
        if (i>=POS_MAGNECTIC_WORK_1 && i<=POS_MAGNECTIC_WORK_4) {
            if (thrift_mag_pos_disable_get(i-POS_MAGNECTIC_WORK_1) == 1) {
                pos_used_flag[i] = 1;
            } else {
                pos_used_flag[i] = 0;
            }
        } else if (i>=POS_OPTICAL_WORK_1 && i<=POS_OPTICAL_WORK_8) {
            if (thrift_optical_pos_disable_get(i-POS_OPTICAL_WORK_1) == 1) {
                pos_used_flag[i] = 1;
            } else {
                pos_used_flag[i] = 0;
            }
        } else {
            pos_used_flag[i] = 0;
        }
    }
}

/*  获取一个 可用杯子工位坐标，并占用 杯子工位的占用状态 */
cup_pos_t get_valid_pos(move_pos_t sub_type, pos_t *pos, int lock_flag)
{
    cup_pos_t new_cup_pos = POS_INVALID;
    int i = 0;
    pos_t* pos_record = NULL;
    int pos_record_len = 0;

    switch (sub_type) {
        /* 抓手位置 */
        case MOVE_C_MIX:
            pos_record = pos_record_catcher_mix;
            pos_record_len = sizeof(pos_record_catcher_mix)/sizeof(pos_record_catcher_mix[0]);
            for (i=0; i<pos_record_len; i++) {
                if (pos_used_flag[POS_PRE_PROCESSOR_MIX1+i] == 0) {
                    memcpy(pos, &(pos_record[i]), sizeof(pos_t));
                    break;
                }
            }

            if (i < pos_record_len) {
                new_cup_pos = POS_PRE_PROCESSOR_MIX1+i;
            } else {
                new_cup_pos = POS_INVALID;
            }
            break;
        case MOVE_C_INCUBATION:
            pos_record = pos_record_catcher_incubation;
            pos_record_len = sizeof(pos_record_catcher_incubation)/sizeof(pos_record_catcher_incubation[0]);
            for (i=0; i<pos_record_len; i++) {
                if (pos_used_flag[POS_INCUBATION_WORK_1+i] == 0) {
                    memcpy(pos, &(pos_record[i]), sizeof(pos_t));
                    break;
                }
            }

            if (i < pos_record_len) {
                new_cup_pos = POS_INCUBATION_WORK_1+i;
            } else {
                new_cup_pos = POS_INVALID;
            }
            break;
        case MOVE_C_MAGNETIC:
            pos_record = pos_record_catcher_magnetic;
            pos_record_len = sizeof(pos_record_catcher_magnetic)/sizeof(pos_record_catcher_magnetic[0]);
            for (i=0; i<pos_record_len; i++) {
                if (pos_used_flag[POS_MAGNECTIC_WORK_1+i] == 0) {
                    LOG("i=%d, pos x=%d, y=%d\n", i, pos_record[i].x, pos_record[i].y);
                    memcpy(pos, &(pos_record[i]), sizeof(pos_t));
                    break;
                }
            }

            if (i < pos_record_len) {
                new_cup_pos = POS_MAGNECTIC_WORK_1+i;
            } else {
                new_cup_pos = POS_INVALID;
            }
            break;
        case MOVE_C_OPTICAL:
            pos_record = pos_record_catcher_optical;
            pos_record_len = sizeof(pos_record_catcher_optical)/sizeof(pos_record_catcher_optical[0]);
            for (i=0; i<pos_record_len; i++) {
                if (pos_used_flag[POS_OPTICAL_WORK_1+i] == 0) {
                    memcpy(pos, &(pos_record[i]), sizeof(pos_t));
                    break;
                }
            }

            if (i < pos_record_len) {
                new_cup_pos = POS_OPTICAL_WORK_1+i;
            } else {
                new_cup_pos = POS_INVALID;
            }
            break;
        default:
            LOG("[%s] no sub type: %d\n", __func__, sub_type);
            break;
    }

    if (new_cup_pos != POS_INVALID) {
        if (lock_flag == FLAG_POS_LOCK) {
            pos_used_flag[new_cup_pos] = 1;
        } else if(lock_flag == FLAG_POS_UNLOCK) {
            pos_used_flag[new_cup_pos] = 0;
        }
    }

    return new_cup_pos;
}

/*  仅获取特定的一个 杯子工位坐标，并释放 杯子工位的占用状态 */
cup_pos_t get_special_pos(move_pos_t sub_type, cup_pos_t index, pos_t *pos, int lock_flag)
{
    pos_t* pos_record = NULL;
    cup_pos_t new_cup_pos = POS_INVALID;

    switch (sub_type) {
        /* C1位置 */
        case MOVE_C_PRE:
            pos_record = pos_record_catcher_pre;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_PRE_PROCESSOR;
            break;
        case MOVE_C_MIX:
            pos_record = pos_record_catcher_mix;
            if (index >= POS_PRE_PROCESSOR_MIX1 && index <=POS_PRE_PROCESSOR_MIX2) {
                memcpy(pos, &(pos_record[index-POS_PRE_PROCESSOR_MIX1]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_C_INCUBATION:
            pos_record = pos_record_catcher_incubation;
            if (index >= POS_INCUBATION_WORK_1 && index <=POS_INCUBATION_WORK_30) {
                memcpy(pos, &(pos_record[index-POS_INCUBATION_WORK_1]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_C_DETACH:
            pos_record = pos_record_catcher_detach;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_REACT_CUP_DETACH;
            break;
        case MOVE_C_OPTICAL_MIX:
            pos_record = pos_record_catcher_optical_mix;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = index;
            break;
        case MOVE_C_MAGNETIC:
            pos_record = pos_record_catcher_magnetic;
            if (index >= POS_MAGNECTIC_WORK_1 && index <=POS_MAGNECTIC_WORK_4) {
                memcpy(pos, &(pos_record[index-POS_MAGNECTIC_WORK_1]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_C_OPTICAL:
            pos_record = pos_record_catcher_optical;
            if (index >= POS_OPTICAL_WORK_1 && index <=POS_OPTICAL_WORK_8) {
                if (pos_used_flag[index] == 0 || lock_flag == FLAG_POS_UNLOCK) {
                    memcpy(pos, &(pos_record[index-POS_OPTICAL_WORK_1]), sizeof(pos_t));
                    new_cup_pos = index;
                } else {
                    new_cup_pos = POS_INVALID;
                }
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_C_NEW_CUP:
            pos_record = pos_record_catcher_newcup;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_CUVETTE_SUPPLY_INIT;
            break;

        /* R2 位置*/
        case MOVE_R2_REAGENT:
            pos_record = pos_record_r2_reagent_table;
            if (index >= POS_REAGENT_TABLE_R2_IN && index <= POS_REAGENT_TABLE_R2_OUT) {
                memcpy(pos, &(pos_record[index-POS_REAGENT_TABLE_R2_IN]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_R2_MIX:
            pos_record = pos_record_r2_mix;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_OPTICAL_MIX;
            break;
        case MOVE_R2_MAGNETIC:
            pos_record = pos_record_r2_magnetic;
            if (index >= POS_MAGNECTIC_WORK_1 && index <=POS_MAGNECTIC_WORK_4) {
                memcpy(pos, &(pos_record[index-POS_MAGNECTIC_WORK_1]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_R2_CLEAN:
            pos_record = pos_record_r2_clean;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_INVALID;
            break;
        case MOVE_S_SAMPLE_NOR:
            pos_record = pos_record_s_sample;
            memcpy(pos, &(pos_record[index]), sizeof(pos_t));
            new_cup_pos = POS_INVALID;
            break;
        case MOVE_S_ADD_REAGENT:
            pos_record = pos_record_s_reagent_table;
            if (index >= POS_REAGENT_TABLE_S_IN && index <=POS_REAGENT_TABLE_S_OUT) {
                memcpy(pos, &(pos_record[index-POS_REAGENT_TABLE_S_IN]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_S_CLEAN:
            pos_record = pos_record_s_clean;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_INVALID;
            break;
        case MOVE_S_TEMP:
            pos_record = pos_record_s_temp;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_INVALID;
            break;
        case MOVE_S_ADD_CUP_PRE:
            pos_record = pos_record_s_add_cup_pre;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            new_cup_pos = POS_INVALID;
            break;
        case MOVE_S_DILU:
            pos_record = pos_record_s_dilu;
            if (index >= 0 && index <= 1) {
                memcpy(pos, &(pos_record[index]), sizeof(pos_t));
            }
            new_cup_pos = POS_INVALID;
            break;
        case MOVE_S_ADD_CUP_MIX:
            pos_record = pos_record_s_add_cup_mix;
            if (index >= POS_PRE_PROCESSOR_MIX1 && index <= POS_PRE_PROCESSOR_MIX2) {
                memcpy(pos, &(pos_record[index-POS_PRE_PROCESSOR_MIX1]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_REAGENT_TABLE_FOR_S:
            pos_record = pos_record_reagent_table_for_s;
            if (index >= POS_REAGENT_TABLE_S_IN && index <= POS_REAGENT_TABLE_S_OUT) {
                memcpy(pos, &(pos_record[index-POS_REAGENT_TABLE_S_IN]), sizeof(pos_t));
                new_cup_pos = index;
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_REAGENT_TABLE_FOR_R2:
            pos_record = pos_record_reagent_table_for_r2;
            if (index >= POS_REAGENT_TABLE_R2_IN && index <= POS_REAGENT_TABLE_R2_OUT) {
                memcpy(pos, &(pos_record[index-POS_REAGENT_TABLE_R2_IN]), sizeof(pos_t));
            } else {
                pos->x = 0;
                pos->y = 0;
                pos->z = 0;
            }
            break;
        case MOVE_REAGENT_TABLE_FOR_MIX:
            pos_record = pos_record_reagent_table_for_mix;
            if (index >= POS_REAGENT_TABLE_MIX_IN && index <= POS_REAGENT_TABLE_MIX_OUT) {
                memcpy(pos, &(pos_record[index-POS_REAGENT_TABLE_MIX_IN]), sizeof(pos_t));
            }
            break;
        case MOVE_REAGENT_TABLE_FOR_SCAN:
            pos_record = pos_record_reagent_table_for_scan;
            memcpy(pos, &(pos_record[0]), sizeof(pos_t));
            break;
        default:
            LOG("[%s] no sub type: %d\n", __func__, sub_type);
            break;
    }

    if (new_cup_pos != POS_INVALID) {
        if (lock_flag == FLAG_POS_LOCK) {
            pos_used_flag[new_cup_pos] = 1;
        } else if (lock_flag == FLAG_POS_UNLOCK) {
            pos_used_flag[new_cup_pos] = 0;
        }
    }

    return new_cup_pos;
}

static void show_record_pos(char *record_name, pos_t *record_pos, int len)
{
    int i = 0;
    LOG("============%s==len = %d=======\n", record_name, len);
    for (i=0; i<len; i++) {
        LOG("    %d,    %d,    %d\n", record_pos[i].x, record_pos[i].y, record_pos[i].z);
    }
    LOG("============================================\n");
}

static void pos_catcher_init(const h3600_conf_t *h3600_conf)
{
    int i = 0;
    int delta_x = 0, delta_y = 0, delta_z = 0;

    pos_record_catcher_pre[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_PRE];
    pos_record_catcher_pre[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_PRE];
    pos_record_catcher_pre[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_PRE];

    pos_record_catcher_mix[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MIX_1];
    pos_record_catcher_mix[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MIX_1];
    pos_record_catcher_mix[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MIX_1];
    pos_record_catcher_mix[POS_1].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MIX_2];
    pos_record_catcher_mix[POS_1].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MIX_2];
    pos_record_catcher_mix[POS_1].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MIX_2];

    pos_record_catcher_incubation[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_INCUBATION1];
    pos_record_catcher_incubation[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_INCUBATION1];
    pos_record_catcher_incubation[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_INCUBATION1];
    pos_record_catcher_incubation[POS_9].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_INCUBATION10];
    pos_record_catcher_incubation[POS_9].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_INCUBATION10];
    pos_record_catcher_incubation[POS_9].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_INCUBATION10];
    pos_record_catcher_incubation[POS_29].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_INCUBATION30];
    pos_record_catcher_incubation[POS_29].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_INCUBATION30];
    pos_record_catcher_incubation[POS_29].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_INCUBATION30];
    delta_x = (float)(pos_record_catcher_incubation[POS_29].x-pos_record_catcher_incubation[POS_0].x)/2 + 0.5;
    delta_y = (float)(pos_record_catcher_incubation[POS_29].y-pos_record_catcher_incubation[POS_0].y)/9 + 0.5;
    delta_z = (float)(pos_record_catcher_incubation[POS_29].z-pos_record_catcher_incubation[POS_0].z)/9 + 0.5;
    for (i=POS_1; i<=POS_28; i++) {
        pos_record_catcher_incubation[i].x = pos_record_catcher_incubation[POS_0].x + delta_x*(i/10);
        pos_record_catcher_incubation[i].y = pos_record_catcher_incubation[POS_0].y + delta_y*(i%10);
        pos_record_catcher_incubation[i].z = pos_record_catcher_incubation[POS_0].z + delta_z*(i%10);
    }

    pos_record_catcher_magnetic[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MAG_1];
    pos_record_catcher_magnetic[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MAG_1];
    pos_record_catcher_magnetic[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MAG_1];
    pos_record_catcher_magnetic[POS_3].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MAG_4];
    pos_record_catcher_magnetic[POS_3].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MAG_4];
    pos_record_catcher_magnetic[POS_3].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MAG_4];
    delta_x = (float)(pos_record_catcher_magnetic[POS_3].x-pos_record_catcher_magnetic[POS_0].x)/3 + 0.5;
    delta_y = (float)(pos_record_catcher_magnetic[POS_3].y-pos_record_catcher_magnetic[POS_0].y)/3 + 0.5;
    delta_z = (float)(pos_record_catcher_magnetic[POS_3].z-pos_record_catcher_magnetic[POS_0].z)/3 + 0.5;
    for (i=POS_1; i<=POS_2; i++) {
        pos_record_catcher_magnetic[i].x = pos_record_catcher_magnetic[POS_0].x + delta_x*i;
        pos_record_catcher_magnetic[i].y = pos_record_catcher_magnetic[POS_0].y + delta_y*i;
        pos_record_catcher_magnetic[i].z = pos_record_catcher_magnetic[POS_0].z + delta_z*i;
    }

    pos_record_catcher_optical_mix[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_OPTICAL_MIX];
    pos_record_catcher_optical_mix[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_OPTICAL_MIX];
    pos_record_catcher_optical_mix[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_OPTICAL_MIX];

    pos_record_catcher_optical[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_OPTICAL_1];
    pos_record_catcher_optical[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_OPTICAL_1];
    pos_record_catcher_optical[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_OPTICAL_1];
    pos_record_catcher_optical[POS_7].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_OPTICAL_8];
    pos_record_catcher_optical[POS_7].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_OPTICAL_8];
    pos_record_catcher_optical[POS_7].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_OPTICAL_8];
    for (i=POS_1; i<= POS_6; i++) {
        pos_record_catcher_optical[i].x = pos_record_catcher_optical[POS_0].x+(pos_record_catcher_optical[POS_7].x-pos_record_catcher_optical[POS_0].x)*(i-POS_0)/(POS_7-POS_0);
        pos_record_catcher_optical[i].y = pos_record_catcher_optical[POS_0].y+(pos_record_catcher_optical[POS_7].y-pos_record_catcher_optical[POS_0].y)*(i-POS_0)/(POS_7-POS_0);
        pos_record_catcher_optical[i].z = pos_record_catcher_optical[POS_0].z+(pos_record_catcher_optical[POS_7].z-pos_record_catcher_optical[POS_0].z)*(i-POS_0)/(POS_7-POS_0);
    }

    pos_record_catcher_detach[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_MAG_1];
    pos_record_catcher_detach[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_MAG_1] - 2800;
    pos_record_catcher_detach[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_MAG_1] - 100;

    pos_record_catcher_newcup[POS_0].x = h3600_conf->motor_pos[MOTOR_CATCHER_X][H3600_CONF_POS_C_CUVETTE];
    pos_record_catcher_newcup[POS_0].y = h3600_conf->motor_pos[MOTOR_CATCHER_Y][H3600_CONF_POS_C_CUVETTE];
    pos_record_catcher_newcup[POS_0].z = h3600_conf->motor_pos[MOTOR_CATCHER_Z][H3600_CONF_POS_C_CUVETTE];
}

void catcher_record_pos_reinit(const h3600_conf_t *h3600_conf)
{
    pos_catcher_init(h3600_conf);
    LOG("catcher pos reinit!\n");
}

static void pos_s_init(const h3600_conf_t *h3600_conf)
{
    int i = 0;
    int delta_x = 0, delta_y = 0, delta_z = 0;
    /* 由于机械方面的原因，XY需要对调 */
    pos_record_s_sample[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_1];
    pos_record_s_sample[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_1];
    pos_record_s_sample[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_1];

    pos_record_s_sample[POS_9].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_10];
    pos_record_s_sample[POS_9].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_10];
    pos_record_s_sample[POS_9].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_10];

    pos_record_s_sample[POS_59].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_NOR_60];
    pos_record_s_sample[POS_59].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_NOR_60];
    pos_record_s_sample[POS_59].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_NOR_60];
    delta_x = (float)(pos_record_s_sample[POS_59].x-pos_record_s_sample[POS_0].x)/9 + 0.5;
    delta_y = (float)(pos_record_s_sample[POS_59].y-pos_record_s_sample[POS_0].y)/5 + 0.5;
    delta_z = (float)(pos_record_s_sample[POS_59].z-pos_record_s_sample[POS_0].z)/9 + 0.5;
    for (i=POS_1; i<=POS_58; i++) {
        pos_record_s_sample[i].x = pos_record_s_sample[POS_0].x + delta_x*(i%10);
        pos_record_s_sample[i].y = pos_record_s_sample[POS_0].y + delta_y*(i/10);
        pos_record_s_sample[i].z = pos_record_s_sample[POS_0].z + delta_z*(i%10);
    }

    pos_record_s_add_cup_pre[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_PRE];
    pos_record_s_add_cup_pre[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_PRE];
    pos_record_s_add_cup_pre[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_PRE];
    pos_record_s_add_cup_mix[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_MIX1];
    pos_record_s_add_cup_mix[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_MIX1];
    pos_record_s_add_cup_mix[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_MIX1];
    pos_record_s_add_cup_mix[POS_1].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_SAMPLE_ADD_MIX2];
    pos_record_s_add_cup_mix[POS_1].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_SAMPLE_ADD_MIX2];
    pos_record_s_add_cup_mix[POS_1].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_SAMPLE_ADD_MIX2];

    pos_record_s_reagent_table[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_REAGENT_TABLE_IN];
    pos_record_s_reagent_table[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_REAGENT_TABLE_IN];
    pos_record_s_reagent_table[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_REAGENT_TABLE_IN];
    pos_record_s_reagent_table[POS_1].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_REAGENT_TABLE_OUT];
    pos_record_s_reagent_table[POS_1].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_REAGENT_TABLE_OUT];
    pos_record_s_reagent_table[POS_1].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_REAGENT_TABLE_OUT];
    
    pos_record_s_clean[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_TEMP];
    pos_record_s_clean[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_TEMP] + 640;
    pos_record_s_clean[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_TEMP];
    pos_record_s_temp[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_TEMP];
    pos_record_s_temp[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_TEMP];
    pos_record_s_temp[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_TEMP];

    pos_record_s_dilu[POS_0].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_1];
    pos_record_s_dilu[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_1];
    pos_record_s_dilu[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_DILU_1];
    pos_record_s_dilu[POS_1].x = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Y][H3600_CONF_POS_S_DILU_2];
    pos_record_s_dilu[POS_1].y = h3600_conf->motor_pos[MOTOR_NEEDLE_S_X][H3600_CONF_POS_S_DILU_2];
    pos_record_s_dilu[POS_1].z = h3600_conf->motor_pos[MOTOR_NEEDLE_S_Z][H3600_CONF_POS_S_DILU_2];

}

static void pos_r2_init(const h3600_conf_t *h3600_conf)
{
    int i = 0;
    int delta_x = 0, delta_y = 0, delta_z = 0;

    pos_record_r2_reagent_table[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_IN];
    pos_record_r2_reagent_table[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_REAGENT_IN];
    pos_record_r2_reagent_table[POS_1].y = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_REAGENT_OUT];
    pos_record_r2_reagent_table[POS_1].z = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_REAGENT_OUT];

    pos_record_r2_mix[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MIX_1];
    pos_record_r2_mix[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MIX_1];

    pos_record_r2_magnetic[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MAG_1];
    pos_record_r2_magnetic[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MAG_1];
    pos_record_r2_magnetic[POS_3].y = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_MAG_4];
    pos_record_r2_magnetic[POS_3].z = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_MAG_4];
    delta_x = (float)(pos_record_r2_magnetic[POS_3].x-pos_record_r2_magnetic[POS_0].x)/3 + 0.5;
    delta_y = (float)(pos_record_r2_magnetic[POS_3].y-pos_record_r2_magnetic[POS_0].y)/3 + 0.5;
    delta_z = (float)(pos_record_r2_magnetic[POS_3].z-pos_record_r2_magnetic[POS_0].z)/3 + 0.5;
    for (i=POS_1; i<=POS_2; i++) {
        pos_record_r2_magnetic[i].x = pos_record_r2_magnetic[POS_0].x + delta_x*i;
        pos_record_r2_magnetic[i].y = pos_record_r2_magnetic[POS_0].y + delta_y*i;
        pos_record_r2_magnetic[i].z = pos_record_r2_magnetic[POS_0].z + delta_z*i;
    }

    pos_record_r2_clean[POS_0].y = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Y][H3600_CONF_POS_R2_CLEAN];
    pos_record_r2_clean[POS_0].z = h3600_conf->motor_pos[MOTOR_NEEDLE_R2_Z][H3600_CONF_POS_R2_CLEAN];
}

static void pos_reagent_table_init(const h3600_conf_t *h3600_conf)
{
    pos_record_reagent_table_for_r2[POS_0].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_IN];
    pos_record_reagent_table_for_r2[POS_1].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_R2_OUT];

    pos_record_reagent_table_for_s[POS_0].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_IN];
    pos_record_reagent_table_for_s[POS_1].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_S_OUT];

    pos_record_reagent_table_for_mix[POS_0].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_IN];
    pos_record_reagent_table_for_mix[POS_1].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_MIX_OUT];
    pos_record_reagent_table_for_scan[POS_0].x = h3600_conf->motor_pos[MOTOR_REAGENT_TABLE][H3600_CONF_POS_REAGENT_TABLE_FOR_SCAN];
}

void pos_all_init(const h3600_conf_t* h3600_conf, int show_flag)
{
    LOG("show_flag: %d\n", show_flag);

    pos_catcher_init(h3600_conf);
    pos_s_init(h3600_conf);
    pos_r2_init(h3600_conf);
    pos_reagent_table_init(h3600_conf);

    if (show_flag == 1) {
        show_record_pos("needle S sample", pos_record_s_sample, sizeof(pos_record_s_sample)/sizeof(pos_t));
        show_record_pos("needle S add cup", pos_record_s_add_cup_pre, sizeof(pos_record_s_add_cup_pre)/sizeof(pos_t));
        show_record_pos("needle S add hil", pos_record_s_add_cup_mix, sizeof(pos_record_s_add_cup_mix)/sizeof(pos_t));
        show_record_pos("needle S clean", pos_record_s_clean, sizeof(pos_record_s_clean)/sizeof(pos_t));
        show_record_pos("needle S temp", pos_record_s_temp, sizeof(pos_record_s_temp)/sizeof(pos_t));
        show_record_pos("needle S dilu", pos_record_s_dilu, sizeof(pos_record_s_dilu)/sizeof(pos_t));
        show_record_pos("needle S reagent table", pos_record_s_reagent_table, sizeof(pos_record_s_reagent_table)/sizeof(pos_t));

        show_record_pos("needle R2 reagent", pos_record_r2_reagent_table, sizeof(pos_record_r2_reagent_table)/sizeof(pos_t));
        show_record_pos("needle R2 mix", pos_record_r2_mix, sizeof(pos_record_r2_mix)/sizeof(pos_t));
        show_record_pos("needle R2 mag", pos_record_r2_magnetic, sizeof(pos_record_r2_magnetic)/sizeof(pos_t));
        show_record_pos("needle R2 clean", pos_record_r2_clean, sizeof(pos_record_r2_clean)/sizeof(pos_t));

        show_record_pos("catcher mix", pos_record_catcher_mix, sizeof(pos_record_catcher_mix)/sizeof(pos_t));
        show_record_pos("catcher incubation", pos_record_catcher_incubation, sizeof(pos_record_catcher_incubation)/sizeof(pos_t));
        show_record_pos("catcher optical mix", pos_record_catcher_optical_mix, sizeof(pos_record_catcher_optical_mix)/sizeof(pos_t));
        show_record_pos("catcher pre", pos_record_catcher_pre, sizeof(pos_record_catcher_pre)/sizeof(pos_t));
        show_record_pos("catcher detach", pos_record_catcher_detach, sizeof(pos_record_catcher_detach)/sizeof(pos_t));
        show_record_pos("catcher mag", pos_record_catcher_magnetic, sizeof(pos_record_catcher_magnetic)/sizeof(pos_t));
        show_record_pos("catcher optical", pos_record_catcher_optical, sizeof(pos_record_catcher_optical)/sizeof(pos_t));
        show_record_pos("catcher new cup", pos_record_catcher_newcup, sizeof(pos_record_catcher_newcup)/sizeof(pos_t));

        show_record_pos("reagent table for r2", pos_record_reagent_table_for_r2, sizeof(pos_record_reagent_table_for_r2)/sizeof(pos_t));
        show_record_pos("reagent table for s", pos_record_reagent_table_for_s, sizeof(pos_record_reagent_table_for_s)/sizeof(pos_t));
        show_record_pos("reagent table for scan", pos_record_reagent_table_for_scan, sizeof(pos_record_reagent_table_for_scan)/sizeof(pos_t));
        show_record_pos("reagent table for mix", pos_record_reagent_table_for_mix, sizeof(pos_record_reagent_table_for_mix)/sizeof(pos_t));
    }
}

static time_fragment_t catcher_time_frag_table_pt360[] =
{
	/* 加样位/MIX1/MIX2→孵育 0-2500 */
    [FRAG0] = {0,    0.0,  0}, /* XY→ */
    [FRAG1] = {0,    0.0,  0}, /* Z↓ */
    [FRAG2] = {0,    0.0,  0}, /* 抓 */
    [FRAG3] = {0,    0.0,  0}, /* Z↑ */
    [FRAG4] = {0,    0.0,  0}, /* XY→ */
    [FRAG5] = {0,    0.0,  0}, /* Z↓ */
    [FRAG6] = {0,    0.0,  0}, /* 放 */
    [FRAG7] = {0,    0.0,  0}, /* Z↑ */
    [FRAG8] = {0,    0.0,  0}, /* 时间片 */

    /* 进杯 2500-5000 */
    [FRAG9] =  {0,   0.0,  0}, /* XY复位 */
    [FRAG10] = {0,   0.0,  0}, /* XY→ */
    [FRAG11] = {0,   0.0,  0}, /* Z↓ */
    [FRAG12] = {0,   0.0,  0}, /* 抓 */
    [FRAG13] = {0,   0.0,  0}, /* Z↑ */
    [FRAG14] = {0,   0.0,  0}, /* XY→ */
    [FRAG15] = {0,   0.0,  0}, /* Z↓ */
    [FRAG16] = {0,   0.0,  0}, /* 放 */
    [FRAG17] = {0,   0.0,  4300}, /* Z↑ */

    /* 丢杯 5000-7500 */
    [FRAG18] = {0,   0.0,  0}, /* XY→ */
    [FRAG19] = {0,   0.0,  0}, /* Z↓ */
    [FRAG20] = {0,   0.0,  0}, /* 抓 */
    [FRAG21] = {0,   0.0,  0}, /* Z↑ */
    [FRAG22] = {0,   0.0,  0}, /* XY→ */
    [FRAG23] = {0,   0.0,  0}, /* Z↓ */
    [FRAG24] = {0,   0.0,  0}, /* 放 */
    [FRAG25] = {0,   0.0,  0}, /* Z↑ */

    /* 孵育→MIX3/磁珠检测 7500-10000 */
    [FRAG26] = {0,   0.0,  0}, /* XY→ */
    [FRAG27] = {0,   0.0,  0}, /* Z↓ */
    [FRAG28] = {0,   0.0,  0}, /* 抓 */
    [FRAG29] = {0,   0.0,  0}, /* Z↑ */
    [FRAG30] = {0,   0.0,  0}, /* XY→ */
    [FRAG31] = {0,   0.0,  0}, /* Z↓ */
    [FRAG32] = {0,   0.0,  0}, /* 放 */
    [FRAG33] = {0,   0.0,  0}, /* Z↑ */
    [FRAG34] = {0,   0.0,  0},/* XY回原点 */

    /* 占位 */
    [FRAG35] = {0,  0.0,  0},
    [FRAG36] = {0,  0.0,  0},
    [FRAG37] = {0,  0.0,  0},
    [FRAG38] = {0,  0.0,  0},
    [FRAG39] = {0,  0.0,  0},
    [FRAG40] = {0,  0.0,  0},
    [FRAG41] = {0,  0.0,  0},
    [FRAG42] = {0,  0.0,  0},
    [FRAG43] = {0,  0.0,  0},

    /* 占位 */
    [FRAG44] = {0,  0.0,  0},
    [FRAG45] = {0,  0.0,  0},
    [FRAG46] = {0,  0.0,  0},
    [FRAG47] = {0,  0.0,  0},
    [FRAG48] = {0,  0.0,  0},
    [FRAG49] = {0,  0.0,  0},
    [FRAG50] = {0,  0.0,  0},
    [FRAG51] = {0,  0.0,  0},
    [FRAG52] = {0,  0.0,  0},
};

static time_fragment_t catcher_time_frag_table[] =
{
    /* 加样位/MIX1/MIX2→孵育 0-4000 */
    [FRAG0] = {0,       0.40-0.02,  400},  /* XY→ */
    [FRAG1] = {400,     0.30-0.02,  700}, /* Z↓ */
    [FRAG2] = {700,     0.30-0.02,  1000}, /* 抓 */
    [FRAG3] = {1000,    0.30-0.02,  1300}, /* Z↑ */
    [FRAG4] = {1300,    0.40-0.02,  1700}, /* XY→ */
    [FRAG5] = {1700,    0.30-0.02,  2000}, /* Z↓ */
    [FRAG6] = {2000,    0.30-0.02,  2300}, /* 放 */
    [FRAG7] = {2300,    0.30-0.02,  2600}, /* Z↑ */
    [FRAG8] = {2600,    0.0,        4100}, /* 磁珠抓手混匀 */

    /* 进杯 4000-8000 */
    [FRAG9] =  {4100,   0.50-0.02,  4600}, /* XY复位 */
    [FRAG10] = {4600,   0.40-0.02,  5000}, /* XY→ */
    [FRAG11] = {5000,   0.40-0.02,  5400}, /* Z↓ */
    [FRAG12] = {5400,   0.30-0.02,  5700}, /* 抓 */
    [FRAG13] = {5700,   0.40-0.02,  6100}, /* Z↑ */
    [FRAG14] = {6100,   0.60-0.02,  6700}, /* XY→ */
    [FRAG15] = {6700,   0.40-0.02,  7100}, /* Z↓ */
    [FRAG16] = {7100,   0.30-0.02,  7400}, /* 放 */
    [FRAG17] = {7400,   0.40-0.02,  7800}, /* Z↑ */

    /* 丢杯 7800-12500 */
    [FRAG18] = {7800,   0.70-0.02,  8500}, /* XY→ */
    [FRAG19] = {8500,   0.40-0.02,  8900}, /* Z↓ */
    [FRAG20] = {8900,   0.30-0.02,  9200}, /* 抓 */
    [FRAG21] = {9200,   0.40-0.02,  9600}, /* Z↑ */
    [FRAG22] = {9600,   0.70-0.02,  10300}, /* XY→ */
    [FRAG23] = {10300,  0.50-0.02,  10800}, /* Z↓ */
    [FRAG24] = {10800,  0.70-0.02,  11500}, /* 放 */
    [FRAG25] = {11500,  0.50-0.02,  12500}, /* Z↑ */

    /* MIX3→光学检测 12500-16000 */
    [FRAG26] = {12500,  0.50-0.02,  13000}, /* XY→ */
    [FRAG27] = {13000,  0.30-0.02,  13400}, /* Z↓ */
    [FRAG28] = {13400,  0.20-0.02,  13700}, /* 抓 */
    [FRAG29] = {13700,  0.30-0.02,  14100}, /* Z↑ */
    [FRAG30] = {14100,  0.50-0.02,  14600}, /* XY→ */
    [FRAG31] = {14600,  0.30-0.02,  15000}, /* Z↓ */
    [FRAG32] = {15000,  0.30-0.02,  15300}, /* 放 */
    [FRAG33] = {15300,  0.30-0.02,  15700}, /* Z↑ */
    [FRAG34] = {15700,  0.00,       15700}, /* 时间片 */

    /* 孵育→MIX3/磁珠检测 16000-20000 */
    [FRAG35] = {15700,  0.40-0.02,  16100}, /* XY→ */
    [FRAG36] = {16100,  0.30-0.02,  16400}, /* Z↓ */
    [FRAG37] = {16400,  0.20-0.02,  16600}, /* 抓 */
    [FRAG38] = {16600,  0.30-0.02,  16900}, /* Z↑ */
    [FRAG39] = {16900,  0.50-0.02,  17400}, /* XY→ */
    [FRAG40] = {17400,  0.30-0.02,  17700}, /* Z↓ */
    [FRAG41] = {17700,  0.20-0.02,  17900}, /* 放 */
    [FRAG42] = {17900,  0.30-0.02,  18100}, /* Z↑ */
    [FRAG43] = {18100,  0.50-0.02,  18600}, /* XY回原点 */

    /* 光学磁珠切换MIX1/MIX2→孵育 20000-24000 */
    [FRAG44] = {20000,  0.40-0.02,  20400}, /* XY→ */
    [FRAG45] = {20400,  0.40-0.02,  20800}, /* Z↓ */
    [FRAG46] = {20800,  0.30-0.02,  21100}, /* 抓 */
    [FRAG47] = {21100,  0.40-0.02,  21500}, /* Z↑ */
    [FRAG48] = {21500,  0.50-0.02,  22000}, /* XY→ */
    [FRAG49] = {22000,  0.50-0.02,  22500}, /* Z↓ */
    [FRAG50] = {22500,  0.30-0.02,  22800}, /* 放 */
    [FRAG51] = {22800,  0.50-0.02,  23300}, /* Z↑ */
    [FRAG52] = {23300,  0.40-0.02,  23700}, /* XY回原点 */
};

time_fragment_t *catcher_time_frag_table_get()
{
    if (get_throughput_mode() == 1) { /* PT360 */
        return catcher_time_frag_table_pt360;
    } else {
        return catcher_time_frag_table;
    }
}

static time_fragment_t r2_normal_time_frag_table_pt360[] =
{
    /* 常规模式 */
    [FRAG0] = {0,    1.50-0.10,  0},  /* 液面探测 */
    [FRAG1] = {0,    0.60-0.05,  0}, /* 吸样 */
    [FRAG2] = {0,    0.40-0.05,  0}, /* Z复位 */
    [FRAG3] = {0,    0.0,        0}, /* 加热 */
    [FRAG4] = {0,    0.30-0.05,  0}, /* Y加样位 */
    [FRAG5] = {0,    0.30-0.05,  0}, /* Z加样位 */
    [FRAG6] = {0,    0.60-0.05,  0}, /* 吐样 */
    [FRAG7] = {0,    0.30-0.05,  0}, /* Z复位 */
    [FRAG8] = {0,    0.30-0.05,  0}, /* Y清洗池 */
    [FRAG9] = {0,    0.40-0.05,  0}, /* Z清洗池 */
    [FRAG10] ={0,    2.00-0.05,  0}, /* 洗针 */
    [FRAG11] ={0,    0.40-0.05,  0}, /* Z复位 */
    [FRAG12] ={0,    0.60-0.05,  0}, /* Y复位 */
    [FRAG13] ={0,    0.60-0.05,  0}, /* Y到原点 */
    [FRAG14] ={0,    0.60-0.05,  0}, /* Y→ */
};

static time_fragment_t r2_normal_time_frag_table[] =
{
    [FRAG0] = {0,       1.20-0.05, 1200}, /* 液面探测 */
    [FRAG1] = {1200,    1.30-0.05, 2500}, /* 吸样 */
    [FRAG2] = {2500,    0.55-0.10, 3050}, /* Z复位 */
    [FRAG3] = {3050,    1.35-0.02, 4400}, /* 开启加热 */
    [FRAG4] = {4400,    0.60-0.05, 5000}, /* Y加样位 */
    [FRAG5] = {5000,    0.40-0.05, 5400}, /* Z加样位 */
    [FRAG6] = {5400,    1.40-0.20, 6800}, /* 吐样 */
    [FRAG7] = {6800,    0.50-0.05, 7300}, /* Z复位 */
    [FRAG8] = {7300,    0.40-0.05, 7700}, /* Y清洗池 */
    [FRAG9] = {7700,    0.50-0.05, 8200}, /* Z清洗池 */
    [FRAG10] ={8200,    4.20-0.05, 12400}, /* 洗针 */
    [FRAG11] ={12400,   0.40-0.05, 12800}, /* Z复位 */
    [FRAG12] ={12800,   0.60-0.05, 13400}, /* Y复位 */
    [FRAG13] ={13400,   0.60-0.05, 14000}, /* Y到原点 */
    [FRAG14] ={14000,   2.00-0.05, 16000}, /* 等待 */
    [FRAG15] ={16000,   0.50-0.05, 16500}, /* Y→ */
};

time_fragment_t *r2_normal_time_frag_table_get()
{
    if (get_throughput_mode() == 1) { /* PT360 */
        return r2_normal_time_frag_table_pt360;
    } else {
        return r2_normal_time_frag_table;
    }
}

static time_fragment_t s_normal_time_frag_table_pt360[] =
{
    /* 常规模式 */
    [FRAG0] = {0,       2.00-0.05, 2000},   /* 洗针 */
    [FRAG1] = {2000,    0.50-0.05, 2500},   /* XY→ */
    [FRAG2] = {2500,    1.50-0.05, 4000},   /* 液面探测 */
    [FRAG3] = {4000,    1.00-0.05, 5000},   /* 吸样 */
    [FRAG4] = {5000,    0.50-0.05, 5500},   /* Z复位 */
    [FRAG5] = {5500,    0.60-0.05, 6100},   /* XY加样位 */
    [FRAG6] = {6100,    0.60-0.05, 6700},   /* Z加样位 */
    [FRAG7] = {6700,    1.00-0.05, 7700},   /* 吐样 */
    [FRAG8] = {7700,    0.60-0.05, 8300},   /* Z复位 */
    [FRAG9] = {8300,    0.40-0.05, 8700},   /* XY复位 */
    [FRAG10] = {8700,   0.40-0.05, 9100},   /* XY清洗池 */
    [FRAG11] = {9100,   0.40-0.05, 9500},   /* Z清洗池 */
};

static time_fragment_t s_normal_time_frag_table[] =
{
    /* 常规模式 */
    [FRAG0] = {0,       4.00-0.05, 4000},   /* 上周期洗针or等待 */
    [FRAG1] = {4000,    0.60-0.05, 4600},   /* XY→ */
    [FRAG2] = {4600,    1.50-0.05, 6100},   /* 液面探测 */
    [FRAG3] = {6100,    1.20-0.05, 7300},   /* 吸样 */
    [FRAG4] = {7300,    0.70-0.05, 8400},   /* Z复位 */
    [FRAG5] = {8400,    0.40-0.05, 8800},   /* XY加样位 */
    [FRAG6] = {8800,    0.50-0.05, 9300},   /* Z加样位 */
    [FRAG7] = {9300,    1.00-0.05, 10300},   /* 吐样 */
    [FRAG8] = {10300,    0.60-0.05, 10900},  /* Z复位 */
    [FRAG9] = {10900,   0.40-0.05, 11300},  /* XY复位 */
    [FRAG10] = {11300,  0.40-0.05, 11700},  /* XY清洗池 */
    [FRAG11] = {11700,  0.40-0.05, 12100},  /* Z清洗池 */
    [FRAG12] = {12100,  4.00-0.05, 16100},  /* 洗针 */
    [FRAG13] = {16100,  0.40-0.05, 16500},  /* Z复位 */
};

static time_fragment_t s_r1_normal_time_frag_table[] =
{
    /* 常规模式 */
    [FRAG0] = {0,       4.00-0.05, 4000},   /* 洗针 */
    [FRAG1] = {4000,    0.70-0.02, 4700},   /* XY→ */
    [FRAG2] = {4700,    1.50-0.05, 6200},   /* 液面探测 */
    [FRAG3] = {6200,    1.20-0.05, 7400},   /* 吸R1 */
    [FRAG4] = {7400,    0.70-0.02, 8100},   /* Z复位 */
    [FRAG5] = {8100,    0.50-0.12, 8600},   /* XY加样位 */
    [FRAG6] = {8600,    0.40-0.02, 9000},   /* Z加样位 */
    [FRAG7] = {9000,    1.00-0.05, 10000},  /* 吐R1 */
    [FRAG8] = {10000,   0.40-0.05, 10400},  /* Z复位 */
    [FRAG9] = {10400,   2.00-0.05, 12400},  /* 洗针 */
    [FRAG10] = {12400,  0.50-0.05, 12900},  /* XY取样位 */
    [FRAG11] = {12900,  1.50-0.05, 14400},  /* 液面探测 */
    [FRAG12] = {14400,  1.00-0.05, 15400},  /* 吸样 */
    [FRAG13] = {15400,  0.40-0.02, 15800},  /* Z复位 */
    [FRAG14] = {15800,  0.50-0.02, 16300},  /* XY加样位 */
    [FRAG15] = {16300,  0.40-0.05, 16700},  /* Z加样位 */
    [FRAG16] = {16700,  1.00-0.02, 17700},  /* 吐样 */
    [FRAG17] = {17700,  0.40-0.02, 18100},  /* Z复位 */
    [FRAG18] = {18100,  0.30-0.02, 18400},  /* XY复位 */
    [FRAG19] = {18400,  0.20-0.02, 18600},  /* XY清洗池 */
    [FRAG20] = {18600,  0.40-0.05, 19000},  /* Z清洗池 */
};

static time_fragment_t s_r1_only_time_frag_table[] =
{
    /* 单加R1 */
    [FRAG0] = {0,       4.00-0.05, 4000},   /* 上周期洗针or等待 */
    [FRAG1] = {4000,    0.70-0.02, 4700},   /* XY→ */
    [FRAG2] = {4700,    1.50-0.05, 6200},   /* 液面探测 */
    [FRAG3] = {6200,    1.20-0.05, 7400},   /* 吸样 */
    [FRAG4] = {7400,    0.60-0.02, 8400},   /* Z复位 */
    [FRAG5] = {8400,    0.40-0.05, 8800},   /* XY加样位 */
    [FRAG6] = {8800,    0.50-0.05, 9300},   /* Z加样位 */
    [FRAG7] = {9300,    1.00-0.05, 10300},   /* 吐样 */
    [FRAG8] = {10300,   0.60-0.05, 10900},  /* Z复位 */
    [FRAG9] = {10900,   0.40-0.05, 11300},  /* XY复位 */
    [FRAG10] = {11300,  0.40-0.05, 11700},  /* XY清洗池 */
    [FRAG11] = {11700,  0.40-0.05, 12100},  /* Z清洗池 */
    [FRAG12] = {12100,  4.00-0.05, 16100},  /* 洗针 */
    [FRAG13] = {16100,  0.40-0.05, 16500},  /* Z复位 */
};


static time_fragment_t s_dilu_time_frag_table[] =
{
    /* 单次稀释模式 */
    [FRAG0] = {0,       4.00-0.02, 4000},   /* 上周期洗针or等待 */
    [FRAG1] = {4000,    0.40-0.05, 4400},   /* XY→ */
    [FRAG2] = {4400,    1.50-0.02, 5900},   /* 液面探测 */
    [FRAG3] = {5900,    1.00-0.02, 6900},   /* 吸稀释液 */
    [FRAG4] = {6900,    0.60-0.02, 7500},   /* Z复位 */
    [FRAG5] = {7500,    0.50-0.05, 8000},   /* XY吸样位 */
    [FRAG6] = {8000,    1.50-0.02, 9500},   /* 液面探测 */
    [FRAG7] = {9500,    1.00-0.02, 10500},  /* 吸样 */
    [FRAG8] = {10500,   0.60-0.02, 11100},  /* Z复位 */
    [FRAG9] = {11100,   0.60-0.02, 11700},  /* XY加样位 */
    [FRAG10] = {11700,  0.50-0.02, 12200},  /* Z加样位 */
    [FRAG11] = {12200,  1.00-0.02, 13200},  /* 吐样 */
    [FRAG12] = {13200,  0.60-0.02, 13800},  /* Z复位 */
    [FRAG13] = {13800,  0.40-0.02, 14200},  /* XY复位 */
    [FRAG14] = {14200,  0.40-0.02, 14600},  /* XY清洗池 */
    [FRAG15] = {14600,  0.40-0.02, 15000},  /* Z清洗池 */
    [FRAG16] = {15000,  4.00-0.05, 19000},  /* 洗针 */
    [FRAG17] = {19000,  0.60-0.05, 19600},  /* Z复位 */
};

static time_fragment_t s_dilu_r1_normal_time_frag_table[] =
{
    /* 常规模式 */
    [FRAG0] = {0,       4.00-0.05, 4000},   /* 上周期洗针or等待 */
    [FRAG1] = {4000,    0.70-0.02, 4700},   /* XY→ */
    [FRAG2] = {4700,    1.50-0.05, 6200},   /* 液面探测 */
    [FRAG3] = {6200,    1.20-0.05, 7400},   /* 吸R1 */
    [FRAG4] = {7400,    0.70-0.02, 8100},   /* Z复位 */
    [FRAG5] = {8100,    0.50-0.05, 8600},   /* XY加样位 */
    [FRAG6] = {8600,    0.40-0.05, 9000},   /* Z加样位 */
    [FRAG7] = {9000,    1.00-0.05, 10000},  /* 吐R1 */
    [FRAG8] = {10000,   0.40-0.05, 10400},  /* Z复位 */
    [FRAG9] = {10400,   2.00-0.05, 12400},  /* 洗针 */
    [FRAG10] = {12400,  0.30-0.05, 12700},  /* XY稀释液位 */
    [FRAG11] = {12700,  1.50-0.05, 14200},  /* 液面探测 */
    [FRAG12] = {14200,  1.00-0.02, 15200},  /* 吸稀释液 */
    [FRAG13] = {15200,  0.40-0.02, 15600},  /* Z复位 */
    [FRAG14] = {15600,  0.30-0.05, 15900},  /* XY取样位 */
    [FRAG15] = {15900,  1.50-0.05, 17400},  /* 液面探测 */
    [FRAG16] = {17400,  1.00-0.05, 18400},  /* 吸样 */
    [FRAG17] = {18400,  0.40-0.05, 18800},  /* Z复位 */
    [FRAG18] = {18800,  0.60-0.05, 19400},  /* XY加样位 */
    [FRAG19] = {19400,  0.40-0.05, 19800},  /* Z加样位 */
    [FRAG20] = {19800,  1.00-0.02, 20800},  /* 吐样 */
    [FRAG21] = {20800,  0.40-0.05, 21200},  /* Z复位 */
    [FRAG22] = {21200,  0.30-0.05, 21500},  /* XY复位 */
    [FRAG23] = {21500,  0.30-0.05, 21800},  /* XY清洗池 */
    [FRAG24] = {21800,  0.30-0.05, 22100},  /* Z清洗池 */
};

static time_fragment_t s_p_time_frag_table_pt270[] =
{
    /* PT单穿刺通量 */
    [FRAG0] = {0,       2.00-0.05, 2000},   /* 洗针 */
    [FRAG1] = {2000,    0.50-0.05, 2500},   /* XY→ */
    [FRAG2] = {2500,    1.50-0.05, 4000},   /* 液面探测 */
    [FRAG3] = {4000,    1.00-0.05, 5000},   /* 吸样 */
    [FRAG4] = {5000,    0.50-0.05, 5500},   /* Z复位 */
    [FRAG5] = {5500,    0.60-0.05, 6100},   /* XY加样位 */
    [FRAG6] = {6100,    0.60-0.05, 6700},   /* Z加样位 */
    [FRAG7] = {6700,    1.00-0.05, 7700},   /* 吐样 */
    [FRAG8] = {7700,    0.60-0.05, 8300},   /* Z复位 */
    [FRAG9] = {8300,    0.40-0.05, 8700},   /* XY复位 */
    [FRAG10] = {8700,   0.40-0.05, 9100},   /* XY清洗池 */
    [FRAG11] = {9100,   0.40-0.05, 9500},   /* Z清洗池 */
    [FRAG12] = {0,      4.00-0.05, 0},      /* 洗针 */
    [FRAG13] = {0,      0.40-0.05, 0},      /* Z复位 */
};

static time_fragment_t s_p_time_frag_table[] =
{
    /* 穿刺转移 */
    [FRAG0] = {0,       4.00-0.05, 4000},   /* 上周期洗针or等待 */
    [FRAG1] = {4000,    0.50-0.05, 4500},   /* XY→ */
    [FRAG2] = {4500,    2.50-0.05, 7000},   /* 液面探测 */
    [FRAG3] = {7000,    2.00-0.05, 9000},   /* 吸样 */
    [FRAG4] = {9000,    1.50-0.05, 10500},  /* Z复位 */
    [FRAG5] = {10500,   0.60-0.05, 11100},  /* XY加样位 */
    [FRAG6] = {11100,   0.50-0.05, 11600},  /* Z加样位 */
    [FRAG7] = {11600,   2.00-0.05, 13600},  /* 吐样 */
    [FRAG8] = {13600,   0.50-0.05, 14100},  /* Z复位 */
    [FRAG9] = {14100,   0.40-0.05, 14500},  /* XY复位 */
    [FRAG10] = {14500,  0.40-0.05, 14900},  /* XY清洗池 */
    [FRAG11] = {14900,  0.40-0.05, 15300},  /* Z清洗池 */
    [FRAG12] = {15300,  4.00-0.05, 19300},  /* 洗针 */
    [FRAG13] = {19300,  0.40-0.05, 19700},  /* Z复位 */
};

static time_fragment_t s_dilu1_time_frag_table[] =
{
    /* 分杯稀释模式第一周期 */
    [FRAG0] = {0,       4.00-0.02, 4000},   /* 上周期洗针or等待 */
    [FRAG1] = {4000,    0.40-0.05, 4400},   /* XY→ */
    [FRAG2] = {4400,    1.50-0.02, 5900},   /* 液面探测 */
    [FRAG3] = {5900,    1.00-0.02, 6900},   /* 吸稀释液 */
    [FRAG4] = {6900,    0.60-0.02, 7500},   /* Z复位 */
    [FRAG5] = {7500,    0.50-0.05, 8000},   /* XY吸样位 */
    [FRAG6] = {8000,    1.50-0.02, 9500},   /* 液面探测 */
    [FRAG7] = {9500,    1.00-0.02, 10500},  /* 吸样 */
    [FRAG8] = {10500,   0.60-0.02, 11100},  /* Z复位 */
    [FRAG9] = {11100,   0.60-0.02, 11700},  /* XY加样位 */
    [FRAG10] = {11700,  0.50-0.02, 12200},  /* Z加样位 */
    [FRAG11] = {12200,  1.00-0.02, 13200}, /* 吐样 */
    [FRAG12] = {13200,  0.60-0.02, 13800}, /* Z复位 */
    [FRAG13] = {13800,  0.40-0.02, 14200}, /* XY复位 */
    [FRAG14] = {14200,  0.40-0.02, 14600}, /* XY清洗池 */
    [FRAG15] = {14600,  0.40-0.02, 15000}, /* Z清洗池 */
    [FRAG16] = {15000,  4.00-0.05, 19000},  /* 洗针 */
    [FRAG17] = {19000,  0.60-0.05, 19600},  /* Z复位 */
};

static time_fragment_t s_dilu2_time_frag_table[] =
{
    /* 分杯稀释模式第二周期（加R1） */
    [FRAG0] = {0,       4.00-0.05, 4000},   /* 等待 */
    [FRAG1] = {4000,    0.70-0.05, 4700},   /* XY→ */
    [FRAG2] = {4700,    1.50-0.05, 6200},   /* 液面探测 */
    [FRAG3] = {6200,    1.20-0.05, 7400},   /* 吸样 */
    [FRAG4] = {7400,    0.70-0.05, 8100},   /* Z复位 */
    [FRAG5] = {8100,    0.50-0.05, 8600},   /* XY加样位 */
    [FRAG6] = {8600,    0.50-0.05, 9100},   /* Z加样位 */
    [FRAG7] = {9100,    1.00-0.05, 10100},   /* 吐样 */
    [FRAG8] = {10100,   0.60-0.05, 10700},  /* Z复位 */
    [FRAG9] = {10700,   0.40-0.05, 11100},  /* XY复位 */
    [FRAG10] = {11100,  0.40-0.05, 11500},  /* XY清洗池 */
    [FRAG11] = {11500,  0.40-0.05, 11900},  /* Z清洗池 */
    [FRAG12] = {11900,  7.00-0.05, 18900},  /* 洗针 */
    [FRAG13] = {18900,  0.40-0.05, 19300},  /* Z复位 */
};

static time_fragment_t s_dilu3_without_dilu_time_frag_table[] =
{
    /* 分杯稀释模式第三周期（不加稀释液） */
    [FRAG0] = {0,       4.00-0.05, 4600},   /* 等待 */
    [FRAG1] = {4600,    0.60-0.05, 5200},   /* XY→ */
    [FRAG2] = {5200,    1.50-0.05, 6700},   /* Z取样位 */
    [FRAG3] = {6700,    1.20-0.05, 7900},   /* 吸样 */
    [FRAG4] = {7900,    0.70-0.05, 8600},   /* Z复位 */
    [FRAG5] = {8600,    0.40-0.05, 9000},   /* XY加样位 */
    [FRAG6] = {9000,    0.50-0.05, 9500},   /* Z加样位 */
    [FRAG7] = {9500,    1.00-0.05, 10500},   /* 吐样 */
    [FRAG8] = {10500,   0.60-0.05, 11100},  /* Z复位 */
    [FRAG9] = {11100,   0.40-0.05, 11500},  /* XY复位 */
    [FRAG10] = {11500,  0.40-0.05, 11900},  /* XY清洗池 */
    [FRAG11] = {11900,  0.40-0.05, 12300},  /* Z清洗池 */
    [FRAG12] = {12300,  4.00-0.05, 16300},  /* 洗针 */
    [FRAG13] = {16300,  0.40-0.05, 16700},  /* Z复位 */
};

static time_fragment_t s_dilu3_with_dilu_time_frag_table[] =
{
    /* 分杯稀释模式第三周期（加稀释液） */
    [FRAG0] = {0,       4.00-0.02, 4000},   /* 等待 */
    [FRAG1] = {4000,    0.40-0.05, 4400},   /* XY→ */
    [FRAG2] = {4400,    1.50-0.02, 5900},   /* 液面探测 */
    [FRAG3] = {5900,    1.00-0.02, 6900},   /* 吸稀释液 */
    [FRAG4] = {6900,    0.60-0.02, 7500},   /* Z复位 */
    [FRAG5] = {7500,    0.50-0.05, 8000},   /* XY取样位 */
    [FRAG6] = {8000,    1.50-0.02, 9500},   /* Z取样位 */
    [FRAG7] = {9500,    1.00-0.02, 10500},  /* 吸样 */
    [FRAG8] = {10500,   0.60-0.02, 11100},  /* Z复位 */
    [FRAG9] = {11100,   0.60-0.02, 11700},  /* XY加样位 */
    [FRAG10] = {11700,  0.50-0.02, 12200},  /* Z加样位 */
    [FRAG11] = {12200,  1.00-0.02, 13200}, /* 吐样 */
    [FRAG12] = {13200,  0.60-0.02, 13800}, /* Z复位 */
    [FRAG13] = {13800,  0.40-0.02, 14200}, /* XY复位 */
    [FRAG14] = {14200,  0.40-0.02, 14600}, /* XY清洗池 */
    [FRAG15] = {14600,  0.40-0.02, 15000}, /* Z清洗池 */
    [FRAG16] = {15000,  4.00-0.05, 19000},  /* 洗针 */
    [FRAG17] = {19000,  0.40-0.05, 19400},  /* Z复位 */
};

time_fragment_t *s_time_frag_table_get(needle_s_time_t st)
{
    switch (st) {
    case NEEDLE_S_NORMAL_TIME:
        if (get_throughput_mode() == 1) { /* PT360 */
            return s_normal_time_frag_table_pt360;
        } else {
            return s_normal_time_frag_table;
        }
    case NEEDLE_S_R1_TIME:
        return s_r1_normal_time_frag_table;
    case NEEDLE_S_R1_ONLY_TIME:
        return s_r1_only_time_frag_table;
    case NEEDLE_S_DILU_R1_TIME:
        return s_dilu_r1_normal_time_frag_table;
    case NEEDLE_S_DILU_TIME:
        return s_dilu_time_frag_table;
    case NEEDLE_S_DILU1_TIME:
        return s_dilu1_time_frag_table;
    case NEEDLE_S_DILU2_TIME:
        return s_dilu2_time_frag_table;
    case NEEDLE_S_DILU3_WITHOUT_DILU_TIME:
        return s_dilu3_without_dilu_time_frag_table;
    case NEEDLE_S_DILU3_WITH_DILU_TIME:
        return s_dilu3_with_dilu_time_frag_table;
    case NEEDLE_S_P_TIME:
        if (get_throughput_mode() == 1) { /* PT270 */
            return s_p_time_frag_table_pt270;
        } else {
            return s_p_time_frag_table;
        }
    default:
        break;
    }
    return NULL;
}

static time_fragment_t reagent_time_frag_table_pt360[] =
{
    /* 试剂仓时间片 */
    [FRAG0] = {0,       2.60-0.02, 2600},   /* R2占用 */
    [FRAG1] = {0,       0.00-0.00, 0},      /* 空闲 */
    [FRAG2] = {0,       0.00-0.00, 8000},   /* 空闲 */
    [FRAG3] = {8000,    2.00-0.02, 10000},  /* R2转盘 */
    [FRAG4] = {8000,    2.00-0.02, 10000},  /* R2转盘 */
};

static time_fragment_t reagent_time_frag_table[] =
{
    /* 试剂仓时间片 */
    [FRAG0] = {0,       3.10-0.02, 3100},   /* R2占用 */
    [FRAG1] = {3100,    1.40-0.05, 4500},   /* R1转盘 */
    [FRAG2] = {4500,    13.50-0.02,18000},  /* R1占用 */
    [FRAG3] = {18100,   1.40-0.02, 19500},  /* R2转盘(常规) */
    [FRAG4] = {19500,   1.40-0.02, 20900},  /* R2转盘(稀释R1试剂仓质控周期(FDP试剂仓质控周期)) */
};

time_fragment_t *reagent_time_frag_table_get(void)
{
    if (get_throughput_mode() == 1) { /* PT360 */
        return reagent_time_frag_table_pt360;
    } else {
        return reagent_time_frag_table;
    }

    return NULL;
}

