#ifndef RADAR_DIAGNOSTICS_H
#define RADAR_DIAGNOSTICS_H
#include <stdint.h>
#include "ddma_resolver.h"

enum radar_frame_error { RADAR_FRAME_OK, RADAR_FRAME_SEQUENCE,
    RADAR_FRAME_TIMESTAMP, RADAR_FRAME_LENGTH, RADAR_FRAME_CONFIG,
    RADAR_FRAME_ADC, RADAR_FRAME_PROCESSING };
struct radar_rx_diagnostic {
    int minimum, maximum;
    int64_t sum;
    uint64_t sum_squares;
    uint32_t samples, clipped, bad_low_bits;
};
struct radar_cell_diagnostic {
    unsigned int range_bin, folded_bin;
    float energy;
    float bands[RADAR_DDMA_NUM_SUBBANDS];
    struct ddma_result result;
};
struct radar_diagnostic_snapshot {
    unsigned int level, error, cell_count;
    uint32_t frame_id;
    uint64_t timestamp_us, input_interval_us, processing_us;
    struct radar_rx_diagnostic rx[RADAR_NUM_RX];
    struct radar_cell_diagnostic cells[RADAR_DIAGNOSTIC_CELLS];
};
struct radar_stream_diagnostic {
    uint64_t attempted, succeeded, failed, missing_ids, duplicate_ids;
    uint64_t out_of_order_ids, timestamp_errors, overruns, max_processing_us;
};
extern unsigned int radar_diagnostics_level;
/* Single pipeline task. Configure/reset only between frames. Sequence reset
 * is explicit: reconnect/restart must not silently accept stale frame IDs. */
int radar_diagnostics_set_level(unsigned int level);
void radar_diagnostics_reset_stream(void);
void radar_diagnostics_get(struct radar_diagnostic_snapshot *frame,
                           struct radar_stream_diagnostic *stream);
void radar_diagnostics_print(void);
int radar_diagnostics_begin(uint32_t frame_id, uint64_t timestamp_us);
void radar_diagnostics_end(enum radar_frame_error error, uint64_t elapsed_us);
void radar_diagnostics_adc(unsigned int rx, int sample, uint16_t raw);
void radar_diagnostics_cell(unsigned int range, unsigned int folded,
    const float *bands, const struct ddma_result *result);
#endif
