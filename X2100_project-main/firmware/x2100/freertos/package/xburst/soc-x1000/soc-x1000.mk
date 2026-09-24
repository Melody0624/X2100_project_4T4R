CFLAGS += -Ixburst/soc-x1000/include

#-------------------------------------------------------
package_name = soc-x1000
package_depends = xburst
package_builtin_src = xburst/soc-x1000/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_X1000_MAC) += package/xburst/soc-x1000/mac/mac.mk
package-$(CONFIG_MMC) += package/xburst/soc-x1000/mmc/mmc.mk
