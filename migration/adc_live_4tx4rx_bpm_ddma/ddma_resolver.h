#ifndef DDMA_RESOLVER_H
#define DDMA_RESOLVER_H

#include <stdint.h>
#include "radar_config.h"

#define DDMA_RESOLVER_BANDS RADAR_DDMA_NUM_SUBBANDS
#define DDMA_RESOLVER_TX RADAR_NUM_TX

enum ddma_status {
    DDMA_EMPTY = 0,
    DDMA_RESOLVED,
    DDMA_AMBIGUOUS,
    DDMA_CONTAMINATED,
    DDMA_WEAK,
    DDMA_INVALID
};

struct ddma_policy {
    float minimum_amplitude;
    float minimum_tx_to_peak;
    float minimum_winner_ratio;
    float minimum_empty_contrast;
};

/* Quality is a diagnostic score, NOT a calibrated probability.  A resolved
 * result means only that the SINGLE-target template passed these tests. */
struct ddma_result {
    uint8_t status;
    uint8_t anchor;
    uint8_t candidates;
    uint8_t quality;
    float best;
    float runner_up;
    float empty_max;
    float amplitude;
};

extern const struct ddma_policy ddma_default_policy;
void ddma_resolve(const float bands[DDMA_RESOLVER_BANDS],
                  const struct ddma_policy *policy, struct ddma_result *out);
float ddma_signed_bin(float bin, unsigned int fft_size);

#endif
