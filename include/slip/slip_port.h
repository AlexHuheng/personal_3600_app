#ifndef _SLIP_PORT_H_
#define _SLIP_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum slip_port_type {
    PORT_TYPE_NULL,
    PORT_TYPE_CAN = 1,
    PORT_TYPE_UART,
    PORT_TYPE_NETWORK,
    PORT_TYPE_IPI
}slip_port_type_t;

typedef struct slip_port {
    int fd;
    const char *path;
    slip_port_type_t type;
    struct slip_port *next;
}slip_port_t;

slip_port_t *slip_port_create(int fd, const char *path, int type);

slip_port_t *slip_port_register(const char *path, int port_type);

slip_port_t *slip_port_get_all(void);

slip_port_t *slip_port_get_by_fd(int fd);

slip_port_t *slip_port_get_by_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif
