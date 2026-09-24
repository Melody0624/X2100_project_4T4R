#include <common.h>
#include <os.h>
#include <driver/security_aes.h>

unsigned char data_buf[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};   /* 用户输入的数据,也可以是16进制数组 */
unsigned char iv_buff[17] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};    /* 存放初始化向量 */
unsigned char rev_buf[16];  /*加解密之后的数据*/

static struct sc_aes_config config = {
    .mode = SC_CBC_MODE,           /* 编解码是否前后相关: SC_ECB_MODE:ecb SC_CBC_MODE:cbc */
    .iv = {0},                     /* 传入的初始化向量 */
};

void sc_aes_cbc_test(void)
{
    memcpy(config.iv, iv_buff, 16);
    sc_aes_encryption(&config, data_buf, rev_buf, 16);
    /* 测试使用的是x1000_evb_v1.0(nor flash)硬件平台，在安全烧录时已烧录过UKEY */
    /* UKEY可以通过生成key.bin的工具kgen获得(工具路径：securitytool/x1000/keytool/) */
    /* 测试时的UKEY为：0x5eb234ab 7ca34546 1805c32c 0a08f751 */
    /* 由于使用的密钥为板子内的UKEY，若烧录的UKEY与默认的不同，得到的加密结果也不同 */
    /*打印的加密的结果为0x0C 2F 84 14 F3 F7 01 2B  BF B2 55 9F D5 3B 49 AE*/
    for (int i = 0; i < 16; i++)
        printf("rev_buf[%d]: %x \n", i, rev_buf[i]);

    unsigned char enc_data[17];
    memcpy(config.iv, iv_buff, 16);
    sc_aes_decryption(&config, rev_buf, enc_data, 16);

    int ret = memcmp(enc_data, data_buf, 16);
    if (ret == 0)
        printf("the result of decryption is true!\n");
    else
        printf("decryption result not equal to input data!\n");
}