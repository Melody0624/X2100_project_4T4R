#ifndef _CONN_H_
#define _CONN_H_

#include <common.h>

struct conn_node;

void conn_init(void);

int conn_write(struct conn_node *conn, const void *buf, unsigned int size, int timeout_ms);

int conn_read(struct conn_node *conn, void *buf, unsigned int size, int timeout_ms);

void conn_wait_conn_inited(void);

void conn_release(struct conn_node *conn);

struct conn_node *conn_request(const char *name, unsigned int buf_size);

#endif
