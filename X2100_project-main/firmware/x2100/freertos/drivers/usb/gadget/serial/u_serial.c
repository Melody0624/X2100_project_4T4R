#include <common.h>
#include <driver/cache.h>
#include <sys/errno.h>
#include <os.h>

#include "u_serial.h"
#include "../../usb_lock.h"

/* RX and TX queues can buffer QUEUE_SIZE packets before they hit the
 * next layer of buffering.  For TX that's a circular buffer; for RX
 * consider it a NOP.  A third layer is provided by the TTY code.
 */
#define QUEUE_SIZE		8

/*
 * The port structure holds info for each port, one for each minor number
 * (and thus for each /dev/ node).
 */
struct gs_port {
	spinlock_t		port_lock;	/* guard port_* access */
	struct gserial		*port_usb;

	struct list_head	read_pool;
	int read_started;
	int read_allocated;
	struct list_head	read_queue;
	uint32_t	n_read;

	struct list_head	write_pool;
	int write_started;
	int write_allocated;
	struct list_head	write_queue;

	/* REVISIT this state ... */
	struct usb_cdc_serial_param serial_param;	/* 8-N-1 etc */

	thread_waiter_t		read_wait;
	thread_waiter_t		write_wait;

	uint32_t 			ops_busy;
	uint8_t		exit_flag;
	thread_waiter_t	exit_wait;
	thread_waiter_t	connect_wait;

	connect_callback_t connect_cb;
	serial_param_callback_t serial_cb;
};

static struct gs_port *my_ports;
/*
 * DTR状态标志位
 *
 * 在Linux中，使用串口工具连接该设备时，出现了乱码接收问题。
 * 这种情况在USB CDC的接收任务同时运行的过程中，PC端软件连接该设备时，出现概率较高。
 *
 * 具体原因如下：
 * 由于之前未检查该位（ACM_CTRL_DTR），导致在主机尚未准备好（DTR=0）时设备就持续发送，
 * 从而引发端点冲突和乱码。
 * 暂时性的解决方法（应用层发送前检查DTR状态）
 *
 * Linux 主机 ：连接时会将 DTR 设置为1，断开时会将为0
 *             如果收发一起进行，刚连接时会出现脏数据
 * Windows 主机 ：无论连接与否，DTR 都会保持为0（部分CDC驱动）
 *
 */
static volatile int g_dtr_state = 0; // 0: DTR未置位, 1: 已置位
/*-------------------------------------------------------------------------*/

/* I/O glue between TTY (upper) and USB function (lower) driver layers */

/*
 * gs_alloc_req
 *
 * Allocate a usb_request and its buffer.  Returns a pointer to the
 * usb_request or NULL if there is an error.
 */
struct usb_request *gs_alloc_req(struct usb_ep *ep, unsigned len)
{
	struct usb_request *req;

	req = usb_ep_alloc_request(ep);

