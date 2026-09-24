#include <module.h>
#include <err_ptr.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <asm/elf.h>
#include <common.h>
#include <driver/cache.h>
#include <lds_symbol.h>
#include <os.h>

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

#define debug_align(x) (x)

struct load_info {
    Elf_Ehdr *hdr;
    unsigned long len;
    Elf_Shdr *sechdrs;
    char *secstrings, *strtab;
    unsigned long symoffs, stroffs;
    struct _ddebug *debug;
    unsigned int num_debug;
    bool sig_ok;
    struct {
        unsigned int sym, str, mod, vers, info;
    } index;

    int flags;
    struct module *parent;
};

struct module kernel_module;
static struct mutex module_mutex;

/* 模块的边界,用于快速的判断是否地址是否属于模块
 */
static unsigned long module_addr_min = -1UL, module_addr_max = 0;

int is_in_module(unsigned long addr)
{
    return addr >= module_addr_min && addr < module_addr_max;
}

void dump_sec_names(const struct load_info *info)
{
    unsigned int i;

    for (i = 1; i < info->hdr->e_shnum; i++) {
        Elf_Shdr *shdr = &info->sechdrs[i];
        printf("%s - %d,%d (%d %x %d)\n",
            info->secstrings + shdr->sh_name,  shdr->sh_offset, shdr->sh_size,
            shdr->sh_name, shdr->sh_flags, shdr->sh_flags & SHF_ALLOC);
    }
}

/* Find a module section: 0 means not found. */
static unsigned int find_sec(const struct load_info *info, const char *name)
{
    unsigned int i;

    for (i = 1; i < info->hdr->e_shnum; i++) {
        Elf_Shdr *shdr = &info->sechdrs[i];
        /* Alloc bit cleared means "ignore it." */
        if ((shdr->sh_flags & SHF_ALLOC)
            && strcmp(info->secstrings + shdr->sh_name, name) == 0)
            return i;
    }
    return 0;
}

/* Find a module section, or NULL. */
static inline void *section_addr(const struct load_info *info, const char *name)
{
    /* Section 0 has sh_addr 0. */
    return (void *)info->sechdrs[find_sec(info, name)].sh_addr;
}

/* Find a module section, or NULL.  Fill in number of "objects" in section. */
static void *section_objs(const struct load_info *info,
              const char *name,
              size_t object_size,
              unsigned int *num)
{
    unsigned int sec = find_sec(info, name);

    /* Section 0 has sh_addr 0 and sh_size 0. */
    *num = info->sechdrs[sec].sh_size / object_size;
    return (void *)info->sechdrs[sec].sh_addr;
}

static int elf_header_check(struct load_info *info)
{
    if (info->len < sizeof(*(info->hdr)))
        return -ENOEXEC;

    if (memcmp(info->hdr->e_ident, ELFMAG, SELFMAG) != 0
        || info->hdr->e_type != ET_REL
        || !elf_check_arch(info->hdr)
        || info->hdr->e_shentsize != sizeof(Elf_Shdr))
        return -ENOEXEC;

    if (info->hdr->e_shoff >= info->len
        || (info->hdr->e_shnum * sizeof(Elf_Shdr) >
        info->len - info->hdr->e_shoff))
        return -ENOEXEC;

    Elf_Shdr *sechdrs = (void *)info->hdr + info->hdr->e_shoff;

    /*
     * 第一个段的地址一定为0
     */
    if (sechdrs[0].sh_addr != 0)
        return -ENOEXEC;

    return 0;
}

static int rewrite_section_headers(struct load_info *info)
{
    unsigned int i;

    /*
     * 计算出所有段的地址,
     * 当然,这里只是模块的原始数据
     */
    for (i = 1; i < info->hdr->e_shnum; i++) {
        Elf_Shdr *shdr = &info->sechdrs[i];
        if (shdr->sh_type != SHT_NOBITS
            && info->len < shdr->sh_offset + shdr->sh_size) {
            printf("Module len %lu truncated\n",
                   info->len);
            return -ENOEXEC;
        }

        shdr->sh_addr = (size_t)info->hdr + shdr->sh_offset;
    }

    return 0;
}

