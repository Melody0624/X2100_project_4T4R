#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <termios.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "pkt_common.h"
#include "pkt.h"

const unsigned int sig = 0x73746172;
static int receive_package_type = -1;/* 存储当前接收到的包类型 */
static int receive_package_num = -1;

static inline unsigned int package_verify_get(const char *start, unsigned int size)
{
    return crc32(0, start, size);
}

static inline int package_verify_check(const char *buf, unsigned int size, unsigned int verify)
{
    if (package_verify_get(buf, size) != verify)
        return -1;

    return 0;
}

static inline int package_head_check(struct package_info *head)
{
    int type = 0, num = -1;

    if (head->sig != sig)
        return -1;

    int head_size = sizeof(struct package_info);

    type = head->pkt_type;
    num = head->num;
    if (type == 0 || num == -1)
        return -1;

    receive_package_type = type;
    receive_package_num = num;

    unsigned int head_verify = head->head_verify;
    head->head_verify = 0x00000000;
    if (package_verify_check((const char *)head, head_size, head_verify))
        return -1;

    return 0;
}

void package_send(struct pkt_ops *ops, struct pkt_cfg *cfg, char *package_buf)
{
    unsigned int size = cfg->size;

    short head_size = sizeof(struct package_info);
    char *buf = package_buf + head_size;
    struct package_info *head = (struct package_info *)package_buf;
    if (cfg->data != (unsigned char *)buf)
        memmove(buf, cfg->data, size);

    head->sig = sig;
    head->num = cfg->num;

    memmove(&head->code_len, &cfg->size, sizeof(head->code_len));
    head->pkt_type = cfg->pkt_type;

    unsigned int data_verify = package_verify_get((const char *)buf, size);
    memmove(&head->code_verify, &data_verify, sizeof(unsigned int));

    head->head_verify = 0x00000000;
    unsigned int head_verify = package_verify_get((const char *)head, head_size);
    memmove(&head->head_verify, &head_verify, sizeof(unsigned int));

    ops->pkt_write((unsigned char *)package_buf, size + head_size);
}

int package_read(struct pkt_ops *ops, struct pkt_cfg *cfg, unsigned char *package_buf)
{
    int head_size = sizeof(struct package_info);

    int nread = ops->pkt_read(package_buf, head_size);

    if (nread != head_size)
        return -ETIMEDOUT;
    struct package_info *head = (struct package_info *)package_buf;

    while (1) {
        if (package_head_check(head) == 0)
            break;

        memmove(package_buf, &package_buf[1], head_size - 1);
        nread = ops->pkt_read(&package_buf[head_size - 1], 1);
        if (nread != 1)
            return -ETIMEDOUT;
    }

    unsigned char *data_buf = package_buf + head_size;
    int data_size = head->code_len;

    if (data_size > PKT_SIZE_MAX)
        return -1;

    nread = ops->pkt_read(data_buf, data_size);
    if (nread != data_size)
        return -ETIMEDOUT;

    unsigned int data_verify = head->code_verify;
    if (package_verify_check((const char *)data_buf, data_size, data_verify))
        return -1;

    cfg->data = (char *)data_buf;
    cfg->pkt_type = receive_package_type;
    cfg->num  = receive_package_num;
    cfg->size = data_size;

    return 0;
}

void ack_package_send(struct pkt_ops *ops, int num)
{
    char buf[ACK_PKT_SIZE];
    struct pkt_cfg cfg;

    memset(&cfg, 0, sizeof(struct pkt_cfg));
    cfg.num = num;
    cfg.data = (unsigned char *)ACK_DATA;
    cfg.pkt_type = pkt_ack;
    cfg.size = strlen(ACK_DATA);

    package_send(ops, &cfg, buf);

}