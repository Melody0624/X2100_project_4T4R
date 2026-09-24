
INCLUDE build/build.lds.in

OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
 .text :
 {
  __start = .;
  *(.start_section*)
  __text_start = .;
  *(.text*)
  __text_end = .;
 } >.sram

 .rodata : {
  . = ALIGN(4);
  *(SORT_BY_ALIGNMENT(.rodata*))
 } >.sram

 .data : {
  . = ALIGN(4);
  *(SORT_BY_ALIGNMENT(.data*))
 } >.sram

 .preinit_array  : {
   . = ALIGN(4);
   __preinit_array_start = .;
   KEEP (*(.preinit_array))
   __preinit_array_end = .;
 } >.sram

  .init_array     :
  {
    . = ALIGN(4);
    PROVIDE (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array ))
    PROVIDE (__init_array_end = .);
  } >.sram

  .ctors          :
  {
    . = ALIGN(4);
    PROVIDE (__ctors_start = .);
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    PROVIDE (__ctors_end = .);
  } >.sram


  __tcsm_func_sram_start = .;
  .tcsm_func : {
    __tcsm_func_start = .;
    KEEP(*(.tcsm_func*))
    __tcsm_func_end = .;
  } >.tcsm AT>.sram

  .tcsm_func_empty : {
    __tcsm_func_empty_start = .;
  } >.sram
  __tcsm_func_sram_end = .;

 .fini_array     : {
   __fini_array_start = .;
   *(.fini_array)
   __fini_array_end = .;
 } >.sram

 __ksymtab_strings :
 {
   KEEP(*(SORT(__ksymtab_strings*)))
 } >.sram

 ___ksymtab :
 {
   . = ALIGN(8);
   __ksymtab_start = .;
   KEEP(*(SORT(___ksymtab*)))
   __ksymtab_end = .;
 } >.sram

 .func_symbol_index :
 {
   . = ALIGN(16);
   __func_symbol_index_start = .;
   KEEP(*(SORT(.func_symbol_index*)))
   __func_symbol_index_end = .;
 } >.sram

  .func_symbol_str :
 {
   __func_symbol_str_start = .;
   KEEP(*(SORT(.func_symbol_str*)))
   __func_symbol_str_end = .;
 } >.sram

  /* module param */
  __param : {
  . = ALIGN(4);
  __param_start = .;
  KEEP(*(__param))
  __param_stop = .;
  } >.sram

  .data2          :
  {
    . = ALIGN(4);
    __verify_start = .;
    KEEP (*(.verify_text .verify_text.*))
    __verify_end = .;

    __protected_start = .;
    KEEP (*(.protected_text .protected_text.*))
    KEEP (*(.protected_data .protected_data.*))
    __protected_end = .;
  } >.sram

  .data3          :
  {
    . = ALIGN(4);
    __iaac_start = .;
    KEEP(*(.decrypto.iaac .decrypto.iaac*))
    __iaac_end = .;
    __table_start = .;
    KEEP(*(.decrypto.cry .decrypto.cry*))
    __table_end = .;
  } >.sram

  /* gp -> sdata sbss */
 .sdata : {
  . = ALIGN(4);
  __gp_data_start = .;
  PROVIDE(__global_pointer$ = . + 0x7ff0);
  *(SORT_BY_ALIGNMENT(.sdata*))
  *(SORT_BY_ALIGNMENT(.gnu.linkonce.s.*))
  *(SORT_BY_ALIGNMENT(.lit8))
  *(SORT_BY_ALIGNMENT(.lit4))
 } >.sram

 . = ALIGN(4);
 __image_copy_end = .;

  .bss : {
  . = ALIGN(4);
  __bss_start = .;
  *(SORT_BY_ALIGNMENT(.sbss*))
  *(SORT_BY_ALIGNMENT(.gnu.linkonce.sb.*))
  *(SORT_BY_ALIGNMENT(.scommon*))
  . = ALIGN(4);
  __gp_data_end = .;
  *(.bss*)
  *(COMMON)
  . = ALIGN(4);
  } >.sram

  .bss_end : {
  . = ALIGN(4);
  __bss_end = .;
  } >.sram

  __gp_window_start = __global_pointer$ - 0x8000;
  __gp_window_end   = __global_pointer$ + 0x7fff;
  __gp_data_start_ok = (__gp_data_start >= __gp_window_start);
  __gp_data_end_ok   = (__gp_data_end   <= __gp_window_end);

  ASSERT(__gp_data_start_ok, "gp-data start exceeds __global_pointer$ - 32KB")
  ASSERT(__gp_data_end_ok,   "gp-data end exceeds __global_pointer$ + 32KB")

 .exception_section : {
   . = ALIGN(4096);
   __exception_section_start = .;
   . = . + exception_section_size;
   KEEP(*(.exception_section))
   __exception_section_end = .;
 } >.sram

 .user_stack : {
    . = ALIGN(8);
    _user_stack_start = .;
    . = . + main_stack_size;
    KEEP(*(.user_stack))
    _user_stack_end = .;
  } >.sram

 .irq_stack : {
   . = ALIGN(8);
   _irq_stack_start = .;
   . = . + exception_stack_size;
   KEEP(*(.irq_stack))
   _irq_stack_end = .;
 } >.sram

 .exception_stack : {
    . = ALIGN(8);
    _exception_stack_start = .;
    . = . + exception_stack_size;
    KEEP(*(.exception_stack))
    _exception_stack_end = .;
  } >.sram

 .mapped_rtosdata : {
    . = ALIGN(8);
    _mapped_rtosdata_size = .;
  } >.sram

  /* must be last */
  .user_heap : {
    . =  ALIGN(8) ;
    _user_heap_start = .;
    KEEP(*(.user_heap))
    . = __start + sram_size;
    _user_heap_end = .;
  } >.sram

 /DISCARD/ : {
  *(.dynbss)
  *(.dynstr)
  *(.dynamic)
  *(.interp)
  *(.hash)
  *(.gnu.*)
  *(.plt)
  *(.got.plt)
  *(.rel.plt)
  *(.rel.dyn)
 }
}
