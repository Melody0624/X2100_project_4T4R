#ifndef _CPU_CPU_H_
#define _CPU_CPU_H_

void arch_enable_simd(void);

void arch_disable_simd(void);

void arch_enable_fpu(void);

void arch_disable_fpu(void);

int arch_get_cpu_id(void);

void arch_init_cpu(void);

void arch_init_other_cpu(void);

void arch_deinit_cpu(void);

void arch_startup_cpu(int cpu_id, unsigned long init_func_address);

void arch_shutdown_cpu(int cpu_id);
void arch_shutdown_cpu_prepare(int cpu_id);
void arch_shutdown_current_cpu(void);


#define CPU_NAME "xburst2"

#endif /* _CPU_CPU_H_ */