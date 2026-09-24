#include <driver/can.h>
#include <common.h>
#include <os.h>

extern void soc_can_init_driver(void);
extern void soc_can_start(struct can_config *cfg);
extern void soc_can_stop(struct can_config *cfg);
extern int soc_can_send_frame(struct can_config *cfg, struct can_msg *msg);
extern int soc_can_receive_frame(struct can_config *cfg, struct can_msg *msg);
extern void soc_can_set_acceptance_filter(struct can_config *cfg, struct can_filter_cfg *af_cfg);

void can_start(struct can_config *cfg)
{
    soc_can_start(cfg);
}

void can_stop(struct can_config *cfg)
{
    soc_can_stop(cfg);
}

void can_set_acceptance_filter(struct can_config *cfg, struct can_filter_cfg *af_cfg)
{
    soc_can_set_acceptance_filter(cfg, af_cfg);
}

int can_receive_frame(struct can_config *cfg, struct can_msg *msg)
{
    return soc_can_receive_frame(cfg, msg);
}

int can_send_frame(struct can_config *cfg, struct can_msg *msg)
{
    return soc_can_send_frame(cfg, msg);
}

void can_init(void)
{
    soc_can_init_driver();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(can_register);
EXPORT_SYMBOL(can_send_frame);
EXPORT_SYMBOL(can_receive_frame);
EXPORT_SYMBOL(can_set_acceptance_filter);
EXPORT_SYMBOL(can_unregister);