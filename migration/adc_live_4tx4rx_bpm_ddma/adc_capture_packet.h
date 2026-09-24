#ifndef ADC_CAPTURE_PACKET_H
#define ADC_CAPTURE_PACKET_H

#include <stddef.h>
#include <stdint.h>

#define ADC_CAPTURE_TLV_TYPE 13u
#define ADC_CAPTURE_PREFIX_BYTES 36u

/* Builds the MotorCycle Tools header and ADC-frame TLV header only.
 * The caller sends the returned prefix followed by payload_bytes of raw ADC. */
int adc_capture_build_prefix(uint8_t *output, size_t output_bytes,
                             uint32_t frame_id, uint32_t payload_bytes);

#endif

