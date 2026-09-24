#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <sys/time.h>

#include "pkt_common.h"
#include "pkt.h"
#include "list.h"
#include <semaphore.h>

struct ota_flash_info {
    uint32_t pagesize;
    uint64_t partsize;
    uint32_t blocks;
};

struct Private_Data{
    struct ota_flash_info info;

    struct pkt_cfg cfg;

    int ebcnt;
    int DevStat;
    unsigned int FileVerify;

    sem_t flashinfo_wait;
    sem_t devstat_wait;
    sem_t ebcnt_wait;
    sem_t FileVerify_wait;
};

struct Private_Data pri_data;
struct stat statbuf;

int open_file(char *file_name)
{
    int fd, ret;

    fd = open(file_name, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open data error, errno:%d.\n", errno);
        return -1;
    }

    ret = stat(file_name, &statbuf);
    if(ret < 0) {
        fprintf(stderr, "stat fail, errno %d.\n", errno);
        close(fd);
        return -1;
    }

    return fd;
}

static int pkt_send_cmd(struct pkt_dev *dev, int type)
{
    int ret;
    if (type & PKT_RECEIVE_TYPE_FLAG || type & PKT_SEND_DATA_TYPE_FLAG) {
        printf("error cmd send pkt type: %d!\n", type);
        return -1;
    }

    struct pkt_cfg *cfg = malloc(sizeof(struct pkt_cfg));

    cfg->pkt_type = type;
    cfg->size = strlen(CMD_STUFF);
    cfg->data = (unsigned char *)CMD_STUFF;

    ret = pkt_write_sync(dev, cfg);
    if (!ret)
        free(cfg);
    return ret;
}

static int pkt_send_data(struct pkt_dev *dev, int type, unsigned char *data, unsigned int size)
{
    int ret;
    if (type & PKT_RECEIVE_TYPE_FLAG || type & PKT_SEND_CMD_TYPE_FLAG) {
        printf("error data send pkt type: %d!\n", type);
        return -1;
    }

    struct pkt_cfg *cfg = malloc(sizeof(struct pkt_cfg));

    cfg->pkt_type = type;
    cfg->data = data;
    cfg->size = size;

    ret = pkt_write_sync(dev, cfg);
    if (!ret)
        free(cfg);
    return ret;
}

static void pkt_callback_my_r(struct pkt_dev *dev, struct pkt_cfg *cfg, int type)
{
    if (type & PKT_RECEIVE_TYPE_FLAG) {
        switch (type) {
        case pkt_flash_info:
            memcpy(&pri_data.info, cfg->data, cfg->size);
            sem_post(&pri_data.flashinfo_wait);
            break;
        case pkt_erase_blocks_count:
            pri_data.ebcnt = *(int *)cfg->data;
            sem_post(&pri_data.ebcnt_wait);
            break;
        case pkt_partition_context:
            sem_wait(&dev->r_sem);
            pkt_copy_cfg_info(cfg, &pri_data.cfg);
            sem_post(&pri_data.cfg.wait);
            break;
        case pkt_device_status:
            pri_data.DevStat = *(int *)cfg->data;
            sem_post(&pri_data.devstat_wait);
            break;
        case pkt_file_verify:
            pri_data.FileVerify = *(unsigned int *)cfg->data;
            printf("pri_data.FileVerify: %x\n", pri_data.FileVerify);
            sem_post(&pri_data.FileVerify_wait);
            break;
        default:
            printf("unsupport pkt type: %d\n", type);
        }
    }
}

/* 获取设备的状态 */
static int Get_device_status(struct pkt_dev *dev)
{
    int ret = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;

    ret = pkt_send_cmd(dev, pkt_get_device_status);
    if (ret) {
        return -1;
    }

    ret = sem_timedwait(&pri_data.devstat_wait, &ts);
    if (ret == -1) {
        printf("Get Device Status timeout!\n");
        return -1;
    }

    return 0;
}

static int Verify_device_status(struct pkt_dev *dev)
{
    if (!Get_device_status(dev)) {
        printf("Device status not correctly");
        return pri_data.DevStat;
    }
    printf("Device communication error\n");
    return -1;
}