	if (req != NULL) {
		req->length = len;
		req->buf = cache_align_malloc(len);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

/*
 * gs_free_req
 *
 * Free a usb_request and its buffer.
 */
void gs_free_req(struct usb_ep *ep, struct usb_request *req)
{
	free(req->buf);
	usb_ep_free_request(ep, req);
}


/*
 * Context: caller owns port_lock, and port_usb is set
 */
static unsigned gs_start_rx(struct gs_port *port)
/*
__releases(&port->port_lock)
__acquires(&port->port_lock)
*/
{
	struct list_head	*pool = &port->read_pool;
	struct usb_ep		*out = port->port_usb->out;

	while (!list_empty(pool)) {
		struct usb_request	*req;
		int			status;

		if (port->read_started >= QUEUE_SIZE)
			break;

		req = list_entry(pool->next, struct usb_request, list);
		list_del(&req->list);
		req->length = out->maxpacket;

		/* drop lock while we call out; the controller driver
		 * may need to call us back (e.g. for disconnect)
		 */
		status = usb_ep_queue(out, req);
		if (status) {
			printf("%s: %s %s err %d\n", __func__, "queue", out->name, status);
			list_add(&req->list, pool);
			break;
		}
		port->read_started++;
	}
	return port->read_started;
}

static void gs_read_complete(struct usb_ep *ep, struct usb_request *req)
{
	unsigned long flags;
	struct gs_port	*port = ep->driver_data;

	/* Queue all received data until the tty layer is ready for it. */
	usb_spin_lock_irqsave(&port->port_lock, flags);
	list_add_tail(&req->list, &port->read_queue);
	usb_spin_unlock_irqrestore(&port->port_lock, flags);
	thread_waiter_wakeup(&port->read_wait);
}

static void gs_write_complete(struct usb_ep *ep, struct usb_request *req)
{
	unsigned long flags;
	struct gs_port	*port = ep->driver_data;

	usb_spin_lock_irqsave(&port->port_lock, flags);
	list_move(&req->list, &port->write_pool);
	port->write_started--;
	thread_waiter_wakeup(&port->write_wait);

	switch (req->status) {
	default:
		/* presumably a transient fault */
		printf("%s: unexpected %s status %d\n", __func__, ep->name, req->status);
		/* FALL THROUGH */
	case 0:
		/* normal completion */
		break;

	case -ESHUTDOWN:
		/* disconnect */
		printf("%s: %s shutdown\n", __func__, ep->name);
		break;
	}

	usb_spin_unlock_irqrestore(&port->port_lock, flags);
}

static void gs_free_requests(struct usb_ep *ep, struct list_head *head,
							 int *allocated)
{
	struct usb_request	*req;

	while (!list_empty(head)) {
		req = list_entry(head->next, struct usb_request, list);
		list_del(&req->list);
		gs_free_req(ep, req);
		if (allocated)
			(*allocated)--;
	}
}

static int gs_alloc_requests(struct usb_ep *ep, struct list_head *head,
		void (*fn)(struct usb_ep *, struct usb_request *), int *allocated)
{
	int			i;
	struct usb_request	*req;
	int n = allocated ? QUEUE_SIZE - *allocated : QUEUE_SIZE;

	/* Pre-allocate up to QUEUE_SIZE transfers, but if we can't
	 * do quite that many this time, don't fail ... we just won't
	 * be as speedy as we might otherwise be.
	 */
	for (i = 0; i < n; i++) {
		req = gs_alloc_req(ep, ep->maxpacket);
		if (!req)
			return list_empty(head) ? -ENOMEM : 0;

		req->complete = fn;
		list_add_tail(&req->list, head);
		if (allocated)
			(*allocated)++;
	}
	return 0;
}

/**
 * gs_start_io - start USB I/O streams
 * @dev: encapsulates endpoints to use
 * Context: holding port_lock; port_tty and port_usb are non-null
 *
 * We only start I/O when something is connected to both sides of
 * this port.  If nothing is listening on the host side, we may
 * be pointlessly filling up our TX buffers and FIFO.
 */
static int gs_start_io(struct gs_port *port)
{
	struct list_head	*head = &port->read_pool;
	struct usb_ep		*ep = port->port_usb->out;
	int			status;
	unsigned		started;

	/* Allocate RX and TX I/O buffers.  We can't easily do this much
	 * earlier because the requests are coupled to
	 * endpoints, as are the packet sizes we'll be using.  Different
	 * configurations may use different endpoints with a given port;
	 * and high speed vs full speed changes packet sizes too.
	 */
	status = gs_alloc_requests(ep, head, gs_read_complete, &port->read_allocated);
	if (status)
		return status;

	status = gs_alloc_requests(port->port_usb->in, &port->write_pool,
			gs_write_complete, &port->write_allocated);
	if (status) {
		gs_free_requests(ep, head, &port->read_allocated);
		return status;
	}

	/* queue read requests */
	port->n_read = 0;
	started = gs_start_rx(port);

	/* unblock any pending writes into our circular buffer */
	if (!started) {
		gs_free_requests(ep, head, &port->read_allocated);
		gs_free_requests(port->port_usb->in, &port->write_pool,
			&port->write_allocated);
		status = -EIO;
	}

	return status;
}

void gserial_free_line(void)
{
	os_enter_critical();
	assert(my_ports);
	assert(!my_ports->exit_flag);
	my_ports->exit_flag = 1;

	while (my_ports->ops_busy) {
		thread_waiter_wakeup(&my_ports->read_wait);
		thread_waiter_wakeup(&my_ports->write_wait);
		thread_waiter_wakeup(&my_ports->connect_wait);
		os_exit_critical();
		thread_waiter_wait(&my_ports->exit_wait);
		os_enter_critical();
	}
	free(my_ports);
	my_ports = NULL;
	os_exit_critical();
}

int gserial_alloc_line(struct usb_cdc_serial_param *serial_param, connect_callback_t connect_cb, serial_param_callback_t serial_cb)
{
	int		ret = 0;

	assert(serial_param);

	os_enter_critical();

	assert(my_ports == NULL);

	my_ports = malloc(sizeof(struct gs_port));
	if (my_ports == NULL) {
		os_exit_critical();
		printf("%s: out of memory\n", __func__);
		return -ENOMEM;
	}
	memset(my_ports, 0, sizeof(struct gs_port));

	spin_lock_init_recursive(&my_ports->port_lock);
	thread_waiter_init(&my_ports->write_wait);
	thread_waiter_init(&my_ports->read_wait);
	thread_waiter_init(&my_ports->connect_wait);
	thread_waiter_init(&my_ports->exit_wait);

	INIT_LIST_HEAD(&my_ports->read_pool);
	INIT_LIST_HEAD(&my_ports->read_queue);
	INIT_LIST_HEAD(&my_ports->write_pool);
	INIT_LIST_HEAD(&my_ports->write_queue);

	my_ports->serial_param = *serial_param;
	my_ports->connect_cb = connect_cb;
	my_ports->serial_cb = serial_cb;

	os_exit_critical();

	return ret;
}

void gs_port_update_conding(struct usb_cdc_line_coding *coding)
{
	os_enter_critical();

	my_ports->serial_param.dwDTERate = coding->dwDTERate;
	my_ports->serial_param.bCharFormat = coding->bCharFormat;
	my_ports->serial_param.bParityType = coding->bParityType;
	my_ports->serial_param.bDataBits = coding->bDataBits;

	os_exit_critical();

	if (my_ports->serial_cb)
		my_ports->serial_cb(&my_ports->serial_param);
}

int gadget_serial_is_dtr_set(void)
{
	return g_dtr_state;
}

void gadget_serial_dtr_changed(int new_state)
{
	os_enter_critical();

	g_dtr_state = new_state;

	if (new_state)
	{
		// DTR 置位，唤醒可能等待的读写任务
		if (my_ports)
		{
			thread_waiter_wakeup(&my_ports->write_wait);
			thread_waiter_wakeup(&my_ports->read_wait);
		}
	}

	os_exit_critical();
}

/**
 * gserial_connect - notify TTY I/O glue that USB link is active
 * @gser: the function, set up with endpoints and descriptors
 * Context: any (usually from irq)
 *
 * This is called activate endpoints and let the TTY layer know that
 * the connection is active ... not unlike "carrier detect".  It won't
 * necessarily start I/O queues; unless the TTY is held open by any
 * task, there would be no point.  However, the endpoints will be
 * activated so the USB host can perform I/O, subject to basic USB
 * hardware flow control.
 *
 * Returns negative errno or zero.
 * On success, ep->driver_data will be overwritten.
 */
int gserial_connect(struct gserial *gser)
{
	int		status;
	unsigned long	flags;

	assert(my_ports);

	if (my_ports->port_usb) {
		printf("usb serial line is in use.\n");
		return -EBUSY;
	}

	/* activate the endpoints */
	status = usb_ep_enable(gser->in);
	if (status < 0)
		return status;

	status = usb_ep_enable(gser->out);
	if (status < 0)
		goto fail_out;

	/* then tell the tty glue that I/O can work */
	usb_spin_lock_irqsave(&my_ports->port_lock, flags);
	gser->in->driver_data = my_ports;
	gser->out->driver_data = my_ports;

	gser->ioport = my_ports;
	my_ports->port_usb = gser;

	/* REVISIT if waiting on "carrier detect", signal. */

	/* if it's already open, start I/O ... and notify the serial
	 * protocol about open/close status (connect/disconnect).
	 */
	gs_start_io(my_ports);

	if (gser->connect)
		gser->connect(gser);

	usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);

	if (my_ports->connect_cb)
		my_ports->connect_cb(1);

	thread_waiter_wakeup(&my_ports->connect_wait);
	thread_waiter_wakeup(&my_ports->write_wait);

	return 0;

fail_out:
	usb_ep_disable(gser->in);
	return status;
}
/**
 * gserial_disconnect - notify TTY I/O glue that USB link is inactive
 * @gser: the function, on which gserial_connect() was called
 * Context: any (usually from irq)
 *
 * This is called to deactivate endpoints and let the TTY layer know
 * that the connection went inactive ... not unlike "hangup".
 *
 * On return, the state is as if gserial_connect() had never been called;
 * there is no active USB I/O on these endpoints.
 */
void gserial_disconnect(struct gserial *gser)
{
	struct gs_port	*port = gser->ioport;
	unsigned long	flags;

	if (!port)
		return;

	/* tell the TTY glue not to do I/O here any more */
	usb_spin_lock_irqsave(&port->port_lock, flags);

	/* REVISIT as above: how best to track this? */
	port->port_usb = NULL;
	gser->ioport = NULL;

	/* disable endpoints, aborting down any active I/O */
	usb_ep_disable(gser->out);
	usb_ep_disable(gser->in);

	/* finally, free any unused/unusable I/O buffers */
	gs_free_requests(gser->out, &port->read_pool, NULL);
	gs_free_requests(gser->out, &port->read_queue, NULL);
	gs_free_requests(gser->in, &port->write_pool, NULL);

	port->read_allocated = port->read_started =
		port->write_allocated = port->write_started = 0;

	if (gser->disconnect)
		gser->disconnect(gser);

	usb_spin_unlock_irqrestore(&port->port_lock, flags);

	if (my_ports->connect_cb)
		my_ports->connect_cb(0);

	thread_waiter_wakeup(&my_ports->write_wait);
	thread_waiter_wakeup(&my_ports->read_wait);

	gadget_serial_dtr_changed(0);
}

/*-------------------------------------------------------------------------*/

static int check_set_busy(void)
{
	os_enter_critical();

	if (my_ports == NULL) {
		os_exit_critical();
		printf("%s: device not initialized\n", __func__);
		return -ENODEV;
	}

	my_ports->ops_busy++;

	os_exit_critical();

	return 0;
}

static void set_no_busy(void)
{
	os_enter_critical();

	if (my_ports) {
		my_ports->ops_busy--;
		if (my_ports->exit_flag)
			thread_waiter_wakeup(&my_ports->exit_wait);
	}

	os_exit_critical();
}

int gadget_serial_write(const uint8_t *buf, uint32_t count, uint32_t block, uint32_t timeout_ms)
{
	int ret;
	struct usb_ep *in;
	struct list_head	*pool;
	unsigned long	flags;
	uint32_t len = 0;

	if (!count || buf == NULL)
		return 0;

	if (check_set_busy())
		return -ENODEV;

	usb_spin_lock_irqsave(&my_ports->port_lock, flags);

	if (!my_ports->port_usb) {
		ret = -ENOLINK;
		goto out;
	}

	pool = &my_ports->write_pool;

	while (list_empty(pool)) {
		if (!block) {
			ret = -EAGAIN;
			goto out;
		}

		usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
		ret = thread_waiter_wait_timeout(&my_ports->write_wait, timeout_ms);
		usb_spin_lock_irqsave(&my_ports->port_lock, flags);

		if (my_ports->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (!my_ports->port_usb) {
			ret = -ENOLINK;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	in = my_ports->port_usb->in;

	while ((!list_empty(pool)) && count) {
		struct usb_request	*req;

		if (my_ports->write_started >= QUEUE_SIZE)
			break;

		req = list_entry(pool->next, struct usb_request, list);
		if (count > in->maxpacket) {
			req->length = in->maxpacket;
			req->zero = 0;
		} else {
			req->length = count;
			req->zero = 1;
		}

		memcpy(req->buf, buf + len, req->length);

		ret = usb_ep_queue(in, req);
		if (ret) {
			printf("%s: %s %s err %d\n", __func__, "queue", in->name, ret);
			break;
		}

		count -= req->length;
		len += req->length;
		list_move(&req->list, &my_ports->write_queue);
		my_ports->write_started++;
	}

	ret = len;
out:
	usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
	set_no_busy();
	return ret;
}

void gadget_serial_reset_read_wait(void)
{
	if (my_ports) {
		thread_waiter_wakeup(&my_ports->read_wait);
		thread_waiter_init(&my_ports->read_wait);
	}
}

void gadget_serial_reset_write_wait(void)
{
    unsigned long flags;
    struct list_head *queue = &my_ports->write_queue;
    struct list_head *pool = &my_ports->write_pool;
    struct usb_request *req, *tmp;

    if (!my_ports) {
        printf("gadget_serial: no port to reset write wait\n");
        return;
    }

    usb_spin_lock_irqsave(&my_ports->port_lock, flags);

    struct usb_ep *in = my_ports->port_usb ? my_ports->port_usb->in : NULL;

    /* 1. 将 write_queue 中未完成的请求移回 write_pool */
    list_for_each_entry_safe(req, tmp, queue, list) {
        if (in)
            usb_ep_dequeue(in, req);      // 尝试从硬件队列移除
        list_del(&req->list);
        list_add_tail(&req->list, pool);
        if (my_ports->write_started > 0)
            my_ports->write_started--;
    }

    /* 2. 如果 write_pool 仍为空，则重新分配请求 */
    if (list_empty(pool) && in) {
        int allocated = 0;
        // 分配 QUEUE_SIZE 个请求（与 gs_start_io 中一致）
        gs_alloc_requests(in, pool, gs_write_complete, &allocated);
        my_ports->write_allocated += allocated;
        if (allocated == 0) {
            printf("gadget_serial: WARNING: failed to allocate write requests!\n");
        } else {
            printf("gadget_serial: allocated %d write requests\n", allocated);
        }
    }

	thread_waiter_wakeup(&my_ports->write_wait);
    /* 3. 重置等待状态 */
    thread_waiter_init(&my_ports->write_wait);

    usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
}

int gadget_serial_read(uint8_t *buf, uint32_t count, uint8_t block, uint32_t timeout_ms)
{
	int ret;
	uint32_t len = 0;
	unsigned long	flags;
	bool			disconnect = false;

	if (!count || buf == NULL)
		return 0;

	if (check_set_busy())
		return -ENODEV;

	usb_spin_lock_irqsave(&my_ports->port_lock, flags);

	if (!my_ports->port_usb) {
		ret = -ENOLINK;
		goto out;
	}

	while (list_empty(&my_ports->read_queue)) {
		if (!block) {
			ret = -EAGAIN;
			goto out;
		}

		usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
		ret = thread_waiter_wait_timeout(&my_ports->read_wait, timeout_ms);
		usb_spin_lock_irqsave(&my_ports->port_lock, flags);

		if (my_ports->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (!my_ports->port_usb) {
			ret = -ENOLINK;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	while ((!list_empty(&my_ports->read_queue)) && count) {
		struct usb_request	*req;

		req = list_first_entry(&my_ports->read_queue, struct usb_request, list);

		switch (req->status) {
		case -ESHUTDOWN:
			disconnect = true;
			printf("%s: usb serial shutdown\n", __func__);
			break;

		default:
			/* presumably a transient fault */
			printf("usb serial unexpected RX status %d\n", req->status);
			/* FALLTHROUGH */
		case 0:
			/* normal completion */
			break;
		}

		/* push data */
		if (req->actual) {
			char *packet = req->buf + my_ports->n_read;
			uint32_t size = req->actual - my_ports->n_read;

			if (count < size) {
				memcpy(buf + len, packet, count);
				my_ports->n_read += count;
				len += count;
				thread_waiter_wakeup(&my_ports->read_wait);
				break;
			} else {
				memcpy(buf + len, packet, size);
				my_ports->n_read = 0;
				count -= size;
				len += size;
			}
		}

		list_move(&req->list, &my_ports->read_pool);
		my_ports->read_started--;
	}

	ret = len;

	/* If we're still connected, refill the USB RX queue. */
	if (!disconnect && my_ports->port_usb)
		gs_start_rx(my_ports);

out:
	usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
	set_no_busy();
	return ret;
}

int gadget_serial_break_ctl(int duration)
{
	unsigned long	flags;
	int		status = 0;

	if (check_set_busy())
		return -ENODEV;

	usb_spin_lock_irqsave(&my_ports->port_lock, flags);

	if (!my_ports->port_usb) {
		status = -ENOLINK;
		goto out;
	}

	if (my_ports->port_usb->send_break)
		status = my_ports->port_usb->send_break(my_ports->port_usb, duration);

out:
	usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
	set_no_busy();
	return status;
}

int gadget_serial_flush_chars(uint32_t timeout_ms)
{
	int ret = 0;

	if (check_set_busy())
		return -ENODEV;

	if (!my_ports->port_usb) {
		ret = -ENOLINK;
		goto out;
	}

	while (my_ports->write_started) {
		ret = thread_waiter_wait_timeout(&my_ports->write_wait, timeout_ms);

		if (my_ports->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (!my_ports->port_usb) {
			ret = -ENOLINK;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

out:
	set_no_busy();
	return ret;
}

int gadget_serial_clean_write(void)
{
	unsigned long	flags;
	int ret = 0;

	if (check_set_busy())
		return -ENODEV;

	usb_spin_lock_irqsave(&my_ports->port_lock, flags);

	if (!my_ports->port_usb) {
		ret = -ENOLINK;
		goto out;
	}

	if (my_ports->write_started) {
		while (!list_empty(&my_ports->write_queue)) {
			struct usb_request	*req;

			req = list_first_entry(&my_ports->write_queue, struct usb_request, list);

			ret = usb_ep_dequeue(my_ports->port_usb->in, req);
			if (ret) {
				printf("%s: usb_ep_dequeue err %d\n", __func__, ret);
				break;
			}
		}
	}

out:
	usb_spin_unlock_irqrestore(&my_ports->port_lock, flags);
	set_no_busy();
	return ret;
}

int gadget_serial_get_connect_status(void)
{
	int status;

	os_enter_critical();

	if (my_ports == NULL) {
		os_exit_critical();
		printf("%s: device not initialized\n", __func__);
		return -ENODEV;
	}

	if (my_ports->port_usb)
		status = 1;
	else
		status = 0;

	os_exit_critical();

	return status;
}

int gadget_serial_wait_connect(uint32_t timeout_ms)
{
	int ret = 0;

	if (check_set_busy())
		return -ENODEV;

	while (my_ports->port_usb == NULL) {
		ret = thread_waiter_wait_timeout(&my_ports->connect_wait, timeout_ms);

		if (my_ports->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

out:
	set_no_busy();
	return ret;
}

int gadget_serial_get_param(struct usb_cdc_serial_param *cdc_param)
{
	assert(cdc_param);

	os_enter_critical();

	if (my_ports == NULL) {
		os_exit_critical();
		printf("Serial port parameters have not been set\n");
		return -ENODEV;
	}

	memcpy(cdc_param, &my_ports->serial_param, sizeof(struct usb_cdc_serial_param));

	os_exit_critical();
	return 0;
}