static int setup_load_info(struct load_info *info)
{
    unsigned int i;
    int err;

    /* Set up the convenience variables */
    info->sechdrs = (void *)info->hdr + info->hdr->e_shoff;
    info->secstrings = (void *)info->hdr
        + info->sechdrs[info->hdr->e_shstrndx].sh_offset;

    err = rewrite_section_headers(info);
    if (err)
        return err;

    /*
     * 只在加载时需要这两个段,所以清除它们的标记
     */
    info->index.vers = find_sec(info, "__versions");
    info->index.info = find_sec(info, ".modinfo");
    info->sechdrs[info->index.info].sh_flags &= ~(unsigned long)SHF_ALLOC;
    info->sechdrs[info->index.vers].sh_flags &= ~(unsigned long)SHF_ALLOC;

    /* Find internal symbols and strings. */
    for (i = 1; i < info->hdr->e_shnum; i++) {
        if (info->sechdrs[i].sh_type == SHT_SYMTAB) {
            info->index.sym = i;
            info->index.str = info->sechdrs[i].sh_link;
            info->strtab = (char *)info->hdr
                + info->sechdrs[info->index.str].sh_offset;
            break;
        }
    }

    if (info->index.sym == 0) {
        printf("module has no symbols (stripped?)\n");
        return -ENOEXEC;
    }

    info->index.mod = find_sec(info, ".gnu.linkonce.this_module");
    if (!info->index.mod) {
        printf("No module found in object\n");
        return -ENOEXEC;
    }

    return 0;
}

static int check_modinfo(struct module *mod, struct load_info *info)
{
    /*
     * 暂时不做任何标记的检查
     */
    return 0;
}

/* Update size with this section: return offset. */
static long get_offset(struct module *mod, unsigned int *size,
               Elf_Shdr *sechdr, unsigned int section)
{
    long ret;

    *size += arch_mod_section_prepend(mod, section);
    ret = ALIGN(*size, sechdr->sh_addralign ?: 1);
    *size = ret + sechdr->sh_size;
    return ret;
}

/* Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
   might -- code, read-only data, read-write data, small data.  Tally
   sizes, and place the offsets into sh_entsize fields: high bit means it
   belongs in init. */
static void layout_sections(struct module *mod, struct load_info *info)
{
    static unsigned long const masks[][2] = {
        /* NOTE: all executable code must be the first section
         * in this array; otherwise modify the text_size
         * finder in the two loops below */
        { SHF_EXECINSTR | SHF_ALLOC, ARCH_SHF_SMALL },
        { SHF_ALLOC, SHF_WRITE | ARCH_SHF_SMALL },
        { SHF_WRITE | SHF_ALLOC, ARCH_SHF_SMALL },
        { ARCH_SHF_SMALL | SHF_ALLOC, 0 }
    };
    unsigned int m, i;

    for (i = 0; i < info->hdr->e_shnum; i++)
        info->sechdrs[i].sh_entsize = -1;

    debug("Core section allocation order:\n");
    for (m = 0; m < ARRAY_SIZE(masks); ++m) {
        for (i = 0; i < info->hdr->e_shnum; ++i) {
            Elf_Shdr *s = &info->sechdrs[i];
            const char *sname = info->secstrings + s->sh_name;

            if ((s->sh_flags & masks[m][0]) != masks[m][0]
                || (s->sh_flags & masks[m][1])
                || s->sh_entsize != -1)
                continue;
            s->sh_entsize = get_offset(mod, &mod->core.size, s, i);
            debug("\t%s : %d\n", sname, s->sh_entsize);
        }
        switch (m) {
        case 0: /* executable */
            mod->core.size = debug_align(mod->core.size);
            mod->core.text_size = mod->core.size;
            break;
        case 1: /* RO: text and ro-data */
            mod->core.size = debug_align(mod->core.size);
            mod->core.ro_size = mod->core.size;
            break;
        case 3: /* whole core */
            mod->core.size = debug_align(mod->core.size);
            break;
        }
    }
}

static bool is_core_symbol(const Elf_Sym *src, const Elf_Shdr *sechdrs,
                           unsigned int shnum)
{
    const Elf_Shdr *sec;

    if (src->st_shndx == SHN_UNDEF
        || src->st_shndx >= shnum
        || !src->st_name)
        return false;

    sec = sechdrs + src->st_shndx;
    if (!(sec->sh_flags & SHF_ALLOC)
        || !(sec->sh_flags & SHF_EXECINSTR))
        return false;

    return true;
}

