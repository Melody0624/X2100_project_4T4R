#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <termios.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/file.h>
#include "pkt_common.h"
#include "pkt.h"
#define PKT_USB_NAME "usb_pkt"

static unsigned int fd;

struct baud_speed_pair {
    int baude;
    speed_t speed;
};

static int tty_config(int baude,int c_flow, int bits, char parity, int stop)
{
    int i, ret;
    struct termios uart;

    ret = tcgetattr(fd, &uart);
    if (ret < 0) {
        fprintf(stderr, "tcgetattr failed!\n");
        return -1;
    }

    struct baud_speed_pair bauds[] = {
        {50, B50},
        {75, B75},
        {110, B110},
        {134, B134},
        {150, B150},
        {200, B200},
        {300, B300},
        {600, B600},
        {1200, B1200},
        {1800, B1800},
        {2400, B2400},
        {4800, B4800},
        {9600, B9600},
        {19200, B19200},
        {38400, B38400},
        {57600, B57600},
        {115200 , B115200},
        {230400 , B230400},
        {460800 , B460800},
        {500000 , B500000},
        {576000 , B576000},
        {921600 , B921600},
        {1000000, B1000000},
        {1152000, B1152000},
        {1500000, B1500000},
        {2000000, B2000000},
        {2500000, B2500000},
        {3000000, B3000000},
        {3500000, B3500000},
        {4000000, B4000000},
    };

    for (i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        if (bauds[i].baude == baude) {
            cfsetispeed(&uart, bauds[i].speed);  // 设置输入波特率
            cfsetospeed(&uart, bauds[i].speed);  // 设置输出波特率
            break;
        }
    }

    if (i == sizeof(bauds) / sizeof(bauds[0])) {
        fprintf(stderr, "Unknown baudrate: %d.\n", baude);
        return -1;
    }

    uart.c_cflag  |=  CLOCAL | CREAD;   // CLOCAL:忽略modem控制线  CREAD：打开接受者

    switch(c_flow) {
    case 'N':
    case 'n':
        uart.c_cflag &= ~CRTSCTS;   // 不进行硬件流控制
        break;

    case 'H':
    case 'h':
        uart.c_cflag |= CRTSCTS;    // 进行硬件流控制
        break;

    default:
        fprintf(stderr,"Unknown c_cflag.\n");
        return -1;
    }

    switch(bits) {
    case 5:
        uart.c_cflag &= ~CSIZE;     // 屏蔽其他标志位
        uart.c_cflag |= CS5;        // 数据位为5位
        break;

    case 6:
        uart.c_cflag &= ~CSIZE;
        uart.c_cflag |= CS6;
        break;

    case 7:
        uart.c_cflag &= ~CSIZE;
        uart.c_cflag |= CS7;
        break;

    case 8:
        uart.c_cflag &= ~CSIZE;
        uart.c_cflag |= CS8;
        break;

    default:
        fprintf(stderr, "Unknown bits!");
        return -1;
    }

    switch(parity) {
    case 'n':
    case 'N':
        uart.c_cflag &= ~PARENB;    // PARENB：产生奇偶校验
        uart.c_iflag &= ~INPCK;     // NPCK：使奇偶校验起作用
        break;

    case 's':
    case 'S':
        uart.c_cflag &= ~PARENB;
        uart.c_cflag &= ~CSTOPB;     // 使用两位停止位
        break;

    case 'o':
    case 'O':
        uart.c_cflag |= PARENB;
        uart.c_cflag |= PARODD;     // 使用奇校验
        uart.c_iflag |= INPCK;
        uart.c_iflag |= ISTRIP;     // 使字符串剥离第八个字符，即校验位
        break;

    case 'e':
    case 'E':
        uart.c_cflag |= PARENB;
        uart.c_cflag &= ~PARODD;    // 非奇校验，即偶校验
        uart.c_iflag |= INPCK;
        uart.c_iflag |= ISTRIP;
        break;

    default:
        fprintf(stderr,"Unknown parity!\n");
        return -1;
    }

    switch(stop) {
    case 1:
        uart.c_cflag &= ~CSTOPB;    // CSTOPB：使用两位停止位
        break;

    case 2:
        uart.c_cflag |= CSTOPB;
        break;

    default:
        fprintf(stderr,"Unknown stop!\n");
        return -1;
    }

    /* 将串口设置成Raw mode，关闭回显、行控制、转义等功能
    */
    uart.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    uart.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG );
    uart.c_oflag &= ~OPOST;

    ret = tcflush(fd, TCIOFLUSH);   // 清空输入输出缓冲区
    if (ret < 0) {
        fprintf(stderr, "tcflush failed.\n");
        return -1;
    }

    ret = tcsetattr(fd, TCSANOW, &uart);
    if(ret < 0) {
        fprintf(stderr, "tcgetattr failed!\n");
        return -1;
    }

    return 0;
}

static int tty_open(const char *port_name, int baud_speed)
{
    assert(port_name && baud_speed);

    //以阻塞的形式打开串口
    fd = open(port_name, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        printf("usb_pkt: failed to open %s\n", port_name);
        return -1;
    }

    // 尝试对串口进行锁定
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        printf("flock");
        goto err;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
        printf("usb_pkt: failed to fcntl %s\n", port_name);
        goto err;
    }

    // 判断打开的描述符, 是否为串口设备
    if (isatty(fd) == 0) {
        printf("usb_pkt: %s ars not tty device.\n", port_name);
        goto err;
    }

    // 配置串口参数
    int baud = baud_speed;
    if (tty_config(baud, 'N', 8, 'N', 1) < 0) {
        printf("usb_pkt: failed to config tty\n");
        goto err;
    }

    return fd;

err:
    close(fd);
    return -1;
}

static void safe_write(char *w_buf, ssize_t len)
{
    int nwrite, ret;
    size_t left = len;
    char *ptr = w_buf;

    while (left > 0) {
        nwrite = write(fd, ptr, left);
        if (nwrite < 0) {
            printf("usb_pkt: write fail!  \n");
            break;
        }
        left -= nwrite;
        ptr += nwrite;
    }

}

static int safe_read(char *r_buf, ssize_t len)
{
    ssize_t nread;
    size_t left = len;
    char *ptr = r_buf;

    while (left > 0) {
        nread = read(fd, ptr, left);
        if (nread <= 0) {
            printf("usb_pkt: read fail! \n");
            break;
        }
        left -= nread;
        ptr += nread;
    }
    return (len - left);
}

void tty_exit(void)
{
    //解锁串口
    if (flock(fd, LOCK_UN) < 0) {
        printf("flock unlock err\n");
    }

    close(fd);
}

static struct pkt_ops usb_pkt_ops = {
    .pkt_read = safe_read,
    .pkt_write = safe_write,
    .pkt_exit = tty_exit,
};

int pkt_init_tty(unsigned char *port, int baud, pkt_callback cb)
{
    struct pkt_dev *dev = pkt_init(PKT_USB_NAME, &usb_pkt_ops, cb);
    if (dev == NULL)
        return -1;
    if (tty_open(port, baud) < 0) {
        printf("usb_pkt: failed to open tty %s\n", port);
        return -1;
    }
    return 0;
}

struct pkt_dev *pkt_get_usb_dev(void)
{
    return pkt_get_dev(PKT_USB_NAME);
}

void pkt_deinit_tty(struct pkt_dev *dev)
{
    pkt_exit(dev);
}