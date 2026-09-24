
#-------------------------------------------------------
package_name = drivers
package_depends =
package_builtin_src = drivers/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_USB_DRIVER) += package/drivers/usb/usb.mk
