#include <common.h>
#include <driver/cache.h>
#include <ring_mem.h>
#include <os.h>

#include "u_audio.h"
#include "../../usb_lock.h"

#define UAC_NUM_REQUESTS        4

typedef enum {
    PCM_STREAM_PLAYBACK,
    PCM_STREAM_CAPTURE,
} pcm_stream_direction_t;

/* Runtime data params for one stream */
struct uac_rtd_params {
    struct snd_uac_chip *uac; /* parent chip */
    bool ep_enabled; /* if the ep is enabled */

    struct usb_ep *ep;

    pcm_stream_direction_t pcm_dir;

    unsigned int residue;

    unsigned int pktsize;
    unsigned int pktsize_residue;
    unsigned int framesize;
    u8 *data_buffer;
    unsigned int data_size;
    struct ring_mem ring;

    /* Requests */
    unsigned int req_size;
    struct usb_request *req[UAC_NUM_REQUESTS];
    u8 *req_buffer[UAC_NUM_REQUESTS];
    struct list_head req_free;

    spinlock_t lock;
};

struct snd_uac_chip {
    struct g_audio *audio_dev;

    struct uac_rtd_params p_prm;
    struct uac_rtd_params c_prm;

    u8 connect_flag;

    unsigned int ops_busy;
    u8 exit_flag;

    thread_waiter_t    exit_wait;
    thread_waiter_t    connect_wait;
    thread_waiter_t    playback_wait;
    thread_waiter_t    capture_wait;
};

static struct snd_uac_chip my_uac;

static void uac1_capture_data_to_usb(struct uac_rtd_params *prm)
{
    int ret;
    u32 pkt_len, data_len, residue;
    struct usb_request *req;

    while (!list_empty(&prm->req_free)) {
        residue = prm->residue + prm->pktsize_residue;

        if (residue / 1000 >= prm->framesize){
            residue -= prm->framesize * 1000;
            pkt_len = prm->pktsize + prm->framesize;
        } else {
            pkt_len = prm->pktsize;
        }

        data_len = ring_mem_readable_size(&prm->ring);
        if (data_len < pkt_len)
            break;

        prm->residue = residue;

        req = list_first_entry(&prm->req_free, struct usb_request, list);
        list_del(&req->list);

        req->zero = 0;
        req->length = pkt_len;
        ring_mem_read(&prm->ring, req->buf, req->length);

        ret = usb_ep_queue(prm->ep, req);
        if (ret < 0) {
            printf("%s: Failed to queue request (%d).\n", __func__, ret);
            list_add_tail(&req->list, &prm->req_free);
            break;
        }
    }

}

static void uac1_playback_start_req_free(struct uac_rtd_params *prm)
{
    int ret;
    struct usb_request *req;

    while (!list_empty(&prm->req_free)) {
        req = list_first_entry(&prm->req_free, struct usb_request, list);
        list_del(&req->list);

        req->zero = 0;
        req->length = prm->ep->maxpacket;
        ret = usb_ep_queue(prm->ep, req);
        if (ret < 0) {
            printf("%s: Failed to queue request (%d).\n", __func__, ret);
            list_add_tail(&req->list, &prm->req_free);
            break;
        }
    }
}

