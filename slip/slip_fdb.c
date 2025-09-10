#include <stdio.h>
#include <stdlib.h>
#include <slip/slip_fdb.h>
#include <log.h>


typedef struct slip_fdb {
    slip_fdb_entry_t *e;
    struct slip_fdb *next;
}slip_fdb_t;

static slip_fdb_t *g_slip_fdb_head = NULL;
static slip_fdb_t *g_slip_fdb_tail = NULL;

static slip_fdb_t *slip_fdb_entry_create(const slip_port_t *port, unsigned char node_id)
{
    slip_fdb_t *sf = malloc(sizeof(slip_fdb_t));
    sf->e = malloc(sizeof(slip_fdb_entry_t));
    sf->e->port = port;
    sf->e->node_id = node_id;
    sf->next = NULL;
    return sf;
}

slip_fdb_entry_t *slip_fdb_entry_register(const slip_port_t *port, unsigned char node_id)
{
    if (NULL == port) {
        return NULL;
    }

    slip_fdb_t *sf = slip_fdb_entry_create(port, node_id);
    if (g_slip_fdb_head == NULL) {
        g_slip_fdb_head = g_slip_fdb_tail = sf;        
    } else {
        g_slip_fdb_tail->next = sf;
        g_slip_fdb_tail = sf;
    }
    return sf->e;
}

slip_fdb_entry_t *slip_fdb_entry_get(unsigned char node_id)
{
    slip_fdb_t *e = g_slip_fdb_head;

    for (; e; e = e->next) {
        if (e->e->node_id == node_id) {
            return e->e;
        }
    }

    return NULL;
}

slip_fdb_entry_t * slip_fdb_entry_add(const slip_port_t *port, unsigned char node_id)
{
    return slip_fdb_entry_register(port, node_id);
}

void slip_fdb_debug(void)
{
    slip_fdb_t *e = g_slip_fdb_head;
    int i = 0;

    LOG("slip forward table: \n");
    LOG("     idx | fd | node_id | path\n");

    for (; e; e = e->next) {
        LOG("     %-3d | %-2d | %-7d | %s\n", ++i, e->e->port->fd, e->e->node_id, e->e->port->path);
    }
}


