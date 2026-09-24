#ifndef _PRINT_H_
#define _PRINT_H_

#include "irqflags.h"

struct zero {
    unsigned int code[2];
    int (*printer)(const char *str, ...);
    // unsigned int data[4];
};

extern struct zero *zero;

#define print(x...) \
    do { \
        unsigned long _flags_; \
        local_irq_save(_flags_); \
        zero->printer(x); \
        local_irq_restore(_flags_); \
    } while (0)
 
#endif /* _PRINT_H_ */