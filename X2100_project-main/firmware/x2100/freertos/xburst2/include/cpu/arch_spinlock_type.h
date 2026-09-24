#ifndef _ARCH_SPINLOCK_TYPE_H_
#define _ARCH_SPINLOCK_TYPE_H_

typedef union {
	/*
	 * bits	 0..15 : serving_now
	 * bits 16..31 : ticket
	 */
	unsigned int lock;
	struct {
#ifdef __BIG_ENDIAN
		unsigned short ticket;
		unsigned short serving_now;
#else
		unsigned short serving_now;
		unsigned short ticket;
#endif
	} h;
} arch_spinlock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ .lock = 0 }

typedef struct {
	volatile unsigned int lock;
} arch_rwlock_t;

#define __ARCH_RW_LOCK_UNLOCKED		{ 0 }

#endif /* _ARCH_SPINLOCK_TYPE_H_ */
