#-------------------------------------------------------
package_name = application
package_depends =
package_builtin_src = application/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_APPLICATION_FACE) += package/application/face/face.mk
package-$(CONFIG_APPLICATION_LOAD_KERNEL) += package/application/load_kernel/load_kernel.mk
