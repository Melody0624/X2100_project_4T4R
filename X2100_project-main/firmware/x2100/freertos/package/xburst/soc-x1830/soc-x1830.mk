CFLAGS += -Ixburst/soc-x1830/include

#-------------------------------------------------------
package_name = soc-x1830
package_depends = xburst
package_builtin_src = xburst/soc-x1830/
package_make_hook =
package_init_hook = xburst_x1830_init_hook
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

ifeq ($(CONFIG_X1830_MSCALER),y)
define x1830_mscaler_hook
	./xburst/soc-x1830/mscaler/coef.sh $(CONFIG_X1830_MSCALER_DETAIL)
endef
endif

define xburst_x1830_init_hook
	$(x1830_mscaler_hook)
endef
