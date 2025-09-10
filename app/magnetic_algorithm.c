#include "magnetic_algorithm.h"

static CLOT_DATA clot_param[MAGNETIC_CH_NUMBER] = {0};
static FILE *max_data_fd = NULL;
static FILE *mag_error_fd = NULL;

void log_mag_data(const char *format, ...)
{
    static int fd = -1;
    static char log_diy_enable = 0;

    if (fd == -1) {
        fd = open("/tmp/log_mag", O_RDONLY | O_CREAT | O_TRUNC);
    }

    read(fd, &log_diy_enable, 1);
    lseek(fd, 0, SEEK_SET);

    if (log_diy_enable == '1') {
        va_list args;

        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

CLOT_DATA* clot_param_get(void)
{
    return clot_param;
}

void clot_data_init(void)
{
    int i = 0;

    if (max_data_fd == NULL) {
        max_data_fd = fopen(MAX_DATA_FILE, "a");
        if (max_data_fd == NULL) {
            LOG("open mag data file fail\n");
        }
    }

    if (mag_error_fd == NULL) {
        mag_error_fd = fopen(MAG_ERROR_FILE, "a");
        if (mag_error_fd == NULL) {
            LOG("open mag error file fail\n");
        }
    }

    memset(clot_param, 0, sizeof(clot_param));
    for (i=0; i<sizeof(clot_param)/sizeof(CLOT_DATA); i++) {
        clot_param[i].min_time = CLOT_TIME_LOW_LIMIT;
        clot_param[i].max_time = CLOT_TIME_HIGH_LIMIT;
    }
}

void clot_data_free(CLOT_DATA *clot_data)
{
    if (clot_data->m_alldata) {
        free(clot_data->m_alldata);
        clot_data->m_alldata = NULL;
    }

    if (clot_data->m_alldata_clean) {
        free(clot_data->m_alldata_clean);
        clot_data->m_alldata_clean = NULL;
    }

    if (clot_data->m_data) {
        free(clot_data->m_data);
        clot_data->m_data = NULL;
    }

    if (clot_data->m_startdata) {
        free(clot_data->m_startdata);
        clot_data->m_startdata = NULL;
    }
}

static void clot_reset(CLOT_DATA *clot_data)
{
    uint8_t enable = clot_data->enable;
    float clot_percent = clot_data->clot_percent;
    uint32_t max_time = clot_data->max_time;
    uint32_t min_time = clot_data->min_time;
    int order_no = clot_data->order_no;
    int buff_len = 0;

    memset(clot_data, 0, sizeof(CLOT_DATA));
    clot_data->enable = enable;
    clot_data->clot_percent = clot_percent;
    clot_data->max_time = max_time;
    clot_data->min_time = min_time;
    clot_data->order_no = order_no;

    if ((clot_data->max_time*1000)/GET_MAG_DATA_INTERVAL < 3000/GET_MAG_DATA_INTERVAL) {
        buff_len = (3000/GET_MAG_DATA_INTERVAL+2000/GET_MAG_DATA_INTERVAL)*sizeof(int);
    } else {
        buff_len = ((clot_data->max_time*1000)/GET_MAG_DATA_INTERVAL + 2000/GET_MAG_DATA_INTERVAL)*sizeof(int);
    }

    clot_data->m_alldata = (int*)calloc(1, buff_len);
    clot_data->m_alldata_clean = (int*)calloc(1, buff_len);
    clot_data->m_data = (int*)calloc(1, buff_len);
    clot_data->m_startdata = (int*)calloc(1, buff_len);
}

static inline int calc_max(int *data, int data_cnt, int start_pos, int *max_pos)
{
    int data_max = 0;
    int i = 0;

    if (data_cnt < start_pos) {
        return 0;
    }

    for (i=start_pos; i<data_cnt; i++) {
        if (data[i] > data_max) {
            data_max = data[i];
            if (max_pos) {
                *max_pos = i;
            }
        }
    }

    return data_max;
}

static inline float calc_avg(int *data, int len)
{
    int i = 0;
    int total = 0;

    if (len <= 0) {
        return 0.0;
    }

    for (i=0; i<len; i++) {
        total = total + data[i];
    }

    return (float)(total/len);
}

static inline int Bubble(int *r, int n)
{
    int low = 0;
    int high = n - 1;
    int temp = 0;
    int j = 0;

    while (low < high) {
        for (j = low; j < high; ++j) {
            if (r[j] > r[j + 1]) {
                temp = r[j];
                r[j] = r[j + 1];
                r[j + 1] = temp;
            }
        }

        --high;
        for (j = high; j > low; --j) {
            if (r[j] < r[j - 1]) {
                temp = r[j];
                r[j] = r[j - 1];
                r[j - 1] = temp;
            }
        }
        ++low;
    }

    return 0;
}

static int save_origin_data(const CLOT_DATA *clot_data, const max_param_t *max_buff, int max_idx)
{
    int i = 0;
    int num1 = 0, num2 = 0;
    int idx = 0;
    FILE *tmp_fp = NULL;

    if (max_data_fd != NULL) {
        if (ftell(max_data_fd) > MAX_DATA_FILE_MAX_SIZE) {
            fclose(max_data_fd);
            if ((tmp_fp = fopen(MAX_DATA_FILE".0", "r")) != NULL) {
                fclose(tmp_fp);
                remove(MAX_DATA_FILE".0");
            }
            LOG("over mag data file, tmp_fp: %p\n", tmp_fp);
            rename(MAX_DATA_FILE, MAX_DATA_FILE".0");
            max_data_fd = fopen(MAX_DATA_FILE, "w");
            if (max_data_fd == NULL) {
                LOG("reopen mag data file fail\n");
                return -1;
            }
        }

        if (clot_data) {
            fprintf(max_data_fd, "origin_data\t");
            idx = 0;
            num1 = clot_data->m_alldata_cnt/SAVE_FILE_DATA_SIZE;
            num2 = clot_data->m_alldata_cnt%SAVE_FILE_DATA_SIZE;
            for (i=0; i<num1; i++) {
                fprintf(max_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                    clot_data->m_alldata[idx], clot_data->m_alldata[idx+1], clot_data->m_alldata[idx+2],
                    clot_data->m_alldata[idx+3], clot_data->m_alldata[idx+4], clot_data->m_alldata[idx+5]);
                    idx += SAVE_FILE_DATA_SIZE;
            }
            
            for (i=0; i<num2; i++) {
                fprintf(max_data_fd, "%d\t", clot_data->m_alldata[idx]);
                idx++;
            }

            fprintf(max_data_fd, "\n");
        }

        if (max_buff) {
            fprintf(max_data_fd, "origin_max\t");
            idx = 0;
            num1 = max_idx/SAVE_FILE_DATA_SIZE;
            num2 = max_idx%SAVE_FILE_DATA_SIZE;
            for (i=0; i<num1; i++) {
                fprintf(max_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                    max_buff[idx].data, max_buff[idx+1].data, max_buff[idx+2].data,
                    max_buff[idx+3].data, max_buff[idx+4].data, max_buff[idx+5].data);
                idx += SAVE_FILE_DATA_SIZE;
            }

            for (i=0; i<num2; i++) {
                fprintf(max_data_fd, "%d\t", max_buff[idx].data);
                idx++;
            }

            fprintf(max_data_fd, "\n");
        }
    }

    return 0;
}

static int save_clean_data(const CLOT_DATA *clot_data, const max_param_t *max_buff, int max_idx)
{
    int i = 0;
    int num1 = 0, num2 = 0;
    int idx = 0;

    if (max_data_fd != NULL) {
        if (max_buff) {
            fprintf(max_data_fd, "clean_max\t");
            idx = 0;
            num1 = max_idx/SAVE_FILE_DATA_SIZE;
            num2 = max_idx%SAVE_FILE_DATA_SIZE;
            for (i=0; i<num1; i++) {
                fprintf(max_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                    max_buff[idx].data, max_buff[idx+1].data, max_buff[idx+2].data,
                    max_buff[idx+3].data, max_buff[idx+4].data, max_buff[idx+5].data);
                idx += SAVE_FILE_DATA_SIZE;
            }

            for (i=0; i<num2; i++) {
                fprintf(max_data_fd, "%d\t", max_buff[idx+i].data);
            }

            fprintf(max_data_fd, "\n");
        }

        if (clot_data) {
            fprintf(max_data_fd, "clean_data\t");
            idx = 0;
            num1 = clot_data->m_alldata_cnt/SAVE_FILE_DATA_SIZE;
            num2 = clot_data->m_alldata_cnt%SAVE_FILE_DATA_SIZE;
            for (i=0; i<num1; i++) {
                fprintf(max_data_fd, "%d\t%d\t%d\t%d\t%d\t%d\t",
                    clot_data->m_alldata_clean[idx], clot_data->m_alldata_clean[idx+1], clot_data->m_alldata_clean[idx+2],
                    clot_data->m_alldata_clean[idx+3], clot_data->m_alldata_clean[idx+4], clot_data->m_alldata_clean[idx+5]);
                idx += SAVE_FILE_DATA_SIZE;
            }
            
            for (i=0; i<num2; i++) {
                fprintf(max_data_fd, "%d\t", clot_data->m_alldata_clean[idx]);
                idx++;
            }

            fprintf(max_data_fd, "\n");
        }

    }

    return 0;
}

static int save_mag_result(const CLOT_DATA *clot_data, float clot_time_bak, int detect_pos)
{
    struct timeval tv = {0};
    struct tm tm = {0};

    if (max_data_fd != NULL) {
        if (clot_data) {
            gettimeofday(&tv, NULL);
            localtime_r(&tv.tv_sec, &tm);
            
            fprintf(max_data_fd, "==time:%02d-%02d %02d:%02d:%02d, order:%d, detect_pos:%d, clot_time_back:%f, clot_time:%f==\n",
                tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                clot_data->order_no, detect_pos, clot_time_bak, clot_data->clot_time);
        }

        fflush(max_data_fd);
    }

    return 0;
}

static int save_error_data(const CLOT_DATA* clot_data, float clot_time_bak, int detect_pos)
{
    FILE *tmp_fp = NULL;
    struct timeval tv;
    struct tm tm;

    if (mag_error_fd != NULL) {
        if (ftell(mag_error_fd) > MAG_ERROR_FILE_MAX_SIZE) {
            fclose(mag_error_fd);
            if ((tmp_fp = fopen(MAG_ERROR_FILE".0", "r")) != NULL) {
                fclose(tmp_fp);
                remove(MAG_ERROR_FILE".0");
            }
            LOG("over mag error file, tmp_fp: %p\n", tmp_fp);
            rename(MAG_ERROR_FILE, MAG_ERROR_FILE".0");
            mag_error_fd = fopen(MAG_ERROR_FILE, "w");
            if (mag_error_fd == NULL) {
                LOG("reopen mag data file fail\n");
                return -1;
            }
        }

        if (clot_data) {
            gettimeofday(&tv, NULL);
            localtime_r(&tv.tv_sec, &tm);

            fprintf(mag_error_fd, "==time:%02d-%02d %02d:%02d:%02d, order:%d, detect_pos:%d, clot_time_back:%f, clot_time:%f, max_unusual:%d==\n",
                tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                clot_data->order_no, detect_pos, clot_time_bak, clot_data->clot_time, clot_data->max_unusual);
        }

        fflush(mag_error_fd);
    }

    return 0;
}

static int clean_data(CLOT_DATA *clot_data)
{
    int i = 0, j = 0;
    int zero_count = 0, up_flag = 0, down_flag = 0, zero_flag = 0;
    int buff_cnt = 0;
    int idx_start = 0;

    max_param_t *max_buff = NULL;
    int max_idx = 0;
    int *max_temp = NULL;

    float max_avg = 0.0, max_avg1 = 0.0, max_avg2 = 0.0; /* 全局峰值 */
    float avg_before = 0.0; /* 前局部峰值 */
    float avg_after = 0.0; /* 后局部峰值 */

    int avg_sacle_max = 5; /* 计算 前/后局部峰值时，动态扩大的最大次数 */
    int avg_calc_max = 2; /* 计算 前/后局部峰值时，取的交替个数 */

    max_buff = (max_param_t*)calloc(1, sizeof(max_param_t)*clot_data->m_alldata_cnt);
    max_temp = (int*)calloc(1, sizeof(int)*clot_data->m_alldata_cnt);

    if (!max_buff || !max_temp) {
        LOG("malloc fail:%p, %p\n", max_buff, max_temp);
        return -1;
    }

    /* 寻找峰值 */
    for (i=0; i<clot_data->m_alldata_cnt; i++) {
        if (clot_data->m_alldata_clean[i] <= 10) {/* 将小于等于10的数据规定为 一个波段的低位区间 */
            zero_count++;
        } else {
            zero_count = 0;
        }

        if (zero_count>1 || i==0) {
            zero_flag = 1;
        }

        if (zero_flag == 1 && clot_data->m_alldata_clean[i] > 10 && up_flag == 0) {
            up_flag = 1;
            zero_flag = 0;
        } else if (zero_flag == 1 && clot_data->m_alldata_clean[i] <= 10 && up_flag == 1) {
            down_flag = 1;
            zero_flag = 0;
        }

        //LOG("%d, %d, %d, %d, %d\n", clot_data->m_alldata_clean[i], zero_count, zero_flag, up_flag, down_flag);
        if (up_flag == 1) {
            buff_cnt++;
        }

        if (down_flag == 1) {
            idx_start = i-buff_cnt>=0 ? i-buff_cnt : 0;
            max_temp[max_idx] = max_buff[max_idx].data = calc_max(clot_data->m_alldata_clean, i, idx_start, &max_buff[max_idx].idx_max);
            max_buff[max_idx].idx_start = idx_start;
            max_buff[max_idx].idx_end = i;
            max_idx++;

            buff_cnt = 0;
            up_flag = 0;
            down_flag = 0;
            //zero_count = 0;
            zero_flag = 0;
        }

        /* 
            通过扫描m_data，检查曲线是否异常(如有气泡、撞杯壁)
            1. 扫描范围：启动数据开始计算位置 ~ 启动数据结束计算位置-2s
            2. 若m_data < 启动数据*0.5，则记一次异常
        */
        if (i > clot_data->start_index && i < clot_data->end_index-2000/GET_MAG_DATA_INTERVAL) {
            if (clot_data->m_data[i-500/GET_MAG_DATA_INTERVAL] < clot_data->m_max*0.5) {
                //LOG("scan m_data: %d, %d, %f, %d\n", i-500/GET_MAG_DATA_INTERVAL, clot_data->m_data[i-500/GET_MAG_DATA_INTERVAL], clot_data->m_max, clot_data->max_unusual);
                clot_data->max_unusual++;
            }
        }
    }

    /*
     计算全局峰值:
     方式1. 不排序，取中间时间点的交替平均(高矮峰)：若中间时间出现跳点，则大概率不准
     方式2. 升序后，取3/4、7/8处(理论上是高峰)的平均值
    */
#if 0
    /* 方式1 */
    max_avg1 = (max_temp[max_idx/2] + max_temp[(max_idx/2 + 2<max_idx)?(max_idx/2 + 2):(max_idx-1)]) / 2;
    max_avg2 = (max_temp[(max_idx/2 - 1>0)?(max_idx/2 - 1):0] + max_temp[(max_idx/2 + 1<max_idx)?(max_idx/2 + 1):(max_idx-1)]) / 2;
    max_avg = max_avg1 > max_avg2 ? max_avg1 : max_avg2;
#else
    /* 方式2 */
    Bubble(max_temp, max_idx);
    max_avg1 = max_temp[max_idx*3/4];
    max_avg2 = max_temp[max_idx*7/8];
    max_avg = (max_avg1+max_avg2) / 2;
#endif

    /*
       刚加完样立即采集磁珠数据，导致最开始的1000ms数据极其不稳定，
       因此将最开始的1000ms异常高值强制转化成 全局峰值（真实计算从2000ms开始） 
    */
    for (i=0; i<1000/GET_MAG_DATA_INTERVAL; i++) {
        if (clot_data->m_alldata_clean[i] > max_avg) {
            clot_data->m_alldata_clean[i] = max_avg;
        }
    }

//    /* 清洗原始数据 */
//    /* 修复 低值异常 */
//    for (i=0; i<max_idx; i++) {
//        for (j=1; j<avg_sacle_max; j++) {
//            if ((float)max_buff[i].data <= (i==0?max_buff[(i-2*j>0)?(i-2*j):0].data : max_buff[(i-2*j>0)?(i-2*j):0].data/2.0) &&
//                (float)max_buff[i].data <= max_buff[(i+2*j<max_idx)?(i+2*j):(max_idx-1)].data/2.0) {
//
//            } else {
//                break;
//            }
//        }
//
//        if (j >= avg_sacle_max) {
//            //LOG("clean low: i:%d, data:%d\n", i, max_buff[i].data);
//            max_buff[i].data = clot_data->m_alldata_clean[max_buff[i].idx_max] = (max_buff[(i-2>0)?(i-2):0].data + max_buff[(i+2<max_idx)?(i+2):(max_idx-1)].data) / 2;
//        }
//    }

    /* 修复 高值异常 */
    #if 0
    for (i=0; i<max_idx; i++) { /* 顺序分析 */
    #else
    for (i=max_idx-1; i>=0; i--) { /* 逆序分析 */
    #endif
        int before_scale = 1, after_scale = 1; /* 计算 前/后局部峰值的扩张值 */

        #if 0 /* 顺序分析   使用 */ 
        /* 峰值异常的预运算(为了减少运算量) */
        for (j=1; j<avg_sacle_max; j++) {
            if ((float)max_buff[i].data > max_buff[(i-2*j>0)?(i-2*j):0].data*1.2 ||
                (float)max_buff[i].data > max_buff[(i+2*j<max_idx)?(i+2*j):(max_idx-1)].data*1.2) {
                 break;
            }
        }
        if (j >= avg_sacle_max) {
            continue;
        }
        #else /* 逆序分析   使用 */
        float max_avg_2s = 0.0;
        int max_avg_2s_total = 0;
        int k = 0;

        /* 计算最近连续2s峰值的平均值, 2s区间大约有( 2000/(310/2) = 14 )个相邻峰值 */
        for (k=0; k<14; k++) {
            if (max_buff[(i-k>0)?(i-k):0].data >= max_buff[(i-k-1>0)?(i-k-1):0].data) {
                 max_avg_2s_total += max_buff[(i-k>0)?(i-k):0].data;
            } else {
                 max_avg_2s_total += max_buff[(i-k-1>0)?(i-k-1):0].data;
            }
        }
        max_avg_2s = max_avg_2s_total / (float)k;

        /* 当连续2s峰值较为稳定时，则退出数据清洗（当前峰值的前2s峰值的平均值 > 全局峰值*0.8） */
        if (max_avg_2s > max_avg*0.8) {
            LOG("2s data stable: i:%d, max_avg:%f, max_avg_2s:%f\n", i, max_avg, max_avg_2s);
            break;
        }
        #endif

        /* 根据本次扩张范围，计算 前/后局部峰值 */
        while (before_scale < avg_sacle_max && after_scale < avg_sacle_max) {
            int clean_flag = 0; /* 清洗状态 */
            int avg_before_total = 0, avg_after_total = 0;
            int max_avg_scale_flag = 0, avg_before_scale_flag = 0, avg_after_scale_flag = 0;

            /* 计算 前/后局部峰值(取周边两个交替局部峰值的算术平均) */
            for (j=1; j<=avg_calc_max; j++) {
                avg_before_total += max_buff[(i-2*j*before_scale>0)?(i-2*j*before_scale):0].data;
                avg_after_total += max_buff[(i+2*j*after_scale<max_idx)?(i+2*j*after_scale):(max_idx-1)].data;
            }
            avg_before = avg_before_total / (float)avg_calc_max;
            avg_after = avg_after_total /  (float)avg_calc_max;

            if ((float)max_buff[i].data > max_avg*1.2) {
                max_avg_scale_flag = 1;
            }

            if ((float)max_buff[i].data > avg_before*1.2) {
                avg_before_scale_flag = 1;
            }

            if ((float)max_buff[i].data > avg_after*1.2) {
                avg_after_scale_flag = 1;
            }

            //LOG("i:%d, before_scale:%d, after_scale:%d, data:%f, avg_before:%f, avg_after:%f, %f\n", i, before_scale, after_scale, (float)max_buff[i].data, avg_before*1.2, avg_after*1.2, max_avg*1.2);
            if (i == 0) {
                if (max_avg_scale_flag==1 || avg_after_scale_flag==1) {
                    clean_flag = 1;
                     //LOG("idx:%d, %d\n", max_buff[i].idx_end, max_buff[i].idx_start);
                    for (j=max_buff[i].idx_start; j<max_buff[i].idx_end; j++) {
                        if ((float)clot_data->m_alldata_clean[j] > max_avg || (float)clot_data->m_alldata_clean[j]>avg_after) {
                            clot_data->m_alldata_clean[j] = avg_after; /* 取 后平均值 */
                        }
                    }
                }
            } else {
                 if (max_avg_scale_flag==1 || (avg_before_scale_flag==1 && avg_after_scale_flag==1)) {
                     clean_flag = 1;
                     //LOG("idx:%d, %d\n", max_buff[i].idx_end, max_buff[i].idx_start);
                     for (j=max_buff[i].idx_start; j<max_buff[i].idx_end; j++) {
                         if ((float)clot_data->m_alldata_clean[j] > max_avg || ((float)clot_data->m_alldata_clean[j]>avg_before && (float)clot_data->m_alldata_clean[j]>avg_after)) {
                              clot_data->m_alldata_clean[j] = (avg_before*0.3 + avg_after*0.7)/1; /* 取 加权平均值 */
                         }
                     }
                 }
            }

            /* 根据清洗状态，自动扩大 前/后局部峰值的计算范围 */
            if (clean_flag == 0) {
                 if (avg_before_scale_flag == 0) {
                     before_scale++;
                 }

                 if (avg_after_scale_flag == 0) {
                     after_scale++;
                 }
            } else {
                //LOG("clean high: i:%d, before_scale:%d, after_scale:%d\n", i, before_scale, after_scale);
                break;
            }
        }
    }

    if (max_buff) {
        free(max_buff);
    }

    if (max_temp) {
        free(max_temp);
    }

    return 0;
}

/*
review_flag：0：数据清洗前的计算 1：数据清洗后的重新计算
*/
static void calc_clottime(CLOT_DATA *clot_data, int ad_data, int review_flag)
{
    int data_calc = 0;
    int startdata_max = 0;

    if (clot_data->enable == 0) {
        return ;
    } else {
        if (clot_data->start_time == 0) {
            clot_reset(clot_data);
            clot_data->status = 1;
            clot_data->start_time = time(NULL);
        }
    }

    /* 填充 所有数据队列 */
    if (review_flag == 0) {
        clot_data->m_alldata[clot_data->m_alldata_cnt] = ad_data;
        clot_data->m_alldata_clean[clot_data->m_alldata_cnt] = ad_data;
    }
    clot_data->m_alldata_cnt++;

    /* 填充 结果数据队列，前500ms数据不计入峰值 */
    if (clot_data->m_alldata_cnt < 500/GET_MAG_DATA_INTERVAL) {
        return ;
    }

    if (review_flag == 0) {
        data_calc = calc_max(clot_data->m_alldata, clot_data->m_alldata_cnt, clot_data->m_alldata_cnt-500/GET_MAG_DATA_INTERVAL, NULL);
    } else {
        data_calc = calc_max(clot_data->m_alldata_clean, clot_data->m_alldata_cnt, clot_data->m_alldata_cnt-500/GET_MAG_DATA_INTERVAL, NULL);
    }
    clot_data->m_data[clot_data->m_data_cnt++] = data_calc;

//    if (clot_data->startdata_flag == 1 && (float)data_calc < clot_data->m_max*0.80) {
//        clot_data->startdata_recalc = 1;
//    } else {
//        clot_data->startdata_recalc = 0;
//    }

    /* 填充 启动数据队列，并计算启动数据，第2000ms开始填充启动队列；第3000ms初次计算启动值，并判定是否起震 */
    if (clot_data->m_alldata_cnt>2000/GET_MAG_DATA_INTERVAL && clot_data->m_startdata_cnt<1000/GET_MAG_DATA_INTERVAL) {
        clot_data->m_startdata[clot_data->m_startdata_cnt++] = data_calc;
        clot_data->startdata_total += data_calc;
    } else if (clot_data->m_startdata_cnt==1000/GET_MAG_DATA_INTERVAL && clot_data->startdata_flag==0) {
        //clot_data->m_max = calc_avg(clot_data->m_startdata, clot_data->m_startdata_cnt);
        clot_data->m_max = (float)(clot_data->startdata_total/clot_data->m_startdata_cnt);
        clot_data->startdata_flag = 1;
        clot_data->start_index = clot_data->m_data_cnt;
        startdata_max = calc_max(clot_data->m_startdata, clot_data->m_startdata_cnt, 0, NULL);

        if (clot_data->m_max<NO_BEAD_THRESHOLD && startdata_max<NO_BEAD_THRESHOLD) { /* 异常警报： 没有磁珠或没有样本 */
            clot_data->alarm |= CLOT_ALARM_NOBEAD;
            LOG("CLOT_ALARM_NOBEAD, review:%d\n", review_flag);
        }
    } else if (clot_data->startdata_flag == 1 && clot_data->startdata_recalc==0) {
        clot_data->m_startdata[clot_data->m_startdata_cnt++] = data_calc;
        clot_data->startdata_total += data_calc;
        //clot_data->m_max = calc_avg(clot_data->m_startdata, clot_data->m_startdata_cnt);
        clot_data->m_max = (float)(clot_data->startdata_total/clot_data->m_startdata_cnt);
    }

    /* 计算   凝固时间 */
    if (clot_data->clot_percent>0 && clot_data->startdata_flag==1 && clot_data->clottime_flag==0) {
        log_mag_data("clot_data->m_max: %f,  %f, %f, %f; %d, %d, %d\n", 
            (float)data_calc, clot_data->m_max*clot_data->clot_percent, clot_data->m_max, clot_data->clot_percent,
            clot_data->m_alldata_cnt, clot_data->m_data_cnt, clot_data->clot_cnt);

        if ((float)data_calc <= clot_data->m_max*clot_data->clot_percent) {
            if (++clot_data->clot_cnt > 2000/GET_MAG_DATA_INTERVAL) {
                clot_data->clot_time = (float)(clot_data->m_data_cnt-clot_data->clot_cnt) * (GET_MAG_DATA_INTERVAL/1000.0) + 0.05;
                clot_data->clottime_flag = 1;
                clot_data->end_index = clot_data->m_data_cnt-clot_data->clot_cnt;
                LOG("clot_time:%f, review:%d\n", clot_data->clot_time, review_flag);
            }
        } else {
            clot_data->clot_cnt = 0;
        }
    }

    /* 检查 凝固时间 */
    if (clot_data->clottime_flag == 1) {
         if (clot_data->clot_time >= (float)clot_data->max_time) { /* 不凝 */
            clot_data->alarm |= CLOT_ALARM_HIGH;
            clot_data->status = 2;
            LOG("CLOT_ALARM_HIGH clot_time:%f, review:%d\n", clot_data->clot_time, review_flag);
        } else if (clot_data->clot_time <= (float)clot_data->min_time) { /* 强凝 */
            clot_data->alarm |= CLOT_ALARM_LOW;
            clot_data->status = 2;
            LOG("CLOT_ALARM_LOW clot_time:%f, review:%d\n", clot_data->clot_time, review_flag);
        } else {         /* 正常凝固 */
            clot_data->alarm |= CLOT_ALARM_NORMAL;
            clot_data->status = 2;
            LOG("CLOT_ALARM_NORMAL clot_time:%f, review:%d\n", clot_data->clot_time, review_flag);
        }
    } else {
        if (time(NULL)-clot_data->start_time > clot_data->max_time) {
            clot_data->alarm |= CLOT_ALARM_HIGH;
            clot_data->status = 2;
            LOG("CLOT_ALARM_HIGH(TIMEOUT) clot_time:%f, review:%d\n", clot_data->clot_time, review_flag);
        }
    }

    if (clot_data->alarm & CLOT_ALARM_NOBEAD) {
        clot_data->status = 2;
        LOG("CLOT_ALARM_NOBEAD(force finish) clot_time:%f, review:%d\n", clot_data->clot_time, review_flag);
    }
}

void calc_clottime_all(const uint16_t *data)
{
    int i = 0, j = 0;
    int review_cnt = 0;
    float clot_time_back = 0.0;
    uint8_t alarm_back = 0;
    int m_alldata_cnt_bak = 0;
    float error_thr = 0.0;

    for (i=0; i<MAGNETIC_CH_NUMBER; i++) {
//        LOG("ch:%d, %d, %d, %d\n", i, clot_param[i].enable, clot_param[i].status, clot_param[i].start_time);
        calc_clottime(&clot_param[i], (int)data[i], 0);

        if (clot_param[i].status == 2 && clot_param[i].enable == 1) {
            if (clot_param[i].alarm & (CLOT_ALARM_NORMAL | CLOT_ALARM_NOBEAD)) {/* 仅对正常凝固的 进行重新计算 */
                clot_time_back = clot_param[i].clot_time;
                alarm_back = clot_param[i].alarm;
                m_alldata_cnt_bak = clot_param[i].m_alldata_cnt;

                /* 保存清洗前的数据 */
                save_origin_data(&clot_param[i], NULL, 0);

                /* 清洗原始数据, 迭代CLEAN_DATA_NUMBER次 */
                for (j=0; j<CLEAN_DATA_NUMBER; j++) {
                    clean_data(&clot_param[i]);
                }

                /* 保存清洗后的数据 */
                save_clean_data(&clot_param[i], NULL, 0);

                /* 重新计算结果 */
                review_cnt = clot_param[i].m_alldata_cnt+2000/GET_MAG_DATA_INTERVAL; /* 追加2s数据 */
                clot_param[i].m_alldata_cnt = 0;
                clot_param[i].m_startdata_cnt = 0;
                clot_param[i].m_data_cnt = 0;
                clot_param[i].alarm = 0;

                clot_param[i].clottime_flag = 0;
                clot_param[i].startdata_flag = 0;
                clot_param[i].startdata_recalc = 0;

                clot_param[i].startdata_total = 0;
                clot_param[i].clot_cnt = 0;
                clot_param[i].m_max = 0.0;
                clot_param[i].status = 0;

                for (j=0; j<review_cnt; j++) {
                    calc_clottime(&clot_param[i], clot_param[i].m_alldata_clean[j], 1);
                    if (clot_param[i].status == 2) {
                        break;
                    }
                }

                /* 保存磁珠结果的数据 */
                save_mag_result(&clot_param[i], clot_time_back, i+1);

                /* 防止 清洗数据出现失误,因此需回滚计算结果 */
                if (clot_param[i].status != 2) {
                    LOG("mag clean data fail, back to before\n");
                    clot_param[i].alarm = alarm_back;
                    clot_param[i].clot_time = clot_time_back;
                    clot_param[i].status = 2;
                }

                /* 当曲线异常，强制将检测结果转为无钢珠，以触发上位机的再分析 */
                /* 根据凝固时间长短 动态选择曲线异常的阈值 */
                if (clot_time_back > 150.0) {
                    error_thr = 16.0;
                } else if (clot_time_back > 100.0) {
                    error_thr = 12.0;
                } else if (clot_time_back > 50.0) {
                    error_thr = 8.0;
                } else {
                    error_thr = 4.0;
                }

                if (/*clot_param[i].max_unusual > CLEAN_DATA_NUMBER*4 || */fabs(clot_param[i].clot_time-clot_time_back) > error_thr) {
                    LOG("error: mag curve unusual, force to nobead. max_unusual:%d, old alarm:0x%x, error_thr:%f\n", clot_param[i].max_unusual, clot_param[i].alarm, error_thr);
                    clot_param[i].alarm = CLOT_ALARM_NOBEAD;
                }
 
                /* 当前max_unusual异常时仅记录日志，暂不触发上位机的再分析 */
                if (clot_param[i].max_unusual > 0) {
                    LOG("warnning: mag curve unusual. order:%d, max_unusual:%d\n", clot_param[i].order_no, clot_param[i].max_unusual);
                    save_error_data(&clot_param[i], clot_time_back, i+1);
                }

                /* 上报 清洗前的全部原始数据 */
                clot_param[i].m_alldata_cnt = m_alldata_cnt_bak;
            }

            clot_param[i].enable = 0;
            clot_param[i].start_time = 0;
        }
    }
}

