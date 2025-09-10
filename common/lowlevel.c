#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <sys/socket.h>
#include <linux/can/raw.h>
#include <termios.h>

#include <lowlevel.h>
#include <ipi_console.h>
#include <log.h>

static int safe_write(int fd, const void *buf, int count)
{
    int n;
    int repeat = 2;

    do {
        n = write(fd, buf, count);
        if (n < 0) {
            LOG("write error: %s\n", strerror(errno));
            usleep(10000);
            repeat--;
        }
    } while (n < 0 && repeat > 0);
    return n;
}

static int full_write(int fd, const void *buf, int len)
{
    int cc;
    int total;

    total = 0;

    while (len) {
        cc = safe_write(fd, buf, len);

        if (cc < 0) {
            if (total) {
            /* we already wrote some! */
            /* user can do another write to know the error code */
                return total;
            }
            return cc;  /* write() returns -1 on failure. */
        }

        total += cc;
        buf = ((const char *)buf) + cc;
        len -= cc;
    }

    return total;
}


int ll_can_open(const char *path, unsigned char can_id, unsigned int mask)
{
    struct ifreq ifr;
    struct sockaddr_can addr;
	
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);//创建套接字
    if (fd < 0) {
        LOG("create fd fail !");
        return -1;
    }
	
    strcpy(ifr.ifr_name, path);
    ioctl(fd, SIOCGIFINDEX, &ifr); //指定can设备
    if (!ifr.ifr_ifindex) {
        LOG("if_nametoindex");
        return -1;
    }	

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { //将套接字与can绑定
        LOG("bind\n");
        return -1;
    }

    return fd;
}

int ll_can_send(int fd, unsigned char can_id, const unsigned char *buff, int length)
{
    struct can_frame frame;
    int n = 0;
    int can_dlc;
    
    frame.can_id = can_id;

    while (n < length) {
        can_dlc = length - n > sizeof(frame.data) ? sizeof(frame.data) : length - n;
        memcpy(frame.data, &buff[n], can_dlc);
        frame.can_dlc = can_dlc;
        full_write(fd, &frame, sizeof(struct can_frame));
        n += can_dlc;
    }
    
    return n;
}

int ll_can_recv(int fd, unsigned char *can_id, unsigned char *buff, int size)
{
    int count;
    struct can_frame frame;

    count = read(fd, &frame, sizeof(struct can_frame)); 
    if (count <= 0) {
        LOG("ll_can_recv read fail, error: %s\n", strerror(errno));
        return -1;
    }

    memcpy(buff, frame.data, count);
    *can_id = frame.can_id;

    return frame.can_dlc;
}

int ll_ipi_open(const char *path)
{
	return ipi_init();
}

int ll_ipi_send(int fd, const unsigned char *buff, int length)
{
    return ipi_send(fd, (char *)buff, length);
}

int ll_ipi_recv(int fd, unsigned char *buff, int size)
{
    return ipi_recv(fd, (char *)buff, size);
}

/**@brief   设置串口参数：波特率，数据位，停止位和效验位
 * @param[in]  fd         类型  int      打开的串口文件句柄
 * @param[in]  nSpeed     类型  int     波特率
 * @param[in]  nBits      类型  int     数据位   取值 为 7 或者8
 * @param[in]  nParity    类型  int     停止位   取值为 1 或者2
 * @param[in]  nStop      类型  int      效验类型 取值为N,E,O,,S
 * @return     返回设置结果
 * - 0         设置成功
 * - -1     设置失败
 */
