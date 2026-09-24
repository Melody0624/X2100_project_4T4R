#include <common.h>
#include <lds_symbol.h>
#include <func_symbol.h>
#include <module.h>
#include "mips_inst.h"

struct mips_frame_info {
    const char *name;               /* func name */
    void *func;                     /* pc address */
    unsigned long func_size;        /* analyze from start to current PC offset */
    long stack_size;                /* stack size */
    long pc_offset;                 /* ra:next pc in stack offset */
};

#ifdef CONFIG_OS
extern void end_of_thread(void);
#endif

__weak int is_kernel_code_address(unsigned long addr)
{
    if (addr >= (unsigned long)&__text_start && addr < (unsigned long)&__text_end)
        return 1;
    return 0;
}

__weak int is_code_address(unsigned long addr)
{
    if (is_kernel_code_address(addr))
        return 1;
#ifdef CONFIG_OS_MODULE
    if (is_in_module(addr)) {
        return 1;
    }
#endif
    return 0;
}

__weak int is_stack_address(unsigned long addr)
{
    unsigned long start = (unsigned long) &_user_stack_start;
    unsigned long end = (unsigned long) &_user_heap_end;

    return addr >= start && addr < end;
}

static inline int is_jump_ins(union mips_instruction *ip)
{
    if (ip->j_format.opcode == j_op)
        return 1;
    if (ip->j_format.opcode == jal_op)
        return 1;

    return 0;
}

static inline int is_sp_move_ins(union mips_instruction *ip)
{
    /*
     * special:0x27bd:
     * assembly:addiu/daddiu sp,sp,-imm
     */
    if (ip->i_format.rs != 29 || ip->i_format.rt != 29)
        return 0;
    if (ip->i_format.opcode == addiu_op || ip->i_format.opcode == daddiu_op)
        return 1;

    return 0;
}

static inline int is_ra_save_ins(union mips_instruction *ip)
{
    /*
     * special:0xafbf
     * assembly:sw / sd $ra, offset($sp)
     */
    return (ip->i_format.opcode == sw_op || ip->i_format.opcode == sd_op) &&
        ip->i_format.rs == 29 &&
        ip->i_format.rt == 31;
}


static int get_frame_info(struct mips_frame_info *info)
{
    union mips_instruction *ip = info->func;
    unsigned max_insns = info->func_size / sizeof(union mips_instruction);
    unsigned i;

    info->pc_offset = -1;
    info->stack_size = 0;

    if (!ip)
        goto err;

    if (max_insns == 0)
        max_insns = 128U;   /* unknown function size */
    max_insns = min(128U, max_insns);

    for (i = 0; i < max_insns; i++, ip++) {
        debug("unwind: *(%p)=0x%08lx\n", ip, *(unsigned long *)ip);
        if (is_jump_ins(ip)) {
            break;
        }

        if (!info->stack_size) {
            if (is_sp_move_ins(ip)) {
                info->stack_size = - ip->i_format.simmediate;
            }
            continue;
        }

        if (info->pc_offset == -1 && is_ra_save_ins(ip)) {
            info->pc_offset = ip->i_format.simmediate / sizeof(long);
            break;
        }
    }

    debug("unwind: sp_off=0x%lx(%ld), ra_off=%ld\n",
            -info->stack_size, -info->stack_size, info->pc_offset);

    if (info->stack_size && info->pc_offset >= 0) /* nested */
        return 0;

    if (info->pc_offset < 0) /* leaf */
        return 1;
    /* prologue seems boggus... */
err:
    return -1;
}


static const char * kernel_address_lookup(unsigned long addr,
        unsigned long *size, unsigned long *offset, char **modname)
{
    struct func_symbol *symbol;

    symbol = func_symbol_lookup(addr);
    if (!symbol) {
        debug("unwind: pc out of range of kernel symbol\n");
        return NULL;
    }

    if (modname)
        *modname = (char *)symbol->name;

    if (offset)
        *offset = addr - symbol->addr;

    return symbol->name;
}

/*
 * Lookup an address but don't bother to find any names.
 */
