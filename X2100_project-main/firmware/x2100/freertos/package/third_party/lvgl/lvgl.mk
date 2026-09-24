
#-------------------------------------------------------
package_name = lvgl
package_depends =
package_builtin_src = third_party/lvgl/
package_make_hook =
package_init_hook = generate_lvgl_lib
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
lvgl_addr += third_party/lvgl

define generate_lvgl_lib
	@if [ ! -d third_party/lvgl/lvgl ]; then \
		make -f $(lvgl_addr)/get_lvgl_lib.mk; \
	else \
		make -f $(lvgl_addr)/get_lvgl_lib.mk lv_conf; \
	fi
endef
