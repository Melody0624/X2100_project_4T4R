#ifndef FFT_COMPAT_HEAP_MALLOC_H
#define FFT_COMPAT_HEAP_MALLOC_H

/*
 * The legacy radar project redirected malloc/free to a statistics wrapper.
 * The reduced SDK already provides thread-safe newlib allocation, so NE10 can
 * use the standard API without changing any FFT arithmetic implementation.
 */
#include <stdlib.h>

#endif
