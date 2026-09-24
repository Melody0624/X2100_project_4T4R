# 导入当前的配置
sinclude .config.in

# 自动设置工具链路径到 PATH
# 使用 ifneq 而非 ifdef，确保变量非空（避免其他电脑同步后出现 "dirname: 缺少操作数" 错误）
ifneq ($(RTOS_TOOLCHAIN_PATH),)
# 转换相对路径为绝对路径
TOOLCHAIN_ABS_PATH := $(shell echo "$(RTOS_TOOLCHAIN_PATH)" | sed 's|:| |g' | xargs -n1 realpath 2>/dev/null | tr '\n' ':' | sed 's/:$$//')
# 如果转换成功，添加到PATH
ifneq ($(TOOLCHAIN_ABS_PATH),)
export PATH := $(TOOLCHAIN_ABS_PATH):$(PATH)
# 如果当前目标不是 xxx_defconfig，才打印工具链信息（避免在配置切换时显示旧配置的工具链）
ifeq ($(filter %_defconfig, $(MAKECMDGOALS)),)
$(info [工具链] 已自动设置工具链路径: $(RTOS_TOOLCHAIN_PATH))
$(info [工具链] 使用交叉编译前缀: $(CROSS))
endif
endif
endif

export packages
export package_builtin_srcs
export package_builtin_clean
export package_init_hooks
export package_make_hooks
export package_finalize_hooks
export package_clean_hooks
export package_builtin_src_dirs

export Q

# 文件目录
export TOPDIR := $(shell pwd)/
export BUILDDIR := $(TOPDIR)build/
export OBJDIR := $(TOPDIR)objs/

export MODULE_LDFLAGS
export MODULE_CFLAGS
export MODULE_LIBS

# 一些有用的自定义函数
include $(BUILDDIR)config.mk

# make V=1 可以看见Makefile运转的细节
ifneq ($(V),1)
Q = @
SLIENT_ARG = --silent
else
Q =
SLIENT_ARG =
endif

# 如果当前的目标是 xxx_defconfig 那么不打印inlcude 信息
ifneq ($(filter %_defconfig, $(MAKECMDGOALS)),)
no_including_info = y
endif

# 如果编译模块的话,就不打印include 信息
ifneq ($(MODULE_DIR),)
no_including_info = y
endif

project ?= Config.in
config ?= .config.in

menuconfig_project := $(project)
menuconfig_config := $(config)

# 包含此软件包定义的一些变量,函数
include package/target_config.mk

# 默认目标
default:all

# 包含所有 package
package := package/package.mk
include $(BUILDDIR)include_package.mk

all: builtin_srcs make_hooks finalize_hooks target_default

clean: clean_hooks target_clean

clean_hooks: $(package_builtin_clean) $(package_clean_hooks)

init_hooks: $(package_init_hooks)

$(package_builtin_srcs): init_hooks
builtin_srcs: $(package_builtin_srcs)

$(package_make_hooks): builtin_srcs
make_hooks: $(package_make_hooks)

$(package_finalize_hooks): make_hooks target_default
finalize_hooks: $(package_finalize_hooks)

module:
	$(Q)+make -C $(MODULE_DIR) -f $(BUILDDIR)module/rule.mk module

clean_module:
	$(Q)+make -C $(MODULE_DIR) -f $(BUILDDIR)module/rule.mk clean_module

c_compile_flags:
	$(Q)make -f build/show_compiler_flags.mk show_c_flags

cxx_compile_flags:
	$(Q)make -f build/show_compiler_flags.mk show_cxx_flags

menuconfig:
	./tools/menuconfig project=$(menuconfig_project) defconfig=$(menuconfig_config)

# 包含此软件包定义的一些规则
include package/target_rule.mk

.PHONY: default all clean clean_hooks init_hooks builtin_srcs make_hooks finalize_hooks menuconfig
