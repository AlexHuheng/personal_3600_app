#ifndef H3600_MAINTAIN_UTILS_H
#define H3600_MAINTAIN_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#define STARTUP_TIMES_C_X       0.8
#define STARTUP_TIMES_C_Z       0.4
#define STARTUP_TIMES_S_X       0.8
#define STARTUP_TIMES_S_Z       0.6
#define CATCHER_LONG_DISTANCE   10000

typedef enum
{
    MAINTAIN_NORMAL_CLEAN = 0,
    MAINTAIN_SPECIAL_CLEAN,
    MAINTAIN_PIPE_LINE_PREFILL,
}maintain_needle_clean_flag_t;

int get_power_off_stat();
int instrument_self_check(uint8_t cup_clear_flag, uint8_t liquid_detect_remain_flag);
void manual_stop_async_handler(void * arg);
int emergency_stop(void);
void machine_maintence_state_set(int sta);
int machine_maintence_state_get();
int emergency_stop_maintain(void);
int power_on_maintain(void);
int power_off_maintain(void);
int power_down_maintain(int temp_regent_table);
int pipeline_fill_maintain(void);
void exit_program(void *arg);
int reset_all_motors_maintain();
int remain_detect_prepare(void);
int remain_detect_done(void);
int check_all_board_connect(void);
int get_shutdown_temp_regent_flag(void);
void upper_start_active_machine(void *arg);

#ifdef __cplusplus
}
#endif

#endif

