
CFLAGS += -Ifilesystem/flash/

src-$(CONFIG_SFC_NOR) += mtd_devices_nor.c mtd_partition_nor_ops.c
src-$(CONFIG_DFS_DEBUGFS) += ram_debugfs_ops.c
src-$(CONFIG_MMC) += block_devices_mmc.c block_partition_mmc_ops.c
src-$(CONFIG_RAMDISK) += ramdisk_block_devices.c ramdisk_block_partition_ops.c
src-$(CONFIG_USB_HOST_MASS_STORAGE_AUTO_MOUNT) += storage_devices.c storage_partition_ops.c


src-$(CONFIG_SFC_NAND) += mtd_devices_nand.c mtd_partition_nand_ops.c
