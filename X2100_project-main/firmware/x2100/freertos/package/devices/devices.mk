#-------------------------------------------------------
package_name = devices
package_depends =
package_builtin_src = devices/
package_make_hook =
package_init_hook = devices_init_hook
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-y += package/devices/wireless/wireless.mk

ifdef MD_LCD_ILCDTOOLS_BIN
define update_lcd_ilcdtools
    touch $(TOPDIR)devices/lcd/ilcdtools/ilt_data.c
endef
endif

define devices_init_hook
    $(update_lcd_ilcdtools)
endef
