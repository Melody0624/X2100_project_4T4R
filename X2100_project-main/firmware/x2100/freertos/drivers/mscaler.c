#include <driver/mscaler.h>

void soc_mscaler_init(void);

int soc_mscaler_convert(struct mscaler_param *ms_param);

int soc_mscaler_align_size(void);

void mscaler_init(void)
{
    soc_mscaler_init();
}

int mscaler_convert(struct mscaler_param *ms_param)
{
    return soc_mscaler_convert(ms_param);
}

int mscaler_align_size(void)
{
    return soc_mscaler_align_size();
}
