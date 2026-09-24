#include <driver/security_aes.h>
#include <common.h>

extern int soc_sc_aes_init(void);
extern int soc_sc_aes_encryption(struct sc_aes_config *config, void *src, void *dst, int len);
extern int soc_sc_aes_decryption(struct sc_aes_config *config, void *src, void *dst, int len);

void sc_aes_init(void)
{
    soc_sc_aes_init();
}

int sc_aes_encryption(struct sc_aes_config *config, void *src, void *dst, int len)
{
    return soc_sc_aes_encryption(config, src, dst, len);
}

int sc_aes_decryption(struct sc_aes_config *config, void *src, void *dst, int len)
{
    return soc_sc_aes_decryption(config, src, dst, len);
}