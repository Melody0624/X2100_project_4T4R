
#include <common.h>
#include <os.h>
#include <list.h>
#include <string.h>

#include <cpu/cpu.h>
#include <soc/conn.h>
#include <soc/ccu.h>

#include "mailbox.c"

#define NOTIFY_TIMEOUT_MS 1000

static DEFINE_MUTEX(lock);

enum conn_cmd {
    CONN_bind = 0xac,
    CONN_unbind = 0xab,
    CONN_unbind_confirm = 0xad,
    CONN_writeable = 0xae,
    CONN_readable = 0xaf,
};

#define LINUX_CPU_ID    0
#define RTOS_CPU_ID     1

struct conn_notify {
    enum conn_cmd cmd;
    struct conn_node *conn;
};

struct conn_node {
    int id;
    char *name;
    struct ring_mem ring;

    char *conn_buf;
    struct mutex write_mutex;
    struct mutex read_mutex;

    thread_waiter_t write_wait;
    thread_waiter_t read_wait;
    thread_waiter_t write_complete_wait;
    thread_waiter_t unbind_confirm_wait;
};

struct conn_table {
    volatile struct conn_node *this_side;
    volatile struct conn_node *other_side;
};

struct conn_table conn_table[CONFIG_X2000_CONN_MAX_LINK_COUNT];

static int conn_max_link_count = CONFIG_X2000_CONN_MAX_LINK_COUNT;
static int conn_is_init = 0;

static thread_waiter_t inited_wait;

static int conn_send_notify(struct conn_node *conn, enum conn_cmd cmd)
{
    int ret;
    int size;
    struct conn_notify notify;

    notify.cmd = cmd;
    notify.conn = conn;

    size = sizeof(notify);

    ret = mailbox_send(&notify, size, NOTIFY_TIMEOUT_MS);
    if (ret != size) {
        printf("CONN: send notify timeout.(CPU%d)\n", RTOS_CPU_ID);
        return -1;
    }

    return 0;
}

static int get_useable_id_by_name(const char *name)
{
    int i;

    for (i = 0; i < conn_max_link_count; i++) {
        const char *conn_name;
        if (conn_table[i].this_side)
            conn_name = conn_table[i].this_side->name;
        else if (conn_table[i].other_side)
            conn_name = conn_table[i].other_side->name;
        else
            continue;

        if (!strcmp(name, conn_name))
            return i;
    }

    return -1;
}

static int get_unused_id(void)
{
    int i;

    for (i = 0; i < conn_max_link_count; i++) {
        if (!conn_table[i].this_side && !conn_table[i].other_side)
            return i;
    }
    return -1;
}

static int get_useable_id(const char *name)
{
    int id;

    /* 优先选择同名的conn设备进行匹配,并返回对应的ID */
    id = get_useable_id_by_name(name);
    if (id >= 0 && id < conn_max_link_count)
        return id;

    /* 若没有同名设备存在,选择一个没被用到的ID用作设备ID */
    return get_unused_id();
}

static int conn_other_side_bind(struct conn_notify *notify)
{
    int id;
    struct conn_node *conn = notify->conn;

    mutex_lock(&lock);

    id = get_useable_id(conn->name);
    if (id < 0) {
        printf("CONN: conn bind failed, not enough id to be used !(CPU%d)\n", RTOS_CPU_ID);
        goto unlock;
    }

    conn_table[id].other_side = conn;

unlock:
    mutex_unlock(&lock);

    return 0;
}

static int conn_other_side_unbind(struct conn_notify *notify)
{
    int id;
    int ret;
    struct conn_node *this_side;
    struct conn_node *other_side = notify->conn;

    mutex_lock(&lock);

    id = get_useable_id_by_name(other_side->name);
    if (id < 0) {
        printf("CONN: failed to get conn(%s)!(CPU%d)\n", other_side->name, RTOS_CPU_ID);
        goto unlock;
    }

    this_side = (struct conn_node *)conn_table[id].this_side;

    conn_table[id].other_side = NULL;

    if (this_side) {
        thread_waiter_wakeup(&this_side->write_wait);

        thread_waiter_wakeup(&this_side->read_wait);

        thread_waiter_wait_timeout(&this_side->write_complete_wait, 100);
    }

    ret = conn_send_notify(other_side, CONN_unbind_confirm);
    if (ret < 0)
        printf("CONN: conn(%s) failed to send bind_ok cmd !(CPU%d)\n", other_side->name, RTOS_CPU_ID);

unlock:
    mutex_unlock(&lock);

    return 0;
}

