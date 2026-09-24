# gcc 相关的变量导出
export CROSS
export CFLAGS
export CXXFLAGS
export ASM_FLAGS
export LDFLAGS
export FINAL_LDFLAGS
export PLATFORM_LIBGCC
export LDS_FILE
export AR = $(CROSS)ar
export AS = $(CROSS)as
export LD = $(CROSS)ld
export NM = $(CROSS)nm
export CC = $(CROSS)gcc
export GCC = $(CROSS)gcc
export CPP = $(CROSS)cpp
export CXX = $(CROSS)g++
export FC = $(CROSS)gfortran
export F77 = $(CROSS)gfortran
export RANLIB = $(CROSS)ranlib
export READELF = $(CROSS)readelf
export STRIP = $(CROSS)strip
export OBJCOPY = $(CROSS)objcopy
export OBJDUMP = $(CROSS)objdump

# 工具链路径辅助变量
TOOLCHAIN_PATHS_RAW := $(if $(TOOLCHAIN_ABS_PATH),$(TOOLCHAIN_ABS_PATH),$(RTOS_TOOLCHAIN_PATH))
TOOLCHAIN_PATHS_RAW := $(strip $(TOOLCHAIN_PATHS_RAW))
TOOLCHAIN_BIN_DIRS := $(subst :, ,$(TOOLCHAIN_PATHS_RAW))
TOOLCHAIN_CC_ABS := $(firstword $(foreach dir,$(TOOLCHAIN_BIN_DIRS),$(wildcard $(dir)/$(CROSS)gcc)))
TOOLCHAIN_CC_FOR_SHELL := $(if $(TOOLCHAIN_CC_ABS),$(TOOLCHAIN_CC_ABS),$(CC))
export TOOLCHAIN_CC_FOR_SHELL

# 为源文件导入当前的配置
CFLAGS += -include $(TOPDIR)include/config.h
ASM_FLAGS += -include $(TOPDIR)include/config.h

CXXFLAGS = $(filter-out -Wstrict-prototypes -std=gnu99,$(CFLAGS))
CXXFLAGS += -std=c++11 -D_GNU_SOURCE
CXXFLAGS += -fno-exceptions -fno-unwind-tables -fno-threadsafe-statics

# 编译builtin.o
define MK_BUILTIN_O
	$(Q)mkdir -p $(OBJDIR)$(strip $1)
	$(Q)+make $(SLIENT_ARG) -f $(BUILDDIR)rule.mk $(strip $1)builtin.o D=$(strip $1)
endef

# 清除builtin.o
define MK_BUILTIN_CLEAN
	$(Q)+make $(SLIENT_ARG) -f $(BUILDDIR)rule.mk clean D=$(strip $1) V=$(v)
endef
