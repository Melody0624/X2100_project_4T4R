CFLAGS += -I$(TOPDIR)net/lwip/include
CFLAGS += -I$(TOPDIR)os/freertos/include
#CFLAGS += -nostdlib

#-------------------------------------------------------
package_name = lwip
package_depends =
package_builtin_src = net/lwip/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
