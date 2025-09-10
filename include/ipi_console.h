#ifndef IPI_CONSOLE_H_
#define IPI_CONSOLE_H_
#include <stdint.h>

int ipi_send(int fd, char *send_buf, int data_len);
int ipi_recv(int fd, char *rcv_buf, int data_len);
int ipi_init(void);

#endif

