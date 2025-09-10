#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include <lowlevel.h>
#include <slip/slip_node.h>
#include <slip/slip_fdb.h>
#include <slip/slip_decoder.h>
#include <slip/slip_process.h>

#include <log.h>
#include <mq.h>

typedef struct
{
    mqd_t rx_mq;
    mqd_t tx_mq;
}slip_process_t;

typedef struct
{
    slip_port_t *port;
    slip_msg_t *msg;
}slip_rx_msg_t;

typedef struct
{
    unsigned char node_id;
    slip_port_t *port;        
    unsigned char *buff;
    int len;
}slip_tx_msg_t;


static slip_process_t g_sp;

void txMq_add(const slip_port_t *port, unsigned char node_id, const unsigned char *buff, int len)
{
    slip_tx_msg_t tx_msg;
    tx_msg.port = (slip_port_t *)port;
    tx_msg.node_id = node_id;
    tx_msg.buff = calloc(1, len);    
    memcpy(tx_msg.buff, buff, len);
    tx_msg.len = len;
    mq_send(g_sp.tx_mq, &tx_msg, sizeof(slip_tx_msg_t), 0);
}

void rxMq_add(const slip_port_t *port, const slip_msg_t *msg)
{
    slip_rx_msg_t rx_msg;
    rx_msg.port = (slip_port_t *)port;
    rx_msg.msg = calloc(1, sizeof(slip_msg_t));
    memcpy(rx_msg.msg, msg, sizeof(slip_msg_t));
    mq_send(g_sp.rx_mq, &rx_msg, sizeof(slip_rx_msg_t), 0);
}

static void slip_msg_handler(const slip_port_t *port, const slip_msg_t *msg)
{    
    slip_fdb_entry_t *entry;
    unsigned char node_id = slip_node_id_get();

    //LOG("src: %d, dst: %d, my: %d\n", msg->src, msg->dst, node_id);

    if (msg->dst == node_id) {        
        rxMq_add(port, msg);
    } else {   
        entry = slip_fdb_entry_get(msg->dst);
        if (entry) {   
            if (entry->port != port) {
                LOG("slip message forward, port: %s, node id: %d\n", entry->port->path, entry->node_id); 
                slip_send(entry->port, msg);
            } else {
                LOG("Same port, do not forward\n");
            }
        } else {
//            LOG("No entry found, do not forward\n");
        }   
    }   
}

static void *slip_ipi_recv_thread(void *arg)
{
    slip_port_t *port = (slip_port_t *)arg;
    int fd = port->fd;
    unsigned char buff[1024];
    int count;    

    while (1) {
        count = ll_ipi_recv(fd, buff, sizeof(buff));
        if (count) {            
            slip_decoder_t *decoder = slip_decoder_get(port, 0);
            if (decoder) {
                slip_decode(decoder, buff, count);
            }
        }
    }

    return NULL;
}

static void *slip_recv_thread(void *arg)
{   
    unsigned char buf[1024];
    unsigned char can_id;
    int count;
    slip_decoder_t *decoder;
    slip_port_t *port_list = slip_port_get_all();
    slip_port_t *port;
    pthread_t tid;

    for (port = port_list; port; port = port->next) {
        if (port->type != PORT_TYPE_IPI) {
            continue;
        }
        pthread_create(&tid, NULL, slip_ipi_recv_thread, port);
    }
    
    while (1) {
        int max_fd = 0;
        fd_set readfds;
        FD_ZERO(&readfds);
        
        for (port = port_list; port; port = port->next) {
            if (port->type == PORT_TYPE_IPI) {
                continue;
            }
            max_fd = max_fd < port->fd ? port->fd : max_fd;
            FD_SET(port->fd, &readfds);
        }
        
        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret > 0) {
            for (port = port_list; port; port = port->next) {
                if (FD_ISSET(port->fd, &readfds)) {
                    memset(buf, 0, sizeof(buf));
                    can_id = 0;
                    if (port->type == PORT_TYPE_CAN) {
                        count = ll_can_recv(port->fd, &can_id, buf, sizeof(buf));
                    } else {
                        count = ll_recv(port->fd, buf, sizeof(buf));
                    }

                    if (count <= 0) {
                        LOG("port: %s read() fail, count: %d, error: %s\n", port->path, count, strerror(errno));
                        usleep(1000);
                        continue;
                    }
                    #if 0
                    LOG("recv %d bytes from port %s(%d)\n", count, port->path, port->type);
                    LOG(" ");
                    int i = 0;
                    for (i = 0; i < count; i++) {
                        printf("%02x ", buf[i]);
                    }
                    printf("\n");
                    #endif
                    decoder = slip_decoder_get(port, can_id);
                    if (decoder) {
                        slip_decode(decoder, buf, count);
                    }
                }
            }
        }
    }
    
    return NULL;
}

