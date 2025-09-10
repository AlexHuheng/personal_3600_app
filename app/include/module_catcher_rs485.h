#ifndef __MODULE_CATCHER_RS485_H__
#define __MODULE_CATCHER_RS485_H__

#ifdef __cplusplus
extern "C" {
#endif

#define CATCHER_CLOSE_CTL 0x4A
#define CATCHER_OPEN_CTL  0x0

#define CATCHER_CLOSE_AUTO_CTL 0x64  /* 自动标定时需设定最大行程至100 */

#define CATCHER_CLOSE_MIN_PER   32
#define CATCHER_CLOSE_MAX_PER   72

typedef enum
{
    CATCHER_OPEN = 0,
    CATCHER_CLOSE = 1,
    CATCHER_AUTO_CLOSE = 2,
    CATCHER_AUTO_CLOSE_X = 3,
}catcher_ctl_t;

int catcher_rs485_init(void);
int catcher_ctl(catcher_ctl_t ctl);
int catcher_release(void);
int catcher_set_low_curr(void);
int get_catcher_curr_step(void);
int check_catcher_status(void);

#ifdef __cplusplus
}
#endif

#endif

