#ifndef _UART_CONSOLE_H_
#define _UART_CONSOLE_H_

void uart_console_init(void);
void uart_console_disconnect_to_console(void);
void uart_console_connect_to_console(void);
void uart_console_receive(char *buf, int len);
void uart_console_send(const char *str, int len);
int uart_console_receive_timeout(char *buf, int len, int timeout_ms);

#endif /* _UART_CONSOLE_H_ */
