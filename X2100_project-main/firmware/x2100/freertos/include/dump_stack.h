#ifndef _DUMP_STACK_H_
#define _DUMP_STACK_H_

void show_stacktrace(unsigned long sp, unsigned long pc, unsigned long ra);

void dump_stack(void);

#endif
