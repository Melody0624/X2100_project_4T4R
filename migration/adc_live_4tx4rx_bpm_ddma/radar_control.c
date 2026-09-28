#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dfs_posix.h>
#include <os.h>
#include <driver/sfc_nor.h>

#include "adc_capture_packet.h"
#include "cheetah/cheetah.h"
#include "config_manager.h"
#include "motorcycle_output.h"
#include "radar_4tx4rx_profile.h"
#include "radar_calibration_store.h"
#include "radar_control.h"

#define RADAR_CONFIG_FLASH_OFFSET 0x1DB000u
#define RADAR_CONFIG_FLASH_LENGTH 0x25000u
#define RADAR_CONFIG_MAGIC 0x52414456u

static int flash_config_valid;
static int32_t frames_remaining = -1;
static uint8_t raw_enabled;
static int32_t target_angle = 666;
static int sd_fd = -1;
static uint32_t sd_frames;
static char control_line[4096];
static char calibration_response[4096];
static float calibration_values[RADAR_CALIB_COMPLEX_FLOATS];
static float calibration_axis[RADAR_CALIB_AXIS_FLOATS];

/* The config partition can be initialized only when every byte is erased.
 * A missing magic word alone does not prove the board has no calibration. */
static int config_partition_erased(void)
{
    uint32_t words[64] __attribute__((aligned(64)));
    for (uint32_t offset = 0; offset < RADAR_CONFIG_FLASH_LENGTH;
         offset += sizeof(words)) {
        const uint8_t *bytes = (const uint8_t *)words;
        if (sfc_nor_flash_read(RADAR_CONFIG_FLASH_OFFSET + offset,
                               sizeof(words), (uint8_t *)words) !=
            sizeof(words))
            return -1;
        for (unsigned int index = 0; index < sizeof(words); ++index)
            if (bytes[index] != 0xFFu)
                return 0;
    }
    return 1;
}

static int write_all(int fd, const uint8_t *data, uint32_t bytes)
{
    uint32_t written = 0;
    while (written < bytes) {
        int result = write(fd, data + written, bytes - written);
        if (result <= 0) return -1;
        written += (uint32_t)result;
    }
    return 0;
}

int radar_control_save_adc(uint32_t frame_id, const void *payload,
                           uint32_t payload_bytes)
{
    uint8_t prefix[ADC_CAPTURE_PREFIX_BYTES];
    if (sd_fd < 0) return 0;
    if (!payload || adc_capture_build_prefix(prefix, sizeof(prefix), frame_id,
                                             payload_bytes) < 0 ||
        write_all(sd_fd, prefix, sizeof(prefix)) < 0 ||
        write_all(sd_fd, payload, payload_bytes) < 0) {
        printf("[SD] ADC write failed at frame %lu; closing file\n",
               (unsigned long)frame_id);
        close(sd_fd);
        sd_fd = -1;
        return -1;
    }
    if (++sd_frames % 10u == 0u && fsync(sd_fd) < 0) {
        printf("[SD] fsync failed; closing file\n");
        close(sd_fd);
        sd_fd = -1;
        return -1;
    }
    return 0;
}

static void reply(const char *message)
{
    motorcycle_output_send_text(message);
    if (strlen(message) > 160u)
        printf("[CMD] sent %lu-byte calibration response\n",
               (unsigned long)strlen(message));
    else
        printf("[CMD] %s", message);
}

static int parse_float_value(const char *text, float *value)
{
    char *end;
    float parsed;
    if (!text || !*text) return -1;
    parsed = strtof(text, &end);
    if (*end || !isfinite(parsed)) return -1;
    *value = parsed;
    return 0;
}

