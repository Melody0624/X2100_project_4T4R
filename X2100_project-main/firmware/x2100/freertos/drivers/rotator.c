#include <driver/rotator.h>


int soc_rotator_init(void);
int soc_rotator_complete_conversion(struct rotator_config_data *data);


void rotator_init(void)
{
    soc_rotator_init();
}

int rotator_conversion(struct rotator_config_data *data)
{
    return soc_rotator_complete_conversion(data);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(rotator_init);
EXPORT_SYMBOL(rotator_conversion);