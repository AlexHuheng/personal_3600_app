#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <log.h>

#include "mq.h"

mqd_t mq_open(const char *mq_name, int oflags, ...)
{
    int ret;
    va_list ap;
    mode_t mode;
    struct mq_attr *attr;
    mqd_t q = NULL;

    va_start(ap, oflags);
    mode = va_arg(ap, mode_t);
    attr = va_arg(ap, struct mq_attr *);
    va_end(ap);

    mode = mode;

    q = (struct mq_des *)calloc(1, sizeof(struct mq_des));
    if (q == NULL)
    {
        LOG("mq open failed %s \n.", mq_name);
        return NULL;
    }

    q->maxmsgs = attr->mq_maxmsg;
    q->maxmsgsize = attr->mq_msgsize;
    q->msgs_max_bytes = attr->mq_maxmsg * attr->mq_msgsize;
    q->msg_head.prev = &q->msg_head;
    q->msg_head.next = &q->msg_head;

    ret = pthread_mutex_init(&q->mutex, NULL);
    if (ret != 0)
    {
        free(q);
        LOG("mq mutex init failed %s \n.", mq_name);
        return NULL;
    }
    
    ret = pthread_cond_init(&q->cond, NULL);
    if (ret != 0)
    {
        pthread_mutex_destroy(&q->mutex);
        free(q);
        LOG("mq cond init failed %s \n.", mq_name);
        return NULL;
    }    
    
    return q;
}

int mq_receive(mqd_t mqdes, void *msg, int msglen, unsigned int *prio)
{
    int length = 0;    
    struct mq_msg *msgq = NULL;

    pthread_mutex_lock(&mqdes->mutex);
    while (mqdes->msg_head.next == &mqdes->msg_head)
        pthread_cond_wait(&mqdes->cond, &mqdes->mutex);
    msgq = mqdes->msg_head.next;
    msgq->prev->next = msgq->next;
    msgq->next->prev = msgq->prev;
    
    length = msgq->length;
    if (length > msglen)
        length = msglen;

    mqdes->msgs_count--;
    mqdes->msgs_total_bytes -= msgq->length;

    pthread_mutex_unlock(&mqdes->mutex);

    memcpy(msg, msgq->buff, length);

    free(msgq->buff);
    free(msgq);
    return length;
}

int mq_send(mqd_t mqdes, const void *msg, int msglen, unsigned int prio)
{
    /* 准备数据 */
    struct mq_msg *msgq = (struct mq_msg *)calloc(1, sizeof(struct mq_msg));
    if (msgq == NULL)
        return -4;
    
    msgq->buff = (char *)calloc(1, msglen);
    if (msgq->buff == NULL)
    {
        free(msgq);
        return -5;
    }
    memcpy(msgq->buff, msg, msglen);    
    msgq->length = msglen;

    /* 判断消息队列是否满 */
    pthread_mutex_lock(&mqdes->mutex);
    int msgs_count = mqdes->msgs_count + 1;
    int msgs_total_bytes = mqdes->msgs_total_bytes + msglen;

    if (msgs_count > mqdes->maxmsgs
        || msgs_total_bytes > mqdes->msgs_max_bytes
        || msglen > mqdes->maxmsgsize)
    {
        pthread_mutex_unlock(&mqdes->mutex);
        free(msgq->buff);
        free(msgq);
        return -1;
    }
    
    mqdes->msgs_count = msgs_count;
    mqdes->msgs_total_bytes = msgs_total_bytes;

    /* 节点放入链表头，唤醒等待的消费者 */    
    msgq->prev = mqdes->msg_head.prev;
    msgq->next = &mqdes->msg_head;
    mqdes->msg_head.prev->next = msgq;
    mqdes->msg_head.prev = msgq;
    pthread_cond_signal(&mqdes->cond);
    pthread_mutex_unlock(&mqdes->mutex);

    return 0;
}

int mq_load_warning(mqd_t mqdes)
{
    pthread_mutex_lock(&mqdes->mutex);

    int ret = (mqdes->msgs_count > (mqdes->maxmsgs / 2) 
            || mqdes->msgs_total_bytes > (mqdes->msgs_max_bytes / 2));

    pthread_mutex_unlock(&mqdes->mutex);

    return ret;
}
