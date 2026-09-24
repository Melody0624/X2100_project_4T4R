
#-------------------------------------------------------
package_name = os
package_depends =
package_builtin_src = os/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_OS_FREERTOS) += package/os/freertos/freertos.mk
package-$(CONFIG_OS_ITOS) += package/os/itos/itos.mk
package-$(CONFIG_OS_POSIX) += package/os/posix/posix.mk
