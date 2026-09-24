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
    mfc0 v0, $15, 1
    andi v0, v0, 0x3ff       /* 获得 cpu id */
    sll  v0, v0, 0x2
    la   v1, os_handler_mode
    addu v0, v0, v1
    sw zero, 0(v0)  /* 设置现在不在中断模式 */

    move k0, sp
    addiu sp,  sp, 4*36
    restore_context_registers
    ehb
    eret
    nop
END(ret_from_irq)
 
NESTED(mailbox_irq_handler, 0, sp)
    .set noat
    .set noreorder
    .set push
    .set mips64r2

    mfc0 v0, $15, 1
    andi v0, v0, 0x3ff       /* 获得 cpu id */
    sll  v0, v0, 0x2

    la v1, (0xb2200000+0x1000) /* v1 CCU_MBR 的基地址 */
    addu v1, v1, v0      /* v0 是cpu_id*4 */
    sw zero, 0(v1)       /* 清除mailbox寄存器 CCU_MBR */

    la   v1, it_current_task
    addu v1, v1, v0
    lw   v1, 0(v1)           /* 获取 当前任务的地址到 v1 */
                             /* 从k0恢复sp,这是handle_int 函数保存的 */
    sw k0, 0(v1)             /* 将sp 的值 赋值给 v1+0 ： 此时v1+0 = stack_pointer*/
    move v0, k0              /* v0 暂存 sp的值,下面会用到 */
    move k0, v1              /* 现在 k0 是当前任务的地址 */

    /*
     * 测试fpu 是否打开
     * 如果打开，那么就保存fpu 寄存器，并且关闭 fpu
     * 下次使用到 fpu的时候触发 cpu unusable 异常
     * 在此异常中重新打开 fpu，并恢复 fpu 寄存器
     */
    mfc0 k1, CP0_STATUS
    ext k1, k1, STATUSB_CU1, 1
    beq k1, zero, 1f
    addiu k0, k0, 8     # 这里是延时槽, 所以会和上一条beq 一起执行

    cfc1 k1,  CP1_STATUS
    sdc1 $f0, (0*8)(k0)
    sdc1 $f1, (1*8)(k0)
    sdc1 $f2, (2*8)(k0)
    sdc1 $f3, (3*8)(k0)
    sdc1 $f4, (4*8)(k0)
    sdc1 $f5, (5*8)(k0)
    sdc1 $f6, (6*8)(k0)
    sdc1 $f7, (7*8)(k0)
    sdc1 $f8, (8*8)(k0)
    sdc1 $f9, (9*8)(k0)
    sdc1 $f10, (10*8)(k0)
    sdc1 $f11, (11*8)(k0)
    sdc1 $f12, (12*8)(k0)
    sdc1 $f13, (13*8)(k0)
    sdc1 $f14, (14*8)(k0)
    sdc1 $f15, (15*8)(k0)
    sdc1 $f16, (16*8)(k0)
    sdc1 $f17, (17*8)(k0)
    sdc1 $f18, (18*8)(k0)
    sdc1 $f19, (19*8)(k0)
    sdc1 $f20, (20*8)(k0)
    sdc1 $f21, (21*8)(k0)
    sdc1 $f22, (22*8)(k0)
    sdc1 $f23, (23*8)(k0)
    sdc1 $f24, (24*8)(k0)
    sdc1 $f25, (25*8)(k0)
    sdc1 $f26, (26*8)(k0)
    sdc1 $f27, (27*8)(k0)
    sdc1 $f28, (28*8)(k0)
    sdc1 $f29, (29*8)(k0)
    sdc1 $f30, (30*8)(k0)
    sdc1 $f31, (31*8)(k0)
    sw   k1,   (32*8)(k0)

    mfc0 k1, CP0_STATUS
    ins k1, zero, STATUSB_CU1, 1
    mtc0 k1, CP0_STATUS

    lw k1,  (35*4)(v0)
    ins k1, zero, STATUSB_CU1, 1
    sw k1,  (35*4)(v0)