/* 升级分区 */
int pkt_ota_upgrade(struct pkt_dev *dev, char *partition_name, char *file_name)
{
    int ret, fd;
    unsigned int FileVerify = 0;
    unsigned int send_len = 0;
    struct ota_flash_info *info = &pri_data.info;
    struct pkt_cfg *cfg = &pri_data.cfg;
    struct timespec ts;

    pri_data.DevStat = -1;
    pri_data.ebcnt = 0;
    pri_data.FileVerify = 0;

    cfg->data = malloc(PKT_SIZE_MAX);
    if (cfg->data == NULL) {
        printf("malloc cfg.data failed\n");
        ret = -1;
        goto err;
    }
    memset(cfg->data, 0, PKT_SIZE_MAX);

    /* 打开需要操作的文件，获取文件属性*/
    fd = open_file(file_name);
    if (fd == -1) {
        printf("open file %s failed\n", file_name);
        ret = -1;
        goto err_free_data;
    }

    unsigned int file_len = statbuf.st_size;
    printf("file_len : %d \n", file_len);

    /* 检测设备状态 */
    if ((Get_device_status(dev) || pri_data.DevStat == STATUS_ERROR)) {
        printf("Device communication error or Device status not correctly\n");
        ret = -1;
        goto err_free_data;
    }

    printf("dev status: %d\n", pri_data.DevStat);
    /* 发送ota升级的命令 */
    ret = pkt_send_cmd(dev, pkt_start);
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_close_fd;
    }

    /* 发送升级的分区名 */
    memcpy(cfg->data, partition_name, strlen(partition_name));
    ret = pkt_send_data(dev, pkt_update_partition_name, cfg->data, strlen(partition_name) + 1);
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_close_fd;
    }

    /* 接收info */
    while(1) {
        if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
            printf("Device status(%d) not correctly!\n", pri_data.DevStat);
            ret = -1;
            goto err_close_fd;
        }
        ret = pkt_send_cmd(dev, pkt_get_partition_info);
        if (ret) {
            ret = Verify_device_status(dev);
            goto err_close_fd;
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 3;
        ret = sem_timedwait(&pri_data.flashinfo_wait, &ts);
        if (ret == -1) {
            printf("ota update: Get part info timeout\n");
            continue;
        }

        if (info->partsize != 0) {
            break;
        }
    }
    printf("pagesize: %d \n", info->pagesize);
    printf("partition_size : %ld \n", info->partsize);
    printf("partition_blocks: %d \n", info->blocks);

    /* 发送升级分区的命令 */
    if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
        printf("Device status(%d) not correctly!\n", pri_data.DevStat);
        ret = -1;
        goto err_close_fd;
    }

    ret = pkt_send_cmd(dev, pkt_ota_upgrade_partiton);
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_close_fd;
    }

    /* 接收擦除的进度 */
    while (1) {
        if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
            printf("Device status(%d) not correctly!\n", pri_data.DevStat);
            ret = -1;
            goto err_close_fd;
        }
        ret = pkt_send_cmd(dev, pkt_get_erase_blocks_count);
        if (ret) {
            ret = Verify_device_status(dev);
            goto err_close_fd;
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 3;
        ret = sem_timedwait(&pri_data.ebcnt_wait, &ts);
        if (ret == -1) {
            printf("ota update: Get erase blocks count timeout\n");
            continue;
        }

        printf("\rwaiting flash erase partition. : %6d / %6d", pri_data.ebcnt, info->blocks);
        fflush(stdout);

        if (pri_data.ebcnt == info->blocks)
            break;

        usleep(30 * 1000);
    }

    printf("\n");
    fflush(stdout);

    /* 发送包含总文件大小的包 */
    if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
        printf("Device status(%d) not correctly!\n", pri_data.DevStat);
        ret = -1;
        goto err_close_fd;
    }
    memcpy(cfg->data, &file_len, sizeof(file_len));
    ret = pkt_send_data(dev, pkt_ota_update_file_len, cfg->data, sizeof(file_len));
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_close_fd;
    }
    /* 等待设备端的is_read_file标志位生效 */
    usleep(2 * 1000);

    while(1) {
        int tinycnt = info->pagesize;
        if (file_len - send_len <= info->pagesize) {
            tinycnt = file_len - send_len;
        }

        ret = read(fd, cfg->data, tinycnt);
        if (ret < 0) {
            fprintf(stderr, "read fail, errno:%d\n", errno);
            ret = -1;
            goto err_close_fd;
        }

        FileVerify = crc32(FileVerify, cfg->data, tinycnt);

        /* 传输数据 */
        if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
            printf("Device status(%d) not correctly!\n", pri_data.DevStat);
            ret = -1;
            goto err_close_fd;
        }

        ret = pkt_send_data(dev, pkt_ota_update_file, cfg->data, tinycnt);
        if (ret) {
            ret = Verify_device_status(dev);
            goto err_close_fd;
        }
        send_len += tinycnt;

        printf("\rsending: %8d ", send_len);
        fflush(stdout);

        if (send_len == file_len) {
            usleep(10 * 1000);
            ret = pkt_send_cmd(dev, pkt_get_file_verify);
            if (ret) {
                ret = Verify_device_status(dev);
                goto err_close_fd;
            }

            printf("FileVerify: %x\n", FileVerify);
            if (pri_data.FileVerify == 0)
                sem_wait(&pri_data.FileVerify_wait);
            if (FileVerify == pri_data.FileVerify)
                break;
            else {
                printf("ota updater: file verify is not correctly\n");
                ret = -1;
                goto err_close_fd;
            }
        }
    }
    ret = 0;
    printf("\n");
    fflush(stdout);

