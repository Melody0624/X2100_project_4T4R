#include "radar_diagnostics.h"
#include <string.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

unsigned int radar_diagnostics_level = RADAR_DIAGNOSTICS_DEFAULT;
static struct radar_diagnostic_snapshot frame;
static struct radar_stream_diagnostic stream;
static uint32_t previous_id;
static uint64_t previous_timestamp;
static int have_previous;

int radar_diagnostics_set_level(unsigned int level)
{
    if (level > 2u) return -1;
    radar_diagnostics_level = level;
    return 0;
}
void radar_diagnostics_reset_stream(void)
{
    memset(&frame, 0, sizeof(frame));
    memset(&stream, 0, sizeof(stream));
    previous_id = 0; previous_timestamp = 0; have_previous = 0;
}
int radar_diagnostics_begin(uint32_t id, uint64_t timestamp)
{
    memset(&frame, 0, sizeof(frame));
    frame.level = radar_diagnostics_level;
    frame.frame_id = id; frame.timestamp_us = timestamp;
    ++stream.attempted;
    for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
        frame.rx[rx].minimum = INT_MAX;
        frame.rx[rx].maximum = INT_MIN;
    }
    if (have_previous) {
        uint32_t delta = id - previous_id; /* Defined unsigned wraparound. */
        if (!delta || delta >= 0x80000000u) {
            if (!delta) ++stream.duplicate_ids; else ++stream.out_of_order_ids;
            frame.error = RADAR_FRAME_SEQUENCE;
            return -1;
        }
        if (timestamp <= previous_timestamp) {
            ++stream.timestamp_errors;
            frame.error = RADAR_FRAME_TIMESTAMP;
            return -1;
        }
        frame.input_interval_us = timestamp - previous_timestamp;
        stream.missing_ids += delta - 1u;
    }
    /* Consume accepted transport metadata even if payload subsequently fails.
     * Rejected sequence/timestamp metadata never moves the watermark. */
    previous_id = id; previous_timestamp = timestamp; have_previous = 1;
    return 0;
}
void radar_diagnostics_end(enum radar_frame_error error, uint64_t elapsed)
{
    frame.error = error; frame.processing_us = elapsed;
    if (error == RADAR_FRAME_OK) ++stream.succeeded; else ++stream.failed;
    if (elapsed > RADAR_FRAME_PERIOD_US) ++stream.overruns;
    if (elapsed > stream.max_processing_us) stream.max_processing_us = elapsed;
}
void radar_diagnostics_get(struct radar_diagnostic_snapshot *f,
                           struct radar_stream_diagnostic *s)
{
    if (f) *f = frame;
    if (s) *s = stream;
}
void radar_diagnostics_adc(unsigned int rx, int sample, uint16_t raw)
{
    struct radar_rx_diagnostic *d = &frame.rx[rx];
    if (sample < d->minimum) d->minimum = sample;
    if (sample > d->maximum) d->maximum = sample;
    d->sum += sample;
    d->sum_squares += (uint64_t)((int64_t)sample * sample);
    ++d->samples;
    d->clipped += (raw >> 4) == 0u || (raw >> 4) == 4095u;
    d->bad_low_bits += (raw & 15u) != 0u;
}
void radar_diagnostics_cell(unsigned int range, unsigned int folded,
    const float *bands, const struct ddma_result *r)
{
    float energy = 0;
    unsigned int at;
    for (unsigned int b = 0; b < RADAR_DDMA_NUM_SUBBANDS; ++b) energy += bands[b];
    if (!isfinite(energy) || energy <= 0) return;
    for (at = 0; at < frame.cell_count; ++at)
        if (energy > frame.cells[at].energy) break;
    if (at >= RADAR_DIAGNOSTIC_CELLS) return;
    if (frame.cell_count < RADAR_DIAGNOSTIC_CELLS) ++frame.cell_count;
    for (unsigned int i = frame.cell_count - 1; i > at; --i)
        frame.cells[i] = frame.cells[i - 1];
    frame.cells[at].range_bin = range;
    frame.cells[at].folded_bin = folded;
    frame.cells[at].energy = energy;
    memcpy(frame.cells[at].bands, bands, sizeof(frame.cells[at].bands));
    frame.cells[at].result = *r;
}
void radar_diagnostics_print(void)
{
    if (!frame.level) return;
    printf("[DIAG] frame=%u error=%u dt_us=%llu processing_us=%llu ok=%llu failed=%llu missing=%llu duplicate=%llu reordered=%llu time_errors=%llu overruns=%llu\n",
        frame.frame_id, frame.error, (unsigned long long)frame.input_interval_us,
        (unsigned long long)frame.processing_us, (unsigned long long)stream.succeeded,
        (unsigned long long)stream.failed, (unsigned long long)stream.missing_ids,
        (unsigned long long)stream.duplicate_ids, (unsigned long long)stream.out_of_order_ids,
        (unsigned long long)stream.timestamp_errors, (unsigned long long)stream.overruns);
    if (frame.level < 2u) return;
    for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
        const struct radar_rx_diagnostic *d = &frame.rx[rx];
        if (!d->samples) continue;
        printf("[DIAG-RX] rx=%u min=%d max=%d mean=%.3f rms=%.3f clipped=%u bad_bits=%u samples=%u\n",
            rx, d->minimum, d->maximum, (double)d->sum / d->samples,
            sqrt((double)d->sum_squares / d->samples), d->clipped, d->bad_low_bits, d->samples);
    }
    /* Strongest cells, not distinct targets; adjacent bins may describe one target. */
    for (unsigned int i = 0; i < frame.cell_count; ++i) {
        const struct radar_cell_diagnostic *d = &frame.cells[i];
        printf("[DIAG-DDMA] rbin=%u folded=%u status=%u anchor=%u candidates=0x%02x best=%.3f second=%.3f empty=%.3f bands=",
            d->range_bin, d->folded_bin, d->result.status, d->result.anchor,
            d->result.candidates, d->result.best, d->result.runner_up, d->result.empty_max);
        for (unsigned int b = 0; b < RADAR_DDMA_NUM_SUBBANDS; ++b)
            printf("%.3f%s", d->bands[b], b + 1 == RADAR_DDMA_NUM_SUBBANDS ? "\n" : ",");
    }
}
