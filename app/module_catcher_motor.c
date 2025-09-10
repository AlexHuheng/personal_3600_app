#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include "lowlevel.h"
#include "crc.h"
#include "log.h"
#include "module_common.h"
#include "movement_config.h"
#include "module_catcher_motor.h"

static int get_catcher_motor_curr_step(void)
{
    return encoder_get_value(CATCHER_ENCODER_ID, 2*1000);
}

int catcher_motor_set_encoder(int value)
{
    return encoder_set_value(CATCHER_ENCODER_ID, value, 2*1000);
}

int catcher_motor_get_encoder(void)
{
    return get_catcher_motor_curr_step();
}

int check_catcher_motor_status(catcher_motor_ctl_t ctl)
{
    int curr_steps = 0;
    curr_steps = get_catcher_motor_curr_step();
    LOG("ctl = %d, curr_steps = %d\n", ctl, curr_steps);
    if (ctl == CATCHER_MOTOR_OPEN) {
        if (curr_steps >= CATCHER_MOTOR_OPEN_MAX_STEP || curr_steps <= CATCHER_MOTOR_OPEN_MIN_STEP) {
            return 1;
        } else {
            return 0;
        }
    } else {
        if (curr_steps >= CATCHER_MOTOR_CLOSE_MIN_STEP && curr_steps <= CATCHER_MOTOR_CLOSE_MAX_STEP) {
            return 1;
        } else {
            return 0;
        }
    }
    return 0;
}


int catcher_motor_ctl(catcher_motor_ctl_t ctl)
{
    int curr_steps = 0;
    curr_steps = get_catcher_motor_curr_step();
    LOG("ctl = %d, curr_steps = %d\n", ctl, curr_steps);
    switch (ctl) {
    case CATCHER_MOTOR_CLOSE:
        motor_move_sync(MOTOR_CATCHER_MOTOR, CMD_MOTOR_MOVE_STEP, CATCHER_MOTOR_CLOSE_STEP, h3600_conf_get()->motor[MOTOR_CATCHER_MOTOR].speed, 2*1000);
        break;
    case CATCHER_MOTOR_AUTO_CLOSE:
        motor_move_sync(MOTOR_CATCHER_MOTOR, CMD_MOTOR_MOVE_STEP, CATCHER_MOTOR_CLOSE_STEP, h3600_conf_get()->motor[MOTOR_CATCHER_MOTOR].speed, 2*1000);
        break;
    case CATCHER_MOTOR_OPEN:
        motor_move_sync(MOTOR_CATCHER_MOTOR, CMD_MOTOR_MOVE_STEP, curr_steps, h3600_conf_get()->motor[MOTOR_CATCHER_MOTOR].speed, 2*1000);
        break;
    default:
        break;
    }

    return 0;
}

int catcher_motor_init(void)
{
    motor_move_sync(MOTOR_CATCHER_MOTOR, CMD_MOTOR_MOVE_STEP, CATCHER_MOTOR_OPEN_MAX, h3600_conf_get()->motor[MOTOR_CATCHER_MOTOR].speed, 2*1000);
    motor_move_sync(MOTOR_CATCHER_MOTOR, CMD_MOTOR_MOVE_STEP, CATCHER_MOTOR_POS_INIT, h3600_conf_get()->motor[MOTOR_CATCHER_MOTOR].speed, 2*1000);
    catcher_motor_set_encoder(0);
    return 0;
}

