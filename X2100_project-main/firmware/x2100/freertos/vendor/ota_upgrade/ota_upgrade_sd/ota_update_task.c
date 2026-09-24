#include <dfs_posix.h>
#include <driver/sfc_nor.h>

#include "heap_malloc.h"

#define OTA_FILE_PATH "/mmcblk2p0/image.bin" // TF卡挂载在 /mmcblk2p0
#define TARGET_PARTITION "uboot"          // 主程序分区名

void ota_update_thread_func(void *param)
{
    int ret;
    size_t file_len;//文件大小

    while (1) {
        // 1. 检测 TF 卡中是否有升级文件
        if (access(OTA_FILE_PATH, F_OK) == 0) {
            printf("OTA: Upgrade file detected: %s, preparing to upgrade...\n", OTA_FILE_PATH);
            
            // ota_verify_flash_vs_file(OTA_FILE_PATH, TARGET_PARTITION, "BEFORE OTA");

            // 2. 获取分区信息 
            uint32_t offset = 0, size = 0;
            if (get_nor_partition_information_by_name(TARGET_PARTITION, &offset, &size)) {
                printf("OTA: Target partition not found!\n");
                sleep(5);
                continue;
            }

            // 3. 打开文件
            struct dfs_fd fd;
            if (dfs_file_open(&fd, OTA_FILE_PATH, O_RDONLY) < 0) {
                printf("OTA: File failed to open!\n");
                sleep(5);
                continue;
            }

            file_len = fd.size;
            printf("OTA: file len is : %d \n", file_len);

            /* 判断文件大小是否超出分区 */
            if (file_len > size) {
                printf("OTA: file len(%d) id exceed partition size(%d)!\n", file_len, size);
                dfs_file_close(&fd);
                return;
            }

            // 4. 开始刷写 Flash
            const struct storage_info *info = sfc_nor_flash_info();
            uint32_t address = offset;
            uint32_t blk_size = info->erasesize;
            uint32_t length, sum = 0;
            uint8_t *buffer = malloc(blk_size);
            memset(buffer, 0, blk_size);
            
            printf("OTA: Start flash writing, please do not disconnect power...\n");

            while (sum < file_len) {
                uint32_t bytes_read = 0;

                // 循环读取，直到填满一个 blk_size，或者触及文件末尾
                while (bytes_read < blk_size) {
                    length = dfs_file_read(&fd, buffer + bytes_read, blk_size - bytes_read);
                    if (length < 0) {
                        printf("OTA: file read error!\n");
                        break;
                    }
                    if (length == 0) {
                        break; // 真正触及文件末尾 (EOF)
                    }
                    bytes_read += length;
                }

                if (bytes_read == 0) break;

                /* 数据长度小于块大小，补齐数据至块大小 */
                if (bytes_read < blk_size)
                    memset(buffer + bytes_read, 0xff, blk_size - bytes_read);

                /* 按块擦除 */
                ret = sfc_nor_flash_erase(address, blk_size);
                if (ret < 0)
                    break;

                /* 按块写入 */
                ret = sfc_nor_flash_write(address, blk_size, buffer);
                if (ret < 0 || address >= offset + size) {
                    printf("OTA: address exceed partition end or write failed!\n");
                    break;
                }

                address += blk_size;
                sum += bytes_read;
                printf("\r update %8dBytes ", sum);
                fflush(stdout);
            }
            printf("\nOTA: Update end!\n");
            fflush(stdout);

            free(buffer);
            dfs_file_close(&fd);

            // 5. 收尾工作
            // 删除 TF 卡里的固件，防止无限重复升级
            // 也可以调整为其他操作，以终止继续烧录。
            unlink(OTA_FILE_PATH);
            
            // ota_verify_flash_vs_file(OTA_FILE_PATH, TARGET_PARTITION, "AFTER OTA");

            // 6. 重启系统
            printf("OTA: The system is about to restart....\n");
            // sleep(1);
            // 调用系统的复位函数，比如:
            // pmu_reboot(); 或 watchdog_reset();
            // 若不不调用，则需手动重启
            break;
        } else{
            printf("OTA: No upgrade files available.\n");        // 没检测到文件
            break;
        }
        
    }
}


