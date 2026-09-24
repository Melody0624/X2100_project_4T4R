CFLAGS += -Ixburst2/soc-x2000/include
CFLAGS += -Ixburst2/soc-x2000/jpeg_encoder/include

#-------------------------------------------------------
package_name = soc-x2000
package_depends = xburst2
package_builtin_src = xburst2/soc-x2000/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_CAMERA) += package/xburst2/soc-x2000/camera/camera.mk
package-$(CONFIG_MMC) += package/xburst2/soc-x2000/mmc/mmc.mk
package-$(CONFIG_X2000_MAC) += package/xburst2/soc-x2000/mac/mac.mk
package-$(CONFIG_X2000_VCODEC) += package/xburst2/soc-x2000/vcodec/vcodec.mk