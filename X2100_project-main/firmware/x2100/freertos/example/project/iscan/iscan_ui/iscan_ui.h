#ifndef __ISCAN_UI_H__
#define __ISCAN_UI_H__

#include <list.h>

#define UI_IMGDIR  "/userdata/UI/"

enum iscan_bat_status {
    ISCAN_BAT_EMPTY,
    ISCAN_BAT_LOW,
    ISCAN_BAT_MED,
    ISCAN_BAT_FULL,
    ISCAN_BAT_CHARGER,
};

enum iscan_encoder_mode {
    ISCAN_ENCODER_JPEG,
    ISCAN_ENCODER_PDF,
};

enum iscan_dpi_mode {
    ISCAN_DPI_LO,
    ISCAN_DPI_HI,
    ISCAN_DPI_FI,
};

enum iscan_color_mode {
    ISCAN_COLOR_COLOR,
    ISCAN_COLOR_MONO,
};

enum iscan_scan_status {
    ISCAN_SCANSTOP,
    ISCAN_SCANING,
    ISCAN_SCANERR,
};

struct iscan_ui_callback {
    void (*get_battery)(enum iscan_bat_status *bat_status);
    void (*get_usb_insert)(int *is_insert);

    void (*start_scan)(int dpi_mode, int color_mode, int encoder_mode, char *file_path);
    void (*get_scan_status)(enum iscan_scan_status *status);
    void (*stop_scan)(void);

    void (*start_cal)(void);
    void (*get_cal_status)(int *status);

    void (*delete_file)(char *file_path);
    void (*format_sdcard)(void);
};


void iscan_ui_init(struct iscan_ui_callback *iscan_ui_callback, char *img_path);
void iscan_ui_key_clicked(int key, int value);

#endif