int setOpt(int fd, int nSpeed, int nBits, int nParity, int nStop)
{
    struct termios newtio, oldtio;

    // 保存测试现有串口参数设置，在这里如果串口号等出错，会有相关的出错信息
    if (tcgetattr(fd, &oldtio) != 0) {
        LOG("SetupSerial 1\n");
        return -1;
    }

    bzero(&newtio, sizeof(newtio));      //新termios参数清零
    newtio.c_cflag |= CLOCAL | CREAD;    //CLOCAL--忽略 modem 控制线,本地连线, 不具数据机控制功能, CREAD--使能接收标志

    // 设置数据位数
    newtio.c_cflag &= ~CSIZE;    //清数据位标志
    switch (nBits) {
        case 7:
            newtio.c_cflag |= CS7;
            break;
        case 8:
            newtio.c_cflag |= CS8;
            break;
        default:
            LOG("Unsupported data size\n");
            return -1;
    }

    // 设置校验位
    switch (nParity) {
        case 'o':
        case 'O':                     //奇校验
            newtio.c_cflag |= PARENB;
            newtio.c_cflag |= PARODD;
            newtio.c_iflag |= (INPCK | ISTRIP);
            break;

        case 'e':
        case 'E':                     //偶校验
            newtio.c_iflag |= (INPCK | ISTRIP);
            newtio.c_cflag |= PARENB;
            newtio.c_cflag &= ~PARODD;
            break;

        case 'n':
        case 'N':                    //无校验
            newtio.c_cflag &= ~PARENB;
            break;

        default:
            LOG("Unsupported parity\n");
            return -1;
    }

    // 设置停止位
    switch (nStop) {
        case 1:
            newtio.c_cflag &= ~CSTOPB;
            break;

        case 2:
            newtio.c_cflag |= CSTOPB;
            break;

        default:
            LOG("Unsupported stop bits\n");
            return -1;
    }

    // 设置波特率 2400/4800/9600/19200/38400/57600/115200/230400
    switch (nSpeed) {
        case 2400:
            cfsetispeed(&newtio, B2400);
            cfsetospeed(&newtio, B2400);
            break;

        case 4800:
            cfsetispeed(&newtio, B4800);
            cfsetospeed(&newtio, B4800);
            break;

        case 9600:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;

        case 19200:
            cfsetispeed(&newtio, B19200);
            cfsetospeed(&newtio, B19200);
            break;

        case 38400:
            cfsetispeed(&newtio, B38400);
            cfsetospeed(&newtio, B38400);
            break;

        case 57600:
            cfsetispeed(&newtio, B57600);
            cfsetospeed(&newtio, B57600);
            break;

        case 115200:
            cfsetispeed(&newtio, B115200);
            cfsetospeed(&newtio, B115200);
            break;

        case 230400:
            cfsetispeed(&newtio, B230400);
            cfsetospeed(&newtio, B230400);
            break;

        default:
            LOG("\tSorry, Unsupported baud rate, set default 9600!\n\n");
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;
    }

    // 设置read读取最小字节数和超时时间
    newtio.c_cc[VTIME] = 1;     // 读取一个字符等待1*(1/10)s
    newtio.c_cc[VMIN] = 1;        // 读取字符的最少个数为1

    tcflush(fd,TCIFLUSH);         //清空缓冲区
    if (tcsetattr(fd, TCSANOW, &newtio) != 0) {    //激活新设置
        LOG("SetupSerial 3");
        return -1;
    }
//      printf("Serial set done!\n");
    return 0;
}

int ll_uart_open_nonblock(const char *path, int speed, int flow_ctrl, int data_bits, int stop_bits, int parity)
{
    int fdSerial;

    // 打开串口设备
    fdSerial = open(path, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
    if (fdSerial < 0) {
        LOG("open %s fail, %s\n", path, strerror(errno));
        return -1;
    }

//	printf("open uart:%s success.\n", path);

//    // 设置串口阻塞， 0：阻塞， FNDELAY：非阻塞
//    if (fcntl(fdSerial, F_SETFL, FNDELAY) < 0)    //阻塞，即使前面在open串口设备时设置的是非阻塞的
//    {
//        LOG("fcntl failed!\n");
//    }
//    else
//    {
//        LOG("fcntl=%d\n", fcntl(fdSerial, F_SETFL, 0));
//    }

    if (isatty(fdSerial) == 0) {
        LOG("standard input is not a terminal device\n");
        close(fdSerial);
        return -1;
    } else {
//        printf("is a tty success!\n");
    }

//    printf("fd-open=%d\n", fdSerial);

    // 设置串口参数
    if (setOpt(fdSerial, speed, data_bits, parity, stop_bits)== -1) {   //设置8位数据位、1位停止位、无校验
        LOG("Set opt Error\n");
        close(fdSerial);
        return -1;
    }

    tcflush(fdSerial, TCIOFLUSH);    //清掉串口缓存
//    fcntl(fdSerial, F_SETFL, 0);    //串口阻塞

    return fdSerial;
}


int ll_uart_open(const char *path, int speed, int flow_ctrl, int data_bits, int stop_bits, int parity)
{
    int fdSerial;

    // 打开串口设备
    fdSerial = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
    if(fdSerial < 0) {
        LOG("open %s fail, %s\n", path, strerror(errno));
        return -1;
    }

//	printf("open uart:%s success.\n", path);
	 
    // 设置串口阻塞， 0：阻塞， FNDELAY：非阻塞
    if (fcntl(fdSerial, F_SETFL, 0) < 0) {   //阻塞，即使前面在open串口设备时设置的是非阻塞的
        LOG("fcntl failed!\n");
    } else {
        LOG("fcntl=%d\n", fcntl(fdSerial, F_SETFL, 0));
    }

    if (isatty(fdSerial) == 0) {
        LOG("standard input is not a terminal device\n");
        close(fdSerial);
        return -1;
    } else {
//        printf("is a tty success!\n");
    }

//    printf("fd-open=%d\n", fdSerial);

    // 设置串口参数
    if (setOpt(fdSerial, speed, data_bits, parity, stop_bits)== -1) {   //设置8位数据位、1位停止位、无校验
        LOG("Set opt Error\n");
        close(fdSerial);
        return -1;
    }

    tcflush(fdSerial, TCIOFLUSH);    //清掉串口缓存
    fcntl(fdSerial, F_SETFL, 0);    //串口阻塞

    return fdSerial;
}

int ll_open(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        LOG("ERROR: open %s\n", path);
        return fd;
    }

    return fd;
}

int ll_send(int fd, const unsigned char *buff, int length)
{
    return write(fd, buff, length);
}

int ll_recv(int fd, unsigned char *buff, int size)
{
    return read(fd, buff, size);
}

int ll_close(int fd)
{
    return close(fd);
}


