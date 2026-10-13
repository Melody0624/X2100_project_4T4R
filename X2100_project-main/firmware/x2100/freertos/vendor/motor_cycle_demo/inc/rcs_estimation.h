#ifndef RCS_ESTIMATION_H
#define RCS_ESTIMATION_H

#include "radar_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Separate storage preserves the DetObj/GlbCtx ABI used by vendor libraries. */
typedef struct {
    float relative_db;  /* pwr + 40*log10(slant_range/1 m), not dBsm */
    float dbsm;         /* NAN when reference calibration is unavailable/cleared */
    bool valid;
    bool calibrated;
} RcsEstimate;

typedef struct {
    int frame_id;
    unsigned int count;
    RcsEstimate points[MAX_DETECTIONS]; /* Same order as final detInfo.detObj */
} RcsFrame;

/* pwr_db is raw DetObj.pwr, BEFORE the protocol's x100 scaling.
 * These APIs are owned by the frame-processing task. Copy results for an
 * asynchronous sender; returned storage is reused on the next frame. */
RcsEstimate rcs_calculate(float pwr_db, float slant_range_m);
bool rcs_set_reference(float reference_dbsm, float reference_range_m,
                       float reference_pwr_db);
void rcs_clear_calibration(void);
void rcs_clear_frame(int pool_index);
int rcs_process_frame(const GlbCtx *ctx, int pool_index);
const RcsFrame *rcs_get_frame(int pool_index);

#ifdef __cplusplus
}
#endif
#endif