static int initialize_blank_config(void)
{
    uint32_t magic_words[64] __attribute__((aligned(64)));
    uint32_t verified = 0;
    int erased;
    if (flash_config_valid) return 0;
    erased = config_partition_erased();
    if (erased != 1) return erased < 0 ? -1 : -2;
    magic_words[0] = RADAR_CONFIG_MAGIC;
    if (sfc_nor_flash_write(RADAR_CONFIG_FLASH_OFFSET, sizeof(magic_words[0]),
                            (const uint8_t *)magic_words) != sizeof(magic_words[0]) ||
        sfc_nor_flash_read(RADAR_CONFIG_FLASH_OFFSET, sizeof(verified),
                           (uint8_t *)&verified) != sizeof(verified) ||
        verified != RADAR_CONFIG_MAGIC) return -3;
    flash_config_valid = 1;
    return 0;
}

static int parse_int(const char *text, int32_t *value)
{
    char *end;
    long parsed;
    if (!text || !*text) return -1;
    parsed = strtol(text, &end, 0);
    if (*end || parsed < INT32_MIN || parsed > INT32_MAX) return -1;
    *value = (int32_t)parsed;
    return 0;
}

static int parse_addr(const char *text, uint16_t *addr)
{
    char *end;
    unsigned long parsed;
    if (!text || !*text) return -1;
    /* The supplier CLI expects hexadecimal register addresses. */
    parsed = strtoul(text, &end, 16);
    if (*end || parsed > UINT16_MAX) return -1;
    *addr = (uint16_t)parsed;
    return 0;
}

void radar_control_init(void)
{
    uint32_t magic = 0;
    int read_result = sfc_nor_flash_read(RADAR_CONFIG_FLASH_OFFSET,
                                         sizeof(magic), (uint8_t *)&magic);
    flash_config_valid = 0;
    radar_4tx4rx_reset_calibration();
    frames_remaining = -1;
    raw_enabled = 0;
    if (read_result == sizeof(magic) &&
        magic == RADAR_CONFIG_MAGIC) {
        unsigned int flags = 0;
        int calibration_state = radar_calibration_load(calibration_values,
                                                       calibration_axis, &flags);
        int32_t saved_frames;
        uint8_t saved_raw;
        flash_config_valid = 1;
        if (param_get(PARAM_MAX_FRAME_CNT, &saved_frames,
                      sizeof(saved_frames)) == 0 && saved_frames >= -1)
            frames_remaining = saved_frames;
        if (param_get(PARAM_OUT_RAW_DATA_FLG, &saved_raw,
                      sizeof(saved_raw)) == 0 && saved_raw <= 1u)
            raw_enabled = saved_raw;
        if (calibration_state == 1) {
            if ((flags & 1u) && !radar_calibration_matrix_zero(calibration_values))
                radar_4tx4rx_set_calibration(calibration_values);
            if (flags & 2u)
                radar_4tx4rx_set_angle_axis(calibration_axis);
        }
        printf("[CAL4] flash=%s matrix=%s angle_axis=%s\n",
               calibration_state == 1 ? "valid" :
               calibration_state == 0 ? "erased" : "invalid",
               (calibration_state == 1 && (flags & 1u) &&
                !radar_calibration_matrix_zero(calibration_values)) ?
                   "flash" : "firmware-candidate",
               (calibration_state == 1 && (flags & 2u)) ? "saved" : "geometry");
    }
    printf("[CMD] config=%s flash_read=%d magic=0x%08lX frames=%ld output=%s\n",
           flash_config_valid ? "valid" : "unavailable",
           read_result, (unsigned long)magic,
           (long)frames_remaining, raw_enabled ? "raw-ADC" : "points");
}

int radar_control_running(void) { return frames_remaining != 0; }
int radar_control_raw_enabled(void) { return raw_enabled != 0u; }
void radar_control_frame_done(void)
{
    if (frames_remaining > 0) --frames_remaining;
}

