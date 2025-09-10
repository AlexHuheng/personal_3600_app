#ifndef TIMER_H
#define TIMER_H

typedef void (*timer_callback)(void *arg);
int timer_init(void);
int timer_start(timer_callback cb, void *user_data, int after_ms, int repeat_ms);
int timer_stop(int td);

#endif
