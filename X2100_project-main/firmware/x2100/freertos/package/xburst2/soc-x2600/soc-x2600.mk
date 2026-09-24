CFLAGS += -Ixburst2/soc-x2600/include

#-------------------------------------------------------
package_name = soc-x2600
package_depends = xburst2
package_builtin_src = xburst2/soc-x2600/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_CAMERA) += package/xburst2/soc-x2600/camera/camera.mk
package-$(CONFIG_X2600_MAC) += package/xburst2/soc-x2600/mac/mac.mk
package-$(CONFIG_MMC) += package/xburst2/soc-x2600/mmc/mmc.mk
package-$(CONFIG_X2600_VCODEC) += package/xburst2/soc-x2600/vcodec/vcodec.mk
package-$(CONFIG_X2600_JPEG) += package/xburst2/soc-x2600/jpeg/jpeg.mk