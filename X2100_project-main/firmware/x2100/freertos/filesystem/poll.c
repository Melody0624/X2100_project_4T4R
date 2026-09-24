#include <stdint.h>


#include <dfs.h>
#include <dfs_file.h>
#include <dfs_posix.h>
#include <dfs_poll.h>


/*
 * add poll wait queue
 * wait_queue_t
 */
struct wqueue_node;
struct poll_node;
struct pollreq;

typedef void (*poll_queue_proc)(wait_queue_t *, struct pollreq *);
typedef int (*wqueue_func_t)(struct wqueue_node *wait, void *key);

extern void wake_up_nr(wait_queue_t *wq, int nr);
extern int wait_event_timeout(uint32_t timeout_ms);
extern void wait_queue_add_current(wait_queue_t *wq);
extern thread_ptr_t wait_queue_del(wait_queue_t *wq);

typedef struct pollreq
{
    poll_queue_proc _proc;
    short _key;
} pollreq_t;


struct wqueue_node
{
    thread_ptr_t polling_thread;
    wait_queue_t   wait_queue;

    wqueue_func_t wakeup;
    uint32_t key;
};


struct poll_table
{
    pollreq_t req;
    uint32_t triggered; /* the waited thread whether triggered */
    thread_ptr_t polling_thread;
    struct poll_node *nodes;
};

struct poll_node
{
    struct wqueue_node wqn;
    struct poll_table *pt;
    struct poll_node *next;
};

#if 0
static void poll_add(wait_queue_t *wq, pollreq_t *req)
{
    if (req && req->_proc && wq)
    {
        req->_proc(wq, req);
    }
}
#endif

static int __wqueue_pollwake(struct wqueue_node *wait, void *key)
{
    struct poll_node *pn;

    if (key && !((unsigned long)key & wait->key))
        return -1;

    pn = container_of(wait, struct poll_node, wqn);
    wake_up_nr(&(pn->wqn.wait_queue), 1);

    return 0;
}

static void _poll_add(wait_queue_t *wq, pollreq_t *req)
{
    struct poll_table *pt;
    struct poll_node *node;

    node = malloc(sizeof(struct poll_node));
    if (node == NULL)
        return;

    pt = container_of(req, struct poll_table, req);

    node->wqn.key = req->_key;
    INIT_LIST_HEAD(&(node->wqn.wait_queue));
    node->wqn.polling_thread = pt->polling_thread;
    node->wqn.wakeup = __wqueue_pollwake;
    node->next = pt->nodes;
    node->pt = pt;
    pt->nodes = node;
    wait_queue_add_current(&(pt->nodes->wqn.wait_queue));
}

static void poll_table_init(struct poll_table *pt)
{
    pt->req._proc = _poll_add;
    pt->triggered = 0;
    pt->nodes = NULL;
    pt->polling_thread = thread_get_current();
}

static int poll_wait_timeout(struct poll_table *pt, int msec)
{
    int ret = 0;

    assert(!os_in_handler_mode());

    assert(os_is_enter_critical());

    /*
     * =0:  wakeup
     * =-1: timeout
     */
    ret = wait_event_timeout(msec);

    return ret;
}

static int do_pollfd(struct pollfd *pollfd, pollreq_t *req)
{
    int mask = 0;
    int fd;

    fd = pollfd->fd;

    if (fd >= 0)
    {
        struct dfs_fd *f = fd_get(fd);
        mask = POLLNVAL;

        if (f)
        {
            mask = POLLMASK_DEFAULT;
            if (f->fops->poll)
            {
                req->_key = pollfd->events | POLLERR | POLLHUP;

                mask = f->fops->poll(f, req);
            }
            /* Mask out unneeded events. */
            mask &= pollfd->events | POLLERR | POLLHUP;
            fd_put(f);
        }
    }
    pollfd->revents = mask;

    return mask;
}

static int poll_do(struct pollfd *fds, nfds_t nfds, struct poll_table *pt, int msec)
{
    int num;
    int istimeout = 0;
    int n;
    struct pollfd *pf;

    if (msec == 0)
    {
        pt->req._proc = NULL;
        istimeout = 1;
    }

    while (1)
    {
        pf = fds;
        num = 0;

        for (n = 0; n < nfds; n ++)
        {
            if (do_pollfd(pf, &pt->req))
            {
                num ++;
                pt->req._proc = NULL;
            }
            pf ++;
        }

        pt->req._proc = NULL;

        if (num || istimeout)
            break;

        if (poll_wait_timeout(pt, msec) < 0)
            istimeout = 1;
    }

    return num;
}

static void poll_teardown(struct poll_table *pt)
{
    struct poll_node *node, *next;

    next = pt->nodes;
    while (next)
    {
        node = next;
        wait_queue_del(&(node->wqn.wait_queue));
        next = node->next;
        free(node);
    }
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    int num;
    struct poll_table table;

    poll_table_init(&table);

    num = poll_do(fds, nfds, &table, timeout);

    poll_teardown(&table);

    return num;
}

