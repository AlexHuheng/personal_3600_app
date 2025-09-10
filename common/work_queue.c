#include <stdio.h>

#include <threadpool.h>
#include <work_queue.h>

#define WORK_QUEUE_SIZE     52
#define WORK_QUEUE_THREAD   30

threadpool_t *work_queue = NULL;

int work_queue_init(void)
{
    work_queue = threadpool_create(WORK_QUEUE_THREAD, WORK_QUEUE_SIZE, 0);
    return work_queue == NULL ? -1 : 0;
}

int work_queue_add(work_queue_function function, void *user_data)
{
    return !!threadpool_add(work_queue, function, user_data, 0);
}

int work_queue_deinit(void)
{
    return !!threadpool_destroy(work_queue, 0);
}