static void process_command(char *line)
{
    char *argv[140];
    unsigned int argc = 0;
    char *token = strtok(line, " \t");
    char response[192];
    int32_t value;

    while (token && argc < sizeof(argv) / sizeof(argv[0])) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    if (!argc) return;
    if (token) {
        reply("ERR too many arguments\r\n");
        return;
    }

    if (strcmp(argv[0], "SetFrameCnt") == 0) {
        if (argc != 2 || parse_int(argv[1], &value) || value < -1) {
            reply("ERR usage: SetFrameCnt <-1|0|positive count>\r\n");
            return;
        }
        frames_remaining = value;
        if (value == 0 && sd_fd >= 0)
            fsync(sd_fd);
        snprintf(response, sizeof(response), "OK SetFrameCnt=%ld\r\n", (long)value);
        reply(response);
        return;
    }
    if (strcmp(argv[0], "SetMaxFrameCnt") == 0) {
        int32_t readback;
        if (argc != 2 || parse_int(argv[1], &value) || value < -1) {
            reply("ERR usage: SetMaxFrameCnt <-1|0|positive count>\r\n");
            return;
        }
        if (!flash_config_valid ||
            param_set(PARAM_MAX_FRAME_CNT, &value, sizeof(value)) != 0 ||
            param_get(PARAM_MAX_FRAME_CNT, &readback,
                      sizeof(readback)) != 0 || readback != value) {
            reply("ERR Flash config unavailable; frame limit unchanged\r\n");
            return;
        }
        frames_remaining = value;
        snprintf(response, sizeof(response),
                 "OK SetMaxFrameCnt=%ld saved to Flash\r\n", (long)value);
        reply(response);
        return;
    }
    if (strcmp(argv[0], "setRawDataFlg") == 0) {
        uint8_t readback;
        int initialize = 0;
        if (argc != 2 || parse_int(argv[1], &value) || (value != 0 && value != 1)) {
            reply("ERR usage: setRawDataFlg <0|1>\r\n");
            return;
        }
        if (!flash_config_valid) {
            int erased = config_partition_erased();
            if (erased != 1) {
                reply(erased < 0 ?
                      "ERR Flash config read failed; no write\r\n" :
                      "ERR Flash config invalid and not blank; no write\r\n");
                return;
            }
            initialize = 1;
        }
        if (param_set(PARAM_OUT_RAW_DATA_FLG, &value, 1u) != 0) {
            reply("ERR Flash write failed; output mode unchanged\r\n");
            return;
        }
        if (param_get(PARAM_OUT_RAW_DATA_FLG, &readback,
                      sizeof(readback)) != 0 || readback != (uint8_t)value) {
            reply("ERR Flash readback unverified; output mode unchanged\r\n");
            return;
        }
        if (initialize) {
            uint32_t magic_words[64] __attribute__((aligned(64)));
            uint32_t verified = 0;
            magic_words[0] = RADAR_CONFIG_MAGIC;
            if (sfc_nor_flash_write(RADAR_CONFIG_FLASH_OFFSET,
                                    sizeof(magic_words[0]),
                                    (const uint8_t *)magic_words) !=
                    sizeof(magic_words[0]) ||
                sfc_nor_flash_read(RADAR_CONFIG_FLASH_OFFSET,
                                   sizeof(verified), (uint8_t *)&verified) !=
                    sizeof(verified) || verified != RADAR_CONFIG_MAGIC) {
                reply("ERR Flash config initialization unverified; output mode unchanged\r\n");
                return;
            }
            flash_config_valid = 1;
        }
        reply(value ? "OK raw ADC flag saved; reboot to take effect\r\n" :
                      "OK point/track flag saved; reboot to take effect\r\n");
        return;
    }
    if (strcmp(argv[0], "getBoardVersion") == 0) {
        reply("MT-4T4R-01 experimental live; X2100L; non-OTA\r\n");
        return;
    }
    if (strcmp(argv[0], "getTemperature") == 0) {
        uint32_t reg = 0;
        if (cheetah_reg_read_single(0, 0x10A2u, &reg, CHEETAH_CHECK) < 0) {
            reply("ERR Cheetah temperature register read failed\r\n");
            return;
        }
        snprintf(response, sizeof(response), "Cheetah temperature: %.2f C\r\n",
                 (((float)(reg >> 20) / 4096.0f) * 1.8f - 0.6454f) * 200.0f + 40.0f);
        reply(response);
        return;
    }
    if (strcmp(argv[0], "readreg") == 0 || strcmp(argv[0], "rr") == 0) {
        uint16_t addr;
        uint32_t reg;
        int32_t index = 0;
        const char *addr_text = argc == 3 ? argv[2] : argc == 2 ? argv[1] : NULL;
        if (!addr_text || (argc == 3 && (parse_int(argv[1], &index) || index != 0)) ||
            parse_addr(addr_text, &addr)) {
            reply("ERR usage: readreg [0] <hex-address>\r\n");
            return;
        }
        if (cheetah_reg_read_single(0, addr, &reg, CHEETAH_CHECK) < 0) {
            reply("ERR Cheetah register read failed\r\n");
            return;
        }
        snprintf(response, sizeof(response), "0x%04X: 0x%08lX\r\n",
                 addr, (unsigned long)reg);
        reply(response);
        return;
    }
    if (strcmp(argv[0], "writereg") == 0 || strcmp(argv[0], "wr") == 0) {
        uint16_t addr;
        int32_t index = 0;
        char *end;
        unsigned long reg;
        const char *addr_text = argc == 4 ? argv[2] : argc == 3 ? argv[1] : NULL;
        const char *reg_text = argc == 4 ? argv[3] : argc == 3 ? argv[2] : NULL;
        if (!addr_text || (argc == 4 && (parse_int(argv[1], &index) || index != 0)) ||
            parse_addr(addr_text, &addr) || !reg_text) {
            reply("ERR usage: writereg [0] <hex-address> <hex-value>\r\n");
            return;
        }
        reg = strtoul(reg_text, &end, 16);
        if (*end || reg > UINT32_MAX) {
            reply("ERR invalid register value\r\n");
            return;
        }
        if (frames_remaining != 0) {
            reply("ERR stop frames with SetFrameCnt 0 before RF register writes\r\n");
            return;
        }
        if (cheetah_reg_write_single(0, addr, (uint32_t)reg, CHEETAH_CHECK) < 0) {
            reply("ERR Cheetah register write failed\r\n");
            return;
        }
        reply("OK register written; verify RF profile before resuming\r\n");
        return;
    }
    if (strcmp(argv[0], "delay") == 0) {
        if (argc != 2 || parse_int(argv[1], &value) || value < 0 || value > 1000) {
            reply("ERR usage: delay <0..1000 milliseconds>\r\n");
            return;
        }
        msleep((unsigned int)value);
        reply("OK delay completed\r\n");
        return;
    }
    if (strcmp(argv[0], "ResetRams") == 0) {
        if (frames_remaining != 0) {
            reply("ERR stop frames with SetFrameCnt 0 before ResetRams\r\n");
            return;
        }
        reply(reset_ram(0) < 0 ? "ERR ResetRams failed\r\n" :
                                 "OK ResetRams completed\r\n");
        return;
    }
    if (strcmp(argv[0], "setAngle") == 0) {
        if (argc != 2 || parse_int(argv[1], &value) ||
            (value != 666 && (value < -90 || value > 90))) {
            reply("ERR usage: setAngle <-90..90|666>\r\n");
            return;
        }
        target_angle = value;
        snprintf(response, sizeof(response),
                 "OK target metadata angle=%ld deg (not installation angle)\r\n",
                 (long)target_angle);
        reply(response);
        return;
    }
    if (strcmp(argv[0], "SetDumpFileName") == 0) {
        char path[128];
        size_t length;
        if (argc != 2 || !(length = strlen(argv[1])) || length > 80u ||
            strspn(argv[1], "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != length) {
            reply("ERR usage: SetDumpFileName <letters/digits/_/->\r\n");
            return;
        }
        snprintf(path, sizeof(path), "/mmcblk2p0/%s_adc.dat", argv[1]);
        if (sd_fd >= 0) {
            fsync(sd_fd);
            close(sd_fd);
            sd_fd = -1;
        }
        sd_fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (sd_fd < 0) {
            reply("ERR SD card unavailable or file already exists; no recording\r\n");
            return;
        }
        sd_frames = 0u;
        snprintf(response, sizeof(response),
                 "OK SD raw ADC file opened: %s (used in raw mode)\r\n", path);
        reply(response);
        return;
    }
    if (strcmp(argv[0], "otaUpgrade") == 0) {
        reply("ERR OTA unavailable: current NOR image has no OTA partition\r\n");
        return;
    }
    if (strcmp(argv[0], "angCalibMat") == 0 || strcmp(argv[0], "angFFT") == 0) {
        unsigned int flag = strcmp(argv[0], "angCalibMat") == 0 ? 1u : 2u;
        unsigned int count = flag == 1u ? RADAR_CALIB_COMPLEX_FLOATS :
                                           RADAR_CALIB_AXIS_FLOATS;
        const char *name = argv[0];
        if (argc == 2 && strcmp(argv[1], "read") == 0) {
            unsigned int saved_flags = 0;
            int state = radar_calibration_load(calibration_values,
                                               calibration_axis, &saved_flags);
            float *data = flag == 1u ? calibration_values : calibration_axis;
            int use_flash = state == 1 && (saved_flags & flag) &&
                (flag != 1u || !radar_calibration_matrix_zero(calibration_values));
            size_t used;
            if (state < 0) {
                reply("ERR 4T4R calibration Flash unreadable or invalid\r\n");
                return;
            }
            if (!use_flash) {
                if (flag == 1u) {
                    for (unsigned int i = 0; i < count / 2u; ++i)
                        radar_4tx4rx_get_firmware_calibration(i, &data[2u * i],
                                                                &data[2u * i + 1u]);
                } else radar_4tx4rx_get_angle_axis(data);
            }
            used = (size_t)snprintf(calibration_response,
                                    sizeof(calibration_response),
                                    "%s source=%s count=%u",
                                    name, use_flash ?
                                    "flash" : flag == 1u ? "firmware" : "default", count);
            for (unsigned int i = 0; i < count && used < sizeof(calibration_response); ++i) {
                int written = snprintf(calibration_response + used,
                                       sizeof(calibration_response) - used,
                                       " %.7g", data[i]);
                if (written < 0 || (size_t)written >= sizeof(calibration_response) - used) {
                    reply("ERR calibration response too long\r\n");
                    return;
                }
                used += (size_t)written;
            }
            strcat(calibration_response, "\r\n");
            reply(calibration_response);
            return;
        }
        if (argc >= 2 && strcmp(argv[1], "write") == 0) {
            float *data = flag == 1u ? calibration_values : calibration_axis;
            if (argc != count + 2u) {
                reply(flag == 1u ?
                      "ERR angCalibMat write requires 32 floats (real imag x16)\r\n" :
                      "ERR angFFT write requires 128 angle values in degrees\r\n");
                return;
            }
            for (unsigned int i = 0; i < count; ++i)
                if (parse_float_value(argv[i + 2u], &data[i])) {
                    reply("ERR calibration values must be finite floats\r\n");
                    return;
                }
            if (!flash_config_valid && initialize_blank_config() != 0) {
                reply("ERR Flash config invalid or not blank; no calibration write\r\n");
                return;
            }
            if (radar_calibration_save(data, flag) != 0) {
                reply("ERR 4T4R calibration Flash write/verify failed\r\n");
                return;
            }
            reply(flag == 1u ?
                  "OK angCalibMat saved (16 complex channels); reboot to take effect\r\n" :
                  "OK angFFT saved (128 angle bins); reboot to take effect\r\n");
            return;
        }
        reply(flag == 1u ?
              "ERR usage: angCalibMat read|write <32 floats>\r\n" :
              "ERR usage: angFFT read|write <128 floats>\r\n");
        return;
    }
    if (strcmp(argv[0], "uds") == 0) {
        reply("ERR command not yet bound to a verified 4T4R backend\r\n");
        return;
    }
    reply("ERR unknown command\r\n");
}

void radar_control_poll(void)
{
    int result = motorcycle_output_take_command(control_line,
                                                sizeof(control_line));
    if (result > 0) process_command(control_line);
    else if (result < 0) reply("ERR command too long\r\n");
}
