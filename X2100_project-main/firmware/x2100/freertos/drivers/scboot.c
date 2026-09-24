#include <os.h>
#include <common.h>
#include <soc/scboot.h>
#include <driver/scboot.h>

__weak int soc_secure_write_key(void *keybin)
{
    return -1;
}
__weak int soc_secure_enable_scboot(void)
{
    return -1;
}

int secure_scboot(void *input, void *output)
{
    return soc_secure_scboot(input, output);
}

int secure_write_key(void *keybin)
{
    return soc_secure_write_key(keybin);
}

int secure_enable_scboot(void)
{
    return soc_secure_enable_scboot();
}