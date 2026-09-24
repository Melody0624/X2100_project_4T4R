#ifndef _CPU_ASM_SYMBOL_H_
#define _CPU_ASM_SYMBOL_H_

extern unsigned int _start;
extern void exception_entry(void);
extern void handle_int(void);
extern void handle_cpu_unusable(void);
extern void handle_msa_disabled(void);

extern void ret_from_irq(void);
extern void handle_exceptions_default(void);

extern void sw0_irq_handler(void);

extern void mailbox_irq_handler(void);

#endif /* _CPU_ASM_SYMBOL_H_ */

