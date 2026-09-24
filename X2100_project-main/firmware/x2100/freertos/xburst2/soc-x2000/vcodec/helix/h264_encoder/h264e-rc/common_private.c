//#include <linux/mm.h>
//#include <linux/slab.h>
#include <malloc.h>
#include <string.h>

//#include <common_private.h>



#if 0
int common_private_printk(unsigned char *fmt, ...)
{
	va_list args;
	int r = 0;

	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);

	return r;
}
#endif

#define printk printf

/* malloc */
void *common_private_malloc(unsigned long size)
{
	return malloc(size);
}

void *common_private_realloc(void *p, unsigned long new_size)
{
	return realloc(p, new_size);
}


void common_private_free(void *p)
{
	return free(p);
}

void *kzalloc(unsigned long size)
{
	void *p = malloc(size);
	memset(p, 0, size);
	return p;
}



