#include <devices/gpio_backlight.h>
#include <devices/pwm_backlight.h>
#include <devices/adc_keyboard.h>
#include <devices/gpio_keyboard.h>
#include <devices/codec_ak4951.h>
#include <devices/codec_es8389.h>
#include <driver/gpio_regulator.h>

#ifdef CONFIG_EMMC_DEVICE
extern int mmc_devices_init(void);
#endif

#ifdef CONFIG_WIRELESS_INFINEON_CYWWHD
extern int wireless_infineonwhd_drv_init(void);
#endif

#ifdef CONFIG_WIRELESS_ATBM
extern int wireless_atbm_drv_init(void);
#endif

#ifdef CONFIG_X1000_SENSOR_SC031IoT
extern void sc031IoT_sensor_init(void);
#endif

#ifdef CONFIG_X1000_SENSOR_GC0308
extern void gc0308_sensor_init(void);
#endif

#ifdef CONFIG_X1000_SENSOR_GC2155
extern void gc2155_sensor_init(void);
#endif

#ifdef CONFIG_X1000_SENSOR_BF20A6
extern void bf20a6_sensor_init(void);
#endif

#ifdef CONFIG_X1520_SENSOR_GC2145
extern void gc2145_sensor_init(void);
#endif

#ifdef CONFIG_X1520_SENSOR_OV9732
extern void ov9732_sensor_init(void);
#endif

#ifdef CONFIG_X1520_SENSOR_OV9281_DVP
extern void ov9281_dvp_sensor_init(void);
#endif

#ifdef CONFIG_X1520_SENSOR_OV9281_MIPI
extern void ov9281_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X1830_SENSOR_OV2735
extern void ov2735_sensor_init(void);
#endif

#ifdef CONFIG_X1830_SENSOR_IMX307
extern void imx307_sensor_init(void);
#endif

#ifdef CONFIG_X1830_SENSOR_AR0522
extern void ar0522_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X1830_SENSOR_AR0230
extern void ar0230_sensor_init(void);
#endif

#ifdef CONFIG_X1021_SENSOR_GC0328
extern void gc0328_sensor_init(void);
#endif

#ifdef CONFIG_X1021_SENSOR_OV9732
extern void ov9732_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_SC031_DVP
extern void sc031_dvp_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_SC031_MIPI
extern void sc031_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_SC035_DVP
extern void sc035_dvp_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_STI2250_MIPI
extern void sti2250_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_SC035_MIPI
extern void sc035_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_SC132_MIPI
extern void sc132_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_GC0328
extern void gc0328_sensor_init(void);
#endif

#ifdef CONFIG_X1600_SENSOR_PAG7930
void pag7930_mipi_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR0_GC2053
extern void gc2053_sensor0_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR1_GC2053
extern void gc2053_sensor1_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_JZ0378
extern void jz0378_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR0_SC2310
extern void sc2310_sensor0_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR1_SC2310
extern void sc2310_sensor1_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR0_SC2355
extern void sc2355_sensor0_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR1_SC2355
extern void sc2355_sensor1_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_SC132
extern void sc132_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR0_SC031
extern void sc031_sensor0_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR1_SC031
extern void sc031_sensor1_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR0_SC035
extern void sc035_sensor0_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR1_SC035
extern void sc035_sensor1_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_OV9281
extern void ov9281_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_OV9284
extern void ov9284_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR0_OV2735
extern void ov2735_sensor0_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR1_OV2735
extern void ov2735_sensor1_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_GVM8666B
extern void gvm8666b_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_GC1054_DVP
extern void gc1054_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_GC2053_DVP
extern void gc2053_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_IMX335_MIPI
extern void imx335_sensor_init(void);
#endif

#ifdef CONFIG_X2000_SENSOR_CHEETAH
extern void cheetah_sensor_init(void);
#endif

#ifdef CONFIG_X2600_SENSOR_SC031
extern void sc031_sensor_init(void);
#endif

#ifdef CONFIG_CIS_DL520
extern int dl520_init(void);
#endif

#ifdef CONFIG_AFE_HT82V38
extern int ht82v38_init(void);
#endif



