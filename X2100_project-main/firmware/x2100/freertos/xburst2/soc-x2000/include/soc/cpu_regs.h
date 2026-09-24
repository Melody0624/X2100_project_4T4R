#ifndef _CPU_REGS_H_
#define _CPU_REGS_H_

#ifndef __ASSEMBLY__
union fp_msa_reg{
	unsigned int msa_r[4];
	unsigned long long  fp_r;
};
struct cpu_regs {
   
      	__attribute__((aligned(8))) union fp_msa_reg fp_msa_regs[32];
    unsigned int fcsr;
    unsigned int reserved;
#ifdef __mips_msa
    unsigned int msa_csr;
#endif
};

void soc_init_cpu_regs(struct cpu_regs *regs);

#endif

#endif /* _CPU_REGS_H_ */
