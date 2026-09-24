#include <driver/efuse.h>

/* 下面以 x1830 为例
 */
void efuse_test(void)
{
    int i;
    unsigned char chip_id_buff[CHIP_ID_SIZE];
    unsigned char user_id_buff[USER_ID_SIZE];
    unsigned char customer_resv_buff[CUSTOMER_RESV_SIZE];

    /* efuse 读 CHIP_ID 段，
     * 每次读的大小和段大小(12byte)保持一致，单位 byte
     */
    efuse_read_segment(CHIP_ID, chip_id_buff, CHIP_ID_SIZE);

    printf("CHIP_ID = ");
    for (i = 0; i < CHIP_ID_SIZE; i++)
        printf("%02x", chip_id_buff[i]);

    printf("\n");

    /* efuse 写 USER_ID 段，
     * 一次写的大小需和段大小保持一致，单位 byte.
     */
    for (i = 0; i < USER_ID_SIZE; i++)
        user_id_buff[i] = 0x11;

    efuse_write_segment(USER_ID, user_id_buff, USER_ID_SIZE);

    /* 读取 efuse CUSTOMER_RESV段
     * 从 第 2 个字节开始的 4 个字节数据
     */
    efuse_read(CUSTOMER_RESV, customer_resv_buff, 2, 4);

    printf("CUSTOMER_RESV = ");
    for (i = 0; i < 4; i++)
        printf("%02x", customer_resv_buff[i]);

    printf("\n");

    /* efuse 读 CUSTOMER_RESV 段，
     * 每次读的大小和段大小(104byte)保持一致，单位 byte
     */
    efuse_read_segment(CUSTOMER_RESV, customer_resv_buff, CUSTOMER_RESV_SIZE);

    printf("CUSTOMER_RESV = ");
    for (i = 0; i < CUSTOMER_RESV_SIZE; i++)
        printf("%02x", customer_resv_buff[i]);

    printf("\n");
}