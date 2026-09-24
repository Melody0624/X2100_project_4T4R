#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/asm.h>

#include "exception_macro.S"

    .set mips32r2
    .set noreorder

    .global exception_entry
    .global exception_handlers

/*
 * 异常处理总入口
 */
NESTED(exception_entry, 0, sp)
    .set push
    .set noat
    mfc0 k1, CP0_CAUSE
    andi k1, k1, CAUSEF_EXCCODE
    lw k0, exception_handlers(k1)
    jr k0
    nop
    .set pop
END(exception_entry)

LEAF(ret_from_irq)
    move k0, sp
    addiu sp,  sp, 4*36
    restore_context_registers
    ehb
    eret
    nop
END(ret_from_irq)

/*
 * 中断异常处理入口 exception_handlers[0]
 */
NESTED(handle_int, 0, sp)
    .set noat
    .set noreorder
    addiu k0, sp, -4 * 36
    save_context_registers
    addiu sp, sp, -4 * 36
    CLI

    la ra, ret_from_irq
    j handle_int_c
    nop
END(handle_int)

/*
 * 其它异常处理入口 exception_handlers[n] (1...31)
 */
LEAF(handle_exceptions_default)
    .set noat
    la k0, _exception_stack_end
    addiu  k0, k0, -4 * 38
    save_context_registers
    sw $29, (29*4)(k0); /* 异常的时候保存sp */
    mfc0 k1, CP0_BADVADDR
    sw k1, (36*4)(k0)
    mfc0 k1, CP0_CAUSE
    sw k1, (37*4)(k0)

    move a0, k0
    move sp, k0
    jal do_exception_default
    nop

    la k0, _exception_stack_end
    addiu  k0, k0, -4 * 38
    lw sp, (29*4)(k0)
    restore_context_registers

    ehb
    eret
    nop
END(handle_exceptions_default)