void ota_init_task(void *param)
{
    thread_ptr_t ota_thread = thread_create("ota_task", 
                                            8192,                   // 分配足够大的栈空间
                                            ota_update_thread_func, 
                                            NULL);
    if (ota_thread == NULL) {
        printf("Failed to create OTA thread!\n");
    }
}

/**
 * @brief 校验 Flash 中的数据与 TF 卡文件是否完全一致
 * @param file_path TF卡文件路径
 * @param part_name Flash 分区名
 * @param stage_msg 打印提示语（如 "BEFORE OTA" 或 "AFTER OTA"）
 * @return 0: 完全一致; -1: 数据不一致; -2: 接口或环境错误
 */
// int ota_verify_flash_vs_file(const char *file_path, const char *part_name, const char *stage_msg)
// {
//     struct dfs_fd fd;
//     uint32_t offset = 0, size = 0;
//     size_t file_len = 0;
//     int ret = 0;

//     printf("\n--- [%s] Verification Start ---\n", stage_msg);

//     /* 1. 获取分区信息 */
//     if (get_nor_partition_information_by_name(part_name, &offset, &size)) {
//         printf("Verify: Target partition '%s' not found!\n", part_name);
//         return -2;
//     }

//     /* 2. 打开文件 */
//     if (dfs_file_open(&fd, file_path, O_RDONLY) < 0) {
//         printf("Verify: File '%s' failed to open!\n", file_path);
//         return -2;
//     }
//     file_len = fd.size;

//     if (file_len > size) {
//         printf("Verify: File len(%d) exceeds partition size(%d)!\n", file_len, size);
//         dfs_file_close(&fd);
//         return -2;
//     }

//     /* 3. 分配双重内存 (使用 4KB 进行对比) */
//     uint32_t blk_size = 4096;
//     uint8_t *file_buf = malloc(blk_size);
//     uint8_t *flash_buf = malloc(blk_size);

//     if (!file_buf || !flash_buf) {
//         printf("Verify: Malloc failed!\n");
//         if (file_buf) free(file_buf);
//         if (flash_buf) free(flash_buf);
//         dfs_file_close(&fd);
//         return -2;
//     }

//     /* 4. 循环对比 */
//     uint32_t address = offset;
//     uint32_t sum = 0;
//     int diff_found = 0;

//     while (sum < file_len) {
//         /* 读取文件 */
//         int length = dfs_file_read(&fd, file_buf, blk_size);
//         if (length <= 0) {
//             printf("\nVerify: File read error at offset 0x%x\n", sum);
//             ret = -2;
//             break;
//         }

//         /* 文件尾部不足一块，将剩余部分填 0xFF 以对齐 Flash 的逻辑 */
//         if (length < blk_size) {
//             memset(file_buf + length, 0xFF, blk_size - length);
//         }

//         /* 读取 Flash */
//         if (sfc_nor_flash_read(address, blk_size, flash_buf) < 0) {
//             printf("\nVerify: Flash read error at address 0x%x\n", address);
//             ret = -2;
//             break;
//         }

//         /* 内存比对 */
//         if (memcmp(file_buf, flash_buf, length) != 0) {
//             /* 找出具体第一个不同的字节并打印 */
//             for (int i = 0; i < length; i++) {
//                 if (file_buf[i] != flash_buf[i]) {
//                     printf("\n[ERROR] Data mismatch at Absolute Flash Addr: 0x%x (File Offset: 0x%x)\n", address + i, sum + i);
//                     printf("-> Expected (from File): 0x%02x, Actual (from Flash): 0x%02x\n", file_buf[i], flash_buf[i]);
//                     break;
//                 }
//             }
//             diff_found = 1;
//             ret = -1;
//             break;
//         }

//         address += blk_size;
//         sum += length;
//         printf("\r verify %8dBytes ", sum);
//         fflush(stdout);
//     }

//     if (diff_found == 0 && ret == 0) {
//         printf("\n[%s] Verification SUCCESS! Flash entirely matches TF file.\n", stage_msg);
//     } else {
//         printf("\n[%s] Verification FAILED!\n", stage_msg);
//     }

//     free(file_buf);
//     free(flash_buf);
//     dfs_file_close(&fd);

//     return ret;
// }