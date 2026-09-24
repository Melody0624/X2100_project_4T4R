
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <driver/cache.h>

void *pvPortMalloc( size_t xWantedSize )
{
    return cache_align_malloc(xWantedSize);
}
/*-----------------------------------------------------------*/

void vPortFree( void *pv )
{
    free(pv);
}
/*-----------------------------------------------------------*/

size_t xPortGetFreeHeapSize( void )
{
    return 0;
}
/*-----------------------------------------------------------*/

size_t xPortGetMinimumEverFreeHeapSize( void )
{
    return 0;
}
/*-----------------------------------------------------------*/

void vPortInitialiseBlocks( void )
{
    /* This just exists to keep the linker quiet. */
}
/*-----------------------------------------------------------*/