1:
#ifdef __mips_msa
    mfc0 k1, CP0_CONFIG, 5
    ext k1, k1, CONFIG5B_MSAEn, 1
    beq k1, zero, 2f
    addiu k0, k0, (33*8) # 此前已经 +8 了, 所以不是 (8+33*8)

    cfcmsa k1, MSA_CSR
    st.d $w0, (0*16)(k0)
    st.d $w1, (1*16)(k0)
    st.d $w2, (2*16)(k0)
    st.d $w3, (3*16)(k0)
    st.d $w4, (4*16)(k0)
    st.d $w5, (5*16)(k0)
    st.d $w6, (6*16)(k0)
    st.d $w7, (7*16)(k0)
    st.d $w8, (8*16)(k0)
    st.d $w9, (9*16)(k0)
    st.d $w10, (10*16)(k0)
    st.d $w11, (11*16)(k0)
    st.d $w12, (12*16)(k0)
    st.d $w13, (13*16)(k0)
    st.d $w14, (14*16)(k0)
    st.d $w15, (15*16)(k0)
    st.d $w16, (16*16)(k0)
    st.d $w17, (17*16)(k0)
    st.d $w18, (18*16)(k0)
    st.d $w19, (19*16)(k0)
    st.d $w20, (20*16)(k0)
    st.d $w21, (21*16)(k0)
    st.d $w22, (22*16)(k0)
    st.d $w23, (23*16)(k0)
    st.d $w24, (24*16)(k0)
    st.d $w25, (25*16)(k0)
    st.d $w26, (26*16)(k0)
    st.d $w27, (27*16)(k0)
    st.d $w28, (28*16)(k0)
    st.d $w29, (29*16)(k0)
    st.d $w30, (30*16)(k0)
    st.d $w31, (31*16)(k0)
    sw k1,     (32*16)(k0)

    mfc0 k1, CP0_CONFIG, 5
    ins k1, zero, CONFIG5B_MSAEn, 1
    mtc0 k1, CP0_CONFIG, 5

    mfc0 k1, CP0_STATUS
    ins k1, zero, STATUSB_CU2, 1
    mtc0 k1, CP0_STATUS

    lw k1,  (35*4)(v0)
    ins k1, zero, STATUSB_CU2, 1
    sw k1,  (35*4)(v0)

2:
#endif
    /* Note: task->stack_pointer have be saved in handle_int  */
    jal it_task_switch_context  /* 任务切换 v0 值表示切换后的任务 */
    nop
    lw sp, 0(v0)         /* 此时v0+0 = stack_pointer = 赋值给 sp */
    j ret_from_irq
    nop
    .set pop
END(mailbox_irq_handler)

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

    mfc0 v0, $15, 1
    andi v0, v0, 0x3ff       /* 获得 cpu id */
    sll  v0, v0, 0x2

    la   v1, os_handler_mode
    addu v1, v1, v0
    li s0, 1
    sw s0, 0(v1)          /* 设置现在在中断模式 */

    la   v1, irq_stack_pointer
    addu v1, v1, v0
    move k0, sp           /* 将sp的值暂存于 k0 */
    lw sp, 0(v1)          /* 中断的时候不用线程的栈 */
    jal handle_int_c
    nop
    j ret_from_irq
    move sp, k0
END(handle_int)

/*
 * 中断异常处理入口 exception_handlers[11]
 */
NESTED(handle_cpu_unusable, 0, sp)
    .set noat
    .set noreorder

    /*
     * 中断中不能使用 fpu/mxu
     */
    mfc0 k0, $15, 1
    andi k0, k0, 0x3ff       /* 获得 cpu id */
    sll  k0, k0, 0x2
    la   k1, os_handler_mode
    addu k0, k0, k1
    lw k0, 0(k0)
    bnez k0, handle_exceptions_default
    nop

    /*
     * 判断是那个协处理器发生了异常
     * 1 : fpu
     * 2 : mxu
     */
    mfc0 k0, CP0_CAUSE
    ext k0, k0, CAUSEB_CE, 2

    addiu k1, k0, -1
    beqz k1, do_handle_fpu_disabled
    nop
#ifdef __mips_msa
    addiu k1, k0, -2
    beqz k1, do_handle_msa_disabled
    nop
#endif
    j handle_exceptions_default
    nop

END(handle_cpu_unusable)

#ifdef __mips_msa
/*
 * 中断异常处理入口 exception_handlers[21]
 */
NESTED(handle_msa_disabled, 0, sp)
    .set noat
    .set noreorder
    /*
     * 中断中不能使用 msa
     */
    mfc0 k0, $15, 1
    andi k0, k0, 0x3ff       /* 获得 cpu id */
    sll  k0, k0, 0x2
    la   k1, os_handler_mode
    addu k0, k0, k1
    lw k0, 0(k0)
    beqz k0, do_handle_msa_disabled
    nop
    j handle_exceptions_default
    nop

END(handle_msa_disabled)
#endif


