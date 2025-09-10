#ifndef _SLIP_NODE_H_
#define _SLIP_NODE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <slip/slip_port.h>
    
typedef enum
{
    SLIP_NODE_TYPE_MASTER,
    SLIP_NODE_TYPE_SLAVE
}slip_node_type_t;

typedef struct 
{
    unsigned char node_id;
    slip_node_type_t node_type;
    char *path;
    slip_port_type_t port_type;
}slip_node_t;

#define SLIP_NODE_MAX 32

slip_node_t *slip_node_register(unsigned char node_id, slip_node_type_t node_type, const char * path, slip_port_type_t port_type);

void slip_node_id_set(unsigned char node_id);

unsigned char slip_node_id_get(void);

char *slip_node_path_get_by_node_id(unsigned char node_id);

void slip_node_init(void);

int slip_node_is_master(void);

#ifdef __cplusplus
}
#endif

#endif

