#ifndef __TIZIANO_SYS_H__
#define __TIZIANO_SYS_H__

enum irqreturn {
	IRQ_NONE		= (0 << 0),
	IRQ_HANDLED		= (1 << 0),
	IRQ_WAKE_THREAD		= (1 << 1),
};

typedef enum irqreturn irqreturn_t;

extern int system_reg_write(void *hdl, unsigned int reg, unsigned int value);
extern unsigned int  system_reg_read(void *hdl, unsigned int reg);
extern int system_lock(void *hdl);
extern int system_unlock(void *hdl);
extern int system_irq_func_set(void *hdl, int irq, void *func, void *data);

#endif