err_close_fd:
    close(fd);
err_free_data:
    free(cfg->data);
err:
    return ret;
}

int pkt_ota_read_partition_context(struct pkt_dev *dev, char *partition_name, char *file_name)
{
    int ret;
    unsigned int FileVerify = 0;
    struct pkt_cfg *cfg = &pri_data.cfg;
    unsigned int read_len = 0;
    struct ota_flash_info *info = &pri_data.info;
    struct timespec ts;

    pri_data.FileVerify = 0;

    cfg->data = malloc(PKT_SIZE_MAX);
    if (cfg->data == NULL) {
        printf("malloc cfg.data failed\n");
        ret = -1;
        goto err;
    }
    memset(cfg->data, 0, PKT_SIZE_MAX);

    /* 打开需要操作的文件，获取文件属性*/
    FILE *file = fopen(file_name, "wb+");
    if (file == NULL) {
        printf("open file %s failed\n", file_name);
        ret = -1;
        goto err_free_data;
    }

    /* 检测设备状态 */
    if (Get_device_status(dev) || pri_data.DevStat == STATUS_ERROR) {
        printf("Device communication error or Device status not correctly\n");
        ret = -1;
        goto err_fclose;
    }
    printf("dev status: %d\n", pri_data.DevStat);

    /* 发送ota升级的命令 */
    ret = pkt_send_cmd(dev, pkt_start);
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_fclose;
    }

    /* 发送升级的分区名 */
    memcpy(cfg->data, partition_name, strlen(partition_name));
    ret = pkt_send_data(dev, pkt_update_partition_name, cfg->data, strlen(partition_name) + 1);
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_fclose;
    }

    /* 接收info */
    while(1) {
        if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
            printf("Device status(%d) not correctly!\n", pri_data.DevStat);
            ret = -1;
            goto err_fclose;
        }
        ret = pkt_send_cmd(dev, pkt_get_partition_info);
        if (ret) {
            ret = Verify_device_status(dev);
            goto err_fclose;
        }

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 3;
        ret = sem_timedwait(&pri_data.flashinfo_wait, &ts);
        if (ret == -1) {
            printf("ota read: Get part info timeout\n");
            continue;
        }
  
        if (info->partsize != 0)
            break;
    }

    printf("pagesize: %d \n", info->pagesize);
    printf("partition_size : %ld \n", info->partsize);
    printf("partition_blocks: %d \n", info->blocks);

    /* 发送回读分区的命令 */
    if (Get_device_status(dev) || pri_data.DevStat != STATUS_OK) {
        printf("Device status(%d) not correctly!\n", pri_data.DevStat);
        ret = -1;
        goto err_fclose;
    }
    ret = pkt_send_cmd(dev, pkt_ota_read_partiton);
    if (ret) {
        ret = Verify_device_status(dev);
        goto err_fclose;
    }

    /* 接收数据 */
    while(1) {
        ret = pkt_read_sync(dev, cfg);
        if (ret) {
            ret = Verify_device_status(dev);
            goto err_fclose;
        }

        FileVerify = crc32(FileVerify, cfg->data, cfg->size);
        fwrite(cfg->data, 1, cfg->size, file);
        read_len += cfg->size;

        printf("\rreading: %8d ", read_len);
        fflush(stdout);

        if (read_len == info->partsize) {
            ret = pkt_send_cmd(dev, pkt_get_file_verify);
            if (ret) {
                ret = Verify_device_status(dev);
                goto err_fclose;
            }

            printf("FileVerify: %x\n", FileVerify);
            if (pri_data.FileVerify == 0)
                sem_wait(&pri_data.FileVerify_wait);
            if (FileVerify == pri_data.FileVerify)
                break;
            else {
                printf("ota updater: file verify is not correctly\n");
                ret = -1;
                goto err_fclose;
            }
        }
    }

    ret = 0;
    printf("\n");
    fflush(stdout);

