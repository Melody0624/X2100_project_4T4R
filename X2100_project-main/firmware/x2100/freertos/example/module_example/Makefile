ifeq ($(strip $(OS_DIR)),)
$(error must define OS_DIR)
endif

src-y += main.c
src-y += test.c

module_name = module_example
module_depends =

all:
	@make -C $(OS_DIR) MODULE_DIR=$(shell pwd)/ module
	@echo "unsigned int relocate_module_hex[] = {" > relocate_module_hex.c
	@hexdump  -v -e '1/4 "0x%08X, " "\n"' $(module_name).mo >> relocate_module_hex.c
	@echo "};" >> relocate_module_hex.c

clean:
	@make --silent -C $(OS_DIR) MODULE_DIR=$(shell pwd)/ clean_module
	rm $(module_name).mo
	rm relocate_module_hex.c
