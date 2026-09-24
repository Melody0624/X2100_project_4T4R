#ifndef _CPU_LDS_SYMBOL_H_
#define _CPU_LDS_SYMBOL_H_

extern unsigned int __bss_start;
extern unsigned int __bss_end;
extern unsigned int __exception_section_start;
extern unsigned int _user_stack_start;
extern unsigned int _user_stack_end;
extern unsigned int _user_heap_start;
extern unsigned int _user_heap_end;
extern unsigned int __text_start;
extern unsigned int __text_end;
extern unsigned int __param_start;
extern unsigned int __param_stop;
extern unsigned int __start;
extern unsigned int _mapped_rtosdata_size;

extern unsigned int __func_symbol_index_start;
extern unsigned int __func_symbol_index_end;
extern unsigned int __func_symbol_str_start;
extern unsigned int __func_symbol_str_end;

extern unsigned int __ksymtab_start;
extern unsigned int __ksymtab_end;

#endif /*  _CPU_LDS_SYMBOL_H_ */
