CFLAGS += -Ithird_party/faad2/include/
CFLAGS += -Ithird_party/faad2/libfaad/
CFLAGS += -DPACKAGE_VERSION=\"2.11.2\"
CFLAGS += -DFIXED_POINT

#-------------------------------------------------------
package_name = faad2
package_depends =
package_builtin_src = third_party/faad2/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
