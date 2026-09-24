
XBUSR_CFLAGS = -G 0 -EL -fno-pic -mno-abicalls -static -march=mips32r2 -mabi=32 #  -mabicalls

XBUSR_CFLAGS += -fsigned-char  #部分版本工具连会把char当作unsigned char处理，强制为signed char

XBUSR_CFLAGS += -mno-check-zero-division #避免除0异常产生, 因为RTOS不支持异常处理

ifeq ($(CONFIG_JTAG_DEBUG), y)
XBUSR_CFLAGS += -g -ggdb -DDEBUG -fomit-frame-pointer
endif

CFLAGS += $(XBUSR_CFLAGS) -Ixburst/include
FINAL_LDFLAGS += -G 0 -static -n -nostdlib -EL -m elf32ltsmip --gc-sections -Bstatic
LDFLAGS += -G 0 -static -n -nostdlib -EL -m elf32ltsmip
LDS_FILE += -T xburst/xburst.lds

MODULE_CFLAGS += $(XBUSR_CFLAGS)
MODULE_LDFLAGS = -r -G 0 -EL -m elf32ltsmip -T $(BUILDDIR)module/module_common.lds

#-------------------------------------------------------
package_name = xburst
package_depends =
package_builtin_src = xburst/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

ifeq ($(CONFIG_SHOW_COMPILE_TIME),y)
make_sure_file_is_new := $(shell touch xburst/lib/show_compile_time.c)
endif

package-$(CONFIG_SOC_X1830S) += package/xburst/soc-x1830/soc-x1830.mk
package-$(CONFIG_SOC_X1600S) += package/xburst/soc-x1600/soc-x1600.mk
package-$(CONFIG_SOC_X1520) += package/xburst/soc-x1520/soc-x1520.mk
package-$(CONFIG_SOC_X1021) += package/xburst/soc-x1021/soc-x1021.mk
package-$(CONFIG_SOC_X1000S) += package/xburst/soc-x1000/soc-x1000.mk
