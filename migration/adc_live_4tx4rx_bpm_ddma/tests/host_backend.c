/* Only the FFT backend and OS/USB transport are replaced on the host.
 * ADC unpack, windows, DDMA, CFAR and AoA are the actual firmware C code. */
#include <NE10_dsp.h>
#include "motorcycle_output.h"
#include "radar_tracking.h"
#include "radar_warning.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

char _user_heap_start, _user_heap_end;
char *heap_ptr;
struct motorcycle_detection host_detections[MOTORCYCLE_OUTPUT_MAX_DETECTIONS];
unsigned int host_count;
uint32_t host_frame;
struct motorcycle_track host_tracks[RADAR_TRACKING_MAX_TRACKS];
unsigned int host_track_count;
struct motorcycle_warning host_warning;

ne10_fft_r2c_cfg_float32_t ne10_fft_alloc_r2c_float32(ne10_int32_t n)
{
    if (n < 2 || n > 512 || (n & (n - 1))) return NULL;
    ne10_fft_r2c_cfg_float32_t p = malloc(sizeof(*p));
    if (p) p->n = (unsigned int)n;
    return p;
}

void ne10_fft_c2c_1d_float32_mxu_ai(ne10_fft_cpx_float32_t *out,
    ne10_fft_cpx_float32_t *in, ne10_fft_r2c_cfg_float32_t cfg)
{
    unsigned int n = cfg->n;
    memcpy(out, in, n * sizeof(*in));
    for (unsigned int i = 1, j = 0; i < n; ++i) {
        unsigned int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { ne10_fft_cpx_float32_t v = out[i]; out[i] = out[j]; out[j] = v; }
    }
    for (unsigned int len = 2; len <= n; len <<= 1) {
        double phi = -6.2831853071795864769 / len;
        double wr0 = cos(phi), wi0 = sin(phi);
        for (unsigned int start = 0; start < n; start += len) {
            double wr = 1.0, wi = 0.0;
            for (unsigned int j = 0; j < len / 2; ++j) {
                unsigned int a = start + j, b = a + len / 2;
                float vr = (float)(out[b].r * wr - out[b].i * wi);
                float vi = (float)(out[b].r * wi + out[b].i * wr);
                float ur = out[a].r, ui = out[a].i;
                out[a].r = ur + vr; out[a].i = ui + vi;
                out[b].r = ur - vr; out[b].i = ui - vi;
                double next_wr = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0; wr = next_wr;
            }
        }
    }
}

void fft_msa(float *in, float *out, ne10_fft_r2c_cfg_float32_t cfg)
{
    ne10_fft_cpx_float32_t a[512], b[512];
    for (unsigned int i = 0; i < cfg->n; ++i) { a[i].r = in[i]; a[i].i = 0; }
    ne10_fft_c2c_1d_float32_mxu_ai(b, a, cfg);
    for (unsigned int i = 0; i <= cfg->n / 2; ++i) {
        out[2 * i] = b[i].r; out[2 * i + 1] = b[i].i;
    }
}

void motorcycle_output_publish(uint32_t frame_id,
    const struct motorcycle_detection *detections, uint16_t count)
{
    assert(count <= MOTORCYCLE_OUTPUT_MAX_DETECTIONS);
    host_count = count; host_frame = frame_id;
    if (count) memcpy(host_detections, detections, count * sizeof(*detections));
}
void motorcycle_output_publish_full(uint32_t frame_id,
    const struct motorcycle_detection *detections, uint16_t count,
    const struct motorcycle_track *tracks, uint16_t track_count,
    const struct motorcycle_warning *warning)
{
    motorcycle_output_publish(frame_id, detections, count);
    assert(track_count <= RADAR_TRACKING_MAX_TRACKS);
    host_track_count = track_count;
    if (track_count)
        memcpy(host_tracks, tracks, track_count * sizeof(*tracks));
    if (warning)
        host_warning = *warning;
    else
        memset(&host_warning, 0, sizeof(host_warning));
}
int motorcycle_output_init(void) { return 0; }
int motorcycle_output_is_connected(void) { return 0; }
void motorcycle_output_get_stats(struct motorcycle_output_stats *s) { memset(s, 0, sizeof(*s)); }
int radar_frontend_init(void) { return -1; }
const unsigned char *radar_frontend_wait_frame(void) { return NULL; }
void radar_frontend_release_frame(const unsigned char *p) { (void)p; }
