#include <soc/hash.h>

void hash_init(void)
{
    soc_hash_init();
}
int hash_request(enum encryption_mode mode, unsigned int timeout_ms)
{
    return soc_hash_request(mode, timeout_ms);
}

int hash_write(int handle, unsigned char *str, unsigned int str_size)
{
    return soc_hash_write(handle, str, str_size);
}

int hash_read_free(int handle, unsigned char *rec, unsigned long hash_size)
{
    return soc_hash_read_free(handle, rec, hash_size);
}

void hash_deinit(void)
{
    soc_hash_deinit();
}