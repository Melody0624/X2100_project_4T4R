#ifndef _CPU_IRQFLAGS_H_
#define _CPU_IRQFLAGS_H_

static inline void local_irq_disable(void)
{
    asm volatile (
    "    .set    push                        \n"
    "    .set    noat                        \n"
    "    di                                  \n"
    "    sll    $0, $0, 3                    \n"
    "    .set    pop                         \n"
    : /* no outputs */
    : /* no inputs */
    : "memory");
}

static inline void local_irq_enable(void)
{
    asm volatile (
    "    .set    push                        \n"
    "    .set    noreorder                   \n"
    "    .set    noat                        \n"
    /*
     * Slow, but doesn't suffer from a relatively unlikely race
     * condition we're having since days 1.
     */
    "    di                                  \n"
    "    ei                                  \n"
    "    sll    $0, $0, 3                    \n"
    "    .set    pop                         \n"
    : /* no outputs */
    : /* no inputs */
    : "memory");
}

static inline unsigned long _local_irq_save(void)
{
    unsigned long flags;

    asm volatile (
    "    .set    push                        \n"
    "    .set    reorder                     \n"
    "    .set    noat                        \n"
    "    di    %[flags]                      \n"
    "    andi    %[flags], 1                 \n"
    "    sll    $0, $0, 3                    \n"
    "    .set    pop                         \n"
    : [flags] "=r" (flags)
    : /* no inputs */
    : "memory");

    return flags;
}

#define local_irq_save(flags) \
    do { \
        flags = _local_irq_save(); \
    } while (0)

static inline void local_irq_restore(unsigned long flags)
{
    unsigned long __tmp1;

    asm volatile (
    "    .set    push                        \n"
    "    .set    noreorder                   \n"
    "    .set    noat                        \n"
    /*
     * Slow, but doesn't suffer from a relatively unlikely race
     * condition we're having since days 1.
     */
    "    beqz    %[flags], 1f                \n"
    "    di                                  \n"
    "    ei                                  \n"
    "1:                                      \n"
    "    sll    $0, $0, 3                    \n"
    "    .set    pop                         \n"
    : [flags] "=r" (__tmp1)
    : "0" (flags)
    : "memory");
}

#endif /* _CPU_IRQFLAGS_H_ */