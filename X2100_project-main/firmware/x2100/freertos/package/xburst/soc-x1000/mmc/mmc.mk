CFLAGS += -Ixburst/soc-x1000/mmc

# MMC driver is ported from Linux, needs Linux errno extensions

#-------------------------------------------------------
package_name = mmc
package_depends =
package_builtin_src = xburst/soc-x1000/mmc/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
