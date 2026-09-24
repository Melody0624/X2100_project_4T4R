#include <common.h>
#include <os.h>
#include <driver/aes.h>

unsigned char data_buf[16] = "user-test-data";   /*用户输入的数据,也可以是16进制数组*/
unsigned char rev_buf[16];  /*加解密之后的数据*/

static struct aes_config config = {
    .keyl = AES256,             /* 密钥长度: AES128:128 AES196:196 AES256:256 */
    .mode = ECB_MODE,           /* 编解码是否前后相关: ECB_MODE:ecb CBC_MODE:cbc */
    .endian = ENDIAN_BIG,       /* 编解码数据输入大小端模式: ENDIAN_LITTLE:little ENDIAN_BIG:big */
    .ukey = "user-test-key",    /* 用户传入的密钥,也可以是16进制数组 */
};

void aes_ecb_test(void)
{
    aes_encryption(&config, data_buf, rev_buf, 16);
    /*打印的加密的结果为0x25 88 93 96 34 A4 FF B6 35 84 42 93 4D 76 99 A7*/
    for (int i = 0; i < 16; i++)
        printf("rev_buf[%d]: %x \n", i, rev_buf[i]);

    unsigned char enc_data[16];
    aes_decryption(&config, rev_buf, enc_data, 16);

    int ret = memcmp(enc_data, data_buf, 16);
    if (ret == 0)
        printf("the result of decryption is true!\n");
    else
        printf("decryption result not equal to input data!\n");
}