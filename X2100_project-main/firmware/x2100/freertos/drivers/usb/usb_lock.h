#ifndef _USB_LOCK_H_
#define _USB_LOCK_H_

#include <cpu/spinlock.h>

#ifdef CONFIG_USB_IRQ_THREAD_MODE
#define usb_spin_lock_irqsave(lock, flags) \
do { \
    (void) flags; \
    spin_lock(lock); \
} while (0)

#define usb_spin_lock_irq(lock) \
do { \
    spin_lock(lock); \
} while (0)

#define usb_spin_unlock_irqrestore(lock, flags) \
do { \
    (void) flags; \
    spin_unlock(lock); \
} while (0)

#define usb_spin_unlock_irq(lock) \
do { \
    spin_unlock(lock); \
} while (0)

#else /* CONFIG_USB_IRQ_THREAD_MODE */

#define usb_spin_lock_irqsave(lock, flags) \
do { \
    spin_lock_irqsave(lock, flags); \
} while (0)

#define usb_spin_lock_irq(lock) \
do { \
    spin_lock_irq(lock); \
} while (0)

#define usb_spin_unlock_irqrestore(lock, flags) \
do { \
    spin_unlock_irqrestore(lock, flags); \
} while (0)

#define usb_spin_unlock_irq(lock) \
do { \
    spin_unlock_irq(lock); \
} while (0)

#endif

#endif /* _USB_LOCK_H_ */