static void conn_this_side_unbind_confirm(struct conn_notify *notify)
{
    struct conn_node *conn = notify->conn;
    if (!conn)
        return;

    mutex_lock(&lock);

    conn_table[conn->id].this_side = NULL;

    thread_waiter_wakeup(&conn->unbind_confirm_wait);

    mutex_unlock(&lock);
}

static void conn_wake_up_writeable(struct conn_notify *notify)
{
    struct conn_node *conn = notify->conn;
    if (!conn)
        return;

    thread_waiter_wakeup(&conn->write_wait);
}

static void conn_wake_up_readable(struct conn_notify *notify)
{
    struct conn_node *conn = notify->conn;
    if (!conn)
        return;

    thread_waiter_wakeup(&conn->read_wait);
}

int soc_conn_write(struct conn_node *conn, const void *buf, unsigned int size, int timeout_ms)
{
    int id;
    int ret;
    struct ring_mem *ring;
    int total_size = size;
    struct conn_node *other_side;

    uint64_t start_time;

    assert(conn && buf && size);

    mutex_lock(&conn->write_mutex);

    start_time = systick_get_time_ms();

    mutex_lock(&lock);

    id = conn->id;

    if (!conn_table[id].other_side) {
        printf("CONN: conn(%s) conn_write err, not be binded !(CPU%d)\n", conn->name, RTOS_CPU_ID);
        ret = -1;
        mutex_unlock(&lock);
        goto unlock;
    }

    other_side = (struct conn_node *)conn_table[id].other_side;

    ring = &other_side->ring;

    mutex_unlock(&lock);

    while (1) {
        if (!conn_table[id].other_side) {
            printf("CONN: conn(%s) write failed, already unbind! (CPU%d)\n", conn->name, RTOS_CPU_ID);
            break;
        }

        int other_side_need_wake_up = (ring_mem_readable_size(ring) == 0);

        ret = ring_mem_write(ring, (void *)buf, size);

        size -= ret;
        buf += ret;

        if (other_side_need_wake_up)
            conn_send_notify(other_side, CONN_readable);

        if (!size)
            break;

        int timeout = timeout_ms - (systick_get_time_ms() - start_time);
        if (timeout <= 0) {
            printf("CONN: conn(%s) write timeout.(CPU%d)\n", conn->name, RTOS_CPU_ID);
            break;
        }

        if (!ring_mem_writable_size(ring))
            thread_waiter_wait_timeout(&conn->write_wait, timeout);
    }

unlock:
    if (!conn_table[id].other_side)
        thread_waiter_wakeup(&conn->write_complete_wait);

    mutex_unlock(&conn->write_mutex);

    return total_size - size;
}

int soc_conn_read(struct conn_node *conn, void *buf, unsigned int size, int timeout_ms)
{
    int ret;
    int id;
    struct ring_mem *ring;
    int total_size = size;

    uint64_t start_time;

    assert(conn && buf && size);

    mutex_lock(&conn->read_mutex);

    start_time = systick_get_time_ms();

    id = conn->id;

    ring = &conn->ring;

    while (1) {
        if (!conn_table[id].other_side)
            break;

        int other_side_need_wake_up = (ring_mem_readable_size(ring) == ring->mem_size);

        ret = ring_mem_read(&conn->ring, buf, size);

        size -= ret;
        buf += ret;

        if (other_side_need_wake_up)
            conn_send_notify((struct conn_node *)conn_table[id].other_side, CONN_writeable);

        if (!size)
            break;

        int timeout = timeout_ms - (systick_get_time_ms() - start_time);
        if (timeout < 0)
            break;

        if (!ring_mem_readable_size(ring))
            thread_waiter_wait_timeout(&conn->read_wait, timeout);
    }

    mutex_unlock(&conn->read_mutex);

    return total_size - size;
}

static void conn_do_release(struct conn_node *conn)
{
    int id = conn->id;

    ring_mem_clean(&conn->ring);

    free(conn->name);
    free(conn->conn_buf);
    free(conn);

    conn_table[id].this_side = NULL;
}

static int soc_conn_get_init_status(void)
{
    int ret;

    mutex_lock(&lock);

    ret = conn_is_init;

    mutex_unlock(&lock);

    return ret;
}

void soc_conn_wait_conn_inited(void)
{
    if (!soc_conn_get_init_status())
        thread_waiter_wait(&inited_wait);
}

