#include <driver/aes.h>
#include <common.h>

extern int soc_aes_init(void);
extern int soc_aes_encryption(struct aes_config *config, void *src, void *dst, int len);
extern int soc_aes_decryption(struct aes_config *config, void *src, void *dst, int len);

void aes_init(void)
{
    soc_aes_init();
}

int aes_encryption(struct aes_config *config, void *src, void *dst, int len)
{
    return soc_aes_encryption(config, src, dst, len);
}

int aes_decryption(struct aes_config *config, void *src, void *dst, int len)
{
    return soc_aes_decryption(config, src, dst, len);
}
