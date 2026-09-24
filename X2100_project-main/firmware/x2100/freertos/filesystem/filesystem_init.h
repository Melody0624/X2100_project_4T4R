#ifndef _FILESYSTEM_INIT_H_
#define _FILESYSTEM_INIT_H_


#ifndef CONFIG_RAM_DEVICE_NAME
#define CONFIG_RAM_DEVICE_NAME          "ram0"
#endif

#ifndef CONFIG_RAM_DEVICE_DIR
#define CONFIG_RAM_DEVICE_DIR           "/sys"
#endif

#ifndef CONFIG_RAM_DEVICE_SIZE
#define CONFIG_RAM_DEVICE_SIZE          (128*1024)
#endif

#ifndef CONFIG_DATA_RES_DEVICE_DIR
#define CONFIG_DATA_RES_DEVICE_DIR      "/data"
#endif

#ifdef CONFIG_DFS
extern void file_system_init(void);
#endif

#endif /* _FILESYSTEM_INIT_H_ */