void soc_conn_release(struct conn_node *conn)
{
    int ret;
    int id;
    int cpu_id = RTOS_CPU_ID;

    assert(conn);

    ret = conn_send_notify(conn, CONN_unbind);
    if (ret < 0) {
        printf("CONN: conn(%s) send unbind notify failed.(CPU%d)\n", conn->name, cpu_id);
        printf("CONN: conn(%s) release failed.(CPU%d)\n", conn->name, cpu_id);
        return;
    }

    mutex_lock(&conn->write_mutex);

    mutex_lock(&conn->read_mutex);

    mutex_lock(&lock);

    id = conn->id;

    while (1) {
        if (!conn_table[id].this_side)
            break;

        mutex_unlock(&lock);

        ret = thread_waiter_wait_timeout(&conn->unbind_confirm_wait, 10);
        if (ret < 0)
            printf("CONN: conn(%s) wait unbind_confirm timeout.(CPU%d)\n", conn->name, cpu_id);

        mutex_lock(&lock);
    }

    mutex_unlock(&conn->read_mutex);

    mutex_unlock(&conn->write_mutex);

    conn_do_release(conn);

    mutex_unlock(&lock);
}

struct conn_node *soc_conn_request(const char *name, unsigned int buf_size)
{
    int id;
    int ret;
    int name_len;
    struct conn_node *conn = NULL;
    int cpu_id = RTOS_CPU_ID;

    mutex_lock(&lock);

    if (!conn_is_init)
        goto unlock;

    if (name == NULL) {
        printf("CONN: name cannot be NULL.(CPU%d)\n", cpu_id);
        goto unlock;
    }

    id = get_useable_id(name);
    if (id < 0) {
        printf("CONN: no enough id to be used !(CPU%d)\n", cpu_id);
        goto unlock;
    }

    if (conn_table[id].this_side) {
        printf("CONN: conn(%s:%d) has been requested !(CPU%d)\n", name, id, cpu_id);
        goto unlock;
    }

    conn = malloc(sizeof(*conn));
    if (conn == NULL) {
        printf("CONN: conn(%s) malloc failed.(CPU%d)\n", name, cpu_id);
        goto unlock;
    }

    /* ring mem size must be a power of 2 */
    buf_size = roundup_pow_of_two(buf_size);
    conn->conn_buf = malloc(buf_size);
    if (conn->conn_buf == NULL) {
        printf("CONN: conn(%s) malloc buf failed.(CPU%d)\n", name, cpu_id);
        goto malloc_buf_err;
    }

    name_len = strlen(name) + 1;
    conn->id = id;

    conn->name = malloc(name_len);
    if (conn->name == NULL) {
        printf("CONN: conn(%s) malloc conn name failed.(CPU%d)\n", name, cpu_id);
        goto malloc_name_err;
    }
    memcpy(conn->name, name, name_len);

    ring_mem_init(&conn->ring, conn->conn_buf, buf_size);

    mutex_init(&conn->write_mutex);
    mutex_init(&conn->read_mutex);

    thread_waiter_init(&conn->write_wait);
    thread_waiter_init(&conn->read_wait);
    thread_waiter_init(&conn->write_complete_wait);
    thread_waiter_init(&conn->unbind_confirm_wait);

    ret = conn_send_notify(conn, CONN_bind);
    if (ret < 0) {
        printf("CONN: conn(%s) send bind notify failed.(CPU%d)\n", name, cpu_id);
        goto send_bind_err;
    }

    conn_table[id].this_side = conn;
    conn_table[id].this_side->name = conn->name;

unlock:
    mutex_unlock(&lock);

    return conn;

send_bind_err:
    free(conn->name);
malloc_name_err:
    free(conn->conn_buf);
malloc_buf_err:
    free(conn);

    conn = NULL;

    mutex_unlock(&lock);

    return conn;
}


static void conn_notify_process(void *data)
{
    int ret;
    struct conn_notify notify;
    int size = sizeof(notify);

    while (1) {
        ret = mailbox_receive(&notify, size, NOTIFY_TIMEOUT_MS);
        if (ret != size)
            continue;

        switch (notify.cmd) {
            case CONN_bind:
                conn_other_side_bind(&notify);
                break;
            case CONN_unbind:
                conn_other_side_unbind(&notify);
                break;
            case CONN_unbind_confirm:
                conn_this_side_unbind_confirm(&notify);
                break;
            case CONN_writeable:
                conn_wake_up_writeable(&notify);
                break;
            case CONN_readable:
                conn_wake_up_readable(&notify);
                break;
            default:
                break;
        }
    }
}

static void conn_init_thread(void *data)
{
    thread_waiter_init(&inited_wait);

    mailbox_init();

    thread_create("conn notify process", 4096, conn_notify_process, NULL);

    mutex_lock(&lock);

    conn_is_init = 1;

    mutex_unlock(&lock);

    thread_waiter_wakeup(&inited_wait);
}

void soc_conn_init(void)
{
    thread_create("conn init thread", 4096, conn_init_thread, NULL);
}