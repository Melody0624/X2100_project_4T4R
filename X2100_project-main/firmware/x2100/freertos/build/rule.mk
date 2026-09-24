include $(BUILDDIR)config.mk

include .config.in

##########################################################
# start of user code                                     #
##########################################################

include $(D)Makefile

##########################################################
# end of user code                                       #
##########################################################

# 重新适配文件路径,例子
# xxx.c -> src/dir/xxx.c # 优先加上源文件目录
# xxx.c -> xxx.c         # 其次,搜索当前Makefile工作目录
#                        # 最后,仍然没匹配到就报错退出
src-y := $(call uniq, $(src-y))
_src := $(patsubst %,$(D)%,$(src-y))
_src_top := $(patsubst $(D)%,%, $(call list_not_exist_file, $(_src)))

_src_exist := $(call list_exist_file, $(_src))
_src_exist += $(call list_exist_file, $(_src_top))

src-y := $(_src_exist)

src_not_exist := $(call list_not_exist_file, $(_src_top))
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
	mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(CC) -c $(CFLAGS) $< -o $@

$(dep_c): $(OBJDIR)%.d:%
	mkdir -p $(dir $@);
	$(CC) -M $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .s
#
$(obj_s): $(OBJDIR)%.o : %
	mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(CC) -c $(ASM_FLAGS) $(CFLAGS) $< -o $@

$(dep_s): $(OBJDIR)%.d:%
	mkdir -p $(dir $@);
	$(CC) -M $(ASM_FLAGS) $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .S
#
$(obj_S): $(OBJDIR)%.o : %
	mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(CC) -c $(ASM_FLAGS) $(CFLAGS) $< -o $@

$(dep_S): $(OBJDIR)%.d:%
	mkdir -p $(dir $@);
	$(CC) -M $(ASM_FLAGS) $(CFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .cc
#
$(obj_cc): $(OBJDIR)%.o : %
	mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(dep_cc): $(OBJDIR)%.d:%
	mkdir -p $(dir $@)
	$(CXX) -M $(CXXFLAGS) -MT $(@:%.d=%.o) $< >$@

#
# make .cpp
#
$(obj_cpp): $(OBJDIR)%.o : %
	mkdir -p $(dir $@)
	@echo $(MSG_CC) $<
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(dep_cpp): $(OBJDIR)%.d:%
	mkdir -p $(dir $@)
	$(CXX) -M $(CXXFLAGS) -MT $(@:%.d=%.o) $< >$@

deps += $(dep_c) $(dep_s) $(dep_S) $(dep_cc) $(dep_cpp)
objs += $(obj_c) $(obj_s) $(obj_S) $(obj_cc) $(obj_cpp)

ifeq ($(filter clean%, $(MAKECMDGOALS)),)
sinclude $(deps)
endif

clean_deps:
	rm -f $(deps)

clean:
	@echo $(MSG_CLEAN) $(D)
	rm -f $(deps)
	rm -f $(objs)
	rm -f $(D)builtin.o

ifeq ($(package_build_static_lib),y)
builtin_static_lib := $(D)builtin.a
else
builtin_static_lib :=
endif

$(D)builtin.a : clean_deps Makefile $(D)Makefile $(objs) $(obj-y)
	@echo $(MSG_LD) $@
	$(Q)rm -f $@;
	$(Q)$(AR) rcs $@ $(objs) $(obj-y)

ifneq ($(strip $(package_has_builtin_o)),y)
$(D)builtin.o: clean_deps Makefile $(D)Makefile $(objs) $(obj-y) $(builtin_static_lib)
	@echo $(MSG_LD) $@
	$(call cmd_link_o_target, $(objs) $(obj-y))
else
# package will define $(D)builtin.o rules
endif

.PHONY: clean
