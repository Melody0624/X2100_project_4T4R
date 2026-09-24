ASM_FLAGS += -mmsa

src-y += init.c
src-y += start.S
src-y += cpu.c
src-$(CONFIG_IRQ) += irq.c
src-$(CONFIG_IRQ) += irq_dispatcher.c
src-$(CONFIG_CACHE) += cache.c
src-$(CONFIG_SPINLOCK) += spinlock.c
src-$(CONFIG_XBURST2_CP0_TIMER) += cp0_timer.c
src-$(CONFIG_OS_FREERTOS) += os_freertos.c
src-$(CONFIG_OS_ITOS) += os_itos.c
src-y += lib/memcpy.S
src-y$(CONFIG_NEWLIB) += lib/div64.S
src-$(CONFIG_DUMP_STACK) += lib/show_stack_trace.c
src-$(CONFIG_RELOCATE) += lib/relocate.c
src-$(CONFIG_OS_MODULE) += lib/module.c
src-$(CONFIG_SHOW_COMPILE_TIME) += lib/show_compile_time.c

exception-y := exception_raw.S
ifeq ($(CONFIG_ENABLE_NESTED_IRQ),y)
exception-$(CONFIG_OS_FREERTOS) := exception_freertos_nested_irq.S
else
exception-$(CONFIG_OS_FREERTOS) := exception_freertos.S
endif
exception-$(CONFIG_OS_ITOS) := exception_itos.S
src-y += $(exception-y)

# package_has_builtin_o = y
