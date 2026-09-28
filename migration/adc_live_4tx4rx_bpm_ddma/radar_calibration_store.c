#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <driver/sfc_nor.h>

#include "radar_calibration_store.h"

/* Last 1 KiB of the 0x1DB000..0x1FFFFF config partition. The legacy packed
 * layout ends before 0x1FFC00; keep its 8-channel matrix untouched. */
#define STORE_OFFSET 0x1FFC00u
#define STORE_END 0x200000u
#define STORE_MAGIC 0x344C4143u /* CAL4 */
#define STORE_VERSION 1u

struct calibration_block {
    uint32_t magic, version, flags, checksum;
    float complex_values[RADAR_CALIB_COMPLEX_FLOATS];
    float angle_axis[RADAR_CALIB_AXIS_FLOATS];
};

typedef char store_fits_last_kib[(sizeof(struct calibration_block) <= 1024u) ? 1 : -1];

static uint32_t checksum(const struct calibration_block *block)
{
    struct calibration_block copy = *block;
    const uint8_t *bytes = (const uint8_t *)&copy;
    uint32_t hash = 2166136261u;
    copy.checksum = 0u;
    for (unsigned int i = 0; i < sizeof(copy); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

int radar_calibration_matrix_zero(const float values[RADAR_CALIB_COMPLEX_FLOATS])
{
    for (unsigned int i = 0; i < RADAR_CALIB_COMPLEX_FLOATS; ++i)
        if (values[i] != 0.0f) return 0;
    return 1;
}

static int valid(const struct calibration_block *block)
{
    if (block->magic != STORE_MAGIC || block->version != STORE_VERSION ||
        (block->flags & ~3u) != 0u || block->checksum != checksum(block))
        return 0;
    if ((block->flags & 1u) &&
        !radar_calibration_matrix_zero(block->complex_values)) {
        for (unsigned int i = 0; i < RADAR_CALIB_COMPLEX_FLOATS; i += 2u) {
            float real = block->complex_values[i];
            float imag = block->complex_values[i + 1u];
            if (!isfinite(real) || !isfinite(imag) ||
                fabsf(real) > 100.0f || fabsf(imag) > 100.0f ||
                fabsf(real) + fabsf(imag) < 0.000001f)
                return 0;
        }
    }
    if (block->flags & 2u) {
        float span = 0.0f;
        for (unsigned int i = 0; i < RADAR_CALIB_AXIS_FLOATS; ++i) {
            if (!isfinite(block->angle_axis[i]) ||
                fabsf(block->angle_axis[i]) > 90.0f)
                return 0;
            if (i && fabsf(block->angle_axis[i] - block->angle_axis[0]) > span)
                span = fabsf(block->angle_axis[i] - block->angle_axis[0]);
        }
        if (span < 1.0f) return 0;
    }
    return 1;
}

static int read_block(struct calibration_block *block)
{
    uint8_t chunk[256] __attribute__((aligned(64)));
    uint32_t first = 0;
    for (uint32_t offset = 0; offset < sizeof(*block); offset += sizeof(chunk)) {
        uint32_t count = sizeof(*block) - offset < sizeof(chunk) ?
                         sizeof(*block) - offset : sizeof(chunk);
        if (sfc_nor_flash_read(STORE_OFFSET + offset, count, chunk) != (int)count)
            return -1;
        memcpy((uint8_t *)block + offset, chunk, count);
    }
    memcpy(&first, block, sizeof(first));
    if (first == 0xFFFFFFFFu) {
        const uint8_t *bytes = (const uint8_t *)block;
        for (unsigned int i = 0; i < sizeof(*block); ++i)
            if (bytes[i] != 0xFFu) return -1;
        return 0;
    }
    return valid(block) ? 1 : -1;
}

int radar_calibration_load(float complex_values[RADAR_CALIB_COMPLEX_FLOATS],
                           float angle_axis[RADAR_CALIB_AXIS_FLOATS],
                           unsigned int *flags)
{
    struct calibration_block block;
    int result = read_block(&block);
    *flags = result == 1 ? block.flags : 0u;
    if (result == 1) {
        memcpy(complex_values, block.complex_values, sizeof(block.complex_values));
        memcpy(angle_axis, block.angle_axis, sizeof(block.angle_axis));
    }
    return result;
}

int radar_calibration_save(const float *values, unsigned int flag)
{
    struct calibration_block block, readback;
    const struct storage_info *info = sfc_nor_flash_info();
    uint32_t sector, start, total;
    uint8_t *allocation, *buffer;
    int state;

    if (!values || (flag != 1u && flag != 2u) || !info || !info->erasesize)
        return -1;
    state = read_block(&block);
    if (state < 0) return -1; /* Never overwrite unknown content. */
    if (state == 0) {
        memset(&block, 0, sizeof(block));
        block.magic = STORE_MAGIC;
        block.version = STORE_VERSION;
    }
    if (flag == 1u)
        memcpy(block.complex_values, values, sizeof(block.complex_values));
    else
        memcpy(block.angle_axis, values, sizeof(block.angle_axis));
    block.flags |= flag;
    block.checksum = checksum(&block);
    if (!valid(&block)) return -1;

    sector = info->erasesize;
    start = (STORE_OFFSET / sector) * sector;
    total = ((STORE_OFFSET + sizeof(block) + sector - 1u) / sector) * sector - start;
    if (start < 0x1DB000u || start + total > STORE_END || total > 0x25000u)
        return -1;
    allocation = malloc(total + 63u);
    if (!allocation) return -1;
    buffer = (uint8_t *)(((uintptr_t)allocation + 63u) & ~(uintptr_t)63u);
    for (uint32_t offset = 0; offset < total; offset += 256u) {
        uint32_t count = total - offset < 256u ? total - offset : 256u;
        if (sfc_nor_flash_read(start + offset, count, buffer + offset) != (int)count) {
            free(allocation);
            return -1;
        }
    }
    memcpy(buffer + STORE_OFFSET - start, &block, sizeof(block));
    if (sfc_nor_flash_erase(start, total) != 0) {
        free(allocation);
        return -1;
    }
    for (uint32_t offset = 0; offset < total; offset += 256u) {
        uint32_t count = total - offset < 256u ? total - offset : 256u;
        if (sfc_nor_flash_write(start + offset, count, buffer + offset) != (int)count) {
            free(allocation);
            return -1;
        }
    }
    free(allocation);
    if (read_block(&readback) != 1 || memcmp(&block, &readback, sizeof(block)))
        return -1;
    return 0;
}
