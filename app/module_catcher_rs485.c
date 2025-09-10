#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include "lowlevel.h"
#include "crc.h"
#include "log.h"
#include "module_catcher_rs485.h"
#include "module_common.h"
#include "movement_config.h"
#include "device_status_count.h"
#include "module_monitor.h"

static int catcher_uart_fd = 0;
static int cather_init_flag = 0;

static void catch_encoder(uint8_t *inbuf, uint8_t *outbuf, int len)
{
    uint16_t crc16_checkout = 0;
    int i;
    
    crc16_checkout = uCRC_Compute(uCRC16_MODBUS, inbuf, 6);
    crc16_checkout = uCRC_ComputeComplete(uCRC16_MODBUS, crc16_checkout);
    crc16_checkout = htons(crc16_checkout);
    for (i=0; i<len; i++) {
        outbuf[i] = inbuf[i];
    }
    outbuf[len++] = (crc16_checkout >> 8) & 0xff;
    outbuf[len++] = (crc16_checkout >> 0) & 0xff;
}

static void show_catch_recv_buf(uint8_t *buff)
{
    LOG("recv = 0x%02X%02X%02X%02X%02X%02X%02X%02X\n",
        buff[0], buff[1], buff[2], buff[3],
        buff[4], buff[5], buff[6], buff[7]);
}

static int ll_catcher_read(int fd, unsigned char *buff, int size)
{
    int res = 0;
    ssize_t bytes_read;
    struct timeval timeout;
    fd_set read_fds;

    /* 设置超时时间为80毫秒 */
    timeout.tv_sec = 0;
    timeout.tv_usec = 80*1000;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    /* 使用select等待fd变为可读，或直到超时 */
    switch (select(fd + 1, &read_fds, NULL, NULL, &timeout)) {
    case 0:
        /* 超时发生 */
        LOG("Timeout occurred before data was available to read.\n");
        res = 1;
        break;
    case -1:
        /* 发生错误 */
        LOG("select error!\n");
        res = -1;
        break;
    default:
        /* 数据可读，读取数据 */
        if (FD_ISSET(fd, &read_fds)) {
            bytes_read = read(fd, buff, size);
            if (bytes_read > 0) {
                buff[bytes_read] = '\0'; /* 确保字符串以null结尾 */
            } else if (bytes_read == -1) {
                LOG("read error!\n");
                res = -1;
            }
        }
        break;
    }
    return res;
}

int catcher_rs485_init(void)
{
    uint8_t catch_send_buff[32] = {0};
    uint8_t catch_recv_buff[32] = {0};
    uint8_t catch_init[6] = {0x01, 0x06, 0x01, 0x00, 0x00, 0x01};
    uint8_t catch_curr[6] = {0x01, 0x06, 0x01, 0x03, 0x00, 0x1e};
    uint8_t catch_speed[6] = {0x01, 0x06, 0x01, 0x04, 0x00, 0x64};
    uint8_t catch_init_stat[6] = {0x01, 0x03, 0x02, 0x00, 0x00, 0x01};
    uint8_t run_stat = 0;

    if (catcher_uart_fd == 0 || catcher_uart_fd == -1) {
        catcher_uart_fd = ll_uart_open("/dev/ttyS7", 115200, 0, 8, 1, 'N');
        if (catcher_uart_fd == -1) {
            LOG("open uart failed!\n");
            return 1;
        }
    }
    catch_encoder(catch_curr, catch_send_buff, 6);
    ll_send(catcher_uart_fd, catch_send_buff, 8);
    if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
        ll_close(catcher_uart_fd);
        catcher_uart_fd = 0;
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
        return 1;
    }
    show_catch_recv_buf(catch_recv_buff);
    catch_encoder(catch_speed, catch_send_buff, 6);
    ll_send(catcher_uart_fd, catch_send_buff, 8);
    if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
        ll_close(catcher_uart_fd);
        catcher_uart_fd = 0;
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
        return 1;
    }
    show_catch_recv_buf(catch_recv_buff);
    catch_encoder(catch_init, catch_send_buff, 6);
    ll_send(catcher_uart_fd, catch_send_buff, 8);
    if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
        ll_close(catcher_uart_fd);
        catcher_uart_fd = 0;
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
        return 1;
    }
    show_catch_recv_buf(catch_recv_buff);
    run_stat = 0;
    while (!run_stat) {
        catch_encoder(catch_init_stat, catch_send_buff, 6);
        ll_send(catcher_uart_fd, catch_send_buff, 8);
        if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
            ll_close(catcher_uart_fd);
            catcher_uart_fd = 0;
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
            return 2;
        }
        if (catch_recv_buff[4] == 0x01) {
            cather_init_flag = 1;
            return 0;
        } else if (catch_recv_buff[4] == 0x02) {
            return 1;
        }
    }

    return 0;
}

