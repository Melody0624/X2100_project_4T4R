#include <stdio.h>
#include <cpu/irqflags.h>
#include <driver/irq.h>
#include <cpu/cpu.h>
#include <common.h>
#include <usb/gadget_serial.h>
#include <os.h>
#include "filesystem/include/mtd_driver_nor.h"

#define PARTITON_NAME "test"

struct rtos_header {
    unsigned int code[2];
    unsigned int tag;
    unsigned int version;
    unsigned long img_start;
    unsigned long img_end;
    unsigned long heap_start;
    unsigned long heap_end;
    unsigned long mapped_rtosdata_size;
};

static const struct gadget_id serial_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a7
};

static const struct usb_cdc_serial_param serial_parameters ={
    .dwDTERate = 115200,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8
};

static void serial_param_callback(struct usb_cdc_serial_param *p)
{
    printf("usb serial: dwDTERate %d, bCharFormat %d, bParityType %d, bDataBits %d\n",
            p->dwDTERate, p->bCharFormat, p->bParityType, p->bDataBits);
}

static void serial_connect_callback(int connect)
{
    printf("serial_connect_callback %d\n", connect);
}

static inline int rtos_check_header(struct rtos_header *rtos)
{
    if (rtos->tag != 0x534f5452) {
        printf("rtos bad tag: %x\n", rtos->tag);
        return -1;
    }
    if (rtos->img_start >= rtos->img_end) {
        printf("rtos bad off: %lx %lx\n", rtos->img_start, rtos->img_end);
        return -1;
    }

    return 0;
}

static inline int rtos_check_intersection(unsigned int start0, unsigned int end0, unsigned int start1, unsigned int end1)
{
    if (start1 < start0 && end1 < start0)
        return 0;
    if (start1 >= end0 && end1 >= end0)
        return 0;
    return 1;
}

static inline void rtos_start(struct rtos_header *rtos, void *arg)
{
    void (*func)(void *arg) = (void *)rtos->img_start;
    flush_cache_all();
    func(arg);
}

static void release_all_resources(void)
{
    usb_core_exit();

    local_irq_disable();
    arch_deinit_cpu();
    // lcd
    // disable_irq(IRQ_LCD);
    // release_irq(IRQ_LCD);

    // intc
    disable_irq(IRQ_V_IP2);
    release_irq(IRQ_V_IP2);

    // ost
    disable_irq(IRQ_V_IP4);
    release_irq(IRQ_V_IP4);
    local_irq_enable();
}

int read_rtos_start_addr_by_name(struct rtos_header *rtos, int *offset, const char *find_node_name)
{
    struct mtd_nor_partition *mtd_part;

    struct mtd_nor_partition *parts = sfc_nor_flash_partition_information();

    if (parts == NULL) {
        return -EIO;
    }

    for (mtd_part = parts; mtd_part->name != NULL; mtd_part++) {
        if ( !strcmp(find_node_name, mtd_part->name) ) {
            *offset = mtd_part->offset;
            sfc_nor_flash_read( mtd_part->offset, sizeof(struct rtos_header), (void *)rtos);
            break;
        }
    }

    return 0;
}

static void boot_app(void)
{
    int ret = 0;
    int offset = 0;

    struct rtos_header *rtos = malloc(sizeof(struct rtos_header));
    assert(rtos != NULL);
    memset(rtos, 0, sizeof(struct rtos_header));

    // 根据烧录工具中设置的分区名字获取镜像信息
    ret = read_rtos_start_addr_by_name(rtos, &offset, PARTITON_NAME);

    if (ret != 0) {
        printf("get partition_information fail!\n");
        goto error;
    }

    if (rtos_check_header(rtos)) {
        goto error;
    }

    // 检查加载是否会覆盖到旧系统地址上
    if (rtos_check_intersection(CONFIG_OS_MEM_ADDR, (CONFIG_OS_MEM_ADDR + CONFIG_OS_MEM_SIZE), rtos->img_start, rtos->img_end)) {
        printf("address overlap\n");
        goto error;
    }

    sfc_nor_flash_read(offset, rtos->img_end -rtos->img_start, (void *)rtos->img_start);

    release_all_resources();

    rtos_start(rtos, NULL);

error:
    free(rtos);
    return ;
}

static unsigned char usb_test_buf[1024];

static void usb_gadget_serial_thread(void *data)
{
    int len, i;

    while (1) {
        msleep(1000);
        len = gadget_serial_read(usb_test_buf, sizeof(usb_test_buf), 0, 0);

        if (len > 0) {
            for (i = 0; i < len; i++){
                printf("usb read %c\n", usb_test_buf[i]);
            }
            if ( !strncmp(usb_test_buf, "boot-app", strlen("boot-app"))) {
                boot_app();
            }
        }
    }
}

int gadget_usb_serial_test(void)
{
    gadget_serial_init(&serial_id, &serial_parameters, serial_connect_callback, serial_param_callback);
    thread_create("usb gadget serial thread", 8192, usb_gadget_serial_thread, NULL);
    return 0;
}