/*
 * We only allocate and copy the strings needed by the parts of symtab
 * we keep.
 */
static void layout_symtab(struct module *mod, struct load_info *info)
{
    Elf_Shdr *symsect = info->sechdrs + info->index.sym;
    Elf_Shdr *strsect = info->sechdrs + info->index.str;
    const Elf_Sym *src;
    unsigned int i, nsrc, ndst, strtab_size = 0;

    src = (void *)info->hdr + symsect->sh_offset;
    nsrc = symsect->sh_size / sizeof(*src);

    /* Compute total space required for the core symbols' strtab. */
    for (ndst = i = 0; i < nsrc; i++) {
        if (i == 0 ||
            is_core_symbol(src+i, info->sechdrs, info->hdr->e_shnum)) {
            strtab_size += strlen(&info->strtab[src[i].st_name])+1;
            ndst++;
        }
    }

    /* Append room for core symbols at end of core part. */
    info->symoffs = ALIGN(mod->core.size, symsect->sh_addralign ?: 1);
    info->stroffs = mod->core.size = info->symoffs + ndst * sizeof(Elf_Sym);
    mod->core.size += strtab_size;

    debug("\t%s, %ld\n", info->secstrings + symsect->sh_name, info->symoffs);
    debug("\t%s, %ld\n", info->secstrings + strsect->sh_name, info->stroffs);
}

static void *module_alloc_update_bounds(unsigned long size)
{
    void *ret = module_alloc(size);

    if (ret) {
        /* Update module bounds. */
        if ((unsigned long)ret < module_addr_min)
            module_addr_min = (unsigned long)ret;
        if ((unsigned long)ret + size > module_addr_max)
            module_addr_max = (unsigned long)ret + size;
    }
    return ret;
}

static void copy_sections_and_update_section_addr(struct module *mod, struct load_info *info)
{
    int i;

    /* Transfer each section which specifies SHF_ALLOC */
    debug("final section addresses:\n");
    for (i = 0; i < info->hdr->e_shnum; i++) {
        void *dest;
        Elf_Shdr *shdr = &info->sechdrs[i];

        if (!(shdr->sh_flags & SHF_ALLOC))
            continue;

        dest = mod->core.data + shdr->sh_entsize;

        if (shdr->sh_type != SHT_NOBITS)
            memcpy(dest, (void *)shdr->sh_addr, shdr->sh_size);
        else
            memset(dest, 0, shdr->sh_size);
        /* Update sh_addr to point to copy in image. */
        shdr->sh_addr = (unsigned long)dest;
        debug("\t0x%lx %s\n",
             (long)shdr->sh_addr, info->secstrings + shdr->sh_name);
    }
}
static int within_module_core(struct module *mod, unsigned long addr)
{
    return (unsigned long)(mod->core.data) <= addr &&
            addr < (unsigned long)(mod->core.data + mod->core.text_size);
}

static const char *get_ksymbol_by_address(struct module *mod,
                   unsigned long addr,
                   unsigned long *size,
                   unsigned long *offset)
{
    unsigned int i, best = 0;
    unsigned long nextval;

    /* At worse, next value is at end of module */
    nextval = (unsigned long)(mod->core.data + mod->core.text_size);

    /* Scan for closest preceding symbol, and next symbol. (ELF
       starts real symbols at 1). */
    for (i = 1; i < mod->core.num_syms; i++) {
        if (mod->core.symtab[i].st_shndx == SHN_UNDEF)
            continue;

        /* We ignore unnamed symbols: they're uninformative
         * and inserted at a whim. */
        if (mod->core.symtab[i].st_value <= addr
            && mod->core.symtab[i].st_value > mod->core.symtab[best].st_value
            && *(mod->core.strtab + mod->core.symtab[i].st_name) != '\0') {
            best = i;
        }

        if (mod->core.symtab[i].st_value > addr
            && mod->core.symtab[i].st_value < nextval
            && *(mod->core.strtab + mod->core.symtab[i].st_name) != '\0') {
            nextval = mod->core.symtab[i].st_value;
        }

    }

    if (!best)
        return NULL;

    if (size)
        *size = nextval - mod->core.symtab[best].st_value;
    if (offset)
        *offset = addr - mod->core.symtab[best].st_value;

    return mod->core.strtab + mod->core.symtab[best].st_name;
}

