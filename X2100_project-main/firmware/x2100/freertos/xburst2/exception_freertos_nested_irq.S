#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/asm.h>

#include "exception_macro.S"

#ifdef CONFIG_TCSM_SECTION_IRQ
    .macro call_func func
    .set noat
    .set noreorder
    la t0, \func
    jalr t0
    .endm
#else
    .macro call_func func
    .set noat
    .set noreorder
    jal \func
    .endm
#endif

    .set mips32r2
    .set noreorder

#ifdef CONFIG_TCSM_SECTION_IRQ
    .section .tcsm_func,"ax",@progbits
#endif
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

NESTED(sw0_irq_handler, 0, sp)
    .set noat
    .set noreorder
    .set push
    .set mips32r2

    la v0, save_sp
    lw v0, 0(v0)         /* 从save_sp恢复sp,这是handle_int 函数保存的 */
    la s0, pxCurrentTCB  /* 获取 pxCurrentTCB 的地址到 s0 */
    lw s0, 0(s0)         /* 将s0+0 的值赋值给 s0 */
    sw v0, 0(s0)         /* 将sp 的值 赋值给 s0 + 0 ： 此时s0+0 = pxTopOfStack*/

    addiu s0, s0, 8 # 此前已经 +8 了, 所以不是 (8+33*8)

    mfc0 s1, CP0_CONFIG, 5
    ext s1, s1, CONFIG5B_MSAEn, 2
    beq s1, zero, 1f
    nop

    cfcmsa s1, MSA_CSR
    st.d $w0, (0*16)(s0)
    st.d $w1, (1*16)(s0)
    st.d $w2, (2*16)(s0)
    st.d $w3, (3*16)(s0)
    st.d $w4, (4*16)(s0)
    st.d $w5, (5*16)(s0)
    st.d $w6, (6*16)(s0)
    st.d $w7, (7*16)(s0)
    st.d $w8, (8*16)(s0)
    st.d $w9, (9*16)(s0)
    st.d $w10, (10*16)(s0)
    st.d $w11, (11*16)(s0)
    st.d $w12, (12*16)(s0)
    st.d $w13, (13*16)(s0)
    st.d $w14, (14*16)(s0)
    st.d $w15, (15*16)(s0)
    st.d $w16, (16*16)(s0)
    st.d $w17, (17*16)(s0)
    st.d $w18, (18*16)(s0)
    st.d $w19, (19*16)(s0)
    st.d $w20, (20*16)(s0)
    st.d $w21, (21*16)(s0)
    st.d $w22, (22*16)(s0)
    st.d $w23, (23*16)(s0)
    st.d $w24, (24*16)(s0)
    st.d $w25, (25*16)(s0)
    st.d $w26, (26*16)(s0)
    st.d $w27, (27*16)(s0)
    st.d $w28, (28*16)(s0)
    st.d $w29, (29*16)(s0)
    st.d $w30, (30*16)(s0)
    st.d $w31, (31*16)(s0)
    sw s1,     (32*16+8)(s0)  /*msacsr*/

    mfc0 s1, CP0_CONFIG, 5
    ins s1, zero, CONFIG5B_MSAEn, 1
    mtc0 s1, CP0_CONFIG, 5

    mfc0 s1, CP0_STATUS
    ins s1, zero, STATUSB_CU2, 1
    mtc0 s1, CP0_STATUS

    lw s1,  (35*4)(v0)         /* mask thread msa enable */
    ins s1, zero, STATUSB_CU2, 1
    sw s1,  (35*4)(v0)

    mfc0 s1, CP0_STATUS            /* check msa && fp*/
    ext s1, s1, STATUSB_CU1, 1
    beq s1, zero, 3f
    nop

    cfc1 s1,  CP1_STATUS
    sw   s1,   (32*16)(s0)
    b   2f
    nop

