#include <stdio.h>
#include <common.h>
#include <string.h>
#include <os.h>
#include <list.h>
#include <assert.h>
#include <sys/time.h>
// #include <malloc.h>
#include <errno.h>
#include <pthread.h>

#include "pkt.h"
#include "heap_malloc.h"
#include <driver/ota.h>

static int get_package_number(int *num)
{
    *num = (*num + 1) % 16;

    return *num;
}

static struct transfer_cfg *get_cfg_from_list(struct list_head *head, int num)
{
    struct list_head *pos;
    struct transfer_cfg *tmp = NULL;
    list_for_each(pos, head) {
        tmp = list_entry(pos, struct transfer_cfg, link);
        if (tmp->num == num) {
            list_del_init(&tmp->link);
            return tmp;
        }
    }
    return NULL;
}

void pkt_check_receive_ack(struct ota_transfer *transfer, int num)
{
    unsigned long flags;
    spin_lock_irqsave(&transfer->w_down_lock, flags);
    struct transfer_cfg *cfg = get_cfg_from_list(&transfer->w_down_list, num);
    spin_unlock_irqrestore(&transfer->w_down_lock, flags);

    if (cfg == NULL) {
        // printf("pkt_api: write down list is NULL!\n");
        return;
    }

    semaphore_post(&cfg->wait);
}

void pkt_send_ack(struct ota_transfer *transfer, int num)
{
    unsigned long flags;

    struct transfer_cfg *cfg = malloc(sizeof(struct transfer_cfg));
    assert(cfg);
    memset(cfg, 0, sizeof(struct transfer_cfg));

    cfg->num = num;
    cfg->pkt_type = pkt_ack;

    spin_lock_irqsave(&transfer->w_lock, flags);
    list_add_tail(&cfg->link, &transfer->w_list);
    semaphore_post(&transfer->w_sem);
    spin_unlock_irqrestore(&transfer->w_lock, flags);
}

void pkt_copy_cfg_info(struct transfer_cfg *src, struct transfer_cfg *dst)
{
    assert(src && src->data && dst && dst->data);
    dst->num = src->num;
    dst->size = src->size;
    dst->pkt_type = src->pkt_type;
    dst->data_verify = src->data_verify;
    memcpy(dst->data, src->data, dst->size);
}

static void transfer_flush(struct ota_transfer *transfer)
{
    unsigned long flags;
    spin_lock_irqsave(&transfer->w_lock, flags);
    INIT_LIST_HEAD(&transfer->w_list);
    spin_unlock_irqrestore(&transfer->w_lock, flags);

    spin_lock_irqsave(&transfer->w_down_lock, flags);
    INIT_LIST_HEAD(&transfer->w_down_list);
    spin_unlock_irqrestore(&transfer->w_down_lock, flags);
}

static void pkt_read_thread(void *data)
{
    struct ota_transfer *transfer = (struct ota_transfer *)data;

    unsigned char *rx_pkt_buf = malloc(PKT_SIZE_MAX);
    assert(rx_pkt_buf);

    struct transfer_cfg tmp;
    tmp.data = rx_pkt_buf + sizeof(struct package_info);
    tmp.pkt_type = -1;

    while (transfer->is_work) {
        if (package_read(transfer->transfer_cb, &tmp, rx_pkt_buf) != 0)
            continue;

        if (tmp.pkt_type == pkt_ack) {
            if(!memcmp(tmp.data, (unsigned char *)ACK_DATA, tmp.size))
                pkt_check_receive_ack(transfer, tmp.num);
            continue;
        }

        if (tmp.pkt_type == pkt_start)
            transfer_flush(transfer);

        if(transfer->pkt_cb(transfer, &tmp, tmp.pkt_type))
            break;

        /*
         * 在包处理完成后再发送 ACK，确保设备已准备好接收下一个包。
         *
         * 对于数据包 (pkt_ota_update_file)，pkt_data_process 内部
         * 会等待 r_sem（OTA升级线程刷写Flash完成后释放）。如果在处理前
         * 就发送 ACK，主机会立即发送下一个数据包，而此时 RX IRQ 尚未使能
         * （package_read 尚未调用），UART FIFO（仅16字节）会溢出导致数据丢失。
         */
        pkt_send_ack(transfer, tmp.num);
    }

    free(rx_pkt_buf);
}

