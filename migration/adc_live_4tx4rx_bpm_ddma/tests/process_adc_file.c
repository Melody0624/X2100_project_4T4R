#include "radar_pipeline.h"
#include "motorcycle_output.h"
#include "radar_tracking.h"
#include "radar_warning.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct motorcycle_detection host_detections[MOTORCYCLE_OUTPUT_MAX_DETECTIONS];
extern unsigned int host_count;
extern struct motorcycle_track host_tracks[RADAR_TRACKING_MAX_TRACKS];
extern unsigned int host_track_count;
extern struct motorcycle_warning host_warning;

static uint32_t u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(int argc, char **argv)
{
    static const unsigned char magic[8] = {2, 1, 4, 3, 6, 5, 8, 7};
    unsigned int limit = 10u, processed = 0u, failed = 0u;
    unsigned char prefix[36];
    unsigned char *payload;
    FILE *file;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s file_adc.dat [frames]\n", argv[0]);
        return 2;
    }
    if (argc == 3)
        limit = (unsigned int)strtoul(argv[2], NULL, 10);
    file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 2;
    }
    payload = malloc(RADAR_PAYLOAD_BYTES);
    if (!payload || radar_pipeline_init() < 0) {
        fprintf(stderr, "pipeline initialization failed\n");
        fclose(file);
        free(payload);
        return 2;
    }

    while (processed < limit && fread(prefix, 1, sizeof(prefix), file) == sizeof(prefix)) {
        uint32_t packet_bytes = u32le(prefix + 12);
        uint32_t frame_id = u32le(prefix + 20);
        uint32_t tlv_type = u32le(prefix + 28);
        uint32_t payload_bytes = u32le(prefix + 32);
        int result;

        if (memcmp(prefix, magic, sizeof(magic)) != 0 ||
            packet_bytes != sizeof(prefix) + RADAR_PAYLOAD_BYTES ||
            tlv_type != 13u || payload_bytes != RADAR_PAYLOAD_BYTES ||
            fread(payload, 1, RADAR_PAYLOAD_BYTES, file) != RADAR_PAYLOAD_BYTES) {
            fprintf(stderr, "invalid/truncated packet after %u frames\n", processed);
            failed++;
            break;
        }
        result = radar_pipeline_process_timed(
            payload, RADAR_PAYLOAD_BYTES, frame_id,
            (uint64_t)frame_id * RADAR_FRAME_PERIOD_US);
        printf("frame=%u result=%d detections=%u tracks=%u",
               frame_id, result, host_count, host_track_count);
        for (unsigned int i = 0; i < host_count; ++i) {
            const struct motorcycle_detection *d = &host_detections[i];
            printf(" | r=%.3f v=%+.3f a=%+.3f snr=%.2f",
                   d->range_m, d->velocity_mps, d->azimuth_deg, d->snr);
        }
        for (unsigned int i = 0; i < host_track_count; ++i) {
            const struct motorcycle_track *t = &host_tracks[i];
            printf(" | T%u=(%.2f,%.2f) v=(%+.2f,%+.2f) miss=%u",
                   t->track_id, t->x_m, t->y_m, t->vx_mps, t->vy_mps,
                   t->misses);
        }
        printf(" | warn=%u%u%u%u%u%u%u ttc=%.2f",
               host_warning.active[0], host_warning.active[1],
               host_warning.active[2], host_warning.active[3],
               host_warning.active[4], host_warning.active[5],
               host_warning.active[6], host_warning.ttc_min_s);
        putchar('\n');
        failed += result < 0;
        processed++;
    }
    printf("REAL_ADC_EXPERIMENTAL frames=%u failed=%u; mapping/calibration unverified\n",
           processed, failed);
    fclose(file);
    free(payload);
    return failed ? 1 : 0;
}
