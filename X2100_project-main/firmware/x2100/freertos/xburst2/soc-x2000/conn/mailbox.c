
#include <common.h>
#include <os.h>
#include <list.h>
#include <driver/irq.h>

#include <cpu/cpu.h>
#include <soc/ccu.h>
#include <ring_mem.h>

#define LINUX_CPU_ID    0

static char in_buffer[256];
static char out_buffer[256];

static unsigned int mailbox_pak_tail;
DEFINE_RING_MEM(write_to_linux, in_buffer);
DEFINE_RING_MEM(read_from_linux, out_buffer);

static DEFINE_MUTEX(send_mutex);
static DEFINE_MUTEX(receive_mutex);
static DEFINE_THREAD_WAITER(mailbox_send_waiter);
static DEFINE_THREAD_WAITER(mailbox_receive_waiter);

#define MAILBOX_PACK_HEADER     0x12345677
#define MAILBOX_PACK_RECV_DATA  0x14753125
#define MAILBOX_PACK_TAIL       0x14753123

static void mailbox_notify(void)
{
    unsigned long mbr;
    unsigned int id = arch_get_cpu_id();

    ccu_spin_lock_critical(id);

    mbr = ccu_read_reg(CCU_MBR(id));
    if (mbr == 0) {
        ccu_write_reg(CCU_MBR(LINUX_CPU_ID), id + 1);
    }

    ccu_spin_unlock_critical(id);
}

static int mailbox_send(const void *buf, int size, int timeout_ms)
{
    int ret;
    int send_len;
    int total_size = size;

    uint64_t now;

    mutex_lock(&send_mutex);

    while (size) {
        now = systick_get_time_ms();

        send_len = ring_mem_write(&write_to_linux, buf, size);
        if (!send_len) {
            ret = thread_waiter_wait_timeout(&mailbox_send_waiter, timeout_ms);
            if (ret) {
                send_len = total_size - size;
                goto unlock;
            }
        }

        size -= send_len;
        buf += send_len;

        timeout_ms = timeout_ms - (systick_get_time_ms() - now);
        if (timeout_ms < 0)
            timeout_ms = 0;
    }

unlock:
    mutex_unlock(&send_mutex);

    if (send_len)
        mailbox_notify();

    return send_len;
}

static int mailbox_receive(void *buf, int size, int timeout_ms)
{
    int ret;
    int receive_len;
    int total_size = size;

    uint64_t now;

    mutex_lock(&receive_mutex);

    while (size) {
        now = systick_get_time_ms();

        receive_len = ring_mem_read(&read_from_linux, buf, size);
        if (!receive_len) {
            ret = thread_waiter_wait_timeout(&mailbox_receive_waiter, timeout_ms);
            if (ret) {
                receive_len = total_size - size;
                goto unlock;
            }
        }

        size -= receive_len;
        buf += receive_len;

        timeout_ms = timeout_ms - (systick_get_time_ms() - now);
        if (timeout_ms < 0)
            timeout_ms = 0;
    }

unlock:
    mutex_unlock(&receive_mutex);

    if (receive_len)
        mailbox_notify();

    return receive_len;
}

static void mailbox_irq_handler(int irq, void *data)
{
    unsigned int id = arch_get_cpu_id();

    ccu_spin_lock_critical(id);

    ccu_write_reg(CCU_MBR(id), 0);

    ccu_spin_unlock_critical(id);

    if (ring_mem_writable_size(&write_to_linux))
        thread_waiter_wakeup(&mailbox_send_waiter);

    if (ring_mem_readable_size(&read_from_linux))
        thread_waiter_wakeup(&mailbox_receive_waiter);
}

static void mailbox_communication_prepare(void)
{
    unsigned long addr[3];
    int ret = 0;
    int mbr = 0;
    int linux_cpu_id = LINUX_CPU_ID;
    int rtos_cpu_id = arch_get_cpu_id();
    unsigned long timeout_us = 300;
    unsigned long timeout = timeout_us * 1000 * 10;

    uint64_t start_time = systick_get_time_us();

    ccu_write_reg(CCU_MBR(linux_cpu_id), MAILBOX_PACK_HEADER);
    while (1) {
        ret = ccu_read_reg(CCU_MBR(rtos_cpu_id));
        if (ret == MAILBOX_PACK_HEADER)
            break;

        if (systick_get_time_us() - start_time > timeout) {
            start_time = systick_get_time_us();
            printf("MAILBOX: CPU%d: read faild\n", arch_get_cpu_id());
        }

        usleep(timeout_us);
    }

    /* 通过邮箱把缓冲区地址发送给 CPU1，并通过 CPU0 邮箱读到的信息确认 CPU1 是否收到 */
    addr[0] = (unsigned long)(&write_to_linux);
    addr[1] = (unsigned long)(&read_from_linux);
    addr[2] = (unsigned long)(&mailbox_pak_tail);

    /* 发送数据地址信息 */
    ccu_write_reg(CCU_MBR(linux_cpu_id), (unsigned long)addr);
    while (mbr != MAILBOX_PACK_RECV_DATA) {
        /* 接收 MAILBOX_PACK_RECV_DATA 表示对端地址信息接收完成 */
        mbr = ccu_read_reg(CCU_MBR(rtos_cpu_id));
        usleep(timeout_us);
    }

    ccu_write_reg(CCU_MBR(linux_cpu_id), MAILBOX_PACK_RECV_DATA);

    /* 等待 CPU1 MAILBOX 准备完成 */
    while (mailbox_pak_tail != MAILBOX_PACK_TAIL)
        usleep(timeout_us);
}

static void mailbox_init(void)
{
    request_irq_disabled(IRQ_V_IP3, 0, mailbox_irq_handler, "mailbox", NULL);

    mailbox_communication_prepare();

    enable_irq(IRQ_V_IP3);
}
