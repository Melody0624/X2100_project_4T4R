CFLAGS += -Ithird_party/libiaac/
#-------------------------------------------------------
package_name = libalg
package_depends =
package_builtin_src = third_party/libiaac/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package_lib-y += third_party/libiaac/libalg.a
package_lib-y += third_party/libiaac/libiaac.a
