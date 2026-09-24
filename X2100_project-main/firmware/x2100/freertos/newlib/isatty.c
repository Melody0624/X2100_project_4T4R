#include <errno.h>
#include <kernel_symbol.h>
#include <common.h>

__weak int isatty(int fd)
{
    errno = EINVAL;
    return 0;
}
EXPORT_SYMBOL(isatty);
