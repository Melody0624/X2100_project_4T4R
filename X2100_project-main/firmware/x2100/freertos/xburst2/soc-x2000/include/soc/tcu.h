#ifndef _SOC_TCU_H_
#define _SOC_TCU_H_

struct tcu {
    int id;
    char *clk_id;
    char *name;
    unsigned long freq;
    unsigned int half_num;
    unsigned int full_num;

    void *data;
    void (*half_cb)(void *data);
    void (*full_cb)(void *data);
};

void tcu_show_support_clk_freqs(void);

void tcu_config(struct tcu *tcu);

unsigned int tcu_get_count(int id);

unsigned long tcu_get_freq(int id);

void tcu_release(int id);

void tcu_clear_tcnt(int id);

unsigned int tcu_test_half_irq(int id);

unsigned int tcu_test_full_irq(int id);

void tcu_enable_half_irq(int id, unsigned int half_num, void (*half_cb)(void *data));

void tcu_disable_half_irq(int id);

void tcu_enable_full_irq(int id, unsigned int full_num, void (*full_cb)(void *data));

void tcu_disable_full_irq(int id);

unsigned int tcu_get_status(int id);

void tcu_set_full_num(int id, unsigned int full_num);

#endif