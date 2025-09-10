#include <stdio.h>
#include <stdlib.h>
#include <ev.h>
#include <pthread.h>
#include <float.h>

#include <timer.h>
#include <log.h>
#include <list.h>



typedef struct {
    ev_timer *timer;
    timer_callback cb;
    void *user_data;
    struct list_head sibling;
}timer_callback_t;

static struct list_head timer_list;
static struct ev_loop *timer_loop = NULL;
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TIMER_LOCK() do {} while (0 != pthread_mutex_lock(&timer_mutex))
#define TIMER_UNLOCK() do {} while (0 != pthread_mutex_unlock(&timer_mutex))

static void timer_idle(void *arg)
{
    
}

static void *timer_thread(void *arg)
{    
    timer_start(timer_idle, NULL, 10000, 10000); /* 空转函数, 防止ev_run退出 */
    ev_run(timer_loop, 0);
    return NULL;
}

static void timer_del(ev_timer *timer)
{
    timer_callback_t *pos, *n;
    TIMER_LOCK();
    list_for_each_entry_safe(pos, n, &timer_list, sibling) {
        if (pos->timer == timer) {
            ev_timer_stop(timer_loop, timer);
            list_del(&pos->sibling);
            free(pos->timer);
            free(pos);
            pos->timer = NULL;
            pos = NULL;
            break;
        }
    }
    TIMER_UNLOCK();
}

static void timer_callback_(EV_P_ ev_timer *timer, int revents)
{
    timer_callback_t *tcb = timer->data;
    if (tcb->cb) {
        tcb->cb(tcb->user_data);
    }

    /* 不重复 */
    if (timer !=NULL && timer->repeat >= -DBL_EPSILON && timer->repeat <= DBL_EPSILON) {    
        timer_del(timer);
    }
}

/* 
    慎用此定时器的单次定时。此定时器是 队列阻塞执行callback（cb1执行完，才执行cb2...cbx）,
    即当某个cb执行耗时较长时，会导致其它cb无法执行
*/
int timer_start(timer_callback cb, void *user_data, int after_ms, int repeat_ms)
{
    int td;
    double after, repeat;
    ev_timer *timer = (ev_timer *)calloc(1, sizeof(ev_timer));
    timer_callback_t *tcb = (timer_callback_t *)calloc(1, sizeof(timer_callback_t));

    after = (double)after_ms / (double)1000;
    repeat = (double)repeat_ms / (double)1000;
    
    td = (int)timer;
    tcb->cb = cb;
    tcb->user_data = user_data;
    tcb->timer = timer;
    timer->data = tcb;
    ev_timer_init(timer, timer_callback_, after, repeat);
    TIMER_LOCK();
    ev_timer_start(timer_loop, timer);
    list_add_tail(&tcb->sibling, &timer_list);
    TIMER_UNLOCK();
    
    return td;
}

int timer_stop(int td)
{
    ev_timer *timer = (ev_timer *)td;
    timer_del(timer);
    return 0;
}

int timer_init(void)
{
    pthread_t tid;
    timer_loop = ev_loop_new(0);
    INIT_LIST_HEAD(&timer_list);    
    pthread_create(&tid, NULL, timer_thread, NULL);
    return 0;
}
