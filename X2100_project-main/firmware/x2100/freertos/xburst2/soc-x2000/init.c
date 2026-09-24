#include <driver/pcm.h>

void icodec_init(void);
void aic_init(void);
int audio_dma_init(void);
void dmic_init(void);

void soc_extra_device_init(void)
{
#ifdef CONFIG_X2000_ICODEC
    icodec_init();
#endif

#ifdef CONFIG_X2000_AIC
    aic_init();
#endif

#ifdef CONFIG_X2000_DMIC
    dmic_init();
#endif

#ifdef CONFIG_X2000_AUDIO
    audio_dma_init();
#endif

}
