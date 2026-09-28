#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <driver/sfc_nor.h>

#include "config_manager.h"
#include "radar_control.h"
#include "radar_4tx4rx_profile.h"

static char command[4096];
static char response[4096];
static uint8_t flash_tail[4096];
static const struct storage_info flash_info = { .erasesize = 4096u };
static int32_t saved_max = -1;
static uint8_t saved_raw;
static int flash_magic_valid = 1;
static int flash_blank;
static int flash_scan_read_failure;
static unsigned int magic_writes;
static unsigned int register_writes;

int sfc_nor_flash_read(uint32_t offset, uint32_t length, uint8_t *buffer)
{
    assert(offset >= 0x1DB000u && offset + length <= 0x200000u);
    if (flash_scan_read_failure && offset == 0x1FFF00u)
        return -1;
    memset(buffer, 0xFF, length);
    if (offset >= 0x1FF000u && offset + length <= 0x200000u)
        memcpy(buffer, flash_tail + offset - 0x1FF000u, length);
    if (offset == 0x1DB000u && flash_magic_valid) {
        const uint32_t magic = 0x52414456u;
        memcpy(buffer, &magic, sizeof(magic));
    }
    if (!flash_magic_valid && !flash_blank &&
        offset == 0x1FFF00u)
        buffer[0] = 0x42;
    return (int)length;
}

int sfc_nor_flash_write(uint32_t offset, uint32_t length,
                        const uint8_t *buffer)
{
    const uint32_t magic = 0x52414456u;
    if (offset == 0x1DB000u) {
        assert(length == 4u && memcmp(buffer, &magic, sizeof(magic)) == 0);
        flash_magic_valid = 1;
        ++magic_writes;
    } else {
        assert(offset >= 0x1FF000u && offset + length <= 0x200000u);
        memcpy(flash_tail + offset - 0x1FF000u, buffer, length);
    }
    return (int)length;
}

int sfc_nor_flash_erase(uint32_t offset, uint32_t length)
{
    assert(offset == 0x1FF000u && length == sizeof(flash_tail));
    memset(flash_tail, 0xFF, sizeof(flash_tail));
    return 0;
}

const struct storage_info *sfc_nor_flash_info(void) { return &flash_info; }

int param_get(ParamID id, void *buffer, uint32_t length)
{
    if (id == PARAM_MAX_FRAME_CNT && length == sizeof(saved_max))
        memcpy(buffer, &saved_max, length);
    else if (id == PARAM_OUT_RAW_DATA_FLG && length == sizeof(saved_raw))
        memcpy(buffer, &saved_raw, length);
    else
        return -1;
    return 0;
}

int param_set(ParamID id, const void *buffer, uint32_t length)
{
    if (id == PARAM_MAX_FRAME_CNT && length == sizeof(saved_max))
        memcpy(&saved_max, buffer, length);
    else if (id == PARAM_OUT_RAW_DATA_FLG && length == sizeof(saved_raw))
        memcpy(&saved_raw, buffer, length);
    else
        return -1;
    return 0;
}

int motorcycle_output_take_command(char *line, uint32_t capacity)
{
    if (!command[0]) return 0;
    assert(strlen(command) < capacity);
    strcpy(line, command);
    command[0] = 0;
    return 1;
}

int motorcycle_output_send_text(const char *text)
{
    assert(strlen(text) < sizeof(response));
    strcpy(response, text);
    return 0;
}

int cheetah_reg_read_single(int index, uint16_t addr, uint32_t *data, int check)
{
    (void)check;
    assert(index == 0);
    *data = addr == 0x10A2u ? 0x80000000u : 0x12345678u;
    return 0;
}

int cheetah_reg_write_single(int index, uint16_t addr, uint32_t data, int check)
{
    (void)check;
    assert(index == 0 && addr == 0x1234u && data == 0xABCDu);
    ++register_writes;
    return 0;
}

int reset_ram(int index) { assert(index == 0); return 0; }

static void send(const char *line)
{
    strcpy(command, line);
    response[0] = 0;
    radar_control_poll();
    assert(response[0]);
}