1:
    mfc0 s1, CP0_STATUS
    ext s1, s1, STATUSB_CU1, 1
    beq s1, zero, 3f
    nop
    cfc1 s1,  CP1_STATUS
    sdc1 $f0, (0*16)(s0)
    sdc1 $f1, (1*16)(s0)
    sdc1 $f2, (2*16)(s0)
    sdc1 $f3, (3*16)(s0)
    sdc1 $f4, (4*16)(s0)
    sdc1 $f5, (5*16)(s0)
    sdc1 $f6, (6*16)(s0)
    sdc1 $f7, (7*16)(s0)
    sdc1 $f8, (8*16)(s0)
    sdc1 $f9, (9*16)(s0)
    sdc1 $f10, (10*16)(s0)
    sdc1 $f11, (11*16)(s0)
    sdc1 $f12, (12*16)(s0)
    sdc1 $f13, (13*16)(s0)
    sdc1 $f14, (14*16)(s0)
    sdc1 $f15, (15*16)(s0)
    sdc1 $f16, (16*16)(s0)
    sdc1 $f17, (17*16)(s0)
    sdc1 $f18, (18*16)(s0)
    sdc1 $f19, (19*16)(s0)
    sdc1 $f20, (20*16)(s0)
    sdc1 $f21, (21*16)(s0)
    sdc1 $f22, (22*16)(s0)
    sdc1 $f23, (23*16)(s0)
    sdc1 $f24, (24*16)(s0)
    sdc1 $f25, (25*16)(s0)
    sdc1 $f26, (26*16)(s0)
    sdc1 $f27, (27*16)(s0)
    sdc1 $f28, (28*16)(s0)
    sdc1 $f29, (29*16)(s0)
    sdc1 $f30, (30*16)(s0)
    sdc1 $f31, (31*16)(s0)
    sw   s1,   (32*16)(s0)

2:
    mfc0 s1, CP0_STATUS
    ins s1, zero, STATUSB_CU1, 1
    mtc0 s1, CP0_STATUS

    lw s1,  (35*4)(v0)
    ins s1, zero, STATUSB_CU1, 1
    sw s1,  (35*4)(v0)

3:
    mfc0 s0, CP0_CAUSE
    ins s0, zero, CAUSEB_IP0, 1 /* 清除 CAUSEB_IP0 */
    mtc0 s0, CP0_CAUSE

    /* Note: pxCurrentTCB->pxTopOfStack have be saved in handle_int  */
    call_func vTaskSwitchContext  /* 任务切换 pxCurrentTCB 值是会变化的 */
    nop

    li a0, 0
    call_func check_enable_common_irqs
    nop

    la s0, pxCurrentTCB  /* 获取 pxCurrentTCB 的地址到 s0 */
    lw s0, 0(s0)         /* 将s0+0 的值赋值给 s0 */
    lw sp, 0(s0)         /* 此时s0+0 = pxTopOfStack = 赋值给 sp */
    j ret_from_irq
    nop
    .set pop
END(sw0_irq_handler)

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

    /*
     * 如果中断第一次发生,保存sp,切换到中断的栈
     * 如果中断嵌套,那么继续使用中断的栈
     */
    la s0, os_handler_mode
    lw s0, (0)(s0)
    bnez s0, 1f
    nop
    la s0, save_sp
    sw sp, (0)(s0)
    la sp, _irq_stack_end /* 中断的时候不用线程的栈 */
    jal handle_int_c
    nop
    lw sp, (0)(s0)
    j ret_from_irq
    nop
1:
    la ra, ret_from_irq
    j handle_int_c
    nop
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
    la k0, os_handler_mode
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
    la k0, os_handler_mode
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
    .set mips32r2

	/*
     * 打开 fpu， 恢复 fpu 寄存器
     */
    mfc0 k0, CP0_STATUS
    li k1, ST0_CU1
    or k0, k0, k1
    mtc0 k0, CP0_STATUS

    la k0, pxCurrentTCB
    lw k0, 0(k0)
    addiu k0, k0, 8

    mfc0 k1, CP0_CONFIG, 5
    ext k1, k1, CONFIG5B_MSAEn, 2
    bne k1, zero, 1f
    nop

    ldc1 $f0, (0*16)(k0)
    ldc1 $f1, (1*16)(k0)
    ldc1 $f2, (2*16)(k0)
    ldc1 $f3, (3*16)(k0)
    ldc1 $f4, (4*16)(k0)
    ldc1 $f5, (5*16)(k0)
    ldc1 $f6, (6*16)(k0)
    ldc1 $f7, (7*16)(k0)
    ldc1 $f8, (8*16)(k0)
    ldc1 $f9, (9*16)(k0)
    ldc1 $f10, (10*16)(k0)
    ldc1 $f11, (11*16)(k0)
    ldc1 $f12, (12*16)(k0)
    ldc1 $f13, (13*16)(k0)
    ldc1 $f14, (14*16)(k0)
    ldc1 $f15, (15*16)(k0)
    ldc1 $f16, (16*16)(k0)
    ldc1 $f17, (17*16)(k0)
    ldc1 $f18, (18*16)(k0)
    ldc1 $f19, (19*16)(k0)
    ldc1 $f20, (20*16)(k0)
    ldc1 $f21, (21*16)(k0)
    ldc1 $f22, (22*16)(k0)
    ldc1 $f23, (23*16)(k0)
    ldc1 $f24, (24*16)(k0)
    ldc1 $f25, (25*16)(k0)
    ldc1 $f26, (26*16)(k0)
    ldc1 $f27, (27*16)(k0)
    ldc1 $f28, (28*16)(k0)
    ldc1 $f29, (29*16)(k0)
    ldc1 $f30, (30*16)(k0)
    ldc1 $f31, (31*16)(k0)

