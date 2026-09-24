#include <common.h>
#include <driver/cache.h>
#include <uboot_lib.h>

#define CONFIG_PARAM_BASE                      0x80000800 /* The base address of parameters*/
static int cleanup_before_linux (void)
{
	return 0;
}

void soc_export_config_for_linux(void);

void jump_to_image_linux(struct rtos_boot_os_args *args)
{
    if (args->cmdargs == NULL)
        panic("must config cmdargs on spl!\n");

	soc_export_config_for_linux();

    static u32 *param_addr = NULL;
	typedef void (*image_entry_arg_t)(int, char **, void *)
		__attribute__ ((noreturn));
	image_entry_arg_t image_entry =
		(image_entry_arg_t) args->entry_point;

	cleanup_before_linux();
	param_addr = (u32 *)CONFIG_PARAM_BASE;
	param_addr[0] = 0;
	param_addr[1] = (u32)args->cmdargs;
	flush_cache_all();
	image_entry(2, (char **)param_addr, NULL);
}
