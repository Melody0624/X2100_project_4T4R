#include <stdio.h>
#include <string.h>
#include <os.h>
#include <delay.h>
#include <driver/can.h>

/**
 * 以x2600e芯片 PD_X2600_EVB_V1.0 针板为例
 * 将 can0 和 can1 分别连接 can收发器 芯片, 再分别连接两芯片输出端差分电平(高接高, 低接低)
 * 至此 can0 与 can1 形成通路, 可进行通讯
 *
 * 本示例代码为 can0 向 can1 发送数据, 且接收其回复的数据是否匹配
 */

struct can_config cfg0 = {
    .bus_id = 0,
    .bus_rate = CAN_BUS_RATE_500000,
    /* 不使用 rx dma 模式接收数据, 使用中断模式 */
    // .rx_dma_mode = 1,
    .rx_bufsize = 256,
};

struct can_config cfg1 = {
    .bus_id = 1,
    .bus_rate = CAN_BUS_RATE_500000,
    /* 使用 rx dma 模式接收数据 */
    .rx_dma_mode = 1,
    .rx_bufsize = 256,
};

void can_msg_dump(struct can_msg *msg, int busid)
{
    if (busid)
        printf("--- ");
    printf("id: %d - %s - %s - id: 0x%02x, len: %d, data: \"%s\"\n", busid,
        (msg->flags & CAN_EXTENDED) ? "extended" : "standard",
        (msg->flags & CAN_REMOTE) ? "remote" : "data",
        msg->frm_id, msg->len, (char*)msg->buf);
}

int can_send_msg(struct can_config *cfg, int id, unsigned char *buf, int len, int flag)
{
    struct can_msg tmsg;
    tmsg.frm_id = id;
    tmsg.len = len > 8 ? 8 : len;
    memcpy(tmsg.buf, buf, tmsg.len);
    tmsg.flags = flag;

    return can_send_frame(cfg, &tmsg);
}

int can_receive_msg(struct can_config *cfg, struct can_msg *rmsg)
{
    memset(rmsg->buf, 0, sizeof(rmsg->buf));

    return can_receive_frame(cfg, rmsg);
}

void can_test1(void *data)
{
    struct can_msg rmsg;
    can_start(&cfg0);
    struct can_filter_cfg af_cfg[4] = {0};
    af_cfg[2].is_enable = 1;
    af_cfg[2].afid = 0x88;
    can_set_acceptance_filter(&cfg0, af_cfg);

    int ret = 0;
    int t = 8;
    int flag = CAN_EXTENDED;
    unsigned char *rp = NULL;
    while (t--) {
        if (t == 0)
            flag = CAN_REMOTE;/* 最后一轮发送远程帧 请求数据 */
        else
            flag = !flag;/* 交替发送 标准 和 扩展帧 */

        int len = sizeof("Hello");
        ret = can_send_msg(&cfg0, 0x88, (unsigned char *)"Hello", len, flag);
        if (ret < 0)
            break;

        ret = can_receive_msg(&cfg0, &rmsg);
        if (ret < 0)
            break;

        can_msg_dump(&rmsg, 0);

        if (t == 0)
            rp = (unsigned char *)"END_ACK";
        else
            rp = (unsigned char *)"World!";

        if (strncmp((const char *)rp, (const char *)rmsg.buf, 8)) {
            printf("err: receive is not match with send!\n");
            break;
        }
    }

    can_stop(&cfg0);
    printf("test1 done!\n");
}

void can_test2(void *data)
{
    struct can_msg rmsg;
    unsigned char *tbuf;
    int tflag = 0, tlen = 0;
    can_start(&cfg1);

    int ret = 0;
    int t = 8;
    while (t--) {
        ret = can_receive_msg(&cfg1, &rmsg);
        if (ret < 0)
            break;

        can_msg_dump(&rmsg, 1);

        if (rmsg.flags & CAN_REMOTE) {
            tbuf = (unsigned char *)"END_ACK";
            tflag = rmsg.flags & ~CAN_REMOTE;
        } else {
            if (!strncmp((const char *)"Hello", (const char *)rmsg.buf, 8))
                tbuf = (unsigned char *)"World!";
            else
                tbuf = rmsg.buf;
            tflag = rmsg.flags;
        }
        tlen = strlen((const char *)tbuf) + 1;

        /* 回复 与接收到的帧 相同类型和ID 的 数据帧 */
        ret = can_send_msg(&cfg1, rmsg.frm_id, tbuf, tlen, tflag);
        if (ret < 0)
            break;
    }

    can_start(&cfg1);
    printf("test2 done!\n");
}

void can_test(void)
{
    thread_create("cantest0", 4096, can_test1, NULL);
    thread_create("cantest1", 4096, can_test2, NULL);
}
