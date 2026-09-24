#include <malloc.h>
#include "string.h"
#include "asm/addrspace.h"
#include <spl_rtos_argument.h>

int soc_mmc_init(void *data);

void mmc_init(void *data)
{
    struct spl_rtos_argument *spl_argument = (struct spl_rtos_argument *)data;
    void *mmc_data = NULL;
    if ((unsigned long)spl_argument > CKSEG0 && (unsigned long)spl_argument < CKSEG2)
        mmc_data = (void *)spl_argument->card_params;

    soc_mmc_init(mmc_data);
}




#include <kernel_symbol.h>