err_fclose:
    fclose(file);
err_free_data:
    free(cfg->data);
err:
    return ret;
}

void usage(char *name)
{
    printf("%s usage\n", name);
    printf("    -h/--help  : show help info\n");
    printf("    dev=       : device node\n");
    printf("    file=      : ota upgrade file name\n");
    printf("    partition= : ota upgrade partition name\n");
    printf("    Example1: %s dev=/dev/ttyACM0 update=1 file=zero.bin partition=rtos\n", name);
    printf("    Example1: %s dev=/dev/ttyACM0 update=0 file=test.bin partition=rtos\n", name);
    printf("    Example2: %s dev=/dev/ttyACM0 reset\n", name);
    printf("    Example3: %s dev=/dev/ttyACM0 exit\n", name);
}

static void init_pri_data(void)
{
    memset(&pri_data, 0, sizeof(struct Private_Data));

    sem_init(&pri_data.devstat_wait, 0, 0);
    sem_init(&pri_data.ebcnt_wait, 0, 0);
    sem_init(&pri_data.flashinfo_wait, 0, 0);
    sem_init(&pri_data.FileVerify_wait, 0, 0);
}

int main(int argc, char **argv)
{
    int ret;
    char *file_name;
    char *partition;
    int is_update;

    if (argc < 3) {
        usage(argv[0]);
        return -1;
    }

    char *dev_path = argv[1] + strlen("dev=");
    /* 对串口设备进行初始化 */
    if (pkt_init_tty(dev_path, 115200, pkt_callback_my_r)) {
        printf("tty devices: %s open failed ! \n", dev_path);
        return -1;
    }
    struct pkt_dev *dev = pkt_get_usb_dev();

    init_pri_data();
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }

        if (strncmp(argv[i], "exit", strlen("exit")) == 0) {
            pkt_send_cmd(dev, pkt_ota_upgrade_end);
            pkt_deinit_tty(dev);
            return 0;
        }

        if (strncmp(argv[i], "reset", strlen("reset")) == 0) {
            pkt_send_cmd(dev, pkt_set_device_reset);
            pkt_deinit_tty(dev);
            return 0;
        }

        if (strncmp(argv[i], "update=", strlen("update=")) == 0) {
            char *endptr;
            is_update = strtoul(argv[i] + strlen("update="), &endptr, 10);
            continue;
        }

        if (strncmp(argv[i], "file=", strlen("file=")) == 0) {
            file_name = argv[i] + strlen("file=");
            continue;
        }

        if (strncmp(argv[i], "partition=", strlen("partition=")) == 0) {
            partition = argv[i] + strlen("partition=");
            continue;
        }

        printf("error: not support this arg: %s \n", argv[i]);
        return -1;
    }

    if (is_update) {
        ret = pkt_ota_upgrade(dev, partition, file_name);
    } else {
        ret = pkt_ota_read_partition_context(dev, partition, file_name);
    }

    pkt_deinit_tty(dev);
    return ret;
}