static void *slip_send_thread(void *arg)
{
    slip_tx_msg_t tx_msg;
    const slip_port_t *port;
    slip_port_type_t port_type;
    int fd;
    unsigned char *buff;
    int len;
    int n;
    
    
    while (1) {
        memset(&tx_msg, 0, sizeof(tx_msg));
        mq_receive(g_sp.tx_mq, (char *)&tx_msg, sizeof(slip_tx_msg_t), NULL);
        port = tx_msg.port;
        port_type = port->type;
        fd = port->fd;
        buff = tx_msg.buff;
        len = tx_msg.len;
        if (port_type == PORT_TYPE_CAN) {
            n = ll_can_send(fd, (unsigned int)tx_msg.node_id, buff, len);
        } else if (port_type == PORT_TYPE_IPI) {
            n = ll_ipi_send(fd, buff, len);
        } else {
            n = ll_send(fd, buff, len);
        }
        #if 0
        LOG("send %d bytes to port %s(%d)\n", n, port->path, port->fd);
        LOG_QUIET(" ");
        int i = 0;
        for (i = 0; i < len; i++) {
            LOG_QUIET("%02x ", buff[i]);
            if ((i+1)%8 == 0) {
                LOG_QUIET("\n");
                LOG_QUIET(" ");
            }            
        }
        LOG_QUIET("\n");
        #endif
        if (n <= 0) {
            LOG("send to port %s(%d) fail, error: %s\n", port->path, port->fd, strerror(errno));
        }
        
        free(tx_msg.buff);
    }

    return NULL;
}

extern void slip_process_api(const slip_port_t *port, slip_msg_t *msg);

static void *slip_msg_process_thread(void *arg)
{
    slip_rx_msg_t rx_msg;
    slip_msg_t *msg;
    slip_port_t *port;

    while (1) {
        memset(&rx_msg, 0, sizeof(rx_msg));
        if (0 >= mq_receive(g_sp.rx_mq, (char *)&rx_msg, sizeof(slip_rx_msg_t), 0)) {
            LOG("mq receive faild\n");
            sleep(1);            
            continue;
        }
        msg = rx_msg.msg;
        port = rx_msg.port;        
        slip_process_api(port, msg);
        free(msg);
    }

    return NULL;
}

void slip_init(void)
{
    pthread_t tid;
    
    mq_attr_t mqAttr;
    mqAttr.mq_flags = 0;
    mqAttr.mq_curmsgs = 0;
    mqAttr.mq_msgsize = 1024;
    mqAttr.mq_maxmsg = 64;

    g_sp.rx_mq = mq_open("/tmp/slip_rxmq", O_RDWR | O_CREAT, 0666, &mqAttr);
    g_sp.tx_mq = mq_open("/tmp/slip_txmq", O_RDWR | O_CREAT, 0666, &mqAttr);
    
    slip_node_init();
    
    slip_decoder_init(slip_msg_handler);
    
    pthread_create(&tid, NULL, slip_recv_thread, NULL);
    pthread_create(&tid, NULL, slip_send_thread, NULL);
    pthread_create(&tid, NULL, slip_msg_process_thread, NULL);
}



