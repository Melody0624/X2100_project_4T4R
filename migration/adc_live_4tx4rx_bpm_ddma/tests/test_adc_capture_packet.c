#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adc_capture_packet.h"
#include "radar_config.h"

static uint16_t u16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

int main(void)
{
    uint8_t prefix[ADC_CAPTURE_PREFIX_BYTES];
    assert(adc_capture_build_prefix(prefix, sizeof(prefix), 0x12345678u,
                                    RADAR_PAYLOAD_BYTES) ==
           (int)sizeof(prefix));
    assert(u16(prefix + 0) == 0x0102u);
    assert(u16(prefix + 2) == 0x0304u);
    assert(u16(prefix + 4) == 0x0506u);
    assert(u16(prefix + 6) == 0x0708u);
    assert(u32(prefix + 12) == RADAR_PAYLOAD_BYTES + sizeof(prefix));
    assert(prefix[16] == 0x24u);
    assert(u32(prefix + 20) == 0x12345678u);
    assert(prefix[24] == 1u);
    assert(u32(prefix + 28) == ADC_CAPTURE_TLV_TYPE);
    assert(u32(prefix + 32) == RADAR_PAYLOAD_BYTES);
    assert(adc_capture_build_prefix(NULL, sizeof(prefix), 1, 1) < 0);
    assert(adc_capture_build_prefix(prefix, sizeof(prefix) - 1u, 1, 1) < 0);
    assert(adc_capture_build_prefix(prefix, sizeof(prefix), 1, 0) < 0);
    puts("ADC_CAPTURE_PACKET=PASS header=28 tlv=8 type=13 payload=522240");
    return 0;
}

