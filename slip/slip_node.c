#include <stdio.h>
#include <string.h>
#include <log.h>
#include <slip/slip_port.h>
#include <slip/slip_fdb.h>
#include <slip/slip_node.h>
#include <slip/slip_decoder.h>


static unsigned char g_node_id = 0;
static slip_node_t g_slip_node_list[SLIP_NODE_MAX];
static int node_index = 0;

static slip_node_t *slip_node_get_by_node_id(unsigned char node_id)
{
    slip_node_t *node;
    int i;

    for (i = 0; i < node_index; i++) {
        node = &g_slip_node_list[i];
        if (node && node->node_id == node_id) {
            return node;
        }
    }

    return NULL;
}

char *slip_node_path_get_by_node_id(unsigned char node_id)
{
    slip_node_t *node;
    int i;

    for (i = 0; i < node_index; i++) {
        node = &g_slip_node_list[i];
        if (node->node_id == node_id) {
            return (char *)node->path;
        }
    }

    return NULL;
}

void slip_node_id_set(unsigned char node_id)
{
    g_node_id = node_id;
}

unsigned char slip_node_id_get(void)
{
    return g_node_id;
}

slip_node_t *slip_node_register(unsigned char node_id, slip_node_type_t node_type, const char *path, slip_port_type_t port_type)
{
    slip_node_t *node = NULL;

    if (node_index >= SLIP_NODE_MAX) {
        LOG("slip node max is %d\n", SLIP_NODE_MAX);
        return NULL;
    }

    node = &g_slip_node_list[node_index++];
    node->node_id = node_id;
    node->node_type = node_type;
    node->path = path ? strdup(path) : NULL;
    node->port_type = port_type;

    return node;
}

/* 当前slip节点是否为主节点 */
int slip_node_is_master(void)
{
    unsigned char node_id = slip_node_id_get();
    slip_node_t *node = slip_node_get_by_node_id(node_id);

    if (node && node->node_type == SLIP_NODE_TYPE_MASTER) {
        return 1;
    }

    return 0;
}

void slip_node_init(void)
{
    unsigned char node_id = slip_node_id_get();
    slip_node_t *node = NULL, *tmp;
    slip_port_t *port = NULL;
    int i;
    
    node = slip_node_get_by_node_id(node_id);
    if (node == NULL) {
        LOG("no such node found\n");
        return;
    }

    if (node->node_type == SLIP_NODE_TYPE_MASTER)
    {
        for (i = 0; i < node_index; i++)
        {
            tmp = &g_slip_node_list[i];
            if (NULL == tmp->path)
            {
                continue;
            }
            port = slip_port_get_by_path(tmp->path);
            if (NULL == port)
            {
                port = slip_port_register(tmp->path, tmp->port_type);
            }
            slip_fdb_entry_register(port, tmp->node_id);
        }
    }
    else
    {
        slip_port_register(node->path, node->port_type);
    } 

    slip_port_t *port_list = slip_port_get_all();
    for (port = port_list; port; port = port->next) {
        LOG("port: %s(%d)\n", port->path, port->fd);
    }
    if (node->node_type == SLIP_NODE_TYPE_MASTER) {
        slip_fdb_debug();
    }
}
