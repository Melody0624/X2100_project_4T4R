#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include <semaphore.h>
#include <time.h>

#include "pkt_common.h"
#include "pkt.h"

static LIST_HEAD(pkt_list);
static pthread_spinlock_t spinlock;
static pthread_t pkt_w_data;
static pthread_t pkt_r_data;
static volatile int package_num = -1;
int get_package_number(void)
{
    package_num = (package_num + 1) % 16;

    return package_num;
}

void pkt_copy_cfg_info(struct pkt_cfg *src, struct pkt_cfg *dst)
{
    assert(src && src->data && dst && dst->data);
    dst->pkt_type = src->pkt_type;
    dst->size = src->size;
    dst->num = src->num;
    memcpy(dst->data, src->data, dst->size);
}

static void pkt_send_ack(struct pkt_dev *dev, int num)
{
    struct pkt_cfg *cfg = malloc(sizeof(struct pkt_cfg));
    assert(cfg);
    memset(cfg, 0, sizeof(struct pkt_cfg));

    cfg->num = num;
    cfg->pkt_type = pkt_ack;

    pthread_spin_lock(&dev->w_lock);
    list_add_tail(&cfg->link, &dev->w_list);
    sem_post(&dev->w_sem);
    pthread_spin_unlock(&dev->w_lock);
}

static struct pkt_cfg *get_cfg_from_list(struct list_head *head, int num)
{
    struct list_head *pos;
    struct pkt_cfg *tmp = NULL;
    list_for_each(pos, head) {
        tmp = list_entry(pos, struct pkt_cfg, link);
        if (tmp->num == num) {
            list_del_init(&tmp->link);
            return tmp;
        }
    }
    return NULL;
}

static void pkt_check_receive_ack(struct pkt_dev *dev, int num)
{
    pthread_spin_lock(&dev->w_down_lock);
    struct pkt_cfg *cfg = get_cfg_from_list(&dev->w_down_list, num);
    pthread_spin_unlock(&dev->w_down_lock);
    if (cfg == NULL) {
        printf("pkt_api: pkt:%d is not in w down list!\n", num);
        return;
    }

    sem_post(&cfg->wait);
}

static void *pkt_read_thread(void *data)
{
    int ret;
    struct pkt_dev *dev = (struct pkt_dev *)data;

    unsigned char *rx_pkt_buf = malloc(PKT_SIZE_MAX);
    assert(rx_pkt_buf);

    struct pkt_cfg tmp;
    tmp.data = rx_pkt_buf + sizeof(struct package_info);
    tmp.pkt_type = -1;

    while (dev->is_work) {
        ret = package_read(dev->pkt_ops, &tmp, rx_pkt_buf);
        if (ret != 0)
            continue;

        if (tmp.pkt_type == pkt_ack) {
            if(!memcmp(tmp.data, (unsigned char *)ACK_DATA, tmp.size))
                pkt_check_receive_ack(dev, tmp.num);
            continue;
        }

        pkt_send_ack(dev, tmp.num);
        dev->cb(dev, &tmp, tmp.pkt_type);
    }

    free(rx_pkt_buf);
    rx_pkt_buf = NULL;
}

static void *pkt_send_thread(void *data)
{
    struct pkt_dev *dev = (struct pkt_dev *)data;
    struct pkt_cfg tmp;

    unsigned char *tx_pkt_buf = malloc(PKT_SIZE_MAX);
    assert(tx_pkt_buf);
    memset(tx_pkt_buf, 0, sizeof(tx_pkt_buf));
    tmp.data = tx_pkt_buf + sizeof(struct package_info);

    while (1) {
        sem_wait(&dev->w_sem);
        if (!dev->is_work)
            break;

        pthread_spin_lock(&dev->w_lock);
        struct pkt_cfg *cfg = list_first_entry_or_null(&dev->w_list, struct pkt_cfg, link);
        list_del_init(&cfg->link);
        pthread_spin_unlock(&dev->w_lock);
        if (cfg == NULL)
            continue;

        if (cfg->pkt_type == pkt_ack) {
            ack_package_send(dev->pkt_ops, cfg->num);
            free(cfg);
            continue;
        }

        pkt_copy_cfg_info(cfg, &tmp);

        pthread_spin_lock(&dev->w_down_lock);
        list_add_tail(&cfg->link, &dev->w_down_list);
        pthread_spin_unlock(&dev->w_down_lock);
        package_send(dev->pkt_ops, &tmp, (char *)tx_pkt_buf);
    }

    free(tx_pkt_buf);
    tx_pkt_buf = NULL;
}

