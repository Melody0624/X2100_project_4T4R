#ifndef __CPU_FFS_H_
#define __CPU_FFS_H_

static inline int __fls(unsigned int val)
{
    unsigned long d = 0;

    asm volatile (
    "    .set    push                        \n"
    "    .set    reorder                     \n"
    "    .set    noat                        \n"
    "    clz    %0, %1                       \n"
    "    .set    pop                         \n"
    : "=r" (d)
    : "r" (val)
    : "memory"
    );

    return 31 - d;
}

static inline int __ffs(unsigned int word)
{
	return __fls(word & -word);
}

static inline int cpu_fls(unsigned int val)
{
    unsigned long d = 0;

    asm volatile (
    "    .set    push                        \n"
    "    .set    reorder                     \n"
    "    .set    noat                        \n"
    "    clz    %0, %1                       \n"
    "    .set    pop                         \n"
    : "=r" (d)
    : "r" (val)
    : "memory"
    );

    return 32 - d;
}

static inline int cpu_ffs(unsigned int word)
{
	if (!word)
		return 0;

	return cpu_fls(word & -word);
}

#endif /* __CPU_FFS_H_ */