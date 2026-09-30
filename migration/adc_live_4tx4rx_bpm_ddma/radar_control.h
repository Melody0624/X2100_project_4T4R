#ifndef RADAR_CONTROL_H
#define RADAR_CONTROL_H

#include <stdint.h>

void radar_control_init(void);
/* Seed RADV magic only when the entire config partition is erased. */
int radar_control_initialize_blank_config(void);
void radar_control_poll(void);
int radar_control_running(void);
int radar_control_raw_enabled(void);
void radar_control_frame_done(void);
int radar_control_save_adc(uint32_t frame_id, const void *payload,
                           uint32_t payload_bytes);

#endif