int main(void)
{
    memset(flash_tail, 0xFF, sizeof(flash_tail));
    flash_magic_valid = 0;
    flash_blank = 0;
    radar_control_init();
    send("setRawDataFlg 1");
    assert(!radar_control_raw_enabled() && saved_raw == 0 && magic_writes == 0 &&
           strstr(response, "ERR Flash config invalid"));

    flash_blank = 1;
    flash_scan_read_failure = 1;
    send("setRawDataFlg 1");
    assert(saved_raw == 0 && magic_writes == 0 &&
           strstr(response, "ERR Flash config read failed"));
    flash_scan_read_failure = 0;
    send("setRawDataFlg 1");
    assert(saved_raw == 1 && !radar_control_raw_enabled() && magic_writes == 1 &&
           strstr(response, "reboot to take effect"));
    radar_control_init();
    assert(radar_control_raw_enabled());
    send("setRawDataFlg 0");
    assert(saved_raw == 0 && radar_control_raw_enabled());
    radar_control_init();
    assert(radar_control_running() && !radar_control_raw_enabled());

    send("SetFrameCnt 2");
    assert(strstr(response, "OK SetFrameCnt=2"));
    radar_control_frame_done();
    assert(radar_control_running());
    radar_control_frame_done();
    assert(!radar_control_running());

    send("writereg 0 1234 ABCD");
    assert(register_writes == 1 && strstr(response, "OK register written"));
    send("SetMaxFrameCnt -1");
    assert(saved_max == -1 && radar_control_running());
    send("writereg 0 1234 ABCD");
    assert(register_writes == 1 && strstr(response, "ERR stop frames"));

    send("setRawDataFlg 1");
    assert(saved_raw == 1 && !radar_control_raw_enabled() &&
           strstr(response, "reboot to take effect"));
    radar_control_init();
    assert(radar_control_raw_enabled());
    send("setRawDataFlg 2");
    assert(saved_raw == 1 && strstr(response, "ERR usage"));
    send("otaUpgrade");
    assert(strstr(response, "ERR OTA unavailable"));
    send("SetDumpFileName ../escape");
    assert(strstr(response, "ERR usage"));

    send("angCalibMat read");
    assert(strstr(response, "source=default count=32 1 0 1 0"));
    send("angFFT read");
    assert(strstr(response, "source=default count=128"));
    send("angCalibMat write 1 0");
    assert(strstr(response, "requires 32 floats"));

    {
        char line[4096];
        size_t used = (size_t)snprintf(line, sizeof(line), "angCalibMat write");
        flash_tail[0] = 0x5Au; /* Legacy data in the shared erase sector. */
        for (unsigned int i = 0; i < 32u; ++i)
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     " %s", i == 2u ? "0.8" : i % 2u ? "0" : "1");
        send(line);
        assert(strstr(response, "OK angCalibMat saved") && flash_tail[0] == 0x5Au);
        send("angCalibMat read");
        assert(strstr(response, "source=flash count=32 1 0 0.8 0"));

        used = (size_t)snprintf(line, sizeof(line), "angFFT write");
        for (unsigned int i = 0; i < 128u; ++i)
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     " %d", i < 64u ? -(int)i : 128 - (int)i);
        send(line);
        assert(strstr(response, "OK angFFT saved") && flash_tail[0] == 0x5Au);
        send("angFFT read");
        assert(strstr(response, "source=flash count=128 0 -1 -2"));
        {
            uint8_t original = flash_tail[0xC20u];
            flash_tail[0xC20u] ^= 1u;
            send("angFFT read");
            assert(strstr(response, "ERR 4T4R calibration Flash unreadable"));
            send(line);
            assert(strstr(response, "ERR 4T4R calibration Flash write/verify failed"));
            flash_tail[0xC20u] = original;
        }
        radar_control_init();
        {
            float real, imag, axis[128];
            radar_4tx4rx_get_calibration(1u, &real, &imag);
            assert(real > 0.79f && real < 0.81f && imag == 0.0f);
            radar_4tx4rx_get_angle_axis(axis);
            assert(axis[1] == -1.0f && axis[127] == 1.0f);
        }
    }
    puts("RADAR_CONTROL=PASS frame/raw/calibration-Flash/RF-write/OTA-reject");
    return 0;
}
