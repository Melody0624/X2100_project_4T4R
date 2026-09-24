CFLAGS += -Ithird_party/libgoodixTof/include/

#-------------------------------------------------------
package_name = libgoodixTof
package_depends =
package_builtin_src =
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

package_lib-y += third_party/libgoodixTof/lib/libGoodixTofAlgo.a
