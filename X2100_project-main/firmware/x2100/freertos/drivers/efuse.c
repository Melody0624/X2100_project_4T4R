#include <driver/efuse.h>
#include <os.h>
#include <common.h>

int soc_efuse_write(enum segment_id seg_id, unsigned char *buf, int start, int size);

int soc_efuse_write_segment(enum segment_id seg_id, unsigned char *buf, int len);

int soc_efuse_read(enum segment_id seg_id, unsigned char *buf, int start, int size);

int soc_efuse_read_segment(enum segment_id seg_id, unsigned char *buf, int len);

void soc_efuse_init_driver(void);

int efuse_write(enum segment_id seg_id, unsigned char *buf, int start, int size)
{
    return soc_efuse_write(seg_id, buf, start, size);
}

int efuse_write_segment(enum segment_id seg_id, unsigned char *buf, int len)
{
    return soc_efuse_write_segment(seg_id, buf, len);
}

int efuse_read(enum segment_id seg_id, unsigned char *buf, int start, int size)
{
    return soc_efuse_read(seg_id, buf, start, size);
}

int efuse_read_segment(enum segment_id seg_id, unsigned char *buf, int len)
{
    return soc_efuse_read_segment(seg_id, buf, len);
}

void efuse_init(void)
{
    soc_efuse_init_driver();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(efuse_write);
EXPORT_SYMBOL(efuse_read);
EXPORT_SYMBOL(efuse_write_segment);
EXPORT_SYMBOL(efuse_read_segment);