static void u_audio_iso_complete(struct usb_ep *ep, struct usb_request *req)
{
    unsigned long flags;
    struct uac_rtd_params *prm = req->context;

    /* i/f shutting down */
    if (!prm->ep_enabled)
        return;

    usb_spin_lock_irqsave(&prm->lock, flags);
    list_add_tail(&req->list, &prm->req_free);
    usb_spin_unlock_irqrestore(&prm->lock, flags);

    if (req->status == -ESHUTDOWN)
        return;

    if (req->status == -ECONNRESET)
        return;

    if (req->status == -ENODATA)
        return;

    /*
     * We can't really do much about bad xfers.
     * Afterall, the ISOCH xfers could fail legitimately.
     */
    if (req->status) {
        printf("%s: iso_complete dir(%d) status(%d) %d/%d\n",
            __func__, prm->pcm_dir, req->status, req->actual, req->length);
    }

    usb_spin_lock_irqsave(&prm->lock, flags);
    if (prm->pcm_dir == PCM_STREAM_CAPTURE) {
        uac1_capture_data_to_usb(prm);
    } else {
        if (req->actual > ring_mem_writable_size(&prm->ring)) {
            ring_mem_read(&prm->ring, NULL, req->actual);
            // printf("%s: playback overrun %d\n", __func__, req->actual);
        }
        ring_mem_write(&prm->ring, req->buf, req->actual);
        uac1_playback_start_req_free(prm);
    }
    usb_spin_unlock_irqrestore(&prm->lock, flags);

}

static void uac_prm_free_requests(struct uac_rtd_params *prm)
{
    unsigned int i;

    for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
        if (prm->req[i]) {
            usb_ep_free_request(prm->ep, prm->req[i]);
            prm->req[i] = NULL;
        }

        if (prm->req_buffer[i]) {
            free(prm->req_buffer[i]);
            prm->req_buffer[i] = NULL;
        }
    }

    if (prm->data_buffer) {
        free(prm->data_buffer);
        prm->data_buffer = NULL;
    }

    prm->req_size = 0;
}

static int uac_prm_alloc_requests(struct uac_rtd_params *prm, u32 req_size, u32 buffer_size_ms)
{
    unsigned int i;

    if (prm->req_size)
        return 0;

    for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
        prm->req_buffer[i] = cache_align_malloc(req_size);
        if (prm->req_buffer[i] == NULL)
            goto error;

        prm->req[i] = usb_ep_alloc_request(prm->ep);
        if (prm->req[i] == NULL)
            goto error;

        prm->req[i]->buf = prm->req_buffer[i];
        prm->req[i]->length = 0;
        prm->req[i]->context = prm;
        prm->req[i]->zero = 0;
        prm->req[i]->complete = u_audio_iso_complete;
    }

    prm->data_buffer = malloc(roundup_pow_of_two(req_size * buffer_size_ms));
    if (prm->data_buffer == NULL)
        goto error;

    prm->req_size = req_size;

    return 0;

error:
    uac_prm_free_requests(prm);
    return -ENOMEM;
}

int u_audio_start_playback(struct g_audio *audio_dev)
{
    struct snd_uac_chip *uac = audio_dev->uac;
    struct uac_rtd_params *prm = &uac->p_prm;
    unsigned long flags;

    if (prm->ep_enabled)
        return 0;

    config_ep_by_speed(audio_dev->gadget, &audio_dev->func, prm->ep);
    usb_ep_enable(prm->ep);

    usb_spin_lock_irqsave(&prm->lock, flags);
    prm->ep_enabled = true;
    prm->data_size = 0;
    usb_spin_unlock_irqrestore(&prm->lock, flags);

    return 0;
}

int u_audio_set_rate_playback(struct g_audio *audio_dev)
{
    struct snd_uac_chip *uac = audio_dev->uac;
    struct uac_rtd_params *prm = &uac->p_prm;
    struct uac1_params *params = &audio_dev->params;
    unsigned long flags;
    int i, ret;

    if (prm->ep_enabled) {
        usb_spin_lock_irqsave(&prm->lock, flags);

        for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
            if (prm->req[i])
                usb_ep_dequeue(prm->ep, prm->req[i]);
        }

        prm->framesize = params->p_ssize * num_channels(params->p_chmask);
        prm->pktsize = (params->p_srate / 1000) * prm->framesize;
        prm->pktsize_residue = (params->p_srate % 1000) * prm->framesize;
        prm->residue = 0;

        prm->data_size = min_t(u32, prm->pktsize * params->buffer_size_ms, prm->req_size * params->buffer_size_ms);
        /* ring mem size must be a power of 2 */
        prm->data_size = roundup_pow_of_two(prm->data_size);
        ring_mem_init(&prm->ring, prm->data_buffer, prm->data_size);

        INIT_LIST_HEAD(&prm->req_free);

        for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
            if (prm->req[i]) {
                prm->req[i]->length = prm->ep->maxpacket;
                prm->req[i]->zero = 0;
                ret = usb_ep_queue(prm->ep, prm->req[i]);
                if (ret < 0) {
                    printf("%s: Failed to queue request (%d).\n", __func__, ret);
                    list_add_tail(&prm->req[i]->list, &prm->req_free);
                }
            }
        }

        usb_spin_unlock_irqrestore(&prm->lock, flags);

        if (params->start_playback_callback)
            params->start_playback_callback(params->p_srate);

        thread_waiter_wakeup(&uac->capture_wait);
    }

    return 0;
}

