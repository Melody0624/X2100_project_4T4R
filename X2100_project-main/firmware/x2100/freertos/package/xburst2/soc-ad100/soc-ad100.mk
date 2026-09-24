CFLAGS += -Ixburst2/soc-ad100/include

#-------------------------------------------------------
package_name = soc-ad100
package_depends = xburst2
package_builtin_src = xburst2/soc-ad100/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_MMC) += package/xburst2/soc-ad100/mmc/mmc.mk
package-$(CONFIG_AD100_VCODEC) += package/xburst2/soc-ad100/vcodec/vcodec.mk
package-$(CONFIG_AD100_MAC) += package/xburst2/soc-ad100/mac/mac.mk
package-$(CONFIG_AD100_JPEG) += package/xburst2/soc-ad100/jpeg/jpeg.mk