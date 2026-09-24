#ifndef _CPU_MODULE_H_
#define _CPU_MODULE_H_

#include <list.h>
#include <stdint.h>
#include <elf.h>
#include <asm/elf.h>

struct module;

struct mod_arch_specific {
    struct mips_hi16 *r_mips_hi16_list;
};

int apply_relocate(Elf_Shdr *sechdrs, const char *strtab,
           unsigned int symindex, unsigned int relsec,
           struct module *me);

int apply_relocate_add(Elf_Shdr *sechdrs,
                     const char *strtab,
                     unsigned int symindex,
                     unsigned int relsec,
                     struct module *me);

/* Additional bytes needed by arch in front of individual sections */
unsigned int arch_mod_section_prepend(struct module *mod,
                         unsigned int section);

int module_arch_check(Elf_Ehdr *hdr,
                     Elf_Shdr *sechdrs,
                     char *secstrings,
                     struct module *mod);

unsigned long module_align(unsigned long size);

void *module_alloc(unsigned long size);

void module_free(void *data);

int module_finalize(const Elf_Ehdr *hdr,
               const Elf_Shdr *sechdrs,
               struct module *me);

void flush_module_icache(const struct module *mod);

#endif /* _CPU_MODULE_H_ */
