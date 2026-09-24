#include <stdio.h>
#include <os.h>
#include <errno.h>
#include <common.h>
#include <crc32.h>
#include <driver/ota.h>
#include "pkt.h"

#define ACK_PKT_SIZE 4 + sizeof(struct package_info)
#define INFO_PKT_SIZE 8 + sizeof(struct package_info)


const unsigned int sig = 0x73746172;

static int receive_package_type = -1;/* 存储当前接收到的包类型 */
static int receive_package_num = -1;
static int receive_package_data_verify = -1;

/* 获得数据的校验值 */
static inline unsigned int package_verify_get(const unsigned char *start, unsigned int size)
{
    return crc32(0, start, size);
}

/* 检查数据的校验值 */
static inline int package_verify_check(const unsigned char *buf, unsigned int size, unsigned int verify)
{
    if (package_verify_get(buf, size) != verify)
        return -1;

    return 0;
}

/**
* @brief 发送数据
* @param cb 传输句柄的回调函数
* @param cfg 应用层传入的参数
* @param package_buf 应用层传入的数据缓存的指针，包括包头
* @return 成功返回0, 失败返回-1
*/
int package_send(struct transfer_cb *cb, struct transfer_cfg *cfg, unsigned char *package_buf)
{
    int ret = -1;
    unsigned int size = cfg->size;

    short head_size = sizeof(struct package_info);
    unsigned char *buf = package_buf + head_size;
    struct package_info *head = (struct package_info *)package_buf;
    if (cfg->data != buf)
        memcpy(buf, cfg->data, size);

    head->sig = sig;
    head->num = cfg->num;

    memmove(&head->code_len, &cfg->size, sizeof(head->code_len));
    head->pkt_type = cfg->pkt_type;

    unsigned int data_verify = package_verify_get(buf, size);
    memmove(&head->code_verify, &data_verify, sizeof(unsigned int));

    head->head_verify = 0x00000000;
    unsigned int head_verify = package_verify_get((unsigned char *)head, head_size);
    memmove(&head->head_verify, &head_verify, sizeof(unsigned int));
    // printf("pkt: send type=0x%x, num=%d, len=%d\n", cfg->pkt_type, cfg->num, cfg->size);

    ret = cb->transfer_write((unsigned char *)package_buf, size + head_size);
    if (ret != size + head_size) {
        printf("pkt: have been written : %dbytes\n", ret);
        return -1;
    }

    return 0;
}

/**
 * @brief 判断接收到的数据是否为头
 * @param head 头起始地址
 * @return 返回 0 是头数据, 否则不是
*/
static inline int package_head_check(struct package_info *head)
{
    int type = 0, num = 0;

    if (head->sig != sig)
        return -1;

    int head_size = sizeof(struct package_info);

    type = head->pkt_type;
    num = head->num;
    if (type == 0 || num == -1)
        return -2;

    receive_package_type = type;
    receive_package_num = num;
    receive_package_data_verify = head->code_verify;

    unsigned int head_verify = head->head_verify;
    head->head_verify = 0x00000000;
    if (package_verify_check((const unsigned char *)head, head_size, head_verify))
        return -3;

    return 0;
}

void ack_package_send(struct transfer_cb *cb, int num)
{
    unsigned char buf[ACK_PKT_SIZE];
    struct transfer_cfg cfg;

    memset(&cfg, 0, sizeof(struct transfer_cfg));
    cfg.num = num;
    cfg.data = (unsigned char *)ACK_DATA;
    cfg.pkt_type = pkt_ack;
    cfg.size = strlen(ACK_DATA);

    package_send(cb, &cfg, buf);
}

int package_read(struct transfer_cb *cb, struct transfer_cfg *cfg, unsigned char *package_buf)
{
    int timeout_ms = PKT_TIMEOUT_MS;
    int head_size = sizeof(struct package_info);

    int ret = cb->transfer_read(package_buf, head_size, timeout_ms);

    if (ret != head_size)
        return -ETIMEDOUT;

    struct package_info *head = (struct package_info *)package_buf;
    // printf("pkt: recv type=0x%x, num=%d, len=%d\n", head->pkt_type, head->num, head->code_len);

    while (1) {
        if (package_head_check(head) == 0)
            break;

        memmove(package_buf, &package_buf[1], head_size - 1);
        ret = cb->transfer_read(&package_buf[head_size - 1], 1, timeout_ms);
        if (ret != 1)
            return -ETIMEDOUT;
    }

    unsigned char *data_buf = package_buf + head_size;
    int data_size = head->code_len;

    if (data_size > PKT_SIZE_MAX) {
        printf("pkt: Exceed once transfer max length\n");
        return -1;
    }

    ret = cb->transfer_read(data_buf, data_size, timeout_ms);
    if (ret != data_size)
        return -ETIMEDOUT;

    unsigned int data_verify = head->code_verify;
    if (package_verify_check(data_buf, data_size, data_verify))
        return -1;

    cfg->data = data_buf;
    cfg->pkt_type = receive_package_type;
    cfg->num  = receive_package_num;
    cfg->size = data_size;
    cfg->data_verify = receive_package_data_verify;

    return 0;
}