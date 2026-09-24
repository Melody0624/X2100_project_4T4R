
package-y += package/build/build.mk
package-$(CONFIG_XBURST) += package/xburst/xburst.mk
package-$(CONFIG_XBURST2) += package/xburst2/xburst2.mk
package-$(CONFIG_LIB) += package/lib/lib.mk
package-$(CONFIG_DRIVERS) += package/drivers/drivers.mk
package-$(CONFIG_OS) += package/os/os.mk
package-$(CONFIG_DFS) += package/filesystem/filesystem.mk
package-$(CONFIG_SYMBOLS) += package/symbols/symbols.mk
package-$(CONFIG_SHELL) += package/shell/shell.mk
package-$(CONFIG_NET_LWIP) += package/net/lwip/lwip.mk
package-$(CONFIG_DEVICES) += package/devices/devices.mk

# 排在最后
package-y += package/vendor/vendor.mk
package-y += package/third_party/third_party.mk
package-$(CONFIG_NEWLIB) += package/newlib/newlib.mk
package-y += package/application/application.mk
