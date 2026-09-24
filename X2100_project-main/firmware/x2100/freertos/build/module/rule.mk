include $(BUILDDIR)/config.mk

include $(TOPDIR).config.in

##########################################################
# start of user code                                     #
##########################################################

include_path := $(filter -I%, $(CFLAGS))
include_path_abs := $(filter -I/%, $(include_path))
include_path := $(filter-out -I/%, $(include_path))
include_path := $(patsubst -I%,-I$(TOPDIR)%, $(include_path))

CFLAGS := $(MODULE_CFLAGS) $(include_path_abs) $(include_path)
LDFLAGS := $(MODULE_LDFLAGS)

OBJDIR := ./objs/

include Makefile

ifeq ($(strip $(module_name)),)
$(error must define your module_name)
endif

CFLAGS += -DTHIS_MODULE_NAME=\"$(module_name)\"

ifeq ($(strip $(module_depends)),)
CFLAGS += -DTHIS_MODULE_DEPENDS=\"depends=$(strip $(module_depends))\"
endif

##########################################################
# end of user code                                       #
##########################################################

src_not_exist := $(call list_not_exist_file, $(src-y))
$(call error_if, $(src_not_exist), $(MSG_FILE_NOT_EXIST))

src_not_supported = $(filter-out %.c %.s %.S %.cc %.cpp, $(src-y))
$(call error_if, $(src_not_supported), $(MSG_FILE_NOT_SUPPORT))

CC ?= gcc
AR ?= ar
LD ?= ld
OBJDIR ?= ./

deps :=
objs :=

src_xobj = $(call to_xobj, $(src-y), $(OBJDIR), $1)
src_xdep = $(call to_xdep, $(src-y), $(OBJDIR), $1)

#
# c objs, c depends
#
obj_c = $(call src_xobj, .c)
dep_c = $(call src_xdep, .c)

#
# asm objs, asm depends
#
obj_s = $(call src_xobj, .s)
dep_s = $(call src_xdep, .s)

#
# asm objs, asm depends
#
obj_S = $(call src_xobj, .S)
dep_S = $(call src_xdep, .S)

#
# cc objs, cc depends
#
obj_cc = $(call src_xobj, .cc)
dep_cc = $(call src_xdep, .cc)

#
# cpp objs, cpp depends
#
obj_cpp = $(call src_xobj, .cpp)
dep_cpp = $(call src_xdep, .cpp)

#
# make .c
#
$(obj_c): $(OBJDIR)%.o : %
	@mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(Q)$(CC) -c $(CFLAGS) $< -o $@

$(dep_c): $(OBJDIR)%.d:%
	@mkdir -p $(dir $@);
	$(Q)$(CC) -M $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .s
#
$(obj_s): $(OBJDIR)%.o : %
	@mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(Q)$(CC) -c $(ASM_FLAGS) $(CFLAGS) $< -o $@

$(dep_s): $(OBJDIR)%.d:%
	@mkdir -p $(dir $@);
	$(Q)$(CC) -M $(ASM_FLAGS) $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .S
#
$(obj_S): $(OBJDIR)%.o : %
	@mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(Q)$(CC) -c $(ASM_FLAGS) $(CFLAGS) $< -o $@

$(dep_S): $(OBJDIR)%.d:%
	@mkdir -p $(dir $@);
	$(Q)$(CC) -M $(ASM_FLAGS) $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .cc
#
$(obj_cc): $(OBJDIR)%.o : %
	@mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(Q)$(CC) -c $(CFLAGS) $< -o $@

$(dep_cc): $(OBJDIR)%.d:%
	@mkdir -p $(dir $@)
	$(Q)$(CC) -M $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .cpp
#
$(obj_cpp): $(OBJDIR)%.o : %
	@mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(Q)$(CC) -c $(CFLAGS) $< -o $@

$(dep_cpp): $(OBJDIR)%.d:%
	@mkdir -p $(dir $@)
	$(Q)$(CC) -M $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

deps += $(dep_c) $(dep_s) $(dep_S) $(dep_cc) $(dep_cpp)
objs += $(obj_c) $(obj_s) $(obj_S) $(obj_cc) $(obj_cpp) $(obj-y)

sinclude $(deps)

$(OBJDIR)$(module_name).mod.o:
	@echo $(MSG_CC) $(TOPDIR)/build/module/this_module.c
	$(Q)$(CC) -c $(CFLAGS) $(TOPDIR)/build/module/this_module.c -o $(OBJDIR)$(module_name).mod.o

$(module_name).mo:$(objs) $(OBJDIR)$(module_name).mod.o clean_deps
	@echo $(MSG_LD_MODULE) $(module_name).mo
	$(Q)$(LD) $(LDFLAGS) $(OBJDIR)$(module_name).mod.o $(objs) $(MODULE_LIBS) -o $(module_name).mo
	$(Q)chmod +x $(module_name).mo

module:$(module_name).mo

clean_deps:
	$(Q)rm -f $(deps)

clean_module:
	@echo "clean module: $(module_name)"
	$(Q)rm -rf $(OBJDIR)

.PHONY: clean $(OBJDIR)$(module_name).mod.o
