#include <stdlib.h>

extern "C"
{
	/**
	 * \brief Pure-virtual workaround.
	 *
	 * The avr-libc does not support a default implementation for handling
	 * possible pure-virtual calls. This is a short and empty workaround for this.
	 */
	void
	__cxa_pure_virtual()
	{
		abort();
	}

	__extension__ typedef int __guard __attribute__((mode (__DI__)));

	int
	__cxa_guard_acquire(__guard *g)
	{
		return !*(char *)(g);
	}

	void
	__cxa_guard_release (__guard *g)
	{
		*(char *)g = 1;
	}

	void
	__cxa_guard_abort (__guard *)
	{
	}

	int
	__cxa_atexit (void (* /*destructor*/)(void*), void* /*object*/, void* /*dso_handle*/)
	{
		return 0;
	}

	void* __dso_handle = (void*) &__dso_handle;
}
