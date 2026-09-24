#ifndef _CONSOLE_H_
#define _CONSOLE_H_

struct console_device {
    void (*con_put_str)(struct console_device *dev, const char *str, int len);
    char (*con_get_char)(struct console_device *dev);
};

void console_put_char(char ch);

char console_get_char(void);

void console_set_device(struct console_device *device);

#endif /* _CONSOLE_H_ */