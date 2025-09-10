#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <mq.h>
#include <crc.h>
#include <log.h>
#include <slip/slip_decoder.h>


/* 转义 C0 -> DB DC, DB -> DB DD */
#define DECODE_MAGIC_C0 (0xC0)
#define DECODE_MAGIC_DB (0xDB)
#define DECODE_MAGIC_DC (0xDC)
#define DECODE_MAGIC_DD (0xDD)

#define FRAME_START DECODE_MAGIC_C0
#define FRAME_END   FRAME_START

typedef enum {
    DECODE_STATE_0 = 0,        /* s0  未找到帧头 */
    DECODE_STATE_10,           /* s10 已找到帧头，接收正常数据 */
    DECODE_STATE_11,           /* s11 已找到帧头，接收转义数据 */
}decode_state_t;

typedef struct _slip_decoder{
    slip_decoder_t *decoder;
    struct _slip_decoder *next;
}slip_decoder_s;

typedef struct {
    slip_port_t *port;
    unsigned char buf[SLIP_DECODE_BUFF_SIZE];
    int len;
}decoder_msg_t;

static slip_decoder_callback decoder_callback = NULL;
static mqd_t decoder_mq;

static slip_decoder_s *g_decoder_head = NULL;
static slip_decoder_s *g_decoder_tail = NULL;
static pthread_mutex_t decoder_mutex = PTHREAD_MUTEX_INITIALIZER;
#define SLIP_DECODER_LOCK() do {} while (0 != pthread_mutex_lock(&decoder_mutex))
#define SLIP_DECODER_UNLOCK() do {} while (0 != pthread_mutex_unlock(&decoder_mutex))

slip_decoder_t *slip_decoder_get(const slip_port_t *port, unsigned char node_id)
{
    slip_decoder_s *p = NULL;

    SLIP_DECODER_LOCK();
    for (p = g_decoder_head; p; p = p->next) {
        if (p->decoder->port == port && p->decoder->node_id == node_id) {
            SLIP_DECODER_UNLOCK();
            return p->decoder;
        }
    }

    slip_decoder_s *decoder = calloc(1, sizeof(slip_decoder_s));
    decoder->decoder = calloc(1, sizeof(slip_decoder_t));
    decoder->decoder->port = (slip_port_t *)port;
    decoder->decoder->node_id = node_id;
    decoder->decoder->decode_state = DECODE_STATE_0;
    decoder->decoder->decode_count = 0;
    decoder->next = NULL;
    if (g_decoder_head == NULL) {
        g_decoder_head = g_decoder_tail = decoder;
    } else {
        g_decoder_tail->next = decoder;
        g_decoder_tail = decoder;
    }
    SLIP_DECODER_UNLOCK();

    return decoder->decoder;
}

static int crc_check(const unsigned char *buf, int len)
{
    uint32_t check1 = 0;
    uint32_t check2 = 0;
    
    check1 = (buf[len - 2] << 8) + buf[len - 1];
    check2 = uCRC_Compute(uCRC16_X25, buf, len - 2);
    check2 = uCRC_ComputeComplete(uCRC16_X25, check2);

    static int error_count = 0;

    if (check1 != check2) {
        LOG("check1: %08x, check2: %08x, error count: %d\n", check1, check2, ++error_count);
        
        int n = 0;
        while (n < len) {
            int pn = len - n > 8 ? 8 : len - n;
            for (int i = 0; i < pn; i++) {
                LOG_QUIET("%02x ", buf[i + n]);
            }        
            LOG_QUIET("\n");
            n += pn;
        }
        LOG("=======================\n");
    }

    return check1 == check2;
}

static void decoderMq_add(const slip_port_t *port, const unsigned char *buf, int len)
{
    decoder_msg_t msg = {0};

    msg.port = (slip_port_t *)port;
    memcpy(msg.buf, buf, len);
    msg.len = len;
    mq_send(decoder_mq, &msg, sizeof(decoder_msg_t), 0);
}

static int decode(slip_decoder_t *decoder, unsigned char ch)
{    
    if (ch == DECODE_MAGIC_C0) {         
        if (decoder->decode_state != DECODE_STATE_0 && decoder->decode_count > 8) { /* 找到帧尾 */
            //LOG("received %d bytes from port %s(%d)\n", decoder->decode_count, decoder->port->path, decoder->port->fd);
            decoder->decode_state = DECODE_STATE_0;
            decoderMq_add(decoder->port, decoder->decode_buff, decoder->decode_count);
        } else { /* 找到帧头 */
            decoder->decode_count = 0;
            decoder->decode_state = DECODE_STATE_10;
            return 0;
        }
    }

    if (decoder->decode_state == DECODE_STATE_10) { /* s10 已找到帧头，接收正常数据 */                
        if (decoder->decode_count >= sizeof(decoder->decode_buff)) {
            LOG("decode count is too big\n");
            decoder->decode_count = 0;
            decoder->decode_state = DECODE_STATE_0;
        } else {
            decoder->decode_buff[decoder->decode_count++] = ch;
        }
    }

    return 0;
}