LEAF(do_handle_fpu_disabled)
    .set noat
    .set noreorder
    .set push
    .set mips64r2

    /*
     * 打开 fpu， 恢复 fpu 寄存器
     */
    mfc0 k0, CP0_STATUS
    li k1, ST0_CU1
    or k0, k0, k1
    mtc0 k0, CP0_STATUS

    mfc0 k0, $15, 1
    andi k0, k0, 0x3ff       /* 获得 cpu id */
    sll  k0, k0, 0x2
    la   k1, it_current_task
    addu k0, k0, k1
    lw   k0, 0(k0)           /* 获取 当前任务的地址到 k0 */

    addiu k0, k0, 8

    ldc1 $f0, (0*8)(k0)
    ldc1 $f1, (1*8)(k0)
    ldc1 $f2, (2*8)(k0)
    ldc1 $f3, (3*8)(k0)
    ldc1 $f4, (4*8)(k0)
    ldc1 $f5, (5*8)(k0)
    ldc1 $f6, (6*8)(k0)
    ldc1 $f7, (7*8)(k0)
    ldc1 $f8, (8*8)(k0)
    ldc1 $f9, (9*8)(k0)
    ldc1 $f10, (10*8)(k0)
    ldc1 $f11, (11*8)(k0)
    ldc1 $f12, (12*8)(k0)
    ldc1 $f13, (13*8)(k0)
    ldc1 $f14, (14*8)(k0)
    ldc1 $f15, (15*8)(k0)
    ldc1 $f16, (16*8)(k0)
    ldc1 $f17, (17*8)(k0)
    ldc1 $f18, (18*8)(k0)
    ldc1 $f19, (19*8)(k0)
    ldc1 $f20, (20*8)(k0)
    ldc1 $f21, (21*8)(k0)
    ldc1 $f22, (22*8)(k0)
    ldc1 $f23, (23*8)(k0)
    ldc1 $f24, (24*8)(k0)
    ldc1 $f25, (25*8)(k0)
    ldc1 $f26, (26*8)(k0)
    ldc1 $f27, (27*8)(k0)
    ldc1 $f28, (28*8)(k0)
    ldc1 $f29, (29*8)(k0)
    ldc1 $f30, (30*8)(k0)
    ldc1 $f31, (31*8)(k0)
    lw   k1,   (32*8)(k0)
    ctc1 k1,   CP1_STATUS

    ehb
    eret
    nop
    .set pop
END(do_handle_fpu_disabled)

#ifdef __mips_msa
LEAF(do_handle_msa_disabled)
    .set noat
    .set noreorder

    /* 打开 msa
     */
    mfc0 k0, CP0_CONFIG, 5
    li k1, CONFIG5F_MSAEn
    or k0, k0, k1
    mtc0 k0, CP0_CONFIG, 5

    /* 打开 mxa (cpu2)
     */
    mfc0 k0, CP0_STATUS
    li k1, ST0_CU2
    or k0, k0, k1
    mtc0 k0, CP0_STATUS

    mfc0 k0, $15, 1
    andi k0, k0, 0x3ff       /* 获得 cpu id */
    sll  k0, k0, 0x2
    la   k1, it_current_task
    addu k0, k0, k1
    lw   k0, 0(k0)           /* 获取 当前任务的地址到 k0 */

    addiu k0, k0, (8+33*8)

    ld.d $w0, (0*16)(k0)
    ld.d $w1, (1*16)(k0)
    ld.d $w2, (2*16)(k0)
    ld.d $w3, (3*16)(k0)
    ld.d $w4, (4*16)(k0)
    ld.d $w5, (5*16)(k0)
    ld.d $w6, (6*16)(k0)
    ld.d $w7, (7*16)(k0)
    ld.d $w8, (8*16)(k0)
    ld.d $w9, (9*16)(k0)
    ld.d $w10, (10*16)(k0)
    ld.d $w11, (11*16)(k0)
    ld.d $w12, (12*16)(k0)
    ld.d $w13, (13*16)(k0)
    ld.d $w14, (14*16)(k0)
    ld.d $w15, (15*16)(k0)
    ld.d $w16, (16*16)(k0)
    ld.d $w17, (17*16)(k0)
    ld.d $w18, (18*16)(k0)
    ld.d $w19, (19*16)(k0)
    ld.d $w20, (20*16)(k0)
    ld.d $w21, (21*16)(k0)
    ld.d $w22, (22*16)(k0)
    ld.d $w23, (23*16)(k0)
    ld.d $w24, (24*16)(k0)
    ld.d $w25, (25*16)(k0)
    ld.d $w26, (26*16)(k0)
    ld.d $w27, (27*16)(k0)
    ld.d $w28, (28*16)(k0)
    ld.d $w29, (29*16)(k0)
    ld.d $w30, (30*16)(k0)
    ld.d $w31, (31*16)(k0)
    lw   k1,   (32*16)(k0)
    ctcmsa MSA_CSR, k1

    ehb
    eret
    nop
END(do_handle_msa_disabled)
#endif

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