static const char *kallsyms_lookup_size_offset(unsigned long addr,
        unsigned long *symbolsize, unsigned long *offset)
{
    if (is_kernel_code_address(addr)) {
        return (kernel_address_lookup(addr, symbolsize, offset, NULL));
    }
#ifdef CONFIG_OS_MODULE
    if (is_in_module(addr))
        return (module_address_lookup(addr, symbolsize, offset, NULL));
#endif
    return NULL;
}

static unsigned long unwind_stack_by_address(unsigned long *sp,
                          unsigned long pc,
                          unsigned long *ra)
{
    struct mips_frame_info info;
    unsigned long size, ofs;
    int leaf;

    if (!kallsyms_lookup_size_offset(pc, &size, &ofs)) {
        debug("unwind: pc out of range. not find symbol\n");
        return 0;
    }

    info.func = (void *)(pc - ofs);
    info.func_size = (ofs < 8) ? ofs : ofs - 8;   /* analyze from start to ofs */
    leaf = get_frame_info(&info);
    if (leaf < 0) {
        debug("unwind: function out of range\n");
        return 0;
    }

    if (leaf) {
        /*
         * For some extreme cases, get_frame_info() can
         * consider wrongly a nested function as a leaf
         * one. In that cases avoid to return always the
         * same value.
         */
        pc = pc != *ra ? *ra : 0;
    } else {
        pc = ((unsigned long *)(*sp))[info.pc_offset];
    }

    *sp += info.stack_size;
    *ra = 0;
    return pc;
}

static unsigned long unwind_stack(unsigned long *sp,
        unsigned long pc, unsigned long *ra)
{
    debug("unwind: *sp=0x%lx, pc=0x%lx, *ra=0x%lx\n", *sp, pc, *ra);

    if (!is_stack_address(*sp)) {
        debug("unwind: *sp out of range 1\n");
        return 0;
    }

    return unwind_stack_by_address(sp, pc, ra);
}

static int print_ip_sym(unsigned long pc, int is_print)
{
    const char *func_name;
    unsigned long offset;

    func_name = kallsyms_lookup_size_offset(pc, NULL, &offset);
    if (!func_name) {
        printf("[<%p> ???]\n", (void *) pc);
        return -1;
    }

    if (is_print) {
        if (offset < 8) {
            printf("[<%p>] %s+0x%lx\n", (void *) (pc -offset), func_name, offset);
        } else {
            printf("[<%p>] %s+0x%lx\n", (void *) (pc -offset), func_name, offset - 8);
        }
    }

    return 0;
}

void show_raw_backtrace(unsigned long sp, unsigned long pc, unsigned long ra)
{
    unsigned long save_pc = -1;
    unsigned long save_ra = -1;
    unsigned long save_sp = -1;
    int is_print = 1;
    printf("Call Trace:\n");
    do {
        save_pc = pc;
        save_ra = ra;
        save_sp = sp;

        print_ip_sym(pc, is_print);
        pc = unwind_stack(&sp, pc, &ra);
        is_print = save_pc != pc;

        if (save_pc == pc && save_ra == ra && save_sp == sp) {
            printf("can't unwind stack anymore\n");
            break;
        }

#ifdef CONFIG_OS
        if (ra == (unsigned long)end_of_thread ||
            pc == (unsigned long)end_of_thread)
            break;
#endif

    } while (pc);
}
void show_stacktrace(unsigned long _sp, unsigned long pc, unsigned long ra)
{
    int i;
    unsigned long *sp = (unsigned long *)_sp;

    printf("Stack :\n");
    if ((unsigned long)sp & 3) {
        printf(" Not aligned!\n");
        return;
    }

    i = 0;
    while (is_stack_address((unsigned long)sp + 32)) {
        dump_mem32(sp, 32, 8);
        sp += 8;
        if (++i == 4)
            break;
    }

    show_raw_backtrace(_sp, pc, ra);
}

static inline unsigned long get_sp(void)
{
    unsigned long reg;

    __asm__ volatile (
        "move %0, $29"
        : "=r" (reg)
        :
    );

    return reg;
}

static inline unsigned long get_ra(void)
{
    unsigned long reg;

    __asm__ volatile (
        "move %0, $31"
        : "=r" (reg)
        :
    );

    return reg;
}

void dump_stack(void)
{
    LLLL:
    show_stacktrace(get_sp(), (unsigned long)&&LLLL, get_ra());
}
EXPORT_SYMBOL(dump_stack);
