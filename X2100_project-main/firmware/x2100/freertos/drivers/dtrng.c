#include <soc/dtrng.h>

void dtrng_init(void)
{
    soc_dtrng_init();
}

unsigned int dtrng_read_random_data(void)
{
    return soc_dtrng_read_random_data();
}

void dtrng_deinit(void)
{
    soc_dtrng_deinit();
}