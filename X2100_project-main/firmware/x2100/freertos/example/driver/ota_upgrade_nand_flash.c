#include <stdio.h>
#include <common.h>
#include <os.h>
#include <usb/gadget_serial.h>
#include <driver/ota.h>
#include <driver/sfc_nand.h>

/*******************************usb cdc device********************************/
static const struct gadget_id serial_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a7,
};

static const struct usb_cdc_serial_param serial_parameters ={
    .dwDTERate = 115200,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8,
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

static int usb_pkt_read(uint8_t *buf, ssize_t size, uint32_t timeout_ms)
{
    int ret = 0;
    int left = size;
    unsigned char *ptr = buf;
    while (left > 0) {
        ret = gadget_serial_read((uint8_t *)ptr, left, 1, timeout_ms);
        if (ret <= 0)
            return ret;

        ptr += ret;
        left -= ret;
    }
    return (size - left);
}

static int usb_pkt_write(uint8_t *buf, ssize_t size)
{
    int ret = 0;
    int left = size;
    unsigned char *ptr = buf;

    while (left > 0) {
        ret = gadget_serial_write((uint8_t *)ptr, left, 1, PKT_TIMEOUT_MS);
        if (ret <= 0)
            break;

        ptr += ret;
        left -= ret;
    }
    return (size -left);
}

static int usb_pkt_init(void)
{
    return gadget_serial_init(&serial_id, &serial_parameters, serial_connect_callback, serial_param_callback);
}

static void usb_pkt_exit(void)
{
    return gadget_serial_cleanup();
}

struct transfer_cb transfer_cb = {
    .transfer_init = usb_pkt_init,
    .transfer_exit = usb_pkt_exit,
    .transfer_read = usb_pkt_read,
    .transfer_write = usb_pkt_write,
};

/*******************************nand flash********************************/
static int ota_storage_read(uint64_t *from, uint64_t len, uint8_t *buf)
{
    return sfc_nand_flash_read_check_badblock((uint32_t *)from, (uint32_t)len, buf);
}

static int ota_storage_write(uint64_t from, uint64_t len, const uint8_t *buf)
{
    return sfc_nand_flash_write_check_badblock((uint32_t)from, (uint32_t)len, buf);
}


static int ota_storage_erase(uint64_t addr, uint64_t len)
{
    return sfc_nand_flash_erase((uint32_t)addr, (uint32_t)len);
}

static const struct storage_info *ota_storage_info_get(void)
{
    const struct storage_info *storage_info = NULL;

    storage_info = sfc_nand_flash_info();

    return storage_info;
}

static int ota_storage_get_partition_information(char *name, uint64_t *offset, uint64_t *size)
{
    int ret = 0;
    uint32_t offset_32;
    ret = get_nand_partition_information_by_name(name, &offset_32, (uint32_t *)size);
    *offset = offset_32;

    return ret;
}

struct storage_cb storage_cb = {
    .storage_read = ota_storage_read,
    .storage_write = ota_storage_write,
    .storage_erase = ota_storage_erase,
    .storage_info_get = ota_storage_info_get,
    .storage_get_partition_information = ota_storage_get_partition_information,
};

void ota_upgrade_nand_flash_test(void)
{
    int ret;
    struct ota_updater *ota_updater = ota_init(&transfer_cb, &storage_cb);
    if (ota_updater == NULL) {
        printf("register ota ota_updater failed \n");
        return;
    }

    ret = ota_work(ota_updater);
    if (ret == 1)
        ota_deinit(ota_updater);
}