static void pkt_send_thread(void *data)
{
    struct ota_transfer *transfer = (struct ota_transfer *)data;
    unsigned long flags;
    struct transfer_cfg tmp;
    unsigned char *tx_pkt_buf = malloc(PKT_SIZE_MAX);
    assert(tx_pkt_buf);
    tmp.data = tx_pkt_buf + sizeof(struct package_info);

    while(1) {
        semaphore_wait(&transfer->w_sem);
        if (!transfer->is_work)
            break;

        spin_lock_irqsave(&transfer->w_lock, flags);
        struct transfer_cfg *cfg = list_first_entry_or_null(&transfer->w_list, struct transfer_cfg, link);
        list_del_init(&cfg->link);
        spin_unlock_irqrestore(&transfer->w_lock, flags);
        assert(cfg);

        if (cfg->pkt_type == pkt_ack) {
            ack_package_send(transfer->transfer_cb, cfg->num);
            free(cfg);
            continue;
        }

        if(cfg->data == NULL)
            break;

        pkt_copy_cfg_info(cfg, &tmp);

        spin_lock_irqsave(&transfer->w_down_lock, flags);
        list_add_tail(&cfg->link, &transfer->w_down_list);
        spin_unlock_irqrestore(&transfer->w_down_lock, flags);
        package_send(transfer->transfer_cb, &tmp, tx_pkt_buf);
    }

    free(tx_pkt_buf);
}

int pkt_read_sync(struct ota_transfer *transfer, struct transfer_cfg *cfg)
{
    /* 3s未接收到数据视作读超时 */
    if(semaphore_wait_timeout(&cfg->wait, PKT_TRANS_TIMEOUT_MS)) {
        printf("pkt_api: pkt read ack timeout\n");
        return -1;
    }
    return 0;
}

int pkt_write_sync(struct ota_transfer *transfer, struct transfer_cfg *cfg)
{
    int ret;
    unsigned long flags;
    semaphore_init(&cfg->wait, 0);

    spin_lock_irqsave(&transfer->w_lock, flags);
    cfg->num = get_package_number(&transfer->num);
    list_add_tail(&cfg->link, &transfer->w_list);
    semaphore_post(&transfer->w_sem);
    spin_unlock_irqrestore(&transfer->w_lock, flags);

    for (int retry = 0; retry < 3; retry++) {
        ret = semaphore_wait_timeout(&cfg->wait, PKT_TRANS_TIMEOUT_MS);
        if (!ret)
            return 0;
        printf("pkt_api: pkt write get ack timeout (retry %d)\n", retry);
    }

    /* 超时清理：从 w_down_list 和 w_list 中移除 */
    spin_lock_irqsave(&transfer->w_down_lock, flags);
    if (!list_empty(&cfg->link))
        list_del_init(&cfg->link);
    spin_unlock_irqrestore(&transfer->w_down_lock, flags);

    spin_lock_irqsave(&transfer->w_lock, flags);
    if (!list_empty(&cfg->link))
        list_del_init(&cfg->link);
    spin_unlock_irqrestore(&transfer->w_lock, flags);

    printf("pkt_api: pkt write get ack timeout after retries\n");
    return -1;
}

struct ota_transfer *ota_transfer_init(struct transfer_cb *cb, pkt_callback pkt_r_cb)
{
    if (cb == NULL || pkt_r_cb == NULL) {
        printf("pkt_api: cb or pkt_r_cb cannot be NULL\n");
        return NULL;
    }

    struct ota_transfer *transfer = malloc(sizeof(struct ota_transfer));
    if (transfer == NULL) {
        printf("pkt_api: malloc transfer failed\n ");
        return NULL;
    }

    memset(transfer, 0, sizeof(struct ota_transfer));
    transfer->transfer_cb = cb;
    transfer->pkt_cb = pkt_r_cb;

    if (cb->transfer_init()) {
        printf("pkt_api: ota pkt init failed\n");
        free(transfer);
        return NULL;
    }

    INIT_LIST_HEAD(&transfer->w_list);
    INIT_LIST_HEAD(&transfer->w_down_list);

    spin_lock_init(&transfer->w_lock);
    spin_lock_init(&transfer->w_down_lock);

    transfer->num = -1;
    transfer->is_work = 1;
    semaphore_init(&transfer->r_sem, 1);
    semaphore_init(&transfer->w_sem, 0);

    transfer->read_thread = thread_create("pkt_receive_data", 1024, pkt_read_thread, transfer);
    transfer->write_thread = thread_create("pkt_send_data", 1024, pkt_send_thread, transfer);

    return transfer;
}

void ota_transfer_deinit(struct ota_transfer *transfer)
{
    transfer->is_work = 0;

    semaphore_post(&transfer->w_sem);

    thread_join(transfer->read_thread, NULL);
    thread_join(transfer->write_thread, NULL);

    transfer->transfer_cb->transfer_exit();

    free(transfer);
    transfer = NULL;
}