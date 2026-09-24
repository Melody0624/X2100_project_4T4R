
CFLAGS += -Ithird_party/libcxx/include/
CFLAGS += -D_LIBCPP_HAS_NO_THREADS
CFLAGS += -D_LIBCPP_HAS_NO_MONOTONIC_CLOCK
CFLAGS += -D_LIBCPP_BUILDING_LIBRARY
CFLAGS += -D_LIBCPP_NO_EXCEPTIONS
CFLAGS += -D_POSIX_TIMERS

ifndef CONFIG_LIBCXX_RTTI
CXXFLAGS += -fno-rtti
endif

#-------------------------------------------------------
package_name = libcxx
package_depends =
package_builtin_src = third_party/libcxx/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