int catcher_ctl(catcher_ctl_t ctl)
{
    uint8_t catch_send_buff[32] = {0};
    uint8_t catch_recv_buff[32] = {0};
    uint8_t catch_curr[6] = {0x01, 0x06, 0x01, 0x03, 0x00, 0x1e};
    uint8_t catch_close[6] = {0x01, 0x06, 0x01, 0x05, 0x00, CATCHER_CLOSE_CTL};
    uint8_t catch_open[6] = {0x01, 0x06, 0x01, 0x05, 0x00, CATCHER_OPEN_CTL};
    uint8_t catch_run_stat[6] = {0x01, 0x03, 0x02, 0x02, 0x00, 0x01};
    uint8_t run_stat = 0, read_stat = 0;
    uint8_t *catch_cmd;
    int res = 0, i = 0, retry = 0;

    switch (ctl) {
    case CATCHER_CLOSE:
        catch_cmd = catch_close;
        device_status_count_add(DS_GRAP_USED_COUNT, 1);
        break;
    case CATCHER_AUTO_CLOSE:
        catch_curr[5] = 0x19;
        catch_encoder(catch_curr, catch_send_buff, 6);
        ll_send(catcher_uart_fd, catch_send_buff, 8);
        if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
            ll_close(catcher_uart_fd);
            catcher_uart_fd = 0;
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
            return 1;
        }

        memset(catch_send_buff, 0, sizeof(catch_send_buff));
        memset(catch_send_buff, 0, sizeof(catch_recv_buff));

        catch_close[5] = CATCHER_CLOSE_AUTO_CTL; /* 自动标定时，允许最大行程至100 */
        catch_cmd = catch_close;
        ctl = CATCHER_CLOSE;
        break;
    case CATCHER_AUTO_CLOSE_X:
        catch_close[5] = 0x14; /* 自动标定X时，行走至20 */
        catch_cmd = catch_close;
        ctl = CATCHER_CLOSE;
        break;
    case CATCHER_OPEN:
        catch_cmd = catch_open;
        catch_curr[5] = 0x3c;
        catch_encoder(catch_curr, catch_send_buff, 6);
        ll_send(catcher_uart_fd, catch_send_buff, 8);
        if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
            ll_close(catcher_uart_fd);
            catcher_uart_fd = 0;
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
            return 1;
        }
        break;
    default:
        break;
    }

    catch_encoder(catch_cmd, catch_send_buff, 6);
    i = 3;
    while (i--) {
        ll_send(catcher_uart_fd, catch_send_buff, 8);
        read_stat = ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32);
        if (read_stat == -1) {
            ll_close(catcher_uart_fd);
            catcher_uart_fd = 0;
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
            return 2;
        } else if (read_stat == 1) {
            retry++;
            if (retry >= 2) {
                break;
            }
            continue;
        }
        if (!strncmp((char *)&catch_send_buff, (char *)&catch_recv_buff, 8)) {
            break;
        } else {
            LOG("send buf & recv buf not match!\n");
            LOG("send:\n");
            show_catch_recv_buf(catch_send_buff);
            LOG("recv:\n");
            show_catch_recv_buf(catch_recv_buff);
        }
    }
    run_stat = 0;
    if (get_throughput_mode() == 1) {   /* PT360 */
        usleep(20*1000);
    } else {
        usleep(150*1000);
    }
    while (1) {
        catch_encoder(catch_run_stat, catch_send_buff, 6);
        ll_send(catcher_uart_fd, catch_send_buff, 8);
        read_stat = ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32);
        if (read_stat == -1) {
            ll_close(catcher_uart_fd);
            catcher_uart_fd = 0;
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
            return 3;
        } else if (read_stat == 1) {
            retry++;
            if (retry >= 2) {
                break;
            }
            continue;
        }
        run_stat = catch_recv_buff[4];
        if (ctl == CATCHER_CLOSE) {
            if (run_stat) {
                res = 0;
                break;
            }
        } else if (ctl == CATCHER_OPEN) {
            if (run_stat == 0x1) {
                catch_curr[5] = 0x1e;
                catch_encoder(catch_curr, catch_send_buff, 6);
                ll_send(catcher_uart_fd, catch_send_buff, 8);
                if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
                    ll_close(catcher_uart_fd);
                    catcher_uart_fd = 0;
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
                    return 4;
                }
                res = 0;
                break;
            }
        }
        res++;
        if (res >= 40) {
            break;
        }
    }

    if (CATCHER_CLOSE == ctl) {
        if (CATCHER_CLOSE_MIN_PER > get_catcher_curr_step()) {
            catch_encoder(catch_cmd, catch_send_buff, 6);
            i = 3;
            while (i--) {
                ll_send(catcher_uart_fd, catch_send_buff, 8);
                if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
                    ll_close(catcher_uart_fd);
                    catcher_uart_fd = 0;
                    FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
                    return 5;
                }
                if (!strncmp((char *)&catch_send_buff, (char *)&catch_recv_buff, 8)) {
                    break;
                } else {
                    LOG("send buf & recv buf not match!\n");
                    LOG("send:\n");
                    show_catch_recv_buf(catch_send_buff);
                    LOG("recv:\n");
                    show_catch_recv_buf(catch_recv_buff);
                }
            }
            usleep(150*1000);
            LOG("CLOSE AGAIN!\n");
        }
    }

    return res;
}