static void cim_sensor_init(void)
{

#ifdef CONFIG_X1000_SENSOR_SC031IoT
    sc031IoT_sensor_init();
#endif

#ifdef CONFIG_X1000_SENSOR_GC0308
    gc0308_sensor_init();
#endif

#ifdef CONFIG_X1000_SENSOR_GC2155
    gc2155_sensor_init();
#endif

#ifdef CONFIG_X1000_SENSOR_BF20A6
    bf20a6_sensor_init();
#endif

#ifdef CONFIG_X1520_SENSOR_GC2145
    gc2145_sensor_init();
#endif

#ifdef CONFIG_X1520_SENSOR_OV9732
    ov9732_sensor_init();
#endif

#ifdef CONFIG_X1520_SENSOR_OV9281_DVP
    ov9281_dvp_sensor_init();
#endif

#ifdef CONFIG_X1520_SENSOR_OV9281_MIPI
    ov9281_mipi_sensor_init();
#endif

#ifdef CONFIG_X1830_SENSOR_OV2735
    ov2735_sensor_init();
#endif

#ifdef CONFIG_X1830_SENSOR_IMX307
    imx307_sensor_init();
#endif

#ifdef CONFIG_X1830_SENSOR_AR0522
    ar0522_mipi_sensor_init();
#endif

#ifdef CONFIG_X1830_SENSOR_AR0230
    ar0230_sensor_init();
#endif

#ifdef CONFIG_X1021_SENSOR_GC0328
    gc0328_sensor_init();
#endif

#ifdef CONFIG_X1021_SENSOR_OV9732
    ov9732_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_SC031_DVP
    sc031_dvp_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_SC031_MIPI
    sc031_mipi_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_SC035_DVP
    sc035_dvp_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_STI2250_MIPI
    sti2250_mipi_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_SC035_MIPI
    sc035_mipi_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_SC132_MIPI
    sc132_mipi_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_GC0328
    gc0328_sensor_init();
#endif

#ifdef CONFIG_X1600_SENSOR_PAG7930
    pag7930_mipi_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR0_GC2053
    gc2053_sensor0_init();
#endif

#ifdef CONFIG_X2000_SENSOR1_GC2053
    gc2053_sensor1_init();
#endif

#ifdef CONFIG_X2000_SENSOR_JZ0378
    jz0378_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR0_SC2310
    sc2310_sensor0_init();
#endif

#ifdef CONFIG_X2000_SENSOR1_SC2310
    sc2310_sensor1_init();
#endif

#ifdef CONFIG_X2000_SENSOR0_SC2355
    sc2355_sensor0_init();
#endif

#ifdef CONFIG_X2000_SENSOR1_SC2355
    sc2355_sensor1_init();
#endif

#ifdef CONFIG_X2000_SENSOR_SC132
    sc132_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR0_SC031
    sc031_sensor0_init();
#endif

#ifdef CONFIG_X2000_SENSOR1_SC031
    sc031_sensor1_init();
#endif

#ifdef CONFIG_X2000_SENSOR0_SC035
    sc035_sensor0_init();
#endif

#ifdef CONFIG_X2000_SENSOR1_SC035
    sc035_sensor1_init();
#endif

#ifdef CONFIG_X2000_SENSOR_OV9281
    ov9281_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR_OV9284
    ov9284_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR_SC120
    sc120_mipi_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR0_OV2735
    ov2735_sensor0_init();
#endif

#ifdef CONFIG_X2000_SENSOR1_OV2735
    ov2735_sensor1_init();
#endif

#ifdef CONFIG_X2000_SENSOR_GVM8666B
    gvm8666b_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR_GC1054_DVP
    gc1054_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR_GC2053_DVP
    gc2053_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR_IMX335_MIPI
    imx335_sensor_init();
#endif

#ifdef CONFIG_X2000_SENSOR_CHEETAH
    cheetah_sensor_init();
#endif

#ifdef CONFIG_X2600_SENSOR_SC031
    sc031_sensor_init();
#endif

}

static void cis_devices_init(void)
{

#ifdef CONFIG_CIS_DL520
    dl520_init();
#endif

}

void external_devices_init(void)
{
#ifdef CONFIG_PWM_BACKLIGHT
    pwm_backlight_init();
#endif

#ifdef CONFIG_GPIO_BACKLIGHT
    gpio_backlight_init();
#endif

#ifdef CONFIG_ADC_KEYBOARD
    adc_keyboard_init();
#endif

#ifdef CONFIG_GPIO_KEYBOARD
    gpio_keyboard_init();
#endif

#ifdef CONFIG_CODEC_AK4951
    ak4951_init();
#endif

#ifdef CONFIG_CODEC_ES8389
    es8389_init();
#endif

#ifdef CONFIG_EMMC_DEVICE
    mmc_devices_init();
#endif

#ifdef CONFIG_GPIO_REGULATOR
    gpio_regulator_init();
#endif

#ifdef CONFIG_WIRELESS_INFINEON_CYWWHD
    wireless_infineonwhd_drv_init();
#endif

#ifdef CONFIG_WIRELESS_ATBM
    wireless_atbm_drv_init();
#endif

#ifdef CONFIG_AFE_HT82V38
    ht82v38_init();
#endif
    cim_sensor_init();
    cis_devices_init();
}
