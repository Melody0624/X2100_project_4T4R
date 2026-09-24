#ifndef _HASH_H_
#define _HASH_H_

#define MD5BYTE             16
#define SHA1BYTE            20
#define SHA224BYTE          28
#define SHA256BYTE          32

enum encryption_mode {
    MD5,
    SHA1,
    SHA224,
    SHA256,
};

void hash_init(void);
int hash_request(enum encryption_mode mode, unsigned int timeout_ms);
int hash_write(int handle, unsigned char *str,unsigned int str_size);
int hash_read_free(int handle, unsigned char *rec, unsigned long hash_size);
void hash_deinit(void);

#endif