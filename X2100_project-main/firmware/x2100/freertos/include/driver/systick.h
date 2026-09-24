#ifndef _SYSTICK_H_
#define _SYSTICK_H_

#include <stdint.h>

#define MSEC_PER_SEC                    1000L
#define USEC_PER_MSEC                   1000L
#define NSEC_PER_USEC                   1000L
#define NSEC_PER_MSEC                   1000000L
#define USEC_PER_SEC                    1000000L
#define NSEC_PER_SEC                    1000000000L

typedef void (*systick_event_handler_t)(void);

uint64_t systick_get_time_us(void);
uint64_t systick_get_time_ns(void);
uint64_t systick_get_time_ms(void);
void systick_init(void);
void systick_set_event_callback(systick_event_handler_t callback);
void systick_set_next_time(uint64_t expires);
void systick_add_time_us(uint64_t usecs);

#endif /* _SYSTICK_H_ */