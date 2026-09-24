# 使用工具链自带的 newlib
# 工具链会自动根据编译选项（-march, -mfp64/-mfp32, -msoft-float等）选择正确的库路径
# XBurst1: mips-sde-elf/lib/ (FP32默认)
# XBurst2: mips-sde-elf/lib/ (FP64默认)
# 保留工程内部的自定义实现（sbrk, lock 等）在 newlib/ 目录下编译

# 头文件优先级（从高到低）：
# 1. os/posix/include: 提供 semaphore.h, mqueue.h 等工具链缺失的 POSIX 头文件
# 2. 工具链 newlib 4.3.0: 提供 pthread.h, stdio.h 等标准头文件

CFLAGS += -D__LINUX_ERRNO_EXTENSIONS__

# 添加 os/posix POSIX 补充头文件（最高优先级）
CFLAGS := -isystem $(TOPDIR)os/posix/include $(CFLAGS)
MODULE_CFLAGS := -isystem $(TOPDIR)os/posix/include $(MODULE_CFLAGS)

# 添加工具链 newlib 头文件路径（主要头文件来源）
# RTOS_TOOLCHAIN_PATH 格式：../tools/toolchains/mips-xburst2-newlib430/bin
# TOOLCHAIN_ROOT：../tools/toolchains/mips-xburst2-newlib430
# TOOLCHAIN_SYSROOT：../tools/toolchains/mips-xburst2-newlib430/mips-sde-elf
# 只有在 RTOS_TOOLCHAIN_PATH 非空时才处理工具链路径
ifneq ($(RTOS_TOOLCHAIN_PATH),)
TOOLCHAIN_ROOT := $(shell dirname "$(RTOS_TOOLCHAIN_PATH)")
TOOLCHAIN_SYSROOT := $(shell realpath $(TOOLCHAIN_ROOT)/mips-sde-elf 2>/dev/null)
ifneq ($(TOOLCHAIN_SYSROOT),)
CFLAGS := $(CFLAGS) -isystem $(TOOLCHAIN_SYSROOT)/include
MODULE_CFLAGS := $(MODULE_CFLAGS) -isystem $(TOOLCHAIN_SYSROOT)/include
ASM_FLAGS := -isystem $(TOOLCHAIN_SYSROOT)/include $(ASM_FLAGS)
endif
endif

# 定义 SSIZE_MAX 以防止 lwip 重复定义 ssize_t
CFLAGS := -DSSIZE_MAX=INT_MAX $(CFLAGS)
MODULE_CFLAGS := -DSSIZE_MAX=INT_MAX $(MODULE_CFLAGS)

# 禁用 MIPS GP 相对寻址优化，避免工具链 newlib 的 _impure_ptr 重定位问题
# 工具链 newlib 可能使用不同的 -G 值编译，导致链接时 GPREL16 重定位溢出
CFLAGS := -mno-gpopt $(CFLAGS)
MODULE_CFLAGS := -mno-gpopt $(MODULE_CFLAGS)

# 定义 POSIX 特性宏以启用完整的 POSIX 支持
# _POSIX_THREADS: 启用 pthread API
# _UNIX98_THREAD_MUTEX_ATTRIBUTES: 启用 pthread_mutexattr_t.type 成员
# _POSIX_BARRIERS: 启用 pthread_barrier API
CFLAGS := -D_POSIX_THREADS -D_UNIX98_THREAD_MUTEX_ATTRIBUTES -D_POSIX_BARRIERS -D_POSIX_TIMERS $(CFLAGS)
MODULE_CFLAGS := -D_POSIX_THREADS -D_UNIX98_THREAD_MUTEX_ATTRIBUTES -D_POSIX_BARRIERS $(MODULE_CFLAGS)

# 添加工具链库文件搜索路径
ifdef TOOLCHAIN_SYSROOT
FINAL_LDFLAGS += -L $(TOOLCHAIN_SYSROOT)/lib
# 添加 GCC 库路径（libgcc.a 所在目录）
# gcc 会根据编译选项（-march, -mfp32/-mfp64等）自动选择正确的 libgcc
ifneq ($(filter %_defconfig,$(MAKECMDGOALS)),)
LIBGCC_FILE :=
LIBGCC_PATH :=
else
LIBGCC_FILE := $(strip $(shell "$(TOOLCHAIN_CC_FOR_SHELL)" -print-libgcc-file-name))
ifneq ($(LIBGCC_FILE),)
LIBGCC_PATH := $(strip $(shell dirname "$(LIBGCC_FILE)"))
ifneq ($(LIBGCC_PATH),)
FINAL_LDFLAGS += -L $(LIBGCC_PATH)
endif
endif
endif
endif

# 库的链接顺序很重要：
# 1. -lc -lg -lm: newlib C 库和数学库（需要系统调用）
# 2. -lnosys: 提供系统调用 stubs（满足 libc 的依赖）
# 3. -lgcc: GCC 运行时库（最后链接）
# 注意：_init 和 _fini 符号由 newlib/init.c 提供空实现
package_lib-y += -lc -lg -lm -lnosys -lgcc
MODULE_LIBS += -lm -lgcc

#-------------------------------------------------------
package_name = newlib
package_depends =
package_builtin_src = newlib/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------
