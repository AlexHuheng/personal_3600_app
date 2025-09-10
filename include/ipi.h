#ifndef IPI_H_
#define IPI_H_

#define check_ret(__x_fun) do{\
    int __x_funret = __x_fun;\
    if (__x_funret != 0)\
    {\
        printf("[error][%s:%d][%d]\n", __FUNCTION__, __LINE__, __x_funret);\
        return __x_funret;\
    }\
}while(0)

struct ring_buff {
    int size;
    int head;
    int tail;
    unsigned char buff[512];
};

int ring_buff_count(struct ring_buff *rbuff)
{
    int count = rbuff->head - rbuff->tail;

    if (count < 0)
        count += rbuff->size;

    return count;
}

int ring_buff_empty(struct ring_buff *rbuff)
{
    return (rbuff->head == rbuff->tail);
}

int ring_buff_full(struct ring_buff *rbuff)
{
    int next_head = rbuff->head + 1;
    if (next_head >= rbuff->size)
        next_head = 0;

    return (next_head == rbuff->tail);
}

int ring_buff_insert(struct ring_buff *rbuff, unsigned char ch)
{
    int next_head = rbuff->head + 1;
    if (next_head >= rbuff->size)
        next_head = 0;
    
    if (next_head == rbuff->tail)
        return -1;
    
    rbuff->buff[rbuff->head] = ch;
    rbuff->head = next_head;
    return 0;
}

int ring_buff_output(struct ring_buff *rbuff, unsigned char *ch)
{
    int next_tail;
    if (rbuff->head == rbuff->tail)
        return -1;

    *ch = rbuff->buff[rbuff->tail];
    next_tail = rbuff->tail + 1;
    if (next_tail >= rbuff->size)
        next_tail = 0;
    rbuff->tail = next_tail;
    return 0;
}

typedef struct ipi_block {
    unsigned int ipi_block_magic;
    struct ring_buff ipi_buff_0to1;
    struct ring_buff ipi_buff_1to0;
}ipi_t;

#define IPI_BASE 0x54000000
#define IPI_BLOCK_MAGIC (0x12345678)
#define IPI_AMP_RTOS (8)		/* SGI8 */

#endif

