#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/asm.h>

LEAF(__div64_32)
            .set noreorder
            lw      t1,4(a0)
            lw      v0,0(a0)
            sltu    v1,t1,a1
            bnez    v1,f8024a4bc
            move    t0,zero
            divu    zero,t1,a1
            move    t3,zero
            mflo    a2
            mul     v1,a1,a2
            b       f8024a4c4
            subu    t1,t1,v1
f8024a4bc:  move    t3,zero
            move    a2,zero
f8024a4c4:  li      v1,1
            move    t2,zero
f8024a4cc:  bgtz    t0,f8024a4e4
            sltu    a3,t0,t1
            bnez    t0,f8024a528
            sltu    a3,t1,t0
            beqz    a1,f8024a524
            sltu    a3,t0,t1
f8024a4e4:  beqz    a3,f8024a514
            sll     a3,a1,0x1
f8024a4ec:  sltu    t4,a3,a1
            sll     t0,t0,0x1
            move    a1,a3
            sll     a3,v1,0x1
            addu    t0,t4,t0
            sll     t2,t2,0x1
            sltu    t4,a3,v1
            addu    t2,t4,t2
            b       f8024a4cc
            move    v1,a3
f8024a514:  bne     t1,t0,f8024a524
            sltu    a3,a1,v0
            bnez    a3,f8024a4ec
            sll     a3,a1,0x1
f8024a524:  sltu    a3,t1,t0
f8024a528:  bnez    a3,f8024a570
            sll     a3,t0,0x1f
            bne     t0,t1,f8024a548
            subu    a3,v0,a1
            sltu    a3,v0,a1
            bnez    a3,f8024a570
            sll     a3,t0,0x1f
            subu    a3,v0,a1
f8024a548:  sltu    t4,v0,a3
            subu    t1,t1,t0
            move    v0,a3
            addu    a3,t3,v1
            subu    t1,t1,t4
            addu    a2,a2,t2
            sltu    t4,a3,t3
            addu    a2,t4,a2
            move    t3,a3
            sll     a3,t0,0x1f
f8024a570:  srl     a1,a1,0x1
            or      a1,a3,a1
            srl     v1,v1,0x1
            sll     a3,t2,0x1f
            or      v1,a3,v1
            srl     t2,t2,0x1
            or      a3,v1,t2
            bnez    a3,f8024a524
            srl     t0,t0,0x1
            sw      t3,0(a0)
            jr      ra
            sw      a2,4(a0)
END(__div64_32)

FEXPORT(__udivdi3)
            .set noat
            .set noreorder
            .ent __udivdi3
            addiu   sp,sp,-32
            sw      ra,28(sp)
            sw      a0,16(sp)
            bnez    a1,f8049a908
            sw      a1,20(sp)
            divu    zero,a0,a2
            sw      zero,20(sp)
            mflo    a2
            b       f8049a914
            sw      a2,16(sp)
f8049a908:  move    a1,a2
            jal     __div64_32
            addiu   a0,sp,16
f8049a914:  lw      ra,28(sp)
            lw      v0,16(sp)
            lw      v1,20(sp)
            jr      ra
            addiu   sp,sp,32
END(__udivdi3)