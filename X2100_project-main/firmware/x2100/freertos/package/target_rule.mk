builtins := $(patsubst %,%builtin.o,$(package_builtin_src_dirs))
builtins := $(filter-out symbols/builtin.o, $(builtins))

ifneq ($(CONFIG_SYMBOLS),)
symbols_o := symbols/builtin.o
endif

# 默认的目标的真正实现
# 将所有的 package source 下的builtin.o 链接在一起
# 生成bin文件
define target_make_elf
	$(Q)> symbols/func_symbols.txt
	@echo $(MSG_LINKING) zero.elf
	$(Q)$(LD) $(FINAL_LDFLAGS) $(LDS_FILE) --start-group $(builtins)  $(package_lib-y) --end-group $(symbols_o) -o zero.elf
endef

define generate_func_symbols
	$(Q)NM=$(NM) tools/mk_func_symbols.sh zero.elf > symbols/func_symbols.txt
	$(call MK_BUILTIN_O, symbols/)
	$(Q)> symbols/func_symbols.txt
endef

define generate_rtos_with_spl_bin
	@echo $(MSG_CAT) "rtos-with-spl.bin <-- $(CONFIG_SPL_BIN_FILE) zero.bin"
	$(Q)cat $(CONFIG_SPL_BIN_FILE) zero.bin > rtos-with-spl.bin
endef

RTOS_HEADER_SIZE=36

