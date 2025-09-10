#ifndef LIB_LOG_H_
#define LIB_LOG_H_
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <elog.h>

#if 0
#define time_before(a,b) ((a) - (b) >= (1 << 30))

#define LOG_EMERG     0  /* System is unusable */
#define LOG_ALERT     1  /* Action must be taken immediately */
#define LOG_CRIT      2  /* Critical conditions */
#define LOG_ERR       3  /* Error conditions */
#define LOG_WARNING   4  /* Warning conditions */
#define LOG_NOTICE    5  /* Normal, but significant, condition */
#define LOG_INFO      6  /* Informational message */
#define LOG_DEBUG     7  /* Debug-level message */

typedef int (*dump_fun)(unsigned char *buff, int length);

static inline void _log(const char *format, ...)
{
    va_list         ap;
    char            buf[1024];
    struct timeval tv;
    struct tm tm;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    printf("%02d:%02d:%02d.%.03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec / 1000);
    
    va_start(ap, format);
    memset(buf, 0, sizeof(buf));
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    printf("%s", buf);
}

#define LOG(format, ...)\
    _log("[info]%s %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define LOG_WARN(format, ...)\
    _log("[warn]%s %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define LOG_ERROR(format, ...)\
    _log("[error]%s %d: " format, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define LOG_RAW(format, ...)\
    _log(format, ##__VA_ARGS__)
#else
#define LOG(...)\
    log_i(__VA_ARGS__)

#define LOG_WARN(...)\
    log_w(__VA_ARGS__)

#define LOG_ERROR(...)\
    log_e(__VA_ARGS__)

#define LOG_DEBUG(...)\
    log_d(__VA_ARGS__)

#define LOG_QUIET(...)\
    log_a(__VA_ARGS__)

#endif

#endif // LIB_LOG_H_

