void soc_mac_init(void);
void soc_mac_deinit(void);

void mac_init(void)
{
    soc_mac_init();
}

void mac_deinit(void)
{
    soc_mac_deinit();
}