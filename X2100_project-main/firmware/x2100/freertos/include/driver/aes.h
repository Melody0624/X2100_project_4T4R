#ifndef _AES_H_
#define _AES_H_
#include <errno.h>

enum aes_keyl {
    AES128,
    AES192,
    AES256,
};

enum aes_mode {
    ECB_MODE,
    CBC_MODE,
};

enum aes_endian {
    ENDIAN_LITTLE,
    ENDIAN_BIG, /* openssl uses */
};

struct aes_config {
    enum aes_keyl keyl;         /* 密钥长度: 0:128 1:192 2:256 */
    enum aes_mode mode;         /* 编解码是否前后相关: 0:ecb 1:cbc */
    enum aes_endian endian;     /* 编解码数据输入大小端模式: 0:little 1:big*/
    unsigned char ukey[33];     /* 用户传入的密钥 */
    unsigned char iv[17];       /* 初始化向量(cbc模式使用) */
};

/*AES 初始化，无返回值*/
void aes_init(void);

/**
 * AES 加密
 * @config: aes结构体，包括加密过程中的相关信息
 * @src: 需要加密的数据地址
 * @dst: 加密完成数据存放地址
 * @len: 需要加密的数据长度
 * 具体用法可参考 aes_cbc_example.c 或 aes_ecb_dma_example.c
 */
int aes_encryption(struct aes_config *config, void *src, void *dst, int len);

/**
 * AES 解密
 * @config: aes结构体，包括解密过程中的相关信息
 * @src: 需要解密的数据地址
 * @dst: 解密完成数据存放地址
 * @len: 需要解密的数据长度
 * 具体用法可参考 aes_cbc_example.c 或 aes_ecb_dma_example.c
 */
int aes_decryption(struct aes_config *config, void *src, void *dst, int len);

#endif