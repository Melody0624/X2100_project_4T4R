#include <stdio.h>
#include <mqueue.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>


static void *send_data(void *arg)
{
    int ret;
    size_t len_s = 8;
    mqd_t mq_id;
    unsigned int prio = 0;
    char data[] = "ingenic";

    mq_id = mq_open("/mqtest", O_RDWR, S_IRUSR | S_IWUSR, NULL);
    if (mq_id == -1) {
        printf("mq_open is failure : %d\n", errno);
        return NULL;
    }

    ret = mq_send(mq_id, data, len_s, prio);
    if (ret == -1) {
        printf("mq_send is failure : %d\n", errno);
        return NULL;
    }

    ret = mq_close(mq_id);
    if (ret) {
        printf("mq_close is failure : %d\n", errno);
        return NULL;
    }

    return NULL;
}

void test_mqueue(void)
{
    int ret;
    mqd_t mqid;
    size_t len_r = 10;
    ssize_t rret;
    pthread_t mqueue_id;
    struct mq_attr mqstat;
    char data[10];
    unsigned int prio;
    struct mq_attr attr;

    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 10;

    mqid = mq_open("/mqtest", O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, &attr);
    if (mqid == -1) {
        printf("mq_open is failure : %d\n", errno);
        return;
    }

    ret = mq_getattr(mqid, &mqstat);
    if (ret) {
        printf("mq_getattr is failure : %d\n", errno);
        return;
    }
    printf("mq_flags : %ld\n", mqstat.mq_flags);
    printf("mq_maxmsg :%ld\n", mqstat.mq_maxmsg);
    printf("mq_msgsize : %ld\n", mqstat.mq_msgsize);
    printf("mq_curmsgs : %ld\n", mqstat.mq_curmsgs);

    mqstat.mq_flags = O_NONBLOCK;

    ret = mq_setattr(mqid, &mqstat, NULL);
    if (ret) {
        printf("mq_setattr is failure : %d\n", errno);
        return;
    }

    pthread_create(&mqueue_id, NULL, send_data, NULL);
    pthread_join(mqueue_id, NULL);

    rret = mq_receive(mqid, data, len_r, &prio);
    if (rret == -1) {
        printf("mq_receive is failure : %d\n", errno);
    }
    else {
        printf("number of recieve byte : %d priority is : %d\n", rret, prio);
        printf("recieve data is : %s\n", data);
    }

    ret = mq_close(mqid);
    if (ret) {
        printf("mq_close is failure : %d\n", errno);
        return;
    }

    ret = mq_unlink("/mqtest");
    if (ret) {
        printf("mq_unlink is failure : %d\n", errno);
        return;
    }
}