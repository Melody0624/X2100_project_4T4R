#ifndef _DFS_DEVICE_H
#define _DFS_DEVICE_H

#include <stdio.h>
#include <common.h>
#include <list.h>
#include <stdbool.h>
#include <stdarg.h>
#include <os.h>
#include <errno.h>

#ifdef CONFIG_OS

#define fs_set_errno(err)               os_set_errno(err)
#define fs_get_errno()                  os_get_errno()

#else
#define fs_set_errno(err)
#define fs_get_errno()                  (0)
#endif

/* command line */
extern int shell_printf(const char *__restrict fmt, ...);
#define elm_printf(...)                 shell_printf(__VA_ARGS__)

#define DEVICE_OFLAG_CLOSE              0x000       /**< device is closed */
#define DEVICE_OFLAG_RDONLY             0x001       /**< read only access */
#define DEVICE_OFLAG_WRONLY             0x002       /**< write only access */
#define DEVICE_OFLAG_RDWR               0x003       /**< read and write */
#define DEVICE_OFLAG_OPEN               0x008       /**< device is opened */
#define DEVICE_OFLAG_MASK               0xf0f       /**< mask of open flag */


#define DEVICE_FLAG_RDONLY              0x001       /**< read only */
#define DEVICE_FLAG_WRONLY              0x002       /**< write only */
#define DEVICE_FLAG_RDWR                0x003       /**< read and write */

#define DEVICE_FLAG_REMOVABLE           0x004       /**< removable device */
#define DEVICE_FLAG_STANDALONE          0x008       /**< standalone device */
#define DEVICE_FLAG_ACTIVATED           0x010       /**< device is activated */
#define DEVICE_FLAG_SUSPENDED           0x020       /**< device is suspended */
#define DEVICE_FLAG_STREAM              0x040       /**< stream mode */

#define DEVICE_CTRL_BLK_GETGEOME        0x10        /**< get geometry information   */
#define DEVICE_CTRL_BLK_SYNC            0x11        /**< flush data to block device */
#define DEVICE_CTRL_BLK_ERASE           0x12        /**< erase block on block device */
#define DEVICE_CTRL_BLK_AUTOREFRESH     0x13        /**< block device : enter/exit auto refresh mode */

/*
 * device (I/O) class type
 */
enum device_class_type {
    Device_Class_Char                   = 0,         /**< character device */
    Device_Class_Block,                              /**< block device */
    Device_Class_NetIf,                              /**< net interface */
    Device_Class_MTD,                                /**< memory device */
    Device_Class_CAN,                                /**< CAN device */
    Device_Class_RTC,                                /**< RTC device */
    Device_Class_Sound,                              /**< Sound device */
    Device_Class_Graphic,                            /**< Graphic device */
    Device_Class_I2CBUS,                             /**< I2C bus device */
    Device_Class_USBDevice,                          /**< USB slave device */
    Device_Class_USBHost,                            /**< USB host bus */
    Device_Class_SPIBUS,                             /**< SPI bus device */
    Device_Class_SPIDevice,                          /**< SPI device */
    Device_Class_SDIO,                               /**< SDIO bus device */
    Device_Class_PM,                                 /**< PM pseudo device */
    Device_Class_Pipe,                               /**< Pipe device */
    Device_Class_Portal,                             /**< Portal device */
    Device_Class_Timer,                              /**< Timer device */
    Device_Class_Miscellaneous,                      /**< Miscellaneous device */
    Device_Class_Unknown                             /**< unknown device */
};

typedef struct device *device_t;

/*
 * Device structure
 */
struct device
{
    const char *name;                   /**< device name  */
    enum device_class_type type;        /**< device type */
    uint8_t ref_count;                  /**< reference count */
    uint32_t flag;
    struct list_head link;              /**< next device, do not modify it */

    /* common device interface */
    int      (*init)(device_t dev);
    int      (*open)(device_t dev, uint16_t oflag);
    int      (*close)(device_t dev);
    uint32_t (*read)(device_t dev, uint64_t pos, void *buffer, uint32_t size);
    uint32_t (*write)(device_t dev, uint64_t pos, const void *buffer, uint32_t size);
    int      (*control)(device_t dev, uint8_t cmd, void *args);

    void *user_data;                    /**< device private data */
};

/*
 * block device geometry structure
 */
struct device_blk_geometry
{
    uint32_t sector_count;              /**< count of sectors */
    uint32_t bytes_per_sector;          /**< number of bytes per sector */
    uint32_t block_size;                /**< number of bytes to erase one block */
};

/*
 * sector arrange struct on block device
 */
struct device_blk_sectors
{
    uint32_t sector_begin;              /**< begin sector */
    uint32_t sector_end;                /**< end sector   */
};

device_t device_find(const char *name);

int device_register(device_t dev);
int device_unregister(device_t dev);

int device_open (device_t dev, uint16_t oflag);
int device_close(device_t dev);

uint32_t device_read (device_t dev, uint64_t pos, void *buffer, uint32_t size);
uint32_t device_write(device_t dev, uint64_t pos, const void *buffer,uint32_t size);
int  device_control(device_t dev, uint8_t cmd, void *arg);

/* do not call it */
void init_dfs_device_driver(void);

/* do not call it */
struct list_head *get_device_list(void);

#endif