int pkt_read_sync(struct pkt_dev *dev, struct pkt_cfg *cfg)
{
    int ret = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;

    sem_post(&dev->r_sem);

    //尝试获取信号量，最多等待1s
    ret = sem_timedwait(&cfg->wait, &ts);
    if (ret == -1) {
        printf("pkt_api: sem_timedwait\n");
        return -1;
    }

    return 0;
}

int pkt_write_sync(struct pkt_dev *dev, struct pkt_cfg *cfg)
{
    int ret = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;

    sem_init(&cfg->wait, 0, 0);

    pthread_spin_lock(&dev->w_lock);
    cfg->num = get_package_number();
    list_add_tail(&cfg->link, &dev->w_list);
    sem_post(&dev->w_sem);
    pthread_spin_unlock(&dev->w_lock);

    //尝试获取信号量，最多等待1s
    ret = sem_timedwait(&cfg->wait, &ts);
    if (ret == -1)
        printf("pkt_api: sem_timedwait\n");

    sem_destroy(&cfg->wait);
    return ret;
}

struct pkt_dev *pkt_get_dev(const char *name)
{
    struct pkt_dev *dev = NULL;
    struct list_head *pos;
    list_for_each(pos, &pkt_list) {
        struct pkt_dev *tmp = list_entry(pos, struct pkt_dev, link);
        if (strncmp(tmp->pkt_name, name, strlen(name)) == 0) {
            dev = tmp;
            break;
        }
    }

    if (dev == NULL || dev->is_work)
        return NULL;

    dev->is_work = 1;

    sem_init(&dev->w_sem, 0, 0);
    sem_init(&dev->r_sem, 0, 0);

    pthread_create(&pkt_r_data, NULL, pkt_read_thread, dev);
    pthread_create(&pkt_w_data, NULL, pkt_send_thread, dev);
    return dev;
}

void pkt_init_cfg(struct pkt_cfg *cfg, struct pkt_dev *dev)
{
    memset(cfg, 0, sizeof(struct pkt_cfg));
    cfg->pdev = dev;
    INIT_LIST_HEAD(&cfg->link);
}

struct pkt_dev *pkt_init(const char *name, struct pkt_ops *ops, pkt_callback cb)
{
    assert(name && ops);

    pthread_spin_init(&spinlock, PTHREAD_PROCESS_PRIVATE);

    pthread_spin_lock(&spinlock);

    struct pkt_dev *dev = pkt_get_dev(name);
    if (dev) {
        printf("pkt_api: %s has been registered\n", name);
        goto unlock;
    }

    dev = malloc(sizeof(struct pkt_dev));
    if (dev == NULL) {
        printf("pkt_api: %s init failed!\n", name);
        goto unlock;
    }

    memset(dev, 0, sizeof(struct pkt_dev));
    memmove(dev->pkt_name, name, strlen(name));
    dev->pkt_ops = ops;
    dev->cb = cb;

    INIT_LIST_HEAD(&dev->w_list);
    INIT_LIST_HEAD(&dev->w_down_list);

    pthread_spin_init(&dev->w_lock, PTHREAD_PROCESS_PRIVATE);
    pthread_spin_init(&dev->w_down_lock, PTHREAD_PROCESS_PRIVATE);

    list_add_tail(&dev->link, &pkt_list);

unlock:
    pthread_spin_unlock(&spinlock);

    return dev;
}

void pkt_exit(struct pkt_dev *dev)
{
    pthread_spin_lock(&spinlock);
    list_del_init(&dev->link);
    pthread_spin_unlock(&spinlock);

    dev->is_work = 0;
    sem_post(&dev->w_sem);

    pthread_cancel(pkt_r_data);

    pthread_join(pkt_r_data, NULL);
    pthread_join(pkt_w_data, NULL);

    dev->pkt_ops->pkt_exit();
    free(dev);
    dev = NULL;
}


