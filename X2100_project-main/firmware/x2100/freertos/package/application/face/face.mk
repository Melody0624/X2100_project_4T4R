CFLAGS += -Iapplication/face

#-------------------------------------------------------
package_name = face
package_depends =
package_builtin_src = application/face/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package-$(CONFIG_APPLICATION_X2000_ILOCK) += package/application/face/x2000_ilock/x2000_ilock.mk
package-$(CONFIG_APPLICATION_X2000_IR_SC2355) += package/application/face/x2000_ir_sc2355/x2000_ir_sc2355.mk
package-$(CONFIG_APPLICATION_X2000_TOF) += package/application/face/x2000_tof/x2000_tof.mk
