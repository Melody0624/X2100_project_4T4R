#include "ddma_resolver.h"
#include <math.h>
#include <string.h>

/* Engineering defaults; these are not a measured false-alarm specification.
 * Amplitude ratios 1.5 and 2 are approximately 3.52 and 6.02 dB. */
const struct ddma_policy ddma_default_policy = {
    RADAR_DDMA_MIN_AMPLITUDE, RADAR_DDMA_MIN_TX_TO_PEAK,
    RADAR_DDMA_WINNER_RATIO, RADAR_DDMA_EMPTY_CONTRAST
};

void ddma_resolve(const float bands[DDMA_RESOLVER_BANDS],
                  const struct ddma_policy *policy, struct ddma_result *out)
{
    float scores[DDMA_RESOLVER_BANDS];
    float peak = 0.0f;
    unsigned int winner = 0u;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->status = DDMA_INVALID;
    if (!bands || !policy || !isfinite(policy->minimum_amplitude) ||
        !isfinite(policy->minimum_tx_to_peak) ||
        !isfinite(policy->minimum_winner_ratio) ||
        !isfinite(policy->minimum_empty_contrast) ||
        policy->minimum_amplitude <= 0.0f ||
        policy->minimum_tx_to_peak <= 0.0f ||
        policy->minimum_tx_to_peak > 1.0f ||
        policy->minimum_winner_ratio <= 1.0f ||
        policy->minimum_empty_contrast <= 1.0f) return;
    for (unsigned int b = 0; b < DDMA_RESOLVER_BANDS; ++b) {
        if (!isfinite(bands[b]) || bands[b] < 0.0f) return;
        if (bands[b] > peak) peak = bands[b];
    }
    if (peak <= policy->minimum_amplitude) {
        out->status = DDMA_EMPTY;
        return;
    }
    /* Retain the original cyclic four-TX max-min score, but never silently
     * choose the first member of a tie as a trustworthy velocity. */
    for (unsigned int a = 0; a < DDMA_RESOLVER_BANDS; ++a) {
        float s = bands[a];
        for (unsigned int tx = 1; tx < DDMA_RESOLVER_TX; ++tx) {
            float v = bands[(a + tx) % DDMA_RESOLVER_BANDS];
            if (v < s) s = v;
        }
        scores[a] = s;
        if (s > scores[winner]) winner = a;
    }
    out->anchor = (uint8_t)winner;
    out->best = scores[winner];
    for (unsigned int a = 0; a < DDMA_RESOLVER_BANDS; ++a) {
        if (a != winner && scores[a] > out->runner_up)
            out->runner_up = scores[a];
        if (scores[a] > policy->minimum_amplitude &&
            scores[a] * policy->minimum_winner_ratio >= out->best)
            out->candidates |= (uint8_t)(1u << a);
        if ((a + DDMA_RESOLVER_BANDS - winner) % DDMA_RESOLVER_BANDS <
            DDMA_RESOLVER_TX)
            out->amplitude += bands[a];
        else if (bands[a] > out->empty_max)
            out->empty_max = bands[a];
    }
    if (!isfinite(out->amplitude)) return;
    if (out->best <= policy->minimum_amplitude ||
        out->best < peak * policy->minimum_tx_to_peak) {
        out->status = DDMA_WEAK;
        return;
    }
    if (out->best <= out->runner_up * policy->minimum_winner_ratio) {
        out->status = DDMA_AMBIGUOUS;
        return;
    }
    if (out->best <= out->empty_max * policy->minimum_empty_contrast) {
        out->status = DDMA_CONTAMINATED;
        return;
    }
    out->status = DDMA_RESOLVED;
    {
        float interference = fmaxf(out->runner_up, out->empty_max);
        float quality = 100.0f * (1.0f - interference / out->best);
        out->quality = (uint8_t)fminf(100.0f, fmaxf(0.0f, quality));
    }
}

float ddma_signed_bin(float bin, unsigned int fft_size)
{
    float n = (float)fft_size;
    if (!fft_size || !isfinite(bin)) return NAN;
    bin = fmodf(bin, n);
    if (bin < 0.0f) bin += n;
    if (bin >= 0.5f * n) bin -= n;
    return bin;
}
