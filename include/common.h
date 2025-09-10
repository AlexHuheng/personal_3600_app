#ifndef COMMON_H
#define COMMON_H

#define ARRAY_SIZE(A) (sizeof(A)/sizeof(A[0]))

#define TIMER_ADD(a, b, result) \
    do {    \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;   \
        (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;    \
        if ((result)->tv_usec >= 1000000)   \
        {   \
            ++(result)->tv_sec; \
            (result)->tv_usec -= 1000000;   \
        }   \
    } while (0)

#define TIMER_SUB(a, b, result) \
    do {    \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;   \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;    \
        if ((result)->tv_usec < 0) {    \
          --(result)->tv_sec;   \
          (result)->tv_usec += 1000000; \
        }   \
    } while (0)

#define TIMER_X_USEC_SUB(x, a, b, result)   \
    {   \
        long long tmp_usec = x*1000000-(((long long)(a)->tv_sec*1000000+(a)->tv_usec) - ((long long)(b)->tv_sec*1000000+(b)->tv_usec));    \
        tmp_usec = tmp_usec>0 ? tmp_usec : 0; \
        (result)->tv_sec = tmp_usec / 1000000;  \
        (result)->tv_usec = tmp_usec % 1000000; \
    }

#define TIMER_MX_USEC_SUB(x, a, b, result)   \
    {   \
        long long tmp_usec = x*1000-(((long long)(a)->tv_sec*1000000+(a)->tv_usec) - ((long long)(b)->tv_sec*1000000+(b)->tv_usec));    \
        tmp_usec = tmp_usec>0 ? tmp_usec : 0; \
        (result)->tv_sec = tmp_usec / 1000000;  \
        (result)->tv_usec = tmp_usec % 1000000; \
    }

#define APP_DIR     "/root/maccura/app/"
#define LOG_DIR     "/root/maccura/log/" 

#define LOG_FILE    LOG_DIR"h3600_app.log"
#define RTOS_FILE   APP_DIR"RTOSDemo.bin"
#define LOAD_IMAGE_FILE "/usr/bin/load_image"
#define COREDUMP_FILE   "core.dump"

#endif

