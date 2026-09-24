#ifndef _RELOCATE_H_
#define _RELOCATE_H_

struct relocate_header {
    unsigned long version;
    unsigned long start;
    unsigned long gp;
    unsigned long rel_dyn_start;
    unsigned long rel_dyn_end;
    unsigned long image_copy_end;
    unsigned long num_got_entries;
    unsigned long got_start;
    unsigned long ram_size;
    unsigned long entry;
    unsigned long bss_start;
    unsigned long bss_end;
};

void relocate_got(struct relocate_header *head, unsigned long new_start);

void dump_relocate_header(struct relocate_header *head);

#endif /* _RELOCATE_GOT_H_ */