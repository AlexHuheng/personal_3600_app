#ifndef __MODULE_CATCHER_H__
#define __MODULE_CATCHER_H__

#ifdef __cplusplus
extern "C" {
#endif

#define CATCHER_AGING_TEST_OFFSET_X    27
#define CATCHER_AGING_TEST_OFFSET_Y    40
#define CATCHER_AGING_TEST_OFFSET_Z    (-40)
#define CATCHER_Z_MOVE_COMP_STEP       (-20)

typedef enum
{
    MAGNETIC_CUP_POS = 0,
    OPTICAL_CUP_POS,
    INCUBATION_CUP_POS,
}module_pos_t;

typedef enum
{
    NOT_USE = 0,
    IN_USE,
}pre_use_t;

typedef enum
{
    SPEED_NORMAL_MODE = 0,
    SPEED_FAST_MODE = 1,
}catcher_aging_speed_mode_t;

typedef enum
{
    OF_NORMAL_MODE = 0,
    OF_OFFSET_MODE = 1,
}catcher_offset_t;

void clear_exist_dilu_cup(void);
void set_clear_catcher_pos(uint8_t num);
int catcher_reset_standby(void);
int catcher_aging_test(int times, catcher_aging_speed_mode_t mode, int drop_cup_flag, catcher_offset_t offset);
int catcher_poweron_check(int times, int drop_cup_flag);
void catcher_motor_aging_test(void);
int catcher_init(void);

#ifdef __cplusplus
}
#endif

#endif

