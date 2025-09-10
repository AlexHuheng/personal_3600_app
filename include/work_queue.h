#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*work_queue_function)(void *user_data);

int work_queue_init(void);
int work_queue_deinit(void);
int work_queue_add(work_queue_function function, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