void u_audio_stop_playback(struct g_audio *audio_dev)
{
    struct snd_uac_chip *uac = audio_dev->uac;
    struct uac_rtd_params *prm = &uac->p_prm;
    unsigned long flags;
    int i;

    if (!prm->ep_enabled)
        return;

    for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
        if (prm->req[i])
            usb_ep_dequeue(prm->ep, prm->req[i]);
    }

    usb_ep_disable(prm->ep);

    usb_spin_lock_irqsave(&prm->lock, flags);
    prm->ep_enabled = false;
    prm->data_size = 0;
    INIT_LIST_HEAD(&prm->req_free);
    usb_spin_unlock_irqrestore(&prm->lock, flags);

    if (audio_dev->params.stop_playback_callback)
        audio_dev->params.stop_playback_callback();

}

int u_audio_start_capture(struct g_audio *audio_dev)
{
    struct snd_uac_chip *uac = audio_dev->uac;
    struct uac_rtd_params *prm = &uac->c_prm;
    unsigned long flags;

    if (prm->ep_enabled)
        return 0;

    config_ep_by_speed(audio_dev->gadget, &audio_dev->func, prm->ep);
    usb_ep_enable(prm->ep);

    usb_spin_lock_irqsave(&prm->lock, flags);
    prm->ep_enabled = true;
    prm->data_size = 0;
    usb_spin_unlock_irqrestore(&prm->lock, flags);

    return 0;
}

int u_audio_set_rate_capture(struct g_audio *audio_dev)
{
    struct snd_uac_chip *uac = audio_dev->uac;
    struct uac_rtd_params *prm = &uac->c_prm;
    struct uac1_params *params = &audio_dev->params;
    unsigned long flags;
    int i;

    if (prm->ep_enabled) {
        usb_spin_lock_irqsave(&prm->lock, flags);

        for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
            if (prm->req[i])
                usb_ep_dequeue(prm->ep, prm->req[i]);
        }

        prm->framesize = params->c_ssize * num_channels(params->c_chmask);
        prm->pktsize = (params->c_srate / 1000) * prm->framesize;
        prm->pktsize_residue = (params->c_srate % 1000) * prm->framesize;
        prm->residue = 0;

        prm->data_size = min_t(u32, prm->pktsize * params->buffer_size_ms, prm->req_size * params->buffer_size_ms);
        /* ring mem size must be a power of 2 */
        prm->data_size = roundup_pow_of_two(prm->data_size);
        ring_mem_init(&prm->ring, prm->data_buffer, prm->data_size);

        INIT_LIST_HEAD(&prm->req_free);

        for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
            if (prm->req[i])
                list_add_tail(&prm->req[i]->list, &prm->req_free);
        }

        usb_spin_unlock_irqrestore(&prm->lock, flags);

        if (params->start_capture_callback)
            params->start_capture_callback(params->c_srate);

        thread_waiter_wakeup(&uac->capture_wait);
    }

    return 0;
}

