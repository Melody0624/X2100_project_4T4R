#ifndef HOST_OS_H
#define HOST_OS_H
#include <stddef.h>
static inline void msleep(unsigned int ms) { (void)ms; }
static inline void *thread_create(const char *name, unsigned int size,
                                  void (*fn)(void *), void *arg)
{ (void)name; (void)size; (void)fn; (void)arg; return NULL; }
#endif
