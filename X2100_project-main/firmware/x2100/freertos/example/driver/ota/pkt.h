#ifndef __PKT_H__
#define __PKT_H__

#include "pkt_common.h"

struct package_info {
    int num;
    unsigned int sig;
    unsigned int code_len;
    unsigned int code_verify;
    unsigned int head_verify;
    enum pkt_type pkt_type;
};
extern unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);

#define ACK_PKT_SIZE 4 + sizeof(struct package_info)

void package_send(struct pkt_ops *ops, struct pkt_cfg *cfg, char *package_buf);

int package_read(struct pkt_ops *ops, struct pkt_cfg *cfg, unsigned char *package_buf);

void ack_package_send(struct pkt_ops *ops, int num);

struct pkt_dev *pkt_init(const char *name, struct pkt_ops *ops, pkt_callback cb);

struct pkt_dev *pkt_get_dev(const char *name);

void pkt_exit(struct pkt_dev *dev);

int pkt_init_tty(unsigned char *port, int baud, pkt_callback cb);

struct pkt_dev *pkt_get_usb_dev(void);

void pkt_deinit_tty(struct pkt_dev *dev);

int pkt_write_sync(struct pkt_dev *dev, struct pkt_cfg *pkt_cfg);

int pkt_read_sync(struct pkt_dev *dev, struct pkt_cfg *pkt_cfg);

void pkt_copy_cfg_info(struct pkt_cfg *src, struct pkt_cfg *dst);

#endif
