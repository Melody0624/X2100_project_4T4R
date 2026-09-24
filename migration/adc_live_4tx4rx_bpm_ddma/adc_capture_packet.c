#include "adc_capture_packet.h"

#include <string.h>

struct __attribute__((packed)) adc_capture_header {
    uint16_t magic_word[4];
    uint32_t version;
    uint32_t total_packet_length;
    uint8_t platform;
    uint8_t reserved_0[3];
    uint32_t frame_number;
    uint8_t number_of_tlvs;
    uint8_t reserved_1[3];
};

struct __attribute__((packed)) adc_capture_tlv {
    uint32_t type;
    uint32_t length;
};

typedef char adc_capture_header_size_check[
    sizeof(struct adc_capture_header) == 28u ? 1 : -1];
typedef char adc_capture_tlv_size_check[
    sizeof(struct adc_capture_tlv) == 8u ? 1 : -1];

int adc_capture_build_prefix(uint8_t *output, size_t output_bytes,
                             uint32_t frame_id, uint32_t payload_bytes)
{
    struct adc_capture_header header;
    struct adc_capture_tlv tlv;

    if (!output || output_bytes < ADC_CAPTURE_PREFIX_BYTES || !payload_bytes ||
        payload_bytes > UINT32_MAX - ADC_CAPTURE_PREFIX_BYTES)
        return -1;

    memset(&header, 0, sizeof(header));
    header.magic_word[0] = 0x0102u;
    header.magic_word[1] = 0x0304u;
    header.magic_word[2] = 0x0506u;
    header.magic_word[3] = 0x0708u;
    header.total_packet_length = ADC_CAPTURE_PREFIX_BYTES + payload_bytes;
    header.platform = 0x24u;
    header.frame_number = frame_id;
    header.number_of_tlvs = 1u;

    tlv.type = ADC_CAPTURE_TLV_TYPE;
    tlv.length = payload_bytes;
    memcpy(output, &header, sizeof(header));
    memcpy(output + sizeof(header), &tlv, sizeof(tlv));
    return (int)ADC_CAPTURE_PREFIX_BYTES;
}

