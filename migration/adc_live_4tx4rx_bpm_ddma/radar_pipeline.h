#ifndef RADAR_PIPELINE_H
#define RADAR_PIPELINE_H
#include <stdint.h>
#include <stddef.h>
#include "ddma_resolver.h"
#include "motorcycle_output.h"
#include "radar_diagnostics.h"

struct radar_pipeline_stats {
    uint32_t status_cells[DDMA_INVALID + 1u];
    uint32_t rejected_peaks;
    uint32_t detection_overflow;
    uint32_t adc_low_nibble_errors;
    uint32_t adc_clipped_samples;
    uint8_t last_rejected_candidates;
};

int radar_pipeline_init(void);
/* Payload is exactly 128 * (32 header + 506 samples * 4 RX * 2 bytes).
 * Host packet headers must be removed before calling.  Single-threaded API. */
int radar_pipeline_process(const unsigned char *payload, size_t length,
                           uint32_t frame_id);
/* Timestamp = caller's monotonic acquisition timestamp in microseconds.
 * Legacy process() uses processing-entry time; it is NOT a sensor timestamp. */
int radar_pipeline_process_timed(const unsigned char *payload, size_t length,
                                uint32_t frame_id, uint64_t timestamp_us);
void radar_pipeline_get_stats(struct radar_pipeline_stats *stats);
unsigned int radar_pipeline_copy_detections(struct motorcycle_detection *out,
                                             unsigned int capacity);
float radar_range_bin_m(void);
float radar_velocity_bin_mps(void);

#endif
