#include <lds_symbol.h>
#include <common.h>
#include <errno.h>

char *heap_ptr = (char *)&_user_heap_start;

/*
 * loader(SPL/Uboot)程序中预加载容量大小  单位:字节
 */
static inline uint32_t user_heap_get_mapped_rtosdata_size(void)
{
#ifdef CONFIG_RAMDISK_DEVICE_MAPPED_PRE_LOAD
    int heap_usable_size = (unsigned int)&_user_heap_end - (unsigned int)&_user_heap_start;

    /* _mapped_rtosdata_size is invalid or too large */
    if (heap_usable_size < _mapped_rtosdata_size)
        return 0;
    else
        return _mapped_rtosdata_size;
#else
    return 0;
#endif
}

/*
 * sbrk -- changes heap size size. Get nbytes more
 *         RAM. We just increment a pointer in what's
 *         left of memory on the board.
 */
char *sbrk (int nbytes)
{
    char *base;
    char *end = (char *)((unsigned int)&_user_heap_end - user_heap_get_mapped_rtosdata_size() );

    base = heap_ptr;

    if (heap_ptr + nbytes >= end) {
        printf("heap memory out of range\n");
        printf("start: %p,%p now: %p, asked:%d\n",
            (char *)&_user_heap_start, end, heap_ptr, nbytes);
        errno = ENOMEM;
        return ((char *)-1);
    } else {
        heap_ptr += nbytes;
    }

    return base;
}

/* Reentrant version required by newlib */
void *_sbrk_r(void *reent, int nbytes)
{
    (void)reent;  /* Ignore reent struct in bare-metal environment */
    return sbrk(nbytes);
}
