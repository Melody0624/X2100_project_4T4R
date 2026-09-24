#include <stdio.h>
#include <asm/mipsregs.h>
#include <cpu/cpu.h>
#include <cpu/irqflags.h>

void show_compile_time(void)
{
    printf("%s rtos @ %s %s, epc: %08lx\n", CPU_NAME, __DATE__, __TIME__, read_c0_errorepc());
}
