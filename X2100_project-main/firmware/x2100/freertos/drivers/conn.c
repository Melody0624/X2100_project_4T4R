#include <soc/conn.h>

void conn_init(void)
{
    soc_conn_init();
}

int conn_write(struct conn_node *conn, const void *buf, unsigned int size, int timeout_ms)
{
    return soc_conn_write(conn, buf, size, timeout_ms);
}

int conn_read(struct conn_node *conn, void *buf, unsigned int size, int timeout_ms)
{
    return soc_conn_read(conn, buf, size, timeout_ms);
}

void conn_wait_conn_inited(void)
{
    soc_conn_wait_conn_inited();
}

void conn_release(struct conn_node *conn)
{
    soc_conn_release(conn);
}

struct conn_node *conn_request(const char *name, unsigned int buf_size)
{
    return soc_conn_request(name, buf_size);
}

