#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <slip/slip_port.h>
#include <slip/slip_node.h>
#include <lowlevel.h>
#include <log.h>

static slip_port_t *g_port_head = NULL;
static slip_port_t *g_port_tail = NULL;

slip_port_t *slip_port_create(int fd, const char *path, int type)
{
    slip_port_t *port = malloc(sizeof(slip_port_t));
    port->fd = fd;
    port->path = path;
    port->type = type;
    port->next = NULL;    
    return port;
}

slip_port_t *slip_port_register(const char *path, int port_type)
{
    int fd = -1;
    
    if (port_type == PORT_TYPE_CAN) {
        if (slip_node_is_master()) {
            fd = ll_can_open(path, 0, 0);
        } else {
            unsigned char can_id = slip_node_id_get();
            fd = ll_can_open(path, can_id, 0xff);
        }
    } else if (port_type == PORT_TYPE_IPI) {
        fd = ll_ipi_open(path);
    } else if (port_type == PORT_TYPE_UART) {
        fd = ll_uart_open(path, 115200, 0, 8, 1, 'N');
    } else {
        fd = ll_open(path);
    }

    if (fd < 0) {
        LOG("port %s register fail\n", path);
        return NULL;
    }

    slip_port_t *port = slip_port_create(fd, path, port_type);

    if (g_port_head == NULL) {
        g_port_head = g_port_tail = port;
    } else {
        g_port_tail->next = port;
        g_port_tail = port;
    }

    return port;

}

slip_port_t *slip_port_get_all(void)
{
    return g_port_head;
}

slip_port_t *slip_port_get_by_path(const char *path)
{
    slip_port_t *port = NULL;

    for (port = g_port_head; path && port; port = port->next) {
        if (0 == strcmp(port->path, path)) {
           break;
        }
    }

    return port;
}

slip_port_t *slip_port_get_by_fd(int fd)
{
    slip_port_t *port = NULL;

    for (port = g_port_head; port; port = port->next) {
        if (fd == port->fd) {
           break;
        }
    }

    return port;
}

