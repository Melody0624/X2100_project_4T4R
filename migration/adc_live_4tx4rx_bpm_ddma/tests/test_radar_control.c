#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_manager.h"
#include "radar_control.h"

static char command[4096];
static char response[256];
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
    assert(offset == 0x1DB000u && length == 4u &&
           memcmp(buffer, &magic, sizeof(magic)) == 0);
    flash_magic_valid = 1;
    ++magic_writes;
    return (int)length;
}

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
    puts("RADAR_CONTROL=PASS frame-limit/raw-switch/flash-guard/RF-write/OTA-reject");
    return 0;
}
