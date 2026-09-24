CFLAGS += -Ixburst/soc-x1600/include

#-------------------------------------------------------
package_name = soc-x1600
package_depends = xburst
package_builtin_src = xburst/soc-x1600/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_CAMERA) += package/xburst/soc-x1600/camera/camera.mk
package-$(CONFIG_MMC) += package/xburst/soc-x1600/mmc/mmc.mk
package-$(CONFIG_X1600_MAC) += package/xburst/soc-x1600/mac/mac.mk
