#include <common.h>
#include <os.h>
#include <usb/gadget_mass_storage.h>

static unsigned char mass_storage_buf[16*2048*512];

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4a5
};

static int fsg_read_callback(void *buf, int count, u64 pos)
{
    memcpy(buf, mass_storage_buf + pos, count);
    return count;
}

static int fsg_write_callback(const void *buf, int count, u64 pos)
{
    memcpy(mass_storage_buf + pos, buf, count);
    return count;
}

static struct fsg_lun_config mass_storage_lun_config = {
	.read_callback = fsg_read_callback,
	.write_callback = fsg_write_callback,

	.ro = 0,
	.removable = 1,
	.cdrom = 0,

	.num_sectors = 16*2048,
	.block_size = 512,
};

static void mass_storage_connect_callback(int connect)
{
    printf("mass_storage_connect_callback %d\n", connect);
}

static struct fsg_config mass_storage_config = {
	.nluns = 1,
	.luns = &mass_storage_lun_config,
	.connect_cb = mass_storage_connect_callback,
};

int gadget_usb_mass_storage_test(void)
{
    gadget_mass_storage_init(&usb_id, &mass_storage_config);

    return 0;
}
