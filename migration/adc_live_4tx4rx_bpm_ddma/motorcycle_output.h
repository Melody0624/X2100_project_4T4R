#ifndef MOTORCYCLE_OUTPUT_H
#define MOTORCYCLE_OUTPUT_H

#include <stdint.h>
#include "radar_config.h"

#define MOTORCYCLE_OUTPUT_MAX_DETECTIONS RADAR_MAX_DETECTIONS

struct motorcycle_detection {
    uint16_t rel_rd_index;
    uint8_t motion_state;
    uint8_t is_peak;
    float velocity_mps;
    float x_output_m;
    float y_output_m;
    float range_m;
    float azimuth_deg;
    float velocity_ambiguous_mps;
    float x_rcs_m;
    float y_rcs_m;
    uint8_t velocity_disamb_confidence;
    uint8_t velocity_disamb_factor;
    float power;
    float snr;
};

struct motorcycle_output_stats {
    uint32_t produced;
    uint32_t sent;
    uint32_t dropped;
    uint32_t errors;
    uint32_t connected;
    int32_t last_error;
    uint32_t consecutive_errors;
};

struct motorcycle_track;
struct motorcycle_warning;

int motorcycle_output_init(void);
int motorcycle_output_is_connected(void);
/* Returns 0 after a complete ADC frame was sent, 1 while USB is disconnected,
 * and a negative value for invalid data or a transport error. */
int motorcycle_output_publish_adc(uint32_t frame_id,
                                  const void *payload,
                                  uint32_t payload_bytes);
void motorcycle_output_publish(uint32_t frame_id,
                               const struct motorcycle_detection *detections,
                               uint16_t detection_count);
void motorcycle_output_publish_full(uint32_t frame_id,
                                    const struct motorcycle_detection *detections,
                                    uint16_t detection_count,
                                    const struct motorcycle_track *tracks,
                                    uint16_t track_count,
                                    const struct motorcycle_warning *warning);
void motorcycle_output_get_stats(struct motorcycle_output_stats *stats);
/* USB CDC command channel shares the same gadget as binary radar packets. */
int motorcycle_output_take_command(char *line, uint32_t capacity);
int motorcycle_output_send_text(const char *message);

#endif