define generate_gzip_split_rtos
	rm -drf $(OBJDIR)split_output; \
	mkdir -p $(OBJDIR)split_output; \
	dd if=zero.bin of=$(OBJDIR)split_output/zero_header.bin bs=1 count=$(RTOS_HEADER_SIZE); \
	touch $(OBJDIR)split_output/gzip_header; \
	count=$(CONFIG_SPLIT_GZIP_HEADER_NUM); \
	split -d -a 4 -b $(CONFIG_SPLIT_GZIP_BLOCK_SIZE) zero.bin $(OBJDIR)split_output/zero.bin.split; \
	for file in $(OBJDIR)split_output/zero.bin.split* ; do gzip -k -n -f -9 $$file; done; \
	for file in $(OBJDIR)split_output/*.gz ; do file_size=$$(stat -c "%s" "$$file"); \
		echo "$$(printf "%08x" $$file_size)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
		echo "$$file  file_size = $$file_size"; \
		count=$$(expr "$$count" - 1); \
		done; \
	for i in $$(seq 1 "$$count"); do echo "$$(printf "%08x" 0)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
		done; \
	count=$(CONFIG_SPLIT_GZIP_HEADER_NUM); \
	for file in $(OBJDIR)split_output/zero.bin.split* ; do \
		case "$$file" in \
		*.gz) \
			;; \
		*) \
			file_size=$$(stat -c %s $$file); \
			echo "$$(printf "%08x" $$file_size)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
			count=$$(expr "$$count" - 1); \
		esac \
		done; \
	for i in $$(seq 1 "$$count"); do echo "$$(printf "%08x" 0)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
		done; \
	cat $(OBJDIR)split_output/*.gz > $(OBJDIR)zero.bin.split.gz; \
	cat $(OBJDIR)split_output/gzip_header $(OBJDIR)zero.bin.split.gz > $(OBJDIR)split_output/temp; \
	file_size=$$(stat -c %s "$(OBJDIR)split_output/temp"); \
	echo "$$(printf "%08x" $$file_size)" | xxd -r -ps | cat >> $(OBJDIR)split_output/zero_header.bin; \
	cat $(OBJDIR)split_output/zero_header.bin $(OBJDIR)split_output/temp > zero_split.gz; \
	rm -drf $(OBJDIR)split_output
endef

define generate_lzma_split_rtos
	rm -drf $(OBJDIR)split_output; \
	mkdir -p $(OBJDIR)split_output; \
	dd if=zero.bin of=$(OBJDIR)split_output/zero_header.bin bs=1 count=$(RTOS_HEADER_SIZE); \
	touch $(OBJDIR)split_output/gzip_header; \
	count=$(CONFIG_SPLIT_GZIP_HEADER_NUM); \
	split -d -a 4 -b $(CONFIG_SPLIT_GZIP_BLOCK_SIZE) zero.bin $(OBJDIR)split_output/zero.bin.split; \
	for file in $(OBJDIR)split_output/zero.bin.split* ; do lzma -9 -k $$file; done; \
	for file in $(OBJDIR)split_output/*.lzma ; do file_size=$$(stat -c "%s" "$$file"); \
		echo "$$(printf "%08x" $$file_size)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
		echo "$$file  file_size = $$file_size"; \
		count=$$(expr "$$count" - 1); \
		done; \
	for i in $$(seq 1 "$$count"); do echo "$$(printf "%08x" 0)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
		done; \
	count=$(CONFIG_SPLIT_GZIP_HEADER_NUM); \
	for file in $(OBJDIR)split_output/zero.bin.split* ; do \
		case "$$file" in \
		*.lzma) \
			;; \
		*) \
			file_size=$$(stat -c %s $$file); \
			echo "$$(printf "%08x" $$file_size)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
			count=$$(expr "$$count" - 1); \
		esac \
		done; \
	for i in $$(seq 1 "$$count"); do echo "$$(printf "%08x" 0)" | xxd -r -ps | cat >> $(OBJDIR)split_output/gzip_header; \
		done; \
	cat $(OBJDIR)split_output/*.lzma > $(OBJDIR)zero.bin.split.lzma; \
	cat $(OBJDIR)split_output/gzip_header $(OBJDIR)zero.bin.split.lzma > $(OBJDIR)split_output/temp; \
	file_size=$$(stat -c %s "$(OBJDIR)split_output/temp"); \
	echo "$$(printf "%08x" $$file_size)" | xxd -r -ps | cat >> $(OBJDIR)split_output/zero_header.bin; \
	cat $(OBJDIR)split_output/zero_header.bin $(OBJDIR)split_output/temp > zero_split.lzma; \
	rm -drf $(OBJDIR)split_output
endef

target_default:builtin_srcs
	$(target_make_elf)
	$(generate_func_symbols)
	$(target_make_elf)
	@echo $(MSG_OBJCOPY) zero.bin
	$(Q)$(OBJCOPY) -O binary zero.elf zero.bin
	$(Q)$(OBJDUMP) -d zero.elf > zero.dump.S
	$(if $(CONFIG_GZIP_SPLIT_RTOS),$(Q)$(generate_gzip_split_rtos))
	$(if $(CONFIG_LZMA_SPLIT_RTOS),$(Q)$(generate_lzma_split_rtos))
	$(if $(CONFIG_SPL_BIN_FILE),$(generate_rtos_with_spl_bin))

# 清除所有的 bultin.o .d .d
target_clean:clean_hooks
	$(if ($strip($OBJDIR)), $(shell rm $(OBJDIR)/* -rf))
	$(Q)rm -f zero.bin
	$(Q)rm -f zero.elf
	$(Q)rm -f zero.dump.S
	$(Q)rm -f rtos-with-spl.bin
	@echo fix warning > /dev/null

# 生成 configs/目录下 *_defconfig 的规则
###########################################################
defconfigs := $(shell ls configs/)

defconfigs := $(filter %_defconfig, $(defconfigs))

ifneq ($strip($(defconfigs)),)

$(defconfigs):
	$(Q)echo "..cleaning old config files"
	$(Q)rm -f .config.in include/config.h
	$(Q)echo "..writing .config.in"
	$(Q)tools/configparser --input Config.in configs/$(@) --defconfig .config.in --header include/config.h > /dev/null
	$(Q)echo "..writing include/config.h"

.PONHY: $(defconfigs)

endif
###########################################################

# 检查 .config.in include/config.h 是否有更新
make_sure_config_update:=$(shell tools/check_config.sh)
