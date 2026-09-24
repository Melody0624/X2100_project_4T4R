#include "config_manager.h"
#include "mmw_msg_pkt.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <driver/sfc_nor.h>
#include <driver/cache.h>

#include "heap_malloc.h"

#define CONFIG_FLASH_OFFSET 0x1DB000
#define CONFIG_MAGIC_WORD 0x52414456 // "RADV" - updated to force recreation for layout shift (added calibResultStatus)

/* 
 * 预留足够大的空间给雷达配置表 
 * cheetah_default_config 实际约130条记录，每条76字节，总计约10KB。
 * 此处分配 40KB (40960 Bytes) 确保空间充足且兼容未来扩展。
 */
#define MAX_CHEETAH_CFG_SIZE 40960

#define MIN_CHEETAH_CFG_SIZE 32768 // 32KB

/* 
 * 强制一字节对齐的虚拟布局，严格映射Flash物理布局。
 * 本结构体仅用于 offsetof 计算，绝对不在RAM中实例化（0常驻内存开销）。
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;                      // 4 Bytes: "RADT" 标识符
    uint8_t  serial_number[5];           // 5 Bytes: 序列号
    uint8_t  production_date[4];         // 4 Bytes: 生产日期
    uint8_t  cheetah_default_config[MAX_CHEETAH_CFG_SIZE];  // 40960 Bytes
    uint8_t  cheetah_fft_comp_config[MIN_CHEETAH_CFG_SIZE];  // 32768 Bytes
    uint8_t  cheetah_calib_config[MAX_CHEETAH_CFG_SIZE];    // 40960 Bytes
    uint8_t  cheetah_dot_config[MIN_CHEETAH_CFG_SIZE];      // 32768 Bytes  
    float    radarInstallAng;            // 4 Bytes
    uint8_t  isCalibInstallAng;          // 1 Byte
    uint8_t  isFlipInstall_azm;          // 1 Byte
    uint8_t  isInhighAltitude;           // 1 Byte
    float    calibTargetDistance;        // 4 Bytes: 标靶距离
    float    calibTargetAngle;           // 4 Bytes: 标靶角度
    float    calibPowerThreshold;        // 4 Bytes: 标靶能量阈值
    float    calibTargetVelocity;        // 4 Bytes: 标靶速度
    float    calibResultAngle;           // 4 Bytes: 校准结果补偿角度
    uint8_t  calibResultStatus;          // 1 Byte: 校准结果状态
    RadarStatusMsg radarStatus;          // 雷达状态配置 (使用 mmw_msg_pkt.h 中的定义)
    float  angCalibMat[16];            // 64 Bytes: 角度补偿矩阵 (8个复数)
    float  angFFT[128];
    int32_t  maxFrameCnt;                // 4 Bytes: 最大帧数 (SetMaxFrameCnt 配置)
    uint8_t  outRawDataFlg;              // 1 Byte: 输出Raw Data标志 (1=输出, 0=不输出)
} SysConfigLayout;
#pragma pack(pop)

typedef struct {
    uint32_t offset;
    uint32_t length;
} ParamInfo;

/*
 * 将偏移映射表固化在ROM (Text段)中，彻底摆脱动态分配负担。
 */
static const ParamInfo g_param_table[PARAM_MAX_COUNT] = {
    [PARAM_SERIAL_NUMBER]        = { offsetof(SysConfigLayout, serial_number), 5 },
    [PARAM_PRODUCTION_DATE]      = { offsetof(SysConfigLayout, production_date), 4 },
    [PARAM_ANG_CALIB_MAT]        = { offsetof(SysConfigLayout, angCalibMat), 16 * sizeof(float) },
    [PARAM_ANG_FFT_CFG]          = { offsetof(SysConfigLayout, angFFT), 128 * sizeof(float) },
    [PARAM_CHEETAH_DEFAULT_CFG]  = { offsetof(SysConfigLayout, cheetah_default_config), MAX_CHEETAH_CFG_SIZE },
    [PARAM_CHEETAH_FFT_COMP_CFG] = { offsetof(SysConfigLayout, cheetah_fft_comp_config), MIN_CHEETAH_CFG_SIZE },
    [PARAM_CHEETAH_CALIB_CFG]    = { offsetof(SysConfigLayout, cheetah_calib_config), MAX_CHEETAH_CFG_SIZE },
    [PARAM_CHEETAH_DOT_CFG]      = { offsetof(SysConfigLayout, cheetah_dot_config), MIN_CHEETAH_CFG_SIZE },
    [PARAM_RADAR_INSTALL_ANG]    = { offsetof(SysConfigLayout, radarInstallAng), 4 },
    [PARAM_IS_CALIB_INSTALL_ANG] = { offsetof(SysConfigLayout, isCalibInstallAng), 1 },
    [PARAM_IS_FLIP_INSTALL_AZM]  = { offsetof(SysConfigLayout, isFlipInstall_azm), 1 },
    [PARAM_IS_IN_HIGH_ALTITUDE]  = { offsetof(SysConfigLayout, isInhighAltitude), 1 },
    [PARAM_CALIB_TARGET_DIST]    = { offsetof(SysConfigLayout, calibTargetDistance), 4 },
    [PARAM_CALIB_TARGET_ANG]     = { offsetof(SysConfigLayout, calibTargetAngle), 4 },
    [PARAM_CALIB_POWER_TH]       = { offsetof(SysConfigLayout, calibPowerThreshold), 4 },
    [PARAM_CALIB_TARGET_VEL]     = { offsetof(SysConfigLayout, calibTargetVelocity), 4 },
    [PARAM_CALIB_RESULT_ANG]     = { offsetof(SysConfigLayout, calibResultAngle), 4 },
    [PARAM_CALIB_RESULT]         = { offsetof(SysConfigLayout, calibResultStatus), 1 },
    [PARAM_RADAR_STATUS]         = { offsetof(SysConfigLayout, radarStatus), sizeof(RadarStatusMsg) },
    [PARAM_MAX_FRAME_CNT]        = { offsetof(SysConfigLayout, maxFrameCnt), 4 },
    [PARAM_OUT_RAW_DATA_FLG]     = { offsetof(SysConfigLayout, outRawDataFlg), 1 },
};

