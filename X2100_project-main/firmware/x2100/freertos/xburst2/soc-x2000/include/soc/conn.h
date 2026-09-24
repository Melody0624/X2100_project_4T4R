#ifndef _SOC_CONN_H_
#define _SOC_CONN_H_

#include <common.h>

struct conn_node;

void soc_conn_init(void);

int soc_conn_write(struct conn_node *conn, const void *buf, unsigned int size, int timeout_ms);

int soc_conn_read(struct conn_node *conn, void *buf, unsigned int size, int timeout_ms);

void soc_conn_wait_conn_inited(void);

void soc_conn_release(struct conn_node *conn);

struct conn_node *soc_conn_request(const char *name, unsigned int buf_size);

#endif
