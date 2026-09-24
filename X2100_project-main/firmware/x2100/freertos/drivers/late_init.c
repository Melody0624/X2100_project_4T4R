#include <common.h>
#include <os.h>

#ifdef CONFIG_X2000_CONN_VIC
void soc_conn_vic_init(void);
#endif

#ifdef CONFIG_X2000_CONN_FB
void soc_conn_fb_init(void);
#endif

#ifdef CONFIG_X2000_CONN_ISP
void soc_conn_isp_init(void);
#endif

#ifdef CONFIG_X2000_CONN_BLK
void soc_conn_blk_init(void);
#endif

#ifdef CONFIG_X2000_CONN_PCM_PLAYBACK
void soc_conn_pcm_playback_init(void);
#endif

#ifdef CONFIG_X2000_CONN_FS
void soc_conn_fs_init(void);
#endif

#if defined(CONFIG_X2000_CONN) || defined(CONFIG_X2600_CONN)
void conn_wait_conn_inited(void);

static void conn_dev_init_thread(void *data)
{
    conn_wait_conn_inited();

#ifdef CONFIG_X2000_CONN_VIC
    soc_conn_vic_init();
#endif

#ifdef CONFIG_X2000_CONN_FB
    soc_conn_fb_init();
#endif

#ifdef CONFIG_X2000_CONN_ISP
    soc_conn_isp_init();
#endif

#ifdef CONFIG_X2000_CONN_BLK
    soc_conn_blk_init();
#endif

#ifdef CONFIG_X2000_CONN_PCM_PLAYBACK
    soc_conn_pcm_playback_init();
#endif

#ifdef CONFIG_X2000_CONN_FS
    soc_conn_fs_init();
#endif
}
#endif

void soc_late_init(void)
{
#if defined(CONFIG_X2000_CONN) || defined(CONFIG_X2600_CONN)
    thread_create("conn dev init thread", 4096, conn_dev_init_thread, NULL);
#endif
}
