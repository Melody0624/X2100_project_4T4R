#include <driver/des.h>

void soc_des_init(void);
void soc_des_enable(struct des_params *params);
void soc_des_write_read_data(unsigned char *t_buf, unsigned char *r_buf, unsigned int t_len);
void soc_des_disable(void);
void soc_des_deinit(void);

void des_init(void)
{
    soc_des_init();
}

void des_enable(struct des_params* params)
{
    soc_des_enable(params);
}

void des_write_read_data(unsigned char *t_buf, unsigned char *r_buf, unsigned int t_len)
{
    soc_des_write_read_data(t_buf, r_buf, t_len);
}

void des_disable(void)
{
    soc_des_disable();
}

void des_deinit(void)
{
    soc_des_deinit();
}
