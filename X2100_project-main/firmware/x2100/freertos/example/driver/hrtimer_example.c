#include "common.h"
#include <driver/hrtimer.h>
#include <driver/gpio.h>

static struct hrtimer g_timer1;
static struct hrtimer g_timer2;
static struct hrtimer g_timer3;

static void timer1_callback(struct hrtimer *timer)
{
    printf("hr timer1_callback.\n");

    hrtimer_cancel(&g_timer2);
}

static void timer2_callback(struct hrtimer *timer)
{
    printf("hr timer2_callback.\n");
}

static void timer3_callback(struct hrtimer *timer)
{
    hrtimer_restart(timer, 5 * 1000 * 1000);
    printf("hr timer3_callback.\n");
}

void hrtimer_test(void)
{
    hrtimer_init(&g_timer1, timer1_callback);
    hrtimer_init(&g_timer2, timer2_callback);
    hrtimer_init(&g_timer3, timer3_callback);

    hrtimer_start(&g_timer1, 1500 * 1000);

    hrtimer_start(&g_timer2, 4000 * 1000);

    hrtimer_start(&g_timer3, 5 *1000 * 1000);
}