/* 抓手励磁释放 */
int catcher_release(void)
{
    if (cather_init_flag == 0) {
        return 0;
    }

    uint8_t catch_send_buff[32] = {0};
    uint8_t catch_recv_buff[32] = {0};
    uint8_t catch_release[6] = {0x01, 0x06, 0x03, 0x04, 0x00, 0x01};
    catch_encoder(catch_release, catch_send_buff, 6);
    ll_send(catcher_uart_fd, catch_send_buff, 8);
    if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
        ll_close(catcher_uart_fd);
        catcher_uart_fd = 0;
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
    }
    show_catch_recv_buf(catch_recv_buff);

    return 0;
}

/* 抓手电流减小 */
int catcher_set_low_curr(void)
{
    uint8_t catch_send_buff[32] = {0};
    uint8_t catch_recv_buff[32] = {0};
    uint8_t catch_curr[6] = {0x01, 0x06, 0x01, 0x03, 0x00, 0x0a};

    catch_encoder(catch_curr, catch_send_buff, 6);
    ll_send(catcher_uart_fd, catch_send_buff, 8);
    if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
        ll_close(catcher_uart_fd);
        catcher_uart_fd = 0;
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
    }
    show_catch_recv_buf(catch_recv_buff);

    return 0;
}

int get_catcher_curr_step(void)
{
    uint8_t catch_send_buff[32] = {0};
    uint8_t catch_recv_buff[32] = {0};
    uint8_t catch_check_pos[6] = {0x01, 0x03, 0x02, 0x04, 0x00, 0x01};
    uint8_t true_step = 0;

    catch_encoder(catch_check_pos, catch_send_buff, 6);
    ll_send(catcher_uart_fd, catch_send_buff, 8);
    if (ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32)) {
        ll_close(catcher_uart_fd);
        catcher_uart_fd = 0;
        FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
        return 0;
    }
    true_step = catch_recv_buff[4];
    show_catch_recv_buf(catch_recv_buff);
    LOG("true_step = %d\n", true_step);

    return abs(true_step);
}

int check_catcher_status(void)
{
    uint8_t catch_send_buff[32] = {0};
    uint8_t catch_recv_buff[32] = {0};
    uint8_t catch_check_pos[6] = {0x01, 0x03, 0x02, 0x04, 0x00, 0x01};
    uint8_t true_step = 0;
    int catcher_status = 0, i = 0, read_stat = 0, retry = 0;

    catch_encoder(catch_check_pos, catch_send_buff, 6);
    i = 3;
    while (i--) {
        ll_send(catcher_uart_fd, catch_send_buff, 8);
        read_stat = ll_catcher_read(catcher_uart_fd, catch_recv_buff, 32);
        if (read_stat == -1) {
            ll_close(catcher_uart_fd);
            catcher_uart_fd = 0;
            FAULT_CHECK_DEAL(FAULT_CATCHER, MODULE_FAULT_LEVEL2, (void*)MODULE_FAULT_C_CONNECT);
            return 2;
        } else if (read_stat == 1) {
            retry++;
            if (retry >= 2) {
                break;
            }
            continue;
        }
    }

    true_step = catch_recv_buff[4];
    show_catch_recv_buf(catch_recv_buff);
    LOG("true_step = %d\n", true_step);

    if (abs(true_step) >= CATCHER_CLOSE_MIN_PER && abs(true_step) <= CATCHER_CLOSE_MAX_PER) {
        catcher_status = 1;
    } else {
        catcher_status = 0;
    }

    LOG("catcher_status = %d\n", catcher_status);
    return catcher_status;
}


