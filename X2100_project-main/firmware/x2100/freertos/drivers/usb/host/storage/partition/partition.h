#ifndef _HOST_U_DISK_PARTITION_H_
#define _HOST_U_DISK_PARTITION_H_

#include <usb/host_mass_storage.h>
#include <storage_device_driver.h>

#define DISK_MAX_PARTS                  256
#define STORAGE_MAX_PARTITION_NUM       8

#define STORAGE_PARTITION_NAME_LEN      64

int file_system_root_path_is_valid(void);
struct storage_device_partition *efi_partition(const char *udisk_name, struct usb_storage_device *dev);
struct storage_device_partition *msdos_partition(const char *udisk_name, struct usb_storage_device *dev);

#endif /* _HOST_U_DISK_PARTITION_H_ */