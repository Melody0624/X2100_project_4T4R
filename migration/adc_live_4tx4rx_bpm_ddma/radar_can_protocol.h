#ifndef RADAR_CAN_PROTOCOL_H
#define RADAR_CAN_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

/* 20250620 simplified protocol; classic CAN standard DATA frames only.
 * No packed C bitfields, no host-endian memcpy, no implicit physical scaling. */
enum rcan_result { RCAN_OK=0, RCAN_INVALID=-1, RCAN_RANGE=-2,
                   RCAN_UNSUPPORTED=-3, RCAN_IO=-4, RCAN_UNBOUND=-5 };
enum rcan_face { RCAN_LEFT=0, RCAN_FRONT=1, RCAN_RIGHT=2 };
enum { RCAN_MODE=0x001, RCAN_ANGLE=0x002, RCAN_DISTANCE=0x003,
       RCAN_COUNT=0x004, RCAN_PERIOD=0x005, RCAN_POSE=0x011,
       RCAN_UPDATE_DATA=0x021, RCAN_UPDATE_END=0x022,
       RCAN_SELFTEST=0x101, RCAN_HEARTBEAT=0x102, RCAN_UPDATE_REPORT=0x121 };
struct rcan_frame { uint16_t id; uint8_t dlc, flags; uint8_t data[8]; };
/* Integer wire units. Caller must supply VALID semantic data, not FFT power
 * in place of calibrated RCS or radial velocity in place of vector speed.
 * Ambiguous direction values 128..180 are rejected pending vendor agreement. */
struct rcan_point {
    uint16_t rcs_half_dbsm;
    int16_t azimuth_deg;
    uint16_t direction_2deg;
    int16_t elevation_01deg;
    uint8_t elevation_present;
    uint16_t range_02m, point_id;
    int16_t speed_05mps;
};
/* Heartbeat fields already quantized in the DOCUMENT's units. No guessing
 * whether angle/distance 'range' means width, maximum, or absolute bound. */
struct rcan_face_status {
    uint8_t mode, angle_6deg, distance_20m, period_100ms, count_150, fault;
};
struct rcan_command {
    uint16_t id;
    uint8_t modes[3];
    int16_t upper[3], lower[3];
    uint16_t values[3];
    uint16_t heading_01deg, speed_01mps;
    int16_t yaw_rate_01degps;
};
int rcan_codec_selfcheck(void); /* local known-byte test, NO CAN transmission */
int rcan_encode_point(enum rcan_face face, const struct rcan_point *point,
                      struct rcan_frame *out);
int rcan_encode_boundary(enum rcan_face face, uint32_t batch, int end,
                         uint16_t count, struct rcan_frame *out);
int rcan_encode_selftest(const uint8_t fault[3], uint8_t model,
                         struct rcan_frame *out);
int rcan_encode_heartbeat(const struct rcan_face_status status[3], struct rcan_frame *out);
int rcan_encode_update_report(uint8_t failed, uint8_t reason, struct rcan_frame *out);
/* Parsing only: does not apply modes, reset hardware or write firmware. */
int rcan_decode_command(const struct rcan_frame *frame, struct rcan_command *out);
/* Single-owner synchronous batch sender. Callback must return promptly, copy
 * frame if queued, preserve order, return 0 accepted / nonzero failure. */
typedef int (*rcan_send_fn)(void *context, enum rcan_face face, const struct rcan_frame *frame);
struct rcan_tx {
    rcan_send_fn send;
    void *context;
    uint32_t next_batch[3];
    uint64_t batches_ok, batches_failed, frames_accepted;
};
void rcan_tx_init(struct rcan_tx *tx, rcan_send_fn send, void *context);
int rcan_send_batch(struct rcan_tx *tx, enum rcan_face face,
                    const struct rcan_point *points, size_t count);
#endif