const char *module_address_lookup(unsigned long addr,unsigned long *size,
        unsigned long *offset, char **modname)
{
    const char *ret = NULL;
    struct list_head *root_list = get_kernel_module_list();

    struct list_head *pos;
    struct list_head *next;

    list_for_each_safe(pos, next, root_list) {
        struct module *module = list_entry(pos, struct module, link);
        if (module) {
            if (modname)
                *modname = module->name;

            if ( within_module_core(module, addr) ) {
                ret = get_ksymbol_by_address(module, addr, size, offset);
                break;
            }
        }
    } /* end of list_for_each_safe(... */

    return ret;
}

static void find_module_sections(struct module *mod, struct load_info *info)
{
    mod->kp = section_objs(info, "__param",
                   sizeof(*mod->kp), &mod->num_kp);
    mod->syms = section_objs(info, "__ksymtab",
                 sizeof(*mod->syms), &mod->num_syms);
}


struct kernel_symbol *find_symbol_in_array(const struct kernel_symbol *syms, unsigned int nums, const char *name)
{
    int i;

    for (i = 0; i < nums; i++) {
        if (!strcmp(name, syms[i].name))
            return (void *)&syms[i];
    }

    return NULL;
}

struct kernel_symbol *find_symbol_in_module(struct module *mod, const char *name)
{
    struct list_head *pos;
    struct kernel_symbol *sym;

    sym = find_symbol_in_array(mod->syms, mod->num_syms, name);
    if (sym)
        return sym;

    list_for_each(pos, &mod->list) {
        struct module *module = list_entry(pos, struct module, link);

        /*
         * 独立的模块不向外部共享符号
         */
        if (module->is_independent)
            continue;

        sym = find_symbol_in_array(module->syms, module->num_syms, name);
        if (sym)
            return sym;
    }

    return NULL;
}

struct kernel_symbol *find_symbol_in_kernel(const char *name)
{
    struct module *mod = &kernel_module;
    return find_symbol_in_array(mod->syms, mod->num_syms, name);
}

struct kernel_symbol *find_symbol(const char *name)
{
    return find_symbol_in_module(&kernel_module, name);
}

static struct module *find_module_in_kernel(const char *name, struct module *mod)
{
    struct list_head *pos;
    struct list_head *next;

    list_for_each_safe(pos, next, &mod->list) {
        struct module *module = list_entry(pos, struct module, link);
        if (strcmp(module->name, name) == 0) {
            /* find the module */
            return module;

        } else {
            /* find module child list */
            if (!list_empty(&module->list)) {
                find_module_in_kernel(name, module);
            }
        }
    }

    return NULL;
}

struct module *find_module_all(const char *name)
{
    struct module *mod;

    mutex_lock(&module_mutex);
    mod = find_module_in_kernel(name, &kernel_module);
    mutex_unlock(&module_mutex);

    return mod;
}

/* Resolve a symbol for this module.  I.e. if we find one, record usage. */
static const struct kernel_symbol *resolve_symbol(struct module *mod,
                          const struct load_info *info,
                          const char *name)
{
    const struct kernel_symbol *sym;

    if (info->parent) {
        sym = find_symbol_in_kernel(name);
        if (!sym)
            sym = find_symbol_in_module(info->parent, name);
    } else {
        sym = find_symbol(name);
    }

    return sym;
}

static const struct kernel_symbol *
resolve_symbol_wait(struct module *mod,
            const struct load_info *info,
            const char *name)
{
    const struct kernel_symbol *ksym;

    ksym = resolve_symbol(mod, info, name);

    return ksym;
}

