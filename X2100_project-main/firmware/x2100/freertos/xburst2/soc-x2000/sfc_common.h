#ifndef _SFC_FLASH_COMMON_H_
#define _SFC_FLASH_COMMON_H_

#include <stdint.h>
#include <list.h>
#include "sfc_regs.h"
#include "sfc_nor_params.h"
#include <os.h>

#include "mtd_driver_nor.h"

/* the max number of DMA Descriptor */
#define SFC_DESC_MAX_NUM                64
#define SFC_RETRY_COUNT                 3
#define SFC_TRANSFER_TIMEOUT            3000
/*
 * sfc transfer command
 */
struct cmd_info {
    uint8_t cmd;
    uint8_t dataen;
    uint8_t pollen;
    uint8_t sta_exp;
    uint8_t sta_msk;
};

struct sfc_transfer {
    struct cmd_info cmd_info;

    uint8_t addr_len;
    uint8_t direction;
    uint8_t data_dummy_bits;/* addr + data_dummy_bits + data */
    uint32_t addr;
    uint32_t addr_plus;

    uint8_t sfc_mode;
    uint8_t ops_mode;
    /* phase_format: just use default value;phase1:cmd+dummy+addr... phase0:cmd+addr+dummy... */
    uint8_t phase_format;
    uint8_t *data;
    uint32_t len;
    uint32_t cur_len;

    struct list_head list;
};

struct data_config {
    uint32_t datalen;
    uint32_t cur_len;
    uint8_t data_dir;
    uint8_t ops_mode;
    uint8_t *buf;
};

struct sfc_cdt_xfer {
    unsigned short cmd_index;
    uint8_t dataen;

    struct data_config config;
    struct {
        uint32_t columnaddr;
        uint32_t rowaddr;
        uint32_t staaddr0;
        uint32_t staaddr1;
    };
};

/*
 * create cdt table
 */
enum{
    COL_ADDR,
    ROW_ADDR,
    STA_ADDR0,
    STA_ADDR1,
};

struct sfc_cdt{
    uint32_t link;
    uint32_t xfer;
    uint32_t staExp;
    uint32_t staMsk;
};

#define CMD_XFER(ADDR_WIDTH, POLL_EN, DMY_BITS, DATA_EN, CMD) ( \
    (ADDR_WIDTH << TRAN_CONF0_ADDR_WIDTH_OFFSET) \
    | (POLL_EN << TRAN_CONF0_POLL_OFFSET) \
    | (TRAN_CONF0_CMDEN) \
    | (0 << TRAN_CONF0_FMAT_OFFSET) \
    | (DMY_BITS << TRAN_CONF0_DMYBITS_OFFSET) \
    | (DATA_EN << TRAN_CONF0_DATEEN_OFFSET) \
    | CMD \
    )

#define CMD_LINK(LINK, ADDRMODE, TRAN_MODE) ( \
    (LINK << 31) | (TRAN_MODE << TRAN_CONF1_TRAN_MODE_OFFSET) | (ADDRMODE) \
    )

#define MK_CMD(cdt, cmd_info, LINK, ADDRMODE, DATA_EN)  { \
    cdt.link = CMD_LINK(LINK, ADDRMODE, cmd_info.transfer_mode); \
    cdt.xfer = CMD_XFER(cmd_info.addr_nbyte, DISABLE, cmd_info.dummy_byte, DATA_EN, cmd_info.cmd); \
    cdt.staExp = 0; \
    cdt.staMsk = 0; \
}

#define MK_ST(cdt, st_info, LINK, ADDRMODE, ADDR_WIDTH, POLL_EN, DATA_EN, TRAN_MODE)  { \
    cdt.link = CMD_LINK(LINK, ADDRMODE, TRAN_MODE); \
    cdt.xfer = CMD_XFER(ADDR_WIDTH, POLL_EN, st_info.dummy, DATA_EN, st_info.cmd); \
    cdt.staExp = (st_info.val << st_info.bit_shift); \
    cdt.staMsk = (st_info.mask << st_info.bit_shift); \
}

struct sfc_desc {
    unsigned int next_des_addr;
    unsigned int mem_addr;
    unsigned int tran_len;
    unsigned int link;
};

struct jz_sfc {
    struct clk * clk;
    struct clk * clk_gate;
    unsigned long long clk_rate;
    uint32_t threshold;
    uint32_t quad_mode;
    int irq;
    thread_waiter_t   data_wait;
    struct sfc_transfer *transfer;
    volatile void *cdt_addr;
    struct sfc_cdt_xfer *xfer;
    struct mini_spi_nor_info g_nor_info;
    struct sfc_desc *desc;
    uint32_t desc_max_num;
    uint32_t retry_count;
};


/*
 * SFC Flash support
 */
#define SIZEOF_NAME                     (32)

struct sfc_quad_mode {
    char dummy_byte;
    char RDSR_CMD;
    unsigned int RD_DATE_SIZE;  /* the data is write the spi status register for QE bit */

    char WRSR_CMD;
    unsigned int WD_DATE_SIZE;  /* the data is write the spi status register for QE bit */

    unsigned int RDSR_DATE;     /* the data is write the spi status register for QE bit */
    unsigned int WRSR_DATE;     /* this bit should be the flash QUAD mode enable */

    char cmd_read;
    char sfc_mode;
};


