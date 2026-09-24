#include <common.h>
#include <os.h>
#include <driver/security_aes.h>

unsigned char data_buf[16] = "user-test-data";   /*用户输入的数据,也可以是16进制数组*/
unsigned char rev_buf[16];  /*加密之后的数据*/

static struct sc_aes_config config = {
    .mode = SC_ECB_MODE,           /* 编解码是否前后相关: SC_ECB_MODE:ecb SC_CBC_MODE:cbc */
};

void sc_aes_ecb_test(void)
{
    sc_aes_encryption(&config, data_buf, rev_buf, 16);
    /* 测试使用的是x1000_evb_v1.0(nor flash)硬件平台，在安全烧录时已烧录过UKEY */
    /* UKEY可以通过生成key.bin的工具kgen获得(工具路径：securitytool/x1000/keytool/) */
    /* 测试时的UKEY为：0x5eb234ab 7ca34546 1805c32c 0a08f751 */
    /* 由于使用的密钥为板子内的UKEY，若烧录的UKEY与默认的不同，得到的加密结果也不同 */
    /* 打印的加密的结果为0x6A 6D 73 65 2F 78 31 88  DE 45 9A 16 5E A1 7C A9 */
    for (int i = 0; i < 16; i++)
        printf("rev_buf[%d]: %x \n", i, rev_buf[i]);

    unsigned char enc_data[16];
    sc_aes_decryption(&config, rev_buf, enc_data, 16);

    int ret = memcmp(enc_data, data_buf, 16);
    if (ret == 0)
        printf("the result of decryption is true!\n");
    else
        printf("decryption result not equal to input data!\n");
}