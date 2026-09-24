#include <stdio.h>
#include <spinlock.h>
#include <driver/uart.h>
#include <driver/console.h>
#include <os.h>

static struct console_device *con_dev = NULL;

void console_set_device(struct console_device *device)
{
    con_dev = device;
}

void console_put_char(char ch)
{
    static int is_r = 0;
    struct console_device *con = NULL;

#ifdef CONFIG_OS
    if (!os_in_handler_mode())
        con = thread_get_console(NULL);
#endif

    if (!con)
        con = con_dev;

    if (con && con->con_put_str) {
        if (ch == '\n' && !is_r)
            con->con_put_str(con, "\r\n", 2);
        else
            con->con_put_str(con, &ch, 1);

        is_r = ch == '\r';
    }
}

char console_get_char(void)
{
    struct console_device *con = NULL;

#ifdef CONFIG_OS
    if (!os_in_handler_mode())
        con = thread_get_console(NULL);
#endif

    if (!con)
        con = con_dev;

    if (con && con->con_get_char)
        return con->con_get_char(con);
    else
        return 0;
}

int __io_putchar(int ch)
{
    console_put_char(ch);
    return 0;
}

int __io_getchar(void)
{
    return (unsigned char)console_get_char();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(console_set_device);
EXPORT_SYMBOL(console_put_char);
EXPORT_SYMBOL(console_get_char);