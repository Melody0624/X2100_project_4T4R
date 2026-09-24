#ifndef _SC_AES_H_
#define _SC_AES_H_
#include <errno.h>

enum sc_aes_mode {
    SC_ECB_MODE,
    SC_CBC_MODE,
};

struct sc_aes_config {
    enum sc_aes_mode mode; /* 编解码是否前后相关: 0:ecb 1:cbc */
    unsigned char iv[17];       /* 初始化向量(cbc模式使用) */
};

/*SC_AES 初始化，无返回值*/
void sc_aes_init(void);

/**
 * SC_AES 加密
 * @config: sc_aes结构体，包括加密过程中的相关信息
 * @src: 需要加密的数据地址
 * @dst: 加密完成数据存放地址
 * @len: 需要加密的数据长度
 * 具体用法可参考 sc_aes_cbc_example.c 或者 sc_aes_ecb_example.c
 */
int sc_aes_encryption(struct sc_aes_config *config, void *src, void *dst, int len);

/**
 * SC_AES 解密
 * @config: sc_aes结构体，包括解密过程中的相关信息
 * @src: 需要解密的数据地址
 * @dst: 解密完成数据存放地址
 * @len: 需要解密的数据长度
 * 具体用法可参考 sc_aes_cbc_example.c 或者 sc_aes_ecb_example.c
 */
int sc_aes_decryption(struct sc_aes_config *config, void *src, void *dst, int len);

#endif