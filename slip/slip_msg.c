#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <slip/slip_msg.h>
#include <slip/slip_decoder.h>
#include <slip/slip_process.h>
#include <slip/slip_node.h>

#include <log.h>


static unsigned char g_trans_id = 0;
static pthread_mutex_t trans_id_mutex = PTHREAD_MUTEX_INITIALIZER;
#define SLIP_TRANS_ID_LOCK()    do {} while (pthread_mutex_lock(&trans_id_mutex) != 0)
#define SLIP_TRANS_ID_UNLOCK()    do {} while (pthread_mutex_unlock(&trans_id_mutex) != 0)
extern int module_is_upgrading_now(uint8_t dst, uint8_t type);

static unsigned char slip_trans_id_get(void)
{
    unsigned char id;
    SLIP_TRANS_ID_LOCK();
    if (g_trans_id == 255) {
        g_trans_id = 1;
    } else {
        g_trans_id++;
    }
    id = g_trans_id;
    SLIP_TRANS_ID_UNLOCK();
    return id;
}

void slip_msg_create(unsigned char src, unsigned char dst, unsigned char trans_id,
                            unsigned char type, unsigned char sub_type, 
                            unsigned short length, const void *data, 
                            slip_msg_t *sp)
{
    sp->src = src;
    sp->dst = dst;
    sp->trans_id = trans_id;
    sp->version = SLIP_VERSION;
    sp->cmd_type.type = type;
    sp->cmd_type.sub_type = sub_type;
    sp->length = length;

    if (length > SLIP_MSG_DATA_LEN) {
        LOG("slip data message too long, length is %d, max is %d\n", length, SLIP_MSG_DATA_LEN);
        return;
    }
    
    if (length) {
        memcpy(sp->data, data, length);
    }
}

void buff_to_slip_msg(const unsigned char *buff, slip_msg_t *sp)
{   
    unsigned short *length = NULL;
    sp->src = buff[0];
    sp->dst = buff[1];
    sp->trans_id = buff[2];
    sp->version = buff[3];
    sp->cmd_type.type = buff[4];
    sp->cmd_type.sub_type = buff[5];
    length = (unsigned short *)&buff[6];
    sp->length = ntohs(*length);
    if (sp->length > SLIP_MSG_DATA_LEN)
    {
        sp->length = SLIP_MSG_DATA_LEN;
        LOG_ERROR("Rx slip length is invalid.(%d)\r\n", sp->length);
    }
    if (sp->length) {
        memcpy(sp->data, &buff[8], sp->length);
    } else {
        memset(sp->data, 0, sizeof(sp->data));
    }
}

void slip_send(const slip_port_t *port, const slip_msg_t *sp)
{
    int i;
    int count;
    unsigned char inbuff[SLIP_DECODE_BUFF_SIZE];
    unsigned char send_buff[SLIP_DECODE_BUFF_SIZE];
    unsigned short *length;

    if (NULL == port) {
        LOG("port is NULL\n");
        return;
    }

    if (NULL == sp) {
        LOG("slip msg is NULL");
        return;
    }

    inbuff[0] = sp->src;
    inbuff[1] = sp->dst;
    //inbuff[2] = sp->trans_id;
    inbuff[2] = slip_trans_id_get();
    inbuff[3] = sp->version;
    inbuff[4] = sp->cmd_type.type;
    inbuff[5] = sp->cmd_type.sub_type;
    length = (unsigned short *)&inbuff[6];
    *length = htons(sp->length);
    
    for (i = 0; i < sp->length; i++) {
        inbuff[8 + i] = sp->data[i];
    }
    
    count = slip_encode(inbuff, 8 + sp->length, send_buff, sizeof(send_buff));
    if (count <= 0) {   
        LOG("encode failed %d, %hn\n" , count, length);
        return;
    }
    
    txMq_add(port, sp->dst, send_buff, count);
}

void slip_send_fd(int fd, const slip_msg_t *sp)
{
    slip_send(slip_port_get_by_fd(fd), sp);
}

void slip_send_path(const char *path, const slip_msg_t *sp)
{
    slip_send(slip_port_get_by_path(path), sp);
}

void slip_send_node(unsigned char src, unsigned char dst, unsigned char trans_id,
                            unsigned char type, unsigned char sub_type, 
                            unsigned short length, const void *data)
{
    slip_msg_t msg = {0};

    if (module_is_upgrading_now(dst, type)) {
        LOG("the module is upgrading now!\n", dst);
        return;
    }

    msg.src = src;
    msg.dst = dst;
    msg.trans_id = trans_id;
    msg.version = SLIP_VERSION;
    msg.cmd_type.type = type;
    msg.cmd_type.sub_type = sub_type;
    msg.length = length;
    if (length) {
        memcpy(msg.data, data, length);
    }
    slip_send(slip_port_get_by_path(slip_node_path_get_by_node_id(dst)), &msg);   
}


