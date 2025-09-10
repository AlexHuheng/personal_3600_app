#ifndef _LOWLEVEL_H_
#define _LOWLEVEL_H_

#ifdef __cplusplus
extern "C" {
#endif

int ll_can_open(const char *path, unsigned char can_id, unsigned int mask);

int ll_can_send(int fd, unsigned char can_id, const unsigned char *buff, int length);

int ll_can_recv(int fd, unsigned char *can_id, unsigned char *buff, int size);

int ll_ipi_open(const char *path);

int ll_ipi_send(int fd, const unsigned char *buff, int length);

int ll_ipi_recv(int fd, unsigned char *buff, int size);


int ll_open(const char *path);

int ll_send(int fd, const unsigned char *buff, int length);

int ll_recv(int fd, unsigned char *buff, int size);

int ll_uart_open(const char *path, int speed, int flow_ctrl, int data_bits, int stop_bits, int parity);
int ll_uart_open_nonblock(const char *path, int speed, int flow_ctrl, int data_bits, int stop_bits, int parity);
int ll_close(int fd);



#ifdef __cplusplus
}
#endif

#endif
