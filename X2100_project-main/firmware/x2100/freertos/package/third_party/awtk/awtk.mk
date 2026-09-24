#-------------------------------------------------------
package_name = awtk
package_depends =
package_builtin_src = third_party/awtk/
package_make_hook =
package_init_hook = generate_awtk_lib
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
awtk_addr += third_party/awtk

define generate_awtk_lib
	@if [ ! -d third_party/awtk/awtk ]; then make -f $(awtk_addr)/get_awtk_lib.mk; fi
endef