#ifndef _DES_H_
#define _DES_H_

enum des_input_data{
    des_ENCRYPTS_data_input = 0,
    des_DECRYPTS_data_input = (1 << 3),
};

enum des_mode{
    des_ECB_mode = 0,
    des_CBC_mode = (1 << 2),
};

enum des_algorithm{
    des_SDES_algorithm = 0,
    des_TDES_algorithm = (1 << 1),
};

struct des_params {
    enum des_input_data des_input_data;
    enum des_mode des_mode;
    enum des_algorithm des_algorithm;

    unsigned int key_len;
    unsigned int iv_len;
    unsigned char *key_buf;
    unsigned char *iv_buf;
};

void des_init(void);
void des_enable(struct des_params *params);
void des_write_read_data(unsigned char *t_buf, unsigned char *r_buf, unsigned int t_len);
void des_disable(void);
void des_deinit(void);

#endif