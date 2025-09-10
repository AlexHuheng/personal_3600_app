#ifndef __MODULE_CATCHER_MOTOR_H__
#define __MODULE_CATCHER_MOTOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#define CATCHER_MOTOR_POS_INIT      -1200
#define CATCHER_MOTOR_OPEN_MAX      10000
#define CATCHER_MOTOR_OPEN_STEP     2800
#define CATCHER_MOTOR_CLOSE_STEP    -2800

#define CATCHER_MOTOR_OPEN_MAX_STEP     60
#define CATCHER_MOTOR_OPEN_MIN_STEP     -60

#define CATCHER_MOTOR_CLOSE_MAX_STEP    2660
#define CATCHER_MOTOR_CLOSE_MIN_STEP    1400

typedef enum
{
    CATCHER_MOTOR_OPEN = 0,
    CATCHER_MOTOR_CLOSE = 1,
    CATCHER_MOTOR_AUTO_CLOSE = 2,
}catcher_motor_ctl_t;

int catcher_motor_set_encoder(int value);
int catcher_motor_get_encoder(void);
int catcher_motor_ctl(catcher_motor_ctl_t ctl);
int check_catcher_motor_status(catcher_motor_ctl_t ctl);
int catcher_motor_init(void);

#ifdef __cplusplus
}
#endif

#endif