int config_manager_init(const void *default_cheetah_cfg, uint32_t cheetah_cfg_len, float def_radarInstallAng, uint8_t def_isCalibInstallAng, uint8_t def_isInhighAltitude, uint8_t def_isFlipInstall_azm) {
    uint32_t magic = 0;
    int needs_creation = 0;
    int32_t default_max_frame_cnt = -1;

    const struct storage_info *info = sfc_nor_flash_info();
    
    sfc_nor_flash_read(CONFIG_FLASH_OFFSET, sizeof(magic), (uint8_t*)&magic);
    flush_dcache_all();
    
    if (magic == CONFIG_MAGIC_WORD) {
        printf("[CONFIG] Config flash partition verified.\n");
        int32_t saved_cnt = 0;
        if (param_get(PARAM_MAX_FRAME_CNT, &saved_cnt, sizeof(saved_cnt)) == 0) {
            if (saved_cnt == (int32_t)0xFFFFFFFF) {
                printf("[CONFIG] maxFrameCnt is erased state, writing default=0.\n");
                param_set(PARAM_MAX_FRAME_CNT, &default_max_frame_cnt, sizeof(default_max_frame_cnt));
            }
        } else {
            printf("[CONFIG] maxFrameCnt param not readable, writing default=0.\n");
            param_set(PARAM_MAX_FRAME_CNT, &default_max_frame_cnt, sizeof(default_max_frame_cnt));
        }
        return 0;
    }
    
    printf("[CONFIG] Magic word mismatch (magic: 0x%x), re-creating config...\n", magic);
    needs_creation = 1;

    if (needs_creation) {
        uint32_t sector_size = info->erasesize;
        uint32_t config_size = sizeof(SysConfigLayout);
        uint32_t erase_size = ((config_size + sector_size - 1) / sector_size) * sector_size;
        
        printf("[CONFIG] Erasing flash at 0x%x, size: %u bytes\n", CONFIG_FLASH_OFFSET, erase_size);
        int ret = sfc_nor_flash_erase(CONFIG_FLASH_OFFSET, erase_size);
        if (ret != 0) {
            printf("[CONFIG] Flash erase error! ret=%d\n", ret);
            return -1;
        }
        
        uint32_t init_buf[64] __attribute__((aligned(64)));
        uint32_t *init_ptr = init_buf;
        init_ptr[0] = CONFIG_MAGIC_WORD;
        memset(&init_ptr[1], 0xFF, sizeof(init_buf) - 4);
        
        flush_dcache_all();
        
        uint32_t offset = CONFIG_FLASH_OFFSET;
        uint32_t remain = config_size;
        
        while (remain > 0) {
            uint32_t write_len = remain > sizeof(init_buf) ? sizeof(init_buf) : remain;
            flush_dcache_all();
            ret = sfc_nor_flash_write(offset, write_len, (uint8_t *)init_ptr);
            if (ret != write_len) {
                printf("[CONFIG] Flash write error! ret=%d expected=%u\n", ret, write_len);
                return -1;
            }
            offset += write_len;
            remain -= write_len;
            memset(init_buf, 0xFF, sizeof(init_buf));
        }
        
        printf("[CONFIG] Default config (%ld bytes) created successfully at 0x%x.\n", config_size, CONFIG_FLASH_OFFSET);
        
        if (default_cheetah_cfg && cheetah_cfg_len > 0) {
            if (param_set(PARAM_CHEETAH_DEFAULT_CFG, default_cheetah_cfg, cheetah_cfg_len) == 0) {
                printf("[CONFIG] Initial cheetah_default_config written successfully.\n");
            } else {
                printf("[CONFIG] Failed to write initial cheetah_default_config!\n");
            }
        }

        float default_float_zero = 0.0f;
        uint8_t default_u8_ff = 0xFF;
        param_set(PARAM_RADAR_INSTALL_ANG, &def_radarInstallAng, sizeof(float));
        param_set(PARAM_CALIB_RESULT_ANG, &default_float_zero, sizeof(float));
        param_set(PARAM_CALIB_RESULT, &default_u8_ff, sizeof(uint8_t));
        param_set(PARAM_IS_CALIB_INSTALL_ANG, &def_isCalibInstallAng, sizeof(uint8_t));
        param_set(PARAM_IS_IN_HIGH_ALTITUDE, &def_isInhighAltitude, sizeof(uint8_t));
        param_set(PARAM_IS_FLIP_INSTALL_AZM, &def_isFlipInstall_azm, sizeof(uint8_t));
        param_set(PARAM_MAX_FRAME_CNT, &default_max_frame_cnt, sizeof(int32_t));
        printf("[CONFIG] Initialized default angles and flags from vendor_init.\n");
        
        return 0;
    }
    return -1;
}