struct sfc_flash_support {
    unsigned int id_manufactory;
    char id_device;
    char name[SIZEOF_NAME];
    int page_size;
    int oobsize;
    int sector_size;
    int block_size;
    int addr_size;
    int size;
    int page_num;
    unsigned int *page_list;
    unsigned short column_cmdaddr_bits;/* read from cache ,the bits of cmd + addr */
    struct sfc_quad_mode quad_mode;
};



/*
 * SPI Flash Instructions
 */
#define CMD_RSTEN                       0x66    /* Reset Enable */
#define CMD_RST                         0x99    /* Reset */
#define CMD_WREN                        0x06    /* Write Enable */
#define CMD_WRDI                        0x04    /* Write Disable */
#define CMD_RDSR                        0x05    /* Read Status Register */
#define CMD_RDSR_1                      0x35    /* Read Status1 Register */
#define CMD_RDSR_2                      0x15    /* Read Status2 Register */
#define CMD_WRSR                        0x01    /* Write Status Register */
#define CMD_WRSR_1                      0x31    /* Write Status1 Register */
#define CMD_WRSR_2                      0x11    /* Write Status2 Register */
#define CMD_READ                        0x03    /* Read Data low speed*/
#define CMD_FAST_READ                   0x0B    /* Read Data at high speed */
#define CMD_DUAL_READ                   0x3b    /* Read Data at QUAD fast speed*/
#define CMD_QUAD_READ                   0x6b    /* Read Data at QUAD fast speed*/
#define CMD_QUAD_IO_FAST_READ           0xeb    /* Read Data at QUAD IO fast speed*/
#define CMD_PP                          0x02    /* Page Program(write data) */
#define CMD_QPP                         0x32    /* QUAD Page Program(write data) */
#define CMD_SE                          0xD8    /* Sector Erase */
#define CMD_BE                          0xC7    /* Bulk or Chip Erase */
#define CMD_DP                          0xB9    /* Deep Power-Down */
#define CMD_RES                         0xAB    /* Release from Power-Down and Read Electronic Signature */
#define CMD_RDID                        0x9F    /* Read Identification */
#define CMD_SR_WIP                      (1 << 0)
#define CMD_SR_QEP                      (1 << 1)
#define CMD_ERASE_4K                    0x20     /* Block Erase */
#define CMD_ERASE_32K                   0x52     /* Block Erase */
#define CMD_ERASE_64K                   0xD8     /* Block Erase */
#define CMD_ERASE_CE                    0x60
#define CMD_EN4B                        0xB7
#define CMD_EX4B                        0xE9
#define CMD_DIE_SEL                     0xc2     /* Software Die Select */
#define CMD_READ_DIE_ID                 0xf8     /* Read Active Die ID */

#define SSI_FRMHL_CE0_LOW_CE1_LOW       (0 << 30)
#define SSI_FRMHL_CE0_HIGH_CE1_LOW      (1 << 30)
#define SSI_FRMHL_CE0_LOW_CE1_HIGH      (2 << 30)
#define SSI_FRMHL_CE0_HIGH_CE1_HIGH     (3 << 30)
#define SSI_GPCMD                       (1 << 25)


/*
 * SPI Nand(GD)
 */
#define CMD_PARD                        0x13    /* page read */
#define CMD_PE                          0x10    /* program execute*/
#define CMD_PRO_LOAD                    0x02    /* program load */
#define CMD_PRO_RDM                     0x84    /* program random */
#define CMD_R_CACHE                     0x03    /* read from cache */
#define CMD_FR_CACHE                    0x0b    /* fast read from cache */
#define CMD_GET_FEATURE                 0x0f

#define FEATURE_ADDR                    0xc0

#define P_FAIL                          (1 << 3)    /* program fail */
#define E_FAIL                          (1 << 2)    /* erase fail */
#define SPINAND_IS_BUSY                 (1 << 0)    /* read , write ,erase ops is executing*/
#define ECC_UNCORRECTED                 (0x10 << 2) /* ecc uncorrected */

/*
 * 3/4 bytes address
 */
#define CMD_READ4                       0x13    /* Read Data */
#define CMD_FAST_READ4                  0x0C    /* Read Data at high speed */
#define CMD_PP_4B                       0x12    /* Page Program(write data) */
#define CMD_ERASE_4K_4B                 0x21
#define CMD_ERASE_32K_4B                0x5C
#define CMD_ERASE_64K_4B                0xDC

#define NOR_SIZE_16M                    0x1000000
#define DATA_ENABLE                     (1)
#define DATA_DISABLE                    (0)

extern void soc_sfc_smp_delay(struct jz_sfc *jz_sfc, uint32_t value);
extern int32_t soc_set_flash_timing(struct jz_sfc *jz_sfc,
        uint32_t t_hold, uint32_t t_setup, uint32_t t_shslrd, uint32_t t_shslwr);
extern struct jz_sfc * soc_sfc_init(unsigned long rate_clk);
extern void soc_sfc_deinit(struct jz_sfc *jz_sfc);
extern int32_t sfc_sync(struct jz_sfc *sfc, struct sfc_cdt_xfer *head);
extern void sfc_transfer_del(struct sfc_transfer *entry);
extern void sfc_list_add_tail(struct sfc_transfer *new, struct sfc_transfer *head);
extern void sfc_list_init(struct sfc_transfer *head);
extern void sfc_dma_cache_sync_to_device(void *buf, uint32_t len);
extern void sfc_dma_cache_sync_from_device(void *buf, uint32_t len);

#endif /* end of _SFC_FLASH_COMMON_H_ */
