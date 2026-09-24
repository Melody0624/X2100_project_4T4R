#include <common.h>
#include <os.h>
#include <driver/aes.h>

unsigned char data_buf[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};  /*用户输入的数据*/
unsigned char iv_buff[17] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};  /*存放初始化向量*/
unsigned char rev_buf[16];  /*加解密之后的数据*/

struct aes_config config = {
    .keyl = AES256,             /* 密钥长度: AES128:128 AES196:196 AES256:256 */
    .mode = CBC_MODE,           /* 编解码是否前后相关: ECB_MODE:ecb CBC_MODE:cbc */
    .endian = ENDIAN_BIG,       /* 编解码数据输入大小端模式: ENDIAN_LITTLE:little ENDIAN_BIG:big */
    .ukey = "user-test-key",    /* 用户传入的密钥, 也可以是16进制数组 */
    .iv = {0},                  /* 初始化向量(cbc模式使用) */
};

void aes_cbc_test(void)
{
    memcpy(config.iv, iv_buff, 17);
    aes_encryption(&config, data_buf, rev_buf, 16);
    /*打印的加密的结果为0x0D BB 04 75 78 A0 9D 31 E7 10 41 22 22 25 07 61*/
    for (int i = 0; i < 16; i++)
        printf("rev_buf[%d]: %x\n", i, rev_buf[i]);

    unsigned char enc_data[16];
    memcpy(config.iv, iv_buff, 17);
    aes_decryption(&config, rev_buf, enc_data, 16);

    int ret = memcmp(enc_data, data_buf, 16);
    if (ret == 0)
        printf("the result of decryption is true!\n");
    else
        printf("decryption result not equal to input data!\n");
}
