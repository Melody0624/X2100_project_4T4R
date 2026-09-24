#ifndef _DUMP_MEM_H
#define _DUMP_MEM_H

void dump_mem8(void *addr, int size, int line_count);
void dump_mem8_c_style(void *addr, int size, int line_count);

void dump_mem16(void *addr, int size, int line_count);
void dump_mem16_c_style(void *addr, int size, int line_count);

void dump_mem32(void *addr, int size, int line_count);
void dump_mem32_c_style(void *addr, int size, int line_count);

#endif /* _DUMP_MEM_H */