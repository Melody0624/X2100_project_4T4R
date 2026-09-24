#ifndef RADAR_FRONTEND_H
#define RADAR_FRONTEND_H

int radar_frontend_init(void);
const unsigned char *radar_frontend_wait_frame(void);
void radar_frontend_release_frame(const unsigned char *frame);

#endif
