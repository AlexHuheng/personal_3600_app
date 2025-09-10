#ifndef _SLIP_DECODE_H_
#define _SLIP_DECODE_H_

#include <slip/slip_msg.h>
#include <slip/slip_port.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLIP_DECODE_BUFF_SIZE (2 * (SLIP_MSG_HEAD_LEN + SLIP_MSG_DATA_LEN))

typedef void (*slip_decoder_callback)(const slip_port_t *port, const slip_msg_t *msg);

typedef struct {
    slip_port_t *port;
    unsigned char node_id;    
    int decode_state;
    int decode_count;
    unsigned char decode_buff[SLIP_DECODE_BUFF_SIZE];
}slip_decoder_t;

slip_decoder_t *slip_decoder_get(const slip_port_t *port, unsigned char node_id);

int slip_decode(slip_decoder_t *decoder, const unsigned char *buff, int length);

int slip_encode(const unsigned char *inbuff, int in_length, unsigned char *outbuff, int max_outlength);

int slip_msg_encode(const slip_msg_t *sp, unsigned char *buff, int length);

int slip_msg_decode(const unsigned char * in, int in_len, unsigned char * out, int out_len);

void slip_decoder_init(slip_decoder_callback cb);


#ifdef __cplusplus
}
#endif

#endif


