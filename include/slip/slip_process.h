#ifndef _SLIP_PROCESS_H_
#define _SLIP_PROCESS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <slip/slip_port.h>
#include <slip/slip_msg.h>

void slip_process_api(const slip_port_t *port, slip_msg_t *msg);

void txMq_add(const slip_port_t *port, unsigned char node_id, const unsigned char *buff, int len);

void rxMq_add(const slip_port_t *port, const slip_msg_t *msg);

void slip_init(void);

#ifdef __cplusplus
}
#endif

#endif

