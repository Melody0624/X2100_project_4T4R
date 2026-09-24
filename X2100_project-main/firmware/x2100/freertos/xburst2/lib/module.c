#include <module.h>
#include <elf.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <stdio.h>
#include <module.h>
#include <common.h>
#include <driver/cache.h>
#include "err_ptr.h"

struct mips_hi16 {
    struct mips_hi16 *next;
    Elf_Addr *addr;
    Elf_Addr value;
};

int apply_r_mips_none(struct module *me, u32 *location, Elf_Addr v)
{
    return 0;
}

static int apply_r_mips_32_rel(struct module *me, u32 *location, Elf_Addr v)
{
    *location += v;

    return 0;
}

static int apply_r_mips_26_rel(struct module *me, u32 *location, Elf_Addr v)
{
    if (v % 4) {
        printf("module %s: dangerous R_MIPS_26 REL relocation\n",
               me->name);
        return -ENOEXEC;
    }

    if ((v & 0xf0000000) != (((unsigned long)location + 4) & 0xf0000000)) {
        printf(
               "module %s: relocation overflow\n",
               me->name);
        return -ENOEXEC;
    }

    *location = (*location & ~0x03ffffff) |
            ((*location + (v >> 2)) & 0x03ffffff);

    return 0;
}

static int apply_r_mips_hi16_rel(struct module *me, u32 *location, Elf_Addr v)
{
    struct mips_hi16 *n;

    /*
     * We cannot relocate this one now because we don't know the value of
     * the carry we need to add.  Save the information, and let LO16 do the
     * actual relocation.
     */
    n = malloc(sizeof *n);
    if (!n)
        return -ENOMEM;

    n->addr = (Elf_Addr *)location;
    n->value = v;
    n->next = me->arch.r_mips_hi16_list;
    me->arch.r_mips_hi16_list = n;

    return 0;
}

static void free_relocation_chain(struct mips_hi16 *l)
{
    struct mips_hi16 *next;

    while (l) {
        next = l->next;
        free(l);
        l = next;
    }
}

static int apply_r_mips_lo16_rel(struct module *me, u32 *location, Elf_Addr v)
{
    unsigned long insnlo = *location;
    struct mips_hi16 *l;
    Elf_Addr val, vallo;

    /* Sign extend the addend we extract from the lo insn.    */
    vallo = ((insnlo & 0xffff) ^ 0x8000) - 0x8000;

    if (me->arch.r_mips_hi16_list != NULL) {
        l = me->arch.r_mips_hi16_list;
        while (l != NULL) {
            struct mips_hi16 *next;
            unsigned long insn;

            /*
             * The value for the HI16 had best be the same.
             */
            if (v != l->value)
                goto out_danger;

            /*
             * Do the HI16 relocation.  Note that we actually don't
             * need to know anything about the LO16 itself, except
             * where to find the low 16 bits of the addend needed
             * by the LO16.
             */
            insn = *l->addr;
            val = ((insn & 0xffff) << 16) + vallo;
            val += v;

            /*
             * Account for the sign extension that will happen in
             * the low bits.
             */
            val = ((val >> 16) + ((val & 0x8000) != 0)) & 0xffff;

            insn = (insn & ~0xffff) | val;
            *l->addr = insn;

            next = l->next;
            free(l);
            l = next;
        }

        me->arch.r_mips_hi16_list = NULL;
    }

    /*
     * Ok, we're done with the HI16 relocs.     Now deal with the LO16.
     */
    val = v + vallo;
    insnlo = (insnlo & ~0xffff) | (val & 0xffff);
    *location = insnlo;

    return 0;

out_danger:
    free_relocation_chain(l);
    me->arch.r_mips_hi16_list = NULL;

    printf("module %s: dangerous R_MIPS_LO16 REL relocation\n", me->name);

    return -ENOEXEC;
}

static int (*reloc_handlers_rel[]) (struct module *me, u32 *location,
                Elf_Addr v) = {
    [R_MIPS_NONE]        = apply_r_mips_none,
    [R_MIPS_32]        = apply_r_mips_32_rel,
    [R_MIPS_26]        = apply_r_mips_26_rel,
    [R_MIPS_HI16]        = apply_r_mips_hi16_rel,
    [R_MIPS_LO16]        = apply_r_mips_lo16_rel
};

int apply_relocate(Elf_Shdr *sechdrs, const char *strtab,
           unsigned int symindex, unsigned int relsec,
           struct module *me)
{
    Elf_Mips_Rel *rel = (void *) sechdrs[relsec].sh_addr;
    Elf_Sym *sym;
    u32 *location;
    unsigned int i;
    Elf_Addr v;
    int res;

    debug("Applying relocate section %u to %u\n", relsec,
           sechdrs[relsec].sh_info);

    me->arch.r_mips_hi16_list = NULL;

    for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
        /* This is where to make the change */
        location = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
            + rel[i].r_offset;

        /* This is the symbol it is referring to */
        sym = (Elf_Sym *)sechdrs[symindex].sh_addr
            + ELF_MIPS_R_SYM(rel[i]);
        if (IS_ERR_VALUE(sym->st_value)) {
            /* Ignore unresolved weak symbol */
            if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
                continue;
            printf("%s: Unknown symbol %s\n",
                   me->name, strtab + sym->st_name);
            return -ENOENT;
        }

        debug("\t%s %x\n", strtab + sym->st_name, ELF_MIPS_R_TYPE(rel[i]));

        v = sym->st_value;

        res = reloc_handlers_rel[ELF_MIPS_R_TYPE(rel[i])](me, location, v);
        if (res)
            return res;
    }

    /*
     * Normally the hi16 list should be deallocated at this point.    A
     * malformed binary however could contain a series of R_MIPS_HI16
     * relocations not followed by a R_MIPS_LO16 relocation.  In that
     * case, free up the list and return an error.
     */
    if (me->arch.r_mips_hi16_list) {
        free_relocation_chain(me->arch.r_mips_hi16_list);
        me->arch.r_mips_hi16_list = NULL;

        return -ENOEXEC;
    }

    return 0;
}

int apply_relocate_add(Elf_Shdr *sechdrs,
                     const char *strtab,
                     unsigned int symindex,
                     unsigned int relsec,
                     struct module *me)
{
    printf("module %s: REL relocation unsupported\n", me->name);
    return -ENOEXEC;
}

/* Additional bytes needed by arch in front of individual sections */
unsigned int arch_mod_section_prepend(struct module *mod,
                         unsigned int section)
{
    return 0;
}

int module_arch_check(Elf_Ehdr *hdr,
                     Elf_Shdr *sechdrs,
                     char *secstrings,
                     struct module *mod)
{
    return 0;
}


unsigned long module_align(unsigned long size)
{
    return ALIGN(size, cache_line_size());
}

void *module_alloc(unsigned long size)
{
    return cache_align_malloc(size);
}

void module_free(void *data)
{
    free(data);
}

int module_finalize(const Elf_Ehdr *hdr,
               const Elf_Shdr *sechdrs,
               struct module *me)
{
    return 0;
}

void flush_module_icache(const struct module *mod)
{
    /*
     * why not flush_icache ?
     * 因为这些数据可能目前还在dcache 里面
     * 如果 flush_icache 那么下次执行module中的代码的时候
     * icache 的内容会从scache(二级cache)拿,然而二级cache中的数据是未知的
     * 所以要先刷dcache再作废icache
     */
    flush_cache((unsigned long)mod->core.data,
                 mod->core.size);
}