void u_audio_stop_capture(struct g_audio *audio_dev)
{
    struct snd_uac_chip *uac = audio_dev->uac;
    struct uac_rtd_params *prm = &uac->c_prm;
    unsigned long flags;
    int i;

    if (!prm->ep_enabled)
        return;

    for (i = 0; i < UAC_NUM_REQUESTS; ++i) {
        if (prm->req[i])
            usb_ep_dequeue(prm->ep, prm->req[i]);
    }

    usb_ep_disable(prm->ep);

    usb_spin_lock_irqsave(&prm->lock, flags);
    prm->ep_enabled = false;
    prm->data_size = 0;
    INIT_LIST_HEAD(&prm->req_free);
    usb_spin_unlock_irqrestore(&prm->lock, flags);

    if (audio_dev->params.stop_capture_callback)
        audio_dev->params.stop_capture_callback();
}

void u_audio_connect_status_update(struct g_audio *audio_dev, u8 connect)
{
    struct snd_uac_chip *uac = audio_dev->uac;

    if (uac->connect_flag != connect) {
        uac->connect_flag = connect;

        if (audio_dev->params.connect_cb)
            audio_dev->params.connect_cb(connect);

        if (connect)
            thread_waiter_wakeup(&uac->connect_wait);
    }
}

/* ------------------------------------------------- */

int g_audio_setup(struct g_audio *g_audio)
{
    int ret;
    struct uac1_params *params = &g_audio->params;

    os_enter_critical();

    g_audio->uac = &my_uac;
    my_uac.audio_dev = g_audio;

    my_uac.p_prm.pcm_dir = PCM_STREAM_PLAYBACK;
    my_uac.p_prm.uac = &my_uac;
    my_uac.p_prm.ep = g_audio->out_ep;

    my_uac.c_prm.pcm_dir = PCM_STREAM_CAPTURE;
    my_uac.c_prm.uac = &my_uac;
    my_uac.c_prm.ep = g_audio->in_ep;

    my_uac.exit_flag = 0;
    my_uac.connect_flag = 0;
    my_uac.ops_busy = 0;

    spin_lock_init_recursive(&my_uac.p_prm.lock);
    spin_lock_init_recursive(&my_uac.c_prm.lock);

    thread_waiter_init(&my_uac.connect_wait);
    thread_waiter_init(&my_uac.exit_wait);
    thread_waiter_init(&my_uac.playback_wait);
    thread_waiter_init(&my_uac.capture_wait);

    if (params->p_chmask != 0) {
        ret = uac_prm_alloc_requests(&my_uac.p_prm, g_audio->out_ep_maxpsize, params->buffer_size_ms);
        if (ret)
            goto err_alloc_req;
    }

    if (params->c_chmask != 0) {
        ret = uac_prm_alloc_requests(&my_uac.c_prm, g_audio->in_ep_maxpsize, params->buffer_size_ms);
        if (ret)
            goto err_alloc_req;
    }

    os_exit_critical();
    return 0;

err_alloc_req:
    uac_prm_free_requests(&my_uac.c_prm);
    uac_prm_free_requests(&my_uac.p_prm);
    os_exit_critical();
    return ret;

}

void g_audio_cleanup(struct g_audio *g_audio)
{
    if (!g_audio || !g_audio->uac)
        return;

    os_enter_critical();

    assert(my_uac.audio_dev);
    assert(!my_uac.exit_flag);
    my_uac.exit_flag = 1;

    while (my_uac.ops_busy) {
        thread_waiter_wakeup(&my_uac.connect_wait);
        thread_waiter_wakeup(&my_uac.playback_wait);
        thread_waiter_wakeup(&my_uac.capture_wait);
        os_exit_critical();
        thread_waiter_wait(&my_uac.exit_wait);
        os_enter_critical();
    }

    uac_prm_free_requests(&my_uac.c_prm);
    uac_prm_free_requests(&my_uac.p_prm);

    g_audio->uac = NULL;
    my_uac.audio_dev = NULL;
    my_uac.c_prm.ep = NULL;
    my_uac.p_prm.ep = NULL;
    my_uac.connect_flag = 0;

    os_exit_critical();
}

