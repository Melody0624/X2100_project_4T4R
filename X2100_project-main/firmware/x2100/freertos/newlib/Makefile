src-y += sbrk.c
src-y += syscalls_r.c
src-y += init.c
src-y += isatty.c
src-y += gettimeofday.c
src-$(CONFIG_OS_MODULE) += export.c
src-$(CONFIG_OS) += malloc_lock.c
src-$(CONFIG_OS) += lock.c

src-$(CONFIG_LIBCXX) += newlib/init_fix_cxx_stdio_err.cpp
src-$(CONFIG_LIBSTDCPP) += newlib/init_fix_cxx_stdio_err.cpp

src-$(CONFIG_MIPS_SETJMP) += jz_setjmp.S