/* Change all symbols so that st_value encodes the pointer directly. */
static int simplify_symbols(struct module *mod, const struct load_info *info)
{
    Elf_Shdr *symsec = &info->sechdrs[info->index.sym];
    Elf_Sym *sym = (void *)symsec->sh_addr;
    unsigned long secbase;
    unsigned int i;
    int ret = 0;
    const struct kernel_symbol *ksym;

    debug("simplify symbols:\n");

    for (i = 1; i < symsec->sh_size / sizeof(Elf_Sym); i++) {
        const char *name = info->strtab + sym[i].st_name;

        switch (sym[i].st_shndx) {
        case SHN_COMMON:
            /* We compiled with -fno-common.  These are not
               supposed to happen.  */
            printf("Common symbol: %s\n", name);
            printf("%s: please compile with -fno-common\n",
                   mod->name);
            ret = -ENOEXEC;
            break;

        case SHN_ABS:
            /* Don't need to do anything */
            debug("\tAbsolute symbol: %s 0x%08lx\n",
                   name, (long)sym[i].st_value);
            break;

        case SHN_UNDEF:
            debug("\tfind undefined %s\n", name);
            ksym = resolve_symbol_wait(mod, info, name);
            /* Ok if resolved.  */
            if (ksym && !IS_ERR(ksym)) {
                sym[i].st_value = ksym->value;
                break;
            }

            /* Ok if weak.  */
            if (!ksym && ELF_ST_BIND(sym[i].st_info) == STB_WEAK)
                break;

            printf("%s: Unknown symbol %s (err %li)\n",
                   mod->name, name, PTR_ERR(ksym));
            ret = PTR_ERR(ksym) ?: -ENOENT;
            break;

        default:
            /*
             * 重定位symbol的地址
             */
            debug("\trelocate %s\n", name);
            secbase = info->sechdrs[sym[i].st_shndx].sh_addr;
            sym[i].st_value += secbase;
            break;
        }
    }

    return ret;
}

static void copy_symbols(struct module *mod, const struct load_info *info)
{
    unsigned int i, ndst;
    const Elf_Sym *src;
    Elf_Sym *dst;
    char *s;
    Elf_Shdr *symsec = &info->sechdrs[info->index.sym];

    Elf_Sym *symtab = (void *)symsec->sh_addr;
    unsigned int num_symtab = symsec->sh_size / sizeof(Elf_Sym);
    char *strtab = (void *)info->sechdrs[info->index.str].sh_addr;

    /* Set types up while we still have access to sections. */
    // for (i = 0; i < num_symtab; i++)
    //     symtab[i].st_info = elf_type(&symtab[i], info);

    debug("copy symbols:\n");

    mod->core.symtab = dst = mod->core.data + info->symoffs;
    mod->core.strtab = s = mod->core.data + info->stroffs;
    src = symtab;
    for (ndst = i = 0; i < num_symtab; i++) {
        if (i == 0 ||
            is_core_symbol(src+i, info->sechdrs, info->hdr->e_shnum)) {
            dst[ndst] = src[i];
            dst[ndst++].st_name = s - mod->core.strtab;
            strcpy(s, &strtab[src[i].st_name]);
            s += strlen(&strtab[src[i].st_name]) + 1;
            debug("\t%s %p\n", &strtab[src[i].st_name], (void *)src[i].st_value);
        }
    }
    mod->core.num_syms = ndst;
}

static int apply_relocations(struct module *mod, const struct load_info *info)
{
    unsigned int i;
    int err = 0;

    /* Now do relocations. */
    for (i = 1; i < info->hdr->e_shnum; i++) {
        unsigned int infosec = info->sechdrs[i].sh_info;

        /* Not a valid relocation section? */
        if (infosec >= info->hdr->e_shnum)
            continue;

        /* Don't bother with non-allocated sections */
        if (!(info->sechdrs[infosec].sh_flags & SHF_ALLOC))
            continue;

        if (info->sechdrs[i].sh_type == SHT_REL) {
            err = apply_relocate(info->sechdrs, info->strtab,
                         info->index.sym, i, mod);
        } else if (info->sechdrs[i].sh_type == SHT_RELA) {
            err = apply_relocate_add(info->sechdrs, info->strtab,
                         info->index.sym, i, mod);
        }
        if (err < 0)
            break;
    }

    return err;
}

static int add_unformed_module(struct module *mod)
{
    struct module *module;

    /*
     * 根据模块名称判断是否已存在
     */
    module = find_module_all(mod->name);
    if (module) {
        return -EEXIST;
    }

    return 0;
}

struct list_head *get_kernel_module_list(void)
{
    return &kernel_module.list;
}

struct module *load_module_to(void *data, unsigned int size, int flags, struct module *parent)
{
    struct load_info info;
    struct module *mod;
    int err;

    info.hdr = data;
    info.len = size;
    info.flags = flags;
    info.parent = parent;