int slip_decode(slip_decoder_t *decoder, const unsigned char *buff, int length)
{
    int i;
    int ret = 0;
   
    for (i = 0; i < length; i++) {
        ret = decode(decoder, buff[i]); 
    }
    return ret;
}

#define encode_ch(ch) do{\
    if (out_length > max_outlength - 3)\
        return -1;\
    if ((ch) == DECODE_MAGIC_C0) {\
        outbuff[out_length++] = DECODE_MAGIC_DB;\
        outbuff[out_length++] = DECODE_MAGIC_DC;\
    } else if ((ch) == DECODE_MAGIC_DB) {\
        outbuff[out_length++] = DECODE_MAGIC_DB;\
        outbuff[out_length++] = DECODE_MAGIC_DD;\
    } else {\
        outbuff[out_length++] = (ch);\
    }\
}while(0)

int slip_encode(const unsigned char *inbuff, int in_length, unsigned char *outbuff, int max_outlength)
{
    int i;
    int out_length = 0;
    uint32_t check = 0;

    outbuff[out_length++] = DECODE_MAGIC_C0;
    
    for (i = 0; i < in_length; i++) {
        encode_ch(inbuff[i]);
        check += inbuff[i];
    }

    /*CRC校验*/
    check = uCRC_Compute(uCRC16_X25, inbuff, in_length);
    check = uCRC_ComputeComplete(uCRC16_X25, check);

    encode_ch((check >> 8) & 0xFF);
    encode_ch((check >> 0) & 0xFF);


    outbuff[out_length++] = DECODE_MAGIC_C0;
    return out_length;
}

int slip_msg_encode(const slip_msg_t *sp, unsigned char *buff, int length)
{
    int i;
    int count;
    unsigned char inbuff[SLIP_DECODE_BUFF_SIZE];
    unsigned char send_buff[SLIP_DECODE_BUFF_SIZE];
    unsigned short *len;

    inbuff[0] = sp->src;
    inbuff[1] = sp->dst;
    inbuff[2] = sp->trans_id;
    inbuff[3] = sp->version;
    inbuff[4] = sp->cmd_type.type;
    inbuff[5] = sp->cmd_type.sub_type;
    len = (unsigned short *)&inbuff[6];
    *len = htons(sp->length);
    
    for (i = 0; i < sp->length; i++) {
        inbuff[8 + i] = sp->data[i];
    }
    
    count = slip_encode(inbuff, 8 + sp->length, send_buff, sizeof(send_buff));
    if (count <= 0) {   
        LOG("encode failed %d,%d\n" , count, length);
        return -1;
    }

    memcpy(buff, send_buff, count);

    return count;
}

int slip_msg_decode(const unsigned char *in, int in_len, unsigned char *out, int out_len)
{
    int n;
    int i;

    if (in == NULL || in_len == 0 || out == NULL || out_len == 0) {
        return -1;
    }

    i = 0;
    n = 0;
    while (i < in_len && n < out_len) {    
        if (in[i] == DECODE_MAGIC_DB) {
            if (in[i + 1] == DECODE_MAGIC_DC) {
                out[n++] = DECODE_MAGIC_C0;
            } else if (in[i + 1] == DECODE_MAGIC_DD) {
                out[n++] = DECODE_MAGIC_DB;
            }
            i += 2;
        } else {
            out[n++] = in[i++];
        }
    }

    return n;
}

static void *decoder_thread(void *arg)
{
    decoder_msg_t msg;
    slip_msg_t slip_msg;
    unsigned char buf[SLIP_DECODE_BUFF_SIZE];

    while (1)
    {
        if (0 == mq_receive(decoder_mq, &msg, sizeof(decoder_msg_t), 0)) {
            continue;
        }
        int len = slip_msg_decode(msg.buf, msg.len, buf, sizeof(buf));
        if (crc_check(buf, len) && decoder_callback) {
            buff_to_slip_msg(buf, &slip_msg);
            decoder_callback(msg.port, &slip_msg);
        }
    }
    return NULL;
}

void slip_decoder_init(slip_decoder_callback cb)
{
    pthread_t tid;
    mq_attr_t mqAttr;
    mqAttr.mq_flags = 0;
    mqAttr.mq_curmsgs = 0;
    mqAttr.mq_msgsize = 1024;
    mqAttr.mq_maxmsg = 64;

    decoder_callback = cb;
    decoder_mq = mq_open("/tmp/slip_rxmq", 0, 0, &mqAttr);
    pthread_create(&tid, NULL, decoder_thread, NULL);
}

