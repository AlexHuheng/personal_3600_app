#ifndef __MISC_LOG_H__
#define __MISC_LOG_H__

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <h3600_cup_param.h>
#include <h3600_needle.h>
#include <log.h>

#define MAXLINE                         20480
#define MAX_LOG_SIZE                    (20 * 1024 * 1024)

#define LIQUID_SLOG_PATH                "/root/maccura/log/liquid_s.log"
#define LIQUID_SLOG_PATH1               "/root/maccura/log/liquid_s_old.log"
#define LIQUID_R1LOG_PATH               "/root/maccura/log/liquid_r1.log"
#define LIQUID_R1LOG_PATH1              "/root/maccura/log/liquid_r1_old.log"
#define LIQUID_R2LOG_PATH               "/root/maccura/log/liquid_r2.log"
#define LIQUID_R2LOG_PATH1              "/root/maccura/log/liquid_r2_old.log"
#define PRESSURE_LOG_PATH               "/root/maccura/log/pressure.log"
#define PRESSURE_LOG_PATH1              "/root/maccura/log/pressure_old.log"
#define TMP_LOG_PATH                    "/root/maccura/log/hil.log"
#define TMP_LOG_PATH1                   "/root/maccura/log/hil_old.log"

#define CLOT_DATA_PATH                  "/root/maccura/log/clot_data.log"
#define CLOT_DATA_PATH1                 "/root/maccura/log/clot_data_old.log"

#define open1(__p, __f, __m...) open(__p, __f | O_CLOEXEC | O_NONBLOCK, ##__m)
#define open2(__p, __f, __m...) open(__p, __f | O_CLOEXEC | O_NONBLOCK | O_TRUNC, ##__m)
void misc_log_do(int type, char *format, ...);
void misc_log_write(char *format, ...);

#define misc_log(type, fmt, arg...) ({        \
        misc_log_do(type, fmt, ##arg);        \
})

#define clot_write_log(fmt, arg...) ({   \
        misc_log_write(fmt, ##arg);      \
})

int misc_log_init(void);

#endif

