#ifndef _SOC_EFUSE_H_
#define _SOC_EFUSE_H_

enum segment_id {
    CHIP_ID,
    CUSTOMER_ID0,
    CUSTOMER_ID1,
    CUSTOMER_ID2,
    TRIM_DATA0,
    TRIM_DATA1,
    TRIM_DATA2,
    SOC_INFO,
    PROGRAM_PROTECT,
    HIDE_BLOCK,
    CHIP_KEY,
    USER_KEY0,
    USER_KEY1,
    NKU,
};

#ifdef CONFIG_EFUSE_MODE_HAMMING
    #define     CHIP_ID_SIZE            16
    #define     CUSTOMER_ID_SIZE0       16
    #define     CUSTOMER_ID_SIZE1       27
    #define     CUSTOMER_ID_SIZE2       27
    #define     TRIM_DATA_SIZE0         4
    #define     TRIM_DATA_SIZE1         4
    #define     TRIM_DATA_SIZE2         4
    #define     SOC_INFO_SIZE           3
    #define     PROGRAM_PROTECT_SIZE    2
    #define     HIDE_BLOCK_SIZE         2
    #define     CHIP_KEY_SIZE           32
    #define     USER_KEY_SIZE0          32
    #define     USER_KEY_SIZE1          32
    #define     NKU_SIZE                32
#else
    #define     CHIP_ID_SIZE            17
    #define     CUSTOMER_ID_SIZE0       17
    #define     CUSTOMER_ID_SIZE1       29
    #define     CUSTOMER_ID_SIZE2       29
    #define     TRIM_DATA_SIZE0         5
    #define     TRIM_DATA_SIZE1         5
    #define     TRIM_DATA_SIZE2         5
    #define     SOC_INFO_SIZE           5
    #define     PROGRAM_PROTECT_SIZE    4
    #define     HIDE_BLOCK_SIZE         4
    #define     CHIP_KEY_SIZE           34
    #define     USER_KEY_SIZE0          34
    #define     USER_KEY_SIZE1          34
    #define     NKU_SIZE                34
#endif

#endif