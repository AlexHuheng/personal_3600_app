#include <misc_log.h>

typedef struct {
    int fd;
    int msize;          /* 日志最大限制 */
    int cursize;        /* record position after log size > msize,new log will be wrote from start  */
    char cnt;           /* check log size each 10 times */
    char reverted:1;    /* marked log is reverted to start */
    char *name;
    char *name1;
} log_cycle_t;

static log_cycle_t glog[NEEDLE_TYPE_MAX] = {
    {
        .fd         = -1,
        .msize      = MAX_LOG_SIZE,
        .name       = LIQUID_SLOG_PATH,
        .name1      = LIQUID_SLOG_PATH1,
    },
    {
        .fd         = -1,
        .msize      = MAX_LOG_SIZE,
        .name       = LIQUID_R1LOG_PATH,
        .name1      = LIQUID_R1LOG_PATH1,
    },
    {
        .fd         = -1,
        .msize      = MAX_LOG_SIZE,
        .name       = LIQUID_R2LOG_PATH,
        .name1      = LIQUID_R2LOG_PATH1,
    },
    {
        .fd         = -1,
        .msize      = MAX_LOG_SIZE,
        .name       = PRESSURE_LOG_PATH,
        .name1      = PRESSURE_LOG_PATH1,
    },
    {
        .fd         = -1,
        .msize      = MAX_LOG_SIZE,
        .name       = TMP_LOG_PATH,
        .name1      = TMP_LOG_PATH1,
    }
};

static log_cycle_t g_clot_log = {.fd = -1, .msize = MAX_LOG_SIZE - 256 * 1024, .name = CLOT_DATA_PATH, .name1 = CLOT_DATA_PATH1};

/* return file size by given file path or fd */
static int get_file_size(char *file_path, int fd)
{
    int filesize    = -1;
    struct stat statbuff = {0};

    if (file_path) {
        if(stat(file_path, &statbuff) == 0)
            filesize = (int)statbuff.st_size;
    } else {
        if(fstat(fd, &statbuff) == 0)
            filesize = (int)statbuff.st_size;
    }

    return filesize;
}

static int misc_log_write_fd(int fd, char *data, int size)
{
    int rc = 0;
    int ret = -1;
    char *file = data;

    while (size > 0) {
        if ((rc = write(fd, file, size)) != size) {
            if ((errno != EINTR) && (errno != EAGAIN) && (errno != ECHILD)) {
                ret = -1;
                goto bad;
            }
        }

        if (rc > 0) {
            file += rc;
            size -= rc;
        }
    }

    ret = (int)(file - data);
bad:
    //fsync(fd);
    return ret;
}

static inline int misc_log_rcd_write(int type, char * data, size_t size)
{
    int filesize;
    log_cycle_t *gl = glog;

    #if 0
    if ((++gl[type].cnt % 10 == 0)) {
        if (gl[type].reverted) {
            if (gl[type].cursize > gl[type].msize) {
                lseek(gl[type].fd, 0, SEEK_SET);
                gl[type].cursize = 0;
            }
        } else if ((filesize = get_file_size(NULL, gl[type].fd)) > 0 && (int)filesize >= gl[type].msize) {
            LOG("testx SEEK_SET.\n");
            gl[type].cursize     = 0;
            gl[type].reverted    = 1;
            lseek(gl[type].fd, 0, SEEK_SET);
        }
        gl[type].cnt = 0;
    }
    if (gl[type].reverted)
    gl[type].cursize += (int)size;
    #else
    if ((++gl[type].cnt % 10 == 0)) {
        if ((filesize = get_file_size(NULL, gl[type].fd)) > 0 && (int)filesize >= gl[type].msize) {
            close(gl[type].fd);
            rename(gl[type].name, gl[type].name1);
            if ((gl[type].fd = open2(gl[type].name, O_WRONLY | O_CREAT, S_IRWXU)) == -1) {
                goto out;
            }
        }
    }
    #endif

    return misc_log_write_fd(gl[type].fd, data, size);
out:
    return -1;
}

void misc_log_do_va(int type, char *format, va_list *vlist)
{
    log_cycle_t *gl = glog;
    char buf[MAXLINE + 128] = {0};
    size_t n = 0;
    size_t n1 = 0;

    //n = snprintf(buf, MAXLINE, "%s", log_get_time());
    n1 = vsnprintf(buf + n, MAXLINE - n, format, *vlist);

    if (n1 >= (MAXLINE - n)) {
        n = MAXLINE - 1;
        n += snprintf(buf + n, sizeof(buf) - n, "<-data has been truncated->");
    } else {
        n += n1;
    }

    //buf[n++] = '\n';
    buf[n] = 0;

    if (gl[type].fd != -1) {
        misc_log_rcd_write(type, buf, n);
    }
}


void misc_log_do(int type, char *format, ...)
{
    va_list vlist;

    va_start(vlist, format);
    misc_log_do_va(type, format, &vlist);
    va_end(vlist);
}

/* isolation clot data write log function */
void misc_log_write(char *format, ...)
{
    log_cycle_t *gcl = &g_clot_log;
    char buf[MAXLINE + 128] = {0};
    size_t n = 0;
    size_t n1 = 0;
    int filesize;
    va_list vlist;

    va_start(vlist, format);
    n1 = vsnprintf(buf + n, MAXLINE - n, format, vlist);

    if (n1 >= (MAXLINE - n)) {
        n = MAXLINE - 1;
        n += snprintf(buf + n, sizeof(buf) - n, "<-data has been truncated->");
    } else {
        n += n1;
    }
    buf[n] = 0;

    if (gcl->fd != -1) {
        if ((++gcl->cnt % 10 == 0)) {
            if ((filesize = get_file_size(NULL, gcl->fd)) > 0 && (int)filesize >= gcl->msize) {
                close(gcl->fd);
                rename(gcl->name, gcl->name1);
                if ((gcl->fd = open2(gcl->name, O_WRONLY | O_CREAT, S_IRWXU)) == -1) {
                    return;
                }
            }
        }
    }
    misc_log_write_fd(gcl->fd, buf, n);
    va_end(vlist);
}


int misc_log_init(void)
{
    int i = 0;
    log_cycle_t *gl = glog;
    log_cycle_t *gcl = &g_clot_log;//增加凝块数据写入日志

    for (i = 0; i < NEEDLE_TYPE_MAX; i++) {
        if ((gl[i].fd = open1(gl[i].name, O_WRONLY | O_CREAT, S_IRWXU)) == -1) {
            LOG("misc_log_init %d-%s failed.\n", i, gl[i].name);
            continue;
        }
        lseek(gl[i].fd,  0, SEEK_END);
    }

    if ((gcl->fd = open1(gcl->name, O_WRONLY | O_CREAT, S_IRWXU)) == -1) {
        LOG("misc_log_init %d-%s failed.\n", i, gcl->name);
    }
    lseek(gcl->fd,  0, SEEK_END);

    return 0;
}
