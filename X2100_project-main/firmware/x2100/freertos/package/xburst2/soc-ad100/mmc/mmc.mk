CFLAGS += -Ixburst2/soc-ad100/mmc

# MMC driver is ported from Linux, needs Linux errno extensions

#-------------------------------------------------------
package_name = mmc
package_depends =
package_builtin_src = xburst2/soc-ad100/mmc/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
