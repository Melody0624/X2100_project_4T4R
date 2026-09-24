#ifndef _EFUSE_H_
#define _EFUSE_H_

#include <soc/efuse.h>

void efuse_init(void);

int efuse_write(enum segment_id seg_id, unsigned char *buf, int start, int size);

int efuse_write_segment(enum segment_id seg_id, unsigned char *buf, int len);

int efuse_read(enum segment_id seg_id, unsigned char *buf, int start, int size);

int efuse_read_segment(enum segment_id seg_id, unsigned char *buf, int len);

#endif /* _EFUSE_H_ */