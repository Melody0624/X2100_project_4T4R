
XBUSR_CFLAGS = -G 0 -EL -fno-pic -mno-abicalls -static -mfp64 -march=mips32r2 -mabi=32 #  -mabicalls

XBUSR_CFLAGS += -fsigned-char  #部分版本工具连会把char当作unsigned char处理，强制为signed char

XBUSR_CFLAGS += -mno-check-zero-division #避免除0异常产生, 因为RTOS不支持异常处理

ifeq ($(CONFIG_XBURST2_MSA), y)
XBUSR_CFLAGS += -D__mips_msa # 此处不使用-mmsa 避免中断服务函数中代码被编译器msa优化
ASM_FLAGS += -mmsa -mfp64 -Wa,-no-warn  # 汇编文件需要明确指定MSA和FP64, 抑制FP64警告(mips32r2+msa需要fp64的信息性警告)
endif

CFLAGS += $(XBUSR_CFLAGS) -Ixburst2/include
FINAL_LDFLAGS += -G 0 -static -n -nostdlib -EL -m elf32ltsmip --gc-sections -Bstatic
LDFLAGS += -G 0 -static -n -nostdlib -EL -m elf32ltsmip
LDS_FILE += -T xburst2/xburst2.lds

MODULE_CFLAGS += $(XBUSR_CFLAGS)
MODULE_LDFLAGS = -r -G 0 -EL -m elf32ltsmip -T $(BUILDDIR)module/module_common.lds

#-------------------------------------------------------
package_name = xburst2
package_depends =
package_builtin_src = xburst2/
package_make_hook =
package_init_hook =
package_finalize_hook =
package_clean_hook =
#-------------------------------------------------------

ifeq ($(CONFIG_SHOW_COMPILE_TIME),y)
make_sure_file_is_new := $(shell touch xburst2/lib/show_compile_time.c)
endif

package-$(CONFIG_SOC_X2000) += package/xburst2/soc-x2000/soc-x2000.mk
package-$(CONFIG_SOC_X2600) += package/xburst2/soc-x2600/soc-x2600.mk
package-$(CONFIG_SOC_AD100) += package/xburst2/soc-ad100/soc-ad100.mk
