#ifndef _SOC_HASH_H_
#define _SOC_HASH_H_

enum encryption_mode {
    MD5,
    SHA1,
    SHA224,
    SHA256,
};

void soc_hash_init(void);
int soc_hash_request(enum encryption_mode mode, unsigned int timeout_ms);
int soc_hash_write(int handle, unsigned char *str, unsigned int str_size);
int soc_hash_read_free(int handle, unsigned char *rec, unsigned long hash_size);
void soc_hash_deinit(void);

#endif