/* ------------------------------------------------- */

static int check_set_busy(void)
{
    os_enter_critical();

    if (my_uac.audio_dev == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    my_uac.ops_busy++;

    os_exit_critical();

    return 0;
}

static void set_no_busy(void)
{
    os_enter_critical();

    if (my_uac.audio_dev) {
        my_uac.ops_busy--;
        if (my_uac.exit_flag)
            thread_waiter_wakeup(&my_uac.exit_wait);
    }

    os_exit_critical();
}

int gadget_uac1_playback_read(u8 *buffer, u32 len)
{
    int ret;
    unsigned long flags;
    struct uac_rtd_params *prm = &my_uac.p_prm;

    if (buffer == NULL || len == 0)
        return -EINVAL;

    if (check_set_busy())
        return -ENODEV;

    if (my_uac.connect_flag == 0) {
        ret = -ENOLINK;
        goto out;
    }

    if (prm->ep_enabled == false || prm->data_size == 0) {
        ret = -ENOTCONN;
        goto out;
    }

    usb_spin_lock_irqsave(&prm->lock, flags);
    ret = ring_mem_read(&prm->ring, buffer, len);

    uac1_playback_start_req_free(prm);
    usb_spin_unlock_irqrestore(&prm->lock, flags);

out:
    set_no_busy();
    return ret;
}

int gadget_uac1_capture_write(u8 *buffer, u32 len)
{
    int ret;
    unsigned long flags;
    struct uac_rtd_params *prm = &my_uac.c_prm;

    if (buffer == NULL || len == 0)
        return -EINVAL;

    if (check_set_busy())
        return -ENODEV;

    if (my_uac.connect_flag == 0) {
        ret = -ENOLINK;
        goto out;
    }

    if (prm->ep_enabled == false || prm->data_size == 0) {
        ret = -ENOTCONN;
        goto out;
    }

    usb_spin_lock_irqsave(&prm->lock, flags);
    ret = ring_mem_write(&prm->ring, buffer, len);

    uac1_capture_data_to_usb(prm);
    usb_spin_unlock_irqrestore(&prm->lock, flags);

out:
    set_no_busy();
    return ret;
}

int gadget_uac1_get_connect_status(void)
{
    int status;

    os_enter_critical();

    if (my_uac.audio_dev == NULL) {
        os_exit_critical();
        printf("%s: device not initialized\n", __func__);
        return -ENODEV;
    }

    status = my_uac.connect_flag;

    os_exit_critical();

    return status;
}

int gadget_uac1_wait_connect(u32 timeout_ms)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    while (my_uac.connect_flag == 0) {
        ret = thread_waiter_wait_timeout(&my_uac.connect_wait, timeout_ms);
        if (my_uac.exit_flag) {
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

int gadget_uac1_wait_playback_start(u32 timeout_ms, u32 *rate)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    while (my_uac.p_prm.ep_enabled == false || my_uac.p_prm.data_size == 0) {
        ret = thread_waiter_wait_timeout(&my_uac.playback_wait, timeout_ms);

        if (my_uac.exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    if (rate)
        *rate = my_uac.audio_dev->params.p_srate;

out:
    set_no_busy();
    return ret;
}

int gadget_uac1_wait_capture_start(u32 timeout_ms, u32 *rate)
{
    int ret = 0;

    if (check_set_busy())
        return -ENODEV;

    while (my_uac.c_prm.ep_enabled == false || my_uac.c_prm.data_size == 0) {
        ret = thread_waiter_wait_timeout(&my_uac.capture_wait, timeout_ms);
        if (my_uac.exit_flag) {
            ret = -ENODEV;
            goto out;
        }

        if (ret) {
            ret = -ETIMEDOUT;
            goto out;
        }
    }

    if (rate)
        *rate = my_uac.audio_dev->params.c_srate;

out:
    set_no_busy();
    return ret;
}
