#ifndef PLATFORM_LIB_MQ_H
#define PLATFORM_LIB_MQ_H

#ifdef __cplusplus
extern "C" {
#endif
typedef struct mq_attr
{
  int   mq_maxmsg;    /* Max number of messages in queue */
  int   mq_msgsize;   /* Max message size */
  int   mq_flags;     /* Queue flags */
  int   mq_curmsgs;   /* Number of messages currently in queue */
}mq_attr_t;

struct mq_msg {
    struct mq_msg *prev;
    struct mq_msg *next;
    int length;
    char *buff;
};

struct mq_des
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct mq_msg msg_head;
    int maxmsgs;
    int maxmsgsize;
    
    int msgs_count;
    int msgs_total_bytes;
    int msgs_max_bytes;
};

typedef struct mq_des *mqd_t;

mqd_t mq_open(const char *mq_name, int oflags, ...);
int mq_receive(mqd_t mqdes, void *msg, int msglen, unsigned int *prio);
int mq_send(mqd_t mqdes, const void *msg, int msglen, unsigned int prio);
int mq_load_warning(mqd_t mqdes);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_LIB_MQ_H