int param_get(ParamID id, void *buf, uint32_t len) {
    if (id >= PARAM_MAX_COUNT || !buf) return -1;
    if (len > g_param_table[id].length) len = g_param_table[id].length;

    uint32_t offset = CONFIG_FLASH_OFFSET + g_param_table[id].offset;
    
    int ret = sfc_nor_flash_read(offset, len, (uint8_t*)buf);
    
    flush_dcache_all();
    
    return ret == len ? 0 : -1;
}

int param_set(ParamID id, const void *buf, uint32_t len) {
    if (id >= PARAM_MAX_COUNT || !buf) return -1;
    if (len > g_param_table[id].length) len = g_param_table[id].length;

    const struct storage_info *info = sfc_nor_flash_info();
    uint32_t sector_size = info->erasesize;
    uint32_t param_offset = CONFIG_FLASH_OFFSET + g_param_table[id].offset;
    
    uint32_t aligned_start = (param_offset / sector_size) * sector_size;
    uint32_t param_end = param_offset + len;
    uint32_t aligned_end = ((param_end + sector_size - 1) / sector_size) * sector_size;
    uint32_t total_size = aligned_end - aligned_start;
    
    uint8_t *sector_buf = NULL;
    uint8_t *raw_buf = malloc(total_size + cache_line_size());
    if (!raw_buf) {
        printf("[CONFIG] param_set: malloc failed!\n");
        return -1;
    }
    uint32_t aligned_addr = ((uint32_t)raw_buf + cache_line_size() - 1) & ~(cache_line_size() - 1);
    sector_buf = (uint8_t *)aligned_addr;
    
    uint32_t read_size = total_size;
    uint32_t read_offset = aligned_start;
    uint8_t *read_ptr = sector_buf;
    int ret = 0;
    
    while (read_size > 0) {
        uint32_t chunk = read_size > 256 ? 256 : read_size;
        ret = sfc_nor_flash_read(read_offset, chunk, read_ptr);
        if (ret != chunk) {
            printf("[CONFIG] param_set: read failed for param %d! ret=%d\n", id, ret);
            free(raw_buf);
            return -1;
        }
        read_offset += chunk;
        read_ptr += chunk;
        read_size -= chunk;
    }
    
    flush_dcache_all();
    
    uint32_t relative_offset = param_offset - aligned_start;
    memcpy(sector_buf + relative_offset, buf, len);
    
    flush_dcache_all();
    
    ret = sfc_nor_flash_erase(aligned_start, total_size);
    if (ret != 0) {
        printf("[CONFIG] param_set: erase failed for param %d! ret=%d\n", id, ret);
        free(raw_buf);
        return -1;
    }
    
    uint32_t write_size = total_size;
    uint32_t write_offset = aligned_start;
    uint8_t *write_ptr = sector_buf;
    
    while (write_size > 0) {
        uint32_t chunk = write_size > 256 ? 256 : write_size;
        ret = sfc_nor_flash_write(write_offset, chunk, write_ptr);
        if (ret != chunk) {
            printf("[CONFIG] param_set: write failed for param %d! ret=%d\n", id, ret);
            free(raw_buf);
            return -1;
        }
        write_offset += chunk;
        write_ptr += chunk;
        write_size -= chunk;
    }
    
    flush_dcache_all();
    
    free(raw_buf);
    return 0;
}