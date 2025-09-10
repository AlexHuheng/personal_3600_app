#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#include <log.h>
#include <ipi.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <linux/netlink.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

int mem_fd = -1;
unsigned int *ipi_base;
unsigned int *ipi_trig_base;

#define SOCFPGA_GIC_DIC 0xfffed000
#define GIC_DIST_SOFTINT 0xf00

static int s_ipi_start_flag = 0;

#define NETLINK_TEST    	30
#define MSG_LEN            	125
#define MAX_PLOAD        	125

static struct sockaddr_nl daddr;

typedef struct _user_msg_info
{
    struct nlmsghdr hdr;
    char  msg[MSG_LEN];
} user_msg_info;

int mem_map(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        LOG(" failed to open /dev/mem\n");
        return -1;
    }

    ipi_base = mmap(NULL, 256 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, IPI_BASE);
    ipi_trig_base = mmap(NULL, 256, PROT_READ | PROT_WRITE, MAP_SHARED, fd, SOCFPGA_GIC_DIC);

    if (ipi_base == MAP_FAILED || ipi_trig_base == MAP_FAILED) {
        LOG("mmap failed %p, %p\n", ipi_base, ipi_trig_base);
        return -1;
    }

    LOG("mmap %p, %p\n", ipi_base, ipi_trig_base);
    
    return 0;
}

int mem_umap(void)
{
    munmap(ipi_base, 256 * 1024);
    munmap(ipi_trig_base, 256);
    
    close(mem_fd);

    return 0;
}

int ipi_trigger(void)
{
    *(ipi_trig_base + GIC_DIST_SOFTINT / 4) = (2 << 16) | IPI_AMP_RTOS;
    return 0;
}

int ipi_send(int fd, char *send_buf, int data_len)
{
    int ret = 0;
    int count = 0;
    ipi_t *ipi = (ipi_t *)ipi_base;
    char *pbuff = send_buf;

    if (!s_ipi_start_flag) {
        return 0;
    }

    //LOG("\n Begin send----\n");
    
    while (1) {
        if (!ring_buff_full(&ipi->ipi_buff_0to1)) {
            ring_buff_insert(&ipi->ipi_buff_0to1, *pbuff);
            ipi_trigger();
            pbuff++;
            count++;
            
            //LOG("send count:%d\n", count);
            
            if (count >= data_len) {
                ret = count; 
                break;
            }
        } else {
            LOG_ERROR("ipi->ipi_buff_0to1 is full!\n");
        }
    }

    return ret;
}


int ipi_recv(int fd, char *rcv_buf, int data_len)
{
    int ret;
    user_msg_info u_info;
    socklen_t len;
    int count = 0;
    int i = 0;
    unsigned char ch;
    ipi_t *ipi = (ipi_t *)ipi_base;
    char *pbuff = rcv_buf;

    if (!s_ipi_start_flag) {
        return 0;
    }

    memset(&u_info, 0, sizeof(u_info));
    len = sizeof(struct sockaddr_nl);
    ret = recvfrom(fd, &u_info, sizeof(user_msg_info), 0, (struct sockaddr *)&daddr, &len);
    if (!ret) {
        LOG("recv form kernel error\n");
        return 0;
    }
	
    for (i  = 0; i < data_len; i++) {
        if (!ring_buff_empty(&ipi->ipi_buff_1to0)) {
            ring_buff_output(&ipi->ipi_buff_1to0, &ch);
            *pbuff = ch;
            pbuff++;
            count++;
        }
    }

    return count;
}

int ipi_start(void)
{
    ipi_t *ipi = (ipi_t *)ipi_base;

    while (ipi->ipi_block_magic != IPI_BLOCK_MAGIC) {
        sleep(1);
        LOG("wait ipi connect...\n");
    }

    return 1;
}

int ipi_init(void)
{
    int skfd;
    struct sockaddr_nl saddr;

    mem_map();

    /* 创建NETLINK socket */
    skfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_TEST);
    if (skfd == -1) {
        LOG("create socket error, %s\n", strerror(errno));
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.nl_family = AF_NETLINK; //AF_NETLINK
    saddr.nl_pid = 100;  //端口号(port ID) 
    saddr.nl_groups = 0;
    if (bind(skfd, (struct sockaddr *)&saddr, sizeof(saddr)) != 0) {
        LOG("bind() error, %s\n", strerror(errno));
        close(skfd);
        return -1;
    }

    memset(&daddr, 0, sizeof(daddr));
    daddr.nl_family = AF_NETLINK;
    daddr.nl_pid = 0; // to kernel 
    daddr.nl_groups = 0;

    /* 开启核间通信 */
    s_ipi_start_flag = ipi_start();

    return skfd;
}
