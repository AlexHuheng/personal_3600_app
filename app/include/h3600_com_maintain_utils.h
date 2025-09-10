#ifndef H3600_COM_MAINTAIN_UTILS_H
#define H3600_COM_MAINTAIN_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAINTAIN_CMD_RESET              101
#define MAINTAIN_CMD_REAGENT_SCAN       102
#define MAINTAIN_CMD_REAGENT_MIX        103
#define MAINTAIN_CMD_DROP_CUPS          104
#define MAINTAIN_CMD_POWEROFF           105
#define MAINTAIN_CMD_PIPELINE_FILL      106
#define MAINTAIN_CMD_PIPELINE_CLEAN     107
#define MAINTAIN_CMD_S_NOR_CLEAN        108
#define MAINTAIN_CMD_R2_NOR_CLEAN       109
#define MAINTAIN_CMD_S_SPEC_CLEAN       110
#define MAINTAIN_CMD_R2_SPEC_CLEAN      111
#define MAINTAIN_CMD_S_SOAK             112
#define MAINTAIN_CMD_R2_SOAK            113
#define MAINTAIN_CMD_CHECKIO            114
#define MAX_MAINTAIN_CNT                20

#define S_COM_RESET                     BIT(0)
#define S_COM_REAGENT_SCAN              BIT(1)
#define S_COM_REAGENT_MIX               BIT(2)
#define S_COM_DROP_CUPS                 BIT(3)
#define S_COM_POWEROFF                  BIT(4)
#define S_COM_PIPELINE_FILL             BIT(5)
#define S_COM_PIPELINE_CLEAN            BIT(6)
#define S_COM_S_NOR_CLEAN               BIT(7)
#define S_COM_R2_NOR_CLEAN              BIT(8)
#define S_COM_S_SPEC_CLEAN              BIT(9)
#define S_COM_R2_SPEC_CLEAN             BIT(10)
#define S_COM_S_SOAK                    BIT(11)
#define S_COM_R2_SOAK                   BIT(12)
#define S_COM_CHECKIO                   BIT(13)

#define S_COM_MSK (S_COM_RESET | S_COM_REAGENT_SCAN | S_COM_REAGENT_MIX | S_COM_DROP_CUPS \
                  | S_COM_POWEROFF | S_COM_PIPELINE_FILL | S_COM_PIPELINE_CLEAN | S_COM_S_NOR_CLEAN | S_COM_R2_NOR_CLEAN \
                  | S_COM_S_SPEC_CLEAN | S_COM_R2_SPEC_CLEAN | S_COM_S_SOAK | S_COM_R2_SOAK | S_COM_CHECKIO)


typedef struct {
    int item_id;    /* 维护项ID，详见《维护单项目.xlsx》中ID定义 */
    int param; /* 参数 -1.无效 */
}com_maintain_param_t;

typedef struct {
    int size;
    com_maintain_param_t com_maintains[MAX_MAINTAIN_CNT];
}com_maintain_t;

typedef struct {
    int8_t reset_motors;
    int8_t reag_scan;
    int8_t reag_mix;
    int8_t drop_cups;
    int8_t poweroff;
    int8_t pipeline_fill;
    int8_t pipeline_clean;
    int8_t s_nor_clean;
    int8_t r2_nor_clean;
    int8_t s_spec_clean;
    int8_t r2_spec_clean;
    int8_t s_soak;
    int8_t r2_soak;
    int8_t check_io;
}com_maintain_cnt_t;

int get_com_maintain_size();
void set_com_maintain_size(int size);
void set_com_maintain_param(int i, int item_id, int param);
void com_maintain_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif

