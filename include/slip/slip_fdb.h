#ifndef _SLIP_FDB_H_
#define _SLIP_FDB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <slip/slip_port.h>

typedef struct slip_fdb_entry
{
    const slip_port_t *port;
    unsigned char node_id;
}slip_fdb_entry_t;

slip_fdb_entry_t *slip_fdb_entry_register(const slip_port_t *port, unsigned char node_id);

slip_fdb_entry_t *slip_fdb_entry_get(unsigned char node_id);

slip_fdb_entry_t * slip_fdb_entry_add(const slip_port_t *port, unsigned char node_id);

void slip_fdb_debug(void);

#ifdef __cplusplus
}
#endif

#endif

