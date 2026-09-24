/*
 * Common module linker script, always used when linking a module.
 * Archs are free to supply their own linker scripts.  ld will
 * combine them automatically.
 */
SECTIONS {
	/DISCARD/ : { *(.discard) }

	__ksymtab		: { *(SORT(___ksymtab+*)) }
	__kcrctab		: { *(SORT(___kcrctab+*)) }
}