1:
    lw   k1,   (32*16)(k0)
    ctc1 k1,   CP1_STATUS

    ehb
    eret
    nop
    .set pop
END(do_handle_fpu_disabled)


.macro	insert_s_w	ws, n
	.set	push
	.set	noat
	 lw   k1,  (\n*16 + 8)(k0)
	 insert.w  \ws[2], k1
	 lw   k1,  (\n*16 + 12)(k0)
	 insert.w  \ws[3], k1
	.set	pop
	.endm

LEAF(do_handle_msa_disabled)
    .set noat
    .set noreorder
    .set mips32r2


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

    mfc0 k1, CP0_STATUS            /* check msa && fp*/
    ext k1, k1, STATUSB_CU1, 1
    beq k1, zero, 1f  /*fp == true*/
    nop

    la k0, pxCurrentTCB
    lw k0, 0(k0)
    addiu k0, k0, 8

    insert_s_w  $w0, 0
    insert_s_w	$w1, 1
    insert_s_w	$w2, 2
    insert_s_w	$w3, 3
    insert_s_w	$w4, 4
    insert_s_w	$w5, 5
    insert_s_w	$w6, 6
    insert_s_w	$w7, 7
    insert_s_w	$w8, 8
    insert_s_w	$w9, 9
    insert_s_w  $w10, 10
    insert_s_w	$w11, 11
    insert_s_w	$w12, 12
    insert_s_w	$w13, 13
    insert_s_w	$w14, 14
    insert_s_w	$w15, 15
    insert_s_w	$w16, 16
    insert_s_w	$w17, 17
    insert_s_w	$w18, 18
    insert_s_w	$w19, 19
    insert_s_w	$w20, 20
    insert_s_w	$w21, 21
    insert_s_w	$w22, 22
    insert_s_w	$w23, 23
    insert_s_w	$w24, 24
    insert_s_w	$w25, 25
    insert_s_w	$w26, 26
    insert_s_w	$w27, 27
    insert_s_w	$w28, 28
    insert_s_w	$w29, 29
    insert_s_w	$w30, 30
    insert_s_w	$w31, 31

    lw   k1,   (32*16 + 8)(k0)
    ctcmsa MSA_CSR, k1

    b 2f
    nop
1:
    la k0, pxCurrentTCB
    lw k0, 0(k0)
    addiu k0, k0, 8

    ld.w $w0, (0*16)(k0)
    ld.w $w1, (1*16)(k0)
    ld.w $w2, (2*16)(k0)
    ld.w $w3, (3*16)(k0)
    ld.w $w4, (4*16)(k0)
    ld.w $w5, (5*16)(k0)
    ld.w $w6, (6*16)(k0)
    ld.w $w7, (7*16)(k0)
    ld.w $w8, (8*16)(k0)
    ld.w $w9, (9*16)(k0)
    ld.w $w10, (10*16)(k0)
    ld.w $w11, (11*16)(k0)
    ld.w $w12, (12*16)(k0)
    ld.w $w13, (13*16)(k0)
    ld.w $w14, (14*16)(k0)
    ld.w $w15, (15*16)(k0)
    ld.w $w16, (16*16)(k0)
    ld.w $w17, (17*16)(k0)
    ld.w $w18, (18*16)(k0)
    ld.w $w19, (19*16)(k0)
    ld.w $w20, (20*16)(k0)
    ld.w $w21, (21*16)(k0)
    ld.w $w22, (22*16)(k0)
    ld.w $w23, (23*16)(k0)
    ld.w $w24, (24*16)(k0)
    ld.w $w25, (25*16)(k0)
    ld.w $w26, (26*16)(k0)
    ld.w $w27, (27*16)(k0)
    ld.w $w28, (28*16)(k0)
    ld.w $w29, (29*16)(k0)
    ld.w $w30, (30*16)(k0)
    ld.w $w31, (31*16)(k0)
    lw   k1,   (32*16 + 8)(k0)
    ctcmsa MSA_CSR, k1
2:
    ehb
    eret
    nop
END(do_handle_msa_disabled)

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
    call_func do_exception_default
    nop

    la k0, _exception_stack_end
    addiu  k0, k0, -4 * 38
    lw sp, (29*4)(k0)
    restore_context_registers

    ehb
    eret
    nop
END(handle_exceptions_default)
