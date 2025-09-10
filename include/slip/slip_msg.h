#ifndef _SLIP_MSG_H_
#define _SLIP_MSG_H_

#include <slip/slip_port.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char type;        /* command type */
    unsigned char sub_type;    /* command sub type */
}slip_cmd_type_t;

#define SLIP_MSG_HEAD_LEN   8
#define SLIP_MSG_DATA_LEN   255

#define SLIP_VERSION 0X01

typedef struct {
    unsigned char src;          /* source address */
    unsigned char dst;          /* destination address */
    unsigned char trans_id;     /* trans id 传输序列号 */
    unsigned char version;      /* protocol version */
    slip_cmd_type_t cmd_type;   /* command type */
    unsigned short length;      /* data length */
    unsigned char data[SLIP_MSG_DATA_LEN];    /* data 0-255 */
}slip_msg_t;

void slip_msg_create(unsigned char src, unsigned char dst, unsigned char trans_id,
                            unsigned char type, unsigned char sub_type, 
                            unsigned short length, const void *data, 
                            slip_msg_t *msg);

void buff_to_slip_msg(const unsigned char *buff, slip_msg_t *sp);

void slip_send(const slip_port_t *port, const slip_msg_t *sp);

void slip_send_fd(int fd, const slip_msg_t *sp);

void slip_send_path(const char *path, const slip_msg_t *sp);

void slip_send_node(unsigned char src, unsigned char dst, unsigned char trans_id,
                    unsigned char type, unsigned char sub_type, 
                    unsigned short length, const void *data);

#ifdef __cplusplus
}
#endif

#endif