    /*
     * 检查这是不是当前平台的elf文件
     */
    err = elf_header_check(&info);
    if (err) {
        printf("failed to check elf\n");
        goto err_out;
    }

    /*
     * 初始化 struct load_info
     */
    err = setup_load_info(&info);
    if (err) {
        printf("failed setup load info\n");
        goto err_out;
    }

    /*
     * 现在我们拿到一个暂时的module
     */
    mod = (void *)info.sechdrs[info.index.mod].sh_addr;

    /*
     * 检查模块信息是否与当前系统匹配
     */
    err = check_modinfo(mod, &info);
    if (err) {
        printf("failed to check module info\n");
        goto err_out;
    }

    /*
     * 平台对模块进行有效性检查
     */
    err = module_arch_check(info.hdr, info.sechdrs, info.secstrings, mod);
    if (err) {
        printf("arch check module failed\n");
        goto err_out;
    }

    /*
     * 计算module_data的大小,
     * 计算有效段在module中的新偏移
     */
    layout_sections(mod, &info);

    /*
     * 计算出符号段中有效数据的大小,并且加入到module_data
     */
    layout_symtab(mod, &info);

    /*
     * 检查模块是否已经存在
     */
    err = add_unformed_module(mod);
    if (err < 0) {
        goto err_out;
    }

    /*
     * core size 从现在开始不会再改变,所以对齐一下
     */
    mod->core.size = module_align(mod->core.size);

    debug("module size: %d %d %d\n", mod->core.size, mod->core.ro_size, mod->core.text_size);

    /*
     * 分配module data内存,并且更新模块地址边界
     */
    mod->core.data = module_alloc_update_bounds(mod->core.size);
    if (!mod->core.data) {
        printf("failed to allocate module\n");
        goto err_out;
    }

    /*
     * 拷贝有效的段,以及更新他们的地址
     */
    copy_sections_and_update_section_addr(mod, &info);

    /*
     * 现在我们拿到了真正的 module
     */
    mod = (void *)info.sechdrs[info.index.mod].sh_addr;

    /*
     * 初始化子模块列表
     */
    INIT_LIST_HEAD(&mod->list);
    INIT_LIST_HEAD(&mod->link);

    /*
     * 找出所有的模块自定义的段
     */
    find_module_sections(mod, &info);

    /*
     * 重定位已存在符号的值
     * 查找未定义符号的值
     */
    err = simplify_symbols(mod, &info);
    if (err) {
        printf("failed to simplify symbols\n");
        goto free_module_data;
    }

    /*
     * 代码/数据重定位
     */
    err = apply_relocations(mod, &info);
    if (err) {
        printf("failed to relocate module\n");
        goto free_module_data;
    }

    /* Sort exception table now relocations are done. */
    // sort_extable(mod->extable, mod->extable + mod->num_exentries);

    /*
     * 拷贝有效的符号
     */
    copy_symbols(mod, &info);

    /*
     * 平台最终对模块的操作
     */
    err = module_finalize(info.hdr, info.sechdrs, mod);
    if (err) {
        printf("failed to finalize module\n");
        goto free_module_data;
    }

    /*
     * 平台刷新模块的icache
     */
    flush_module_icache(mod);

    /*
     * 加入到当前模块之中
     */
    mutex_lock(&module_mutex);

    if (parent)
        list_add(&mod->link, &parent->list);
    else
        list_add(&mod->link, &kernel_module.list);

    mutex_unlock(&module_mutex);

    return mod;
free_module_data:
    module_free(mod->core.data);
err_out:
    return ERR_PTR(err);
}

struct module *load_module(void *data, unsigned int size, int flags)
{
    return load_module_to(data, size, 0, NULL);
}

void unload_module(struct module *mod)
{

    mutex_lock(&module_mutex);
    list_del(&mod->link);
    mutex_unlock(&module_mutex);

    module_free(mod->core.data);
}

static void init_kernel_module_syms(struct kernel_symbol *syms, unsigned int num_syms)
{
    INIT_LIST_HEAD(&kernel_module.list);
    mutex_init(&module_mutex);

    kernel_module.syms = syms;
    kernel_module.num_syms = num_syms;
}

void kernel_module_init(void)
{
    struct kernel_symbol *start = (void *)&__ksymtab_start;
    struct kernel_symbol *end = (void *)&__ksymtab_end;

    init_kernel_module_syms(start, end - start);
}
