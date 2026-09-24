#include <stdio.h>
#include <list.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <os.h>
#include <common.h>

#include "task.h"
#include "it_task_port.h"

struct it_task {
    /* 当前任务的堆栈指针,平台函数在切换任务时需要操作
     */
    void *stack_pointer;

    /* 平台的额外cpu寄存器数据,用以优化任务切换的速度
     */
    struct cpu_regs cpu_regs;

    /* 平台的额外任务数据,我们这里用于对接os层
     */
    struct task_data task_data;

    /* 当前任务的cpu id,运行时有效
     */
    int cpu_id;

    /* 当前任务的id,主要用以debug时区别重名的任务
     */
    unsigned int task_id;

    const char *task_name;
    int task_priority;
    it_task_entry task_entry;
    enum it_task_status task_status;
    enum it_task_status task_next_status;

    struct list_head link;

    int stack_size;
    void *stack_base;

    unsigned int wait_start;
    unsigned int wait_timeout;
    int wait_is_wakeup;
    unsigned long wait_event_value;
    uint64_t run_time;
};

struct it_task_current {
    void *stack_pointer;
    unsigned int task_id;
};

struct it_task_info {
    /* 当前任务的id,主要用以debug时区别重名的任务
     */
    unsigned int task_id;

    /* 当前任务的cpu id,运行时有效
     */
    int cpu_id;
    const char *task_name;
    int task_priority;
    enum it_task_status task_status;
    uint64_t run_time;
};

enum cpu_status {
    cpu_st_offline,
    cpu_st_online,
    cpu_st_request_offline,
};

/* 当前cpu是否需要被关闭
 */
static volatile int cpu_status[config_it_task_cpu_nums];

/* 当前各个cpu抢占是否关闭,以及抢占打开时是否立马进行调度
 */
static unsigned char preempt_disabled[config_it_task_cpu_nums];
static unsigned char yield_pending[config_it_task_cpu_nums];

/* 当前各个cpu正在运行的任务
 */
struct it_task *it_current_task[config_it_task_cpu_nums];

/* 后台任务,用于删除待删除的任务...
 */
static struct it_task *bg_task;

/* 所有的idle线程,运行时idle线程不一定在对应的cpu上执行
 */
static struct it_task *idle_tasks[config_it_task_cpu_nums];

/* 当前正在运行的任务,每个优先级划分一个列表
 * 不同列表的任务按优先级决定是否换出
 * 相同里列表的任务按排列顺序决定是否换出
 */
static struct list_head running_tasks[config_it_task_priority_nums];

/* 当前就绪的任务,每个优先级划分一个列表
 * 不同列表的任务按优先级决定是否运行
 * 相同里列表的任务按排列顺序决定是否运行
 */
static struct list_head ready_tasks[config_it_task_priority_nums];

/* 延时任务列表:suspend, wait
 * 按延时时间升序排列
 */
static struct list_head delayed_tasks;

/* 待删除任务列表,由idle线程负责执行真正的删除动作
 */
static struct list_head delete_tasks;

/* max_ready_priority: 当前就绪任务的最大优先级
 * ready_tasks_mark:   当前就绪任务的快速检查标记
 * ready_tasks_cnt:    当前就绪任务的个数,按优先级划分
 */
static int max_ready_priority;
static int ready_tasks_mark;
static int ready_tasks_cnt[config_it_task_priority_nums];

/* min_running_priority: 当前运行任务的最小优先级
 * ready_tasks_mark:     当前运行任务的快速检查标记
 * ready_tasks_cnt:      当前运行任务的个数,按优先级划分
 */
static int min_running_priority;
static int running_tasks_mark;
static int running_tasks_cnt[config_it_task_priority_nums];

/* 标记调度器是否已经运行
 */
static int is_started;

/* 标记调度器是否已经初始化
 */
static int is_inited;

/* 调度器运行的总调度周期,可能轮转为0继续递增
 */
static volatile unsigned int total_ticks;

static volatile uint64_t runtime_count[config_it_task_cpu_nums];

static unsigned int max_task_id;

/* 当前总任务数
 */
static unsigned int total_task_nums;

/* 标记当前是否不允许idle线程进行任务删除
 */
static volatile int task_locked;

#define task_timeout(_t) \
    (_t->wait_start + _t->wait_timeout - total_ticks)

static inline struct it_task *list_get_task(struct list_head *head)
{
    if (list_empty(head)) {
        panic("it_task: list %p is empty, [%p %p %p]\n",
            head, running_tasks, ready_tasks, &delayed_tasks);
    }

    return list_first_entry(head, struct it_task, link);
}

static inline void list_move_task(struct list_head *head, struct it_task *task)
{
    list_move_tail(&task->link, head);
}

static void trace_task_status(struct it_task *task, const char *status, int cpu_id)
{
#if !config_enable_it_task_trace
    return;
#endif

    const char *task_name = task ? task->task_name : "null";
    int task_priority = task ? task->task_priority : 0;

    printf("[%d] [%s] [%d] --> %s",
        port_it_task_current_cpu_id(), task_name, task_priority, status);

    if (cpu_id != -1)
        printf(" cpu[%d]\n", cpu_id);
    else
        printf("\n");
}

static void trace_task_api(struct it_task *task, const char *action)
{
#if !config_enable_it_task_trace
    return;
#endif

    printf("[%d] [%s] [%d] --> api-%s\n",
     port_it_task_current_cpu_id(), task->task_name, task->task_priority, action);
}

/* 设置任务为就绪状态
 */
static void task_set_status_ready(struct it_task *task)
{
    task->task_status = it_task_status_ready;

    int task_priority = task->task_priority;
    if (task_priority > max_ready_priority)
        max_ready_priority = task_priority;

    ready_tasks_mark |= 1 << task_priority;
    ready_tasks_cnt[task_priority]++;

    list_move_task(&ready_tasks[task_priority], task);

    trace_task_status(task, "ready", -1);
}

/* 重设当前就绪任务的最大优先级
 */
static void reset_max_ready_priority(void)
{
    ready_tasks_mark &= ~(1 << max_ready_priority);

    int i;
    for (i = max_ready_priority - 1; i >= 0; i--) {
        if ((1 << i) & ready_tasks_mark) {
            max_ready_priority = i;
            return;
        }
    }

    max_ready_priority = 0;
}

/* 获得优先级最高的就绪任务
 */
static struct it_task *task_get_status_ready(void)
{
    struct it_task *task = list_get_task(&ready_tasks[max_ready_priority]);

    if (--ready_tasks_cnt[max_ready_priority] == 0)
        reset_max_ready_priority();

    return task;
}

static void task_cancel_status_ready(struct it_task *task)
{
    int task_priority = task->task_priority;

    if (--ready_tasks_cnt[task_priority] == 0) {
        ready_tasks_mark &= ~(1 << task_priority);
        if (task_priority == max_ready_priority)
            reset_max_ready_priority();
    }
}

/* 设置任务为延时状态,按到期时间升序插入
 */
static void task_set_status_delayed(struct it_task *task)
{
    if (task->wait_timeout == 0) {
        list_move_tail(&task->link, &delayed_tasks);
        return;
    }

    struct list_head *pos;
    list_for_each(pos, &delayed_tasks) {
        struct it_task *t = list_entry(pos, struct it_task, link);
        if (t->wait_timeout == 0)
            break;

        if (task->wait_timeout <= task_timeout(t))
            break;
    }

    list_move_tail(&task->link, pos);
}

/* 设置任务为等待状态
 */
static void task_set_status_wait(struct it_task *task)
{
    trace_task_status(task, "wait", -1);

    task->task_status = it_task_status_wait;
    task_set_status_delayed(task);
}

/* 设置任务为睡眠状态
 */
static void task_set_status_suspend(struct it_task *task)
{
    trace_task_status(task, "suspend", -1);

    task->task_status = it_task_status_suspend;
    task_set_status_delayed(task);
}

/* 设置任务为删除状态
 */
static void task_set_status_delete(struct it_task *task)
{
    trace_task_status(task, "delete", -1);

    task->task_status = it_task_status_delete;
    list_move_task(&delete_tasks, task);
}

/* 设置任务为运行状态
 */
static void task_set_status_running(struct it_task *task, int cpu_id)
{
    trace_task_status(task, "running", cpu_id);

    task->task_status = it_task_status_running;
    task->task_next_status = it_task_status_running;
    task->cpu_id = cpu_id;

    int task_priority = task->task_priority;

    if (task_priority <= min_running_priority)
        min_running_priority = task_priority;

    if (!running_tasks_mark)
        min_running_priority = task_priority;

    running_tasks_mark |= 1 << task_priority;
    running_tasks_cnt[task_priority]++;

    list_move_task(&running_tasks[task_priority], task);

    it_current_task[cpu_id] = task;
}

/* 重设当前运行任务的最小优先级
 */
static void reset_min_running_priority(void)
{
    int i;
    for (i = min_running_priority+1; i < config_it_task_priority_nums; i++) {
        if ((1 << i) & running_tasks_mark) {
            min_running_priority = i;
            return;
        }
    }

    min_running_priority = 0;
}

/* 把任务从运行状态取消
 */
static void task_cancel_status_running(struct it_task *task)
{
    int task_priority = task->task_priority;

    if (--running_tasks_cnt[task_priority])
        return;

    running_tasks_mark &= ~(1 << task_priority);

    if (task_priority == min_running_priority)
        reset_min_running_priority();
}

void dump_delayed_list(void)
{
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &delayed_tasks) {
        struct it_task *t = list_entry(pos, struct it_task, link);

        printf("%s %d %d %d %d\n",
         t->task_name, t->wait_start, t->wait_timeout, total_ticks, task_timeout(t));
    }
}

/* 将到期的延时任务设置为就绪状态
 */
static void set_expired_tasks_to_ready(unsigned int ticks)
{
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &delayed_tasks) {
        struct it_task *t = list_entry(pos, struct it_task, link);
        if (t->wait_timeout == 0)
            break;

        if (ticks < task_timeout(t))
            break;

        t->wait_is_wakeup = 0;
        task_set_status_ready(t);
    }
}

static inline int get_next_usable_ready_priority(int old, int end)
{
    int i;

    for (i = old-1; i >= end; i--) {
        if ((1 << i) & ready_tasks_mark)
            return i;
    }

    return -1;
}

static inline void task_yield(struct it_task *task, int cpu_id, const char *status)
{
    trace_task_status(task, status, cpu_id);

    if (preempt_disabled[cpu_id]) {
        yield_pending[cpu_id] = 1;
        return;
    }

    port_it_task_yield(cpu_id);
}

/* 将"高"优先级的就绪任务设置为运行状态
 */
static void set_high_priority_tasks_to_running(void)
{
    if (!ready_tasks_mark)
        return;

    int cnt = 0;
    int i = min_running_priority, j = max_ready_priority;

    for (; i <= j; i++) {
        struct list_head *pos;
        list_for_each(pos, &running_tasks[i]) {
            struct it_task *task = list_entry(pos, struct it_task, link);
            task->task_next_status = it_task_status_ready;
            task_yield(task, task->cpu_id, "yield-tick");
            if (++cnt == ready_tasks_cnt[j]) {
                cnt = 0;
                j = get_next_usable_ready_priority(j, i);
                if (j == -1)
                    return;
            }
        }
    }
}

void it_task_add_ticks(unsigned int ticks)
{
    port_it_task_enter_critical();

    set_expired_tasks_to_ready(ticks);

    total_ticks += ticks;

    set_high_priority_tasks_to_running();

    port_it_task_exit_critical();
}

static inline void update_task_runtime(int cpu_id)
{
    struct it_task *task = it_current_task[cpu_id];

    uint64_t total_run_time = systick_get_time_us();
    task->run_time += (total_run_time - runtime_count[cpu_id]);
    runtime_count[cpu_id] = total_run_time;
}

struct it_task *it_task_switch_context(void)
{
    int cpu_id = port_it_task_current_cpu_id();

    port_it_task_enter_critical();

    struct it_task *task = it_current_task[cpu_id];

    update_task_runtime(cpu_id);

    switch (task->task_next_status) {
    case it_task_status_ready:
    case it_task_status_running:
        task_cancel_status_running(task);
        task_set_status_ready(task);
        break;
    case it_task_status_suspend:
        task_cancel_status_running(task);
        task->wait_start = total_ticks;
        task_set_status_suspend(task);
        break;
    case it_task_status_wait:
        task_cancel_status_running(task);
        task->wait_start = total_ticks;
        task_set_status_wait(task);
        break;
    case it_task_status_delete:
        it_task_wakeup(bg_task, 0);
        task_cancel_status_running(task);
        task_set_status_delete(task);
        break;
    default:
        assert(0);
    }

    if (cpu_status[cpu_id] == cpu_st_request_offline) {
        if (idle_tasks[cpu_id]) {
            it_task_delete(idle_tasks[cpu_id]);
            idle_tasks[cpu_id] = NULL;
        }
        cpu_status[cpu_id] = cpu_st_offline;
        it_current_task[cpu_id] = NULL;
        port_it_task_exit_critical();
        port_it_task_shutdown_cpu_prepare(cpu_id);
        panic("it_task: can't be here, failed to shutdown cpu%d\n", cpu_id);
    }

    task = task_get_status_ready();
    task_set_status_running(task, cpu_id);

    port_it_task_exit_critical();

    return task;
}

void it_task_suspend(struct it_task *task, unsigned int ticks)
{
    if (ticks == 0)
        return;

    /*
     * 这里必须 +1
     * 1 保证suspend的时间不会少于要求的时间
     * 2 -1 用作外部的无限timeout, -1 + 1 = 0, 0 用作内部的无限timeout
     */
    ticks += 1;

    port_it_task_enter_critical();

    if (task == NULL) {
        int cpu_id = port_it_task_current_cpu_id();
        task = it_current_task[cpu_id];

        /* 当前cpu调度关闭时,不能休眠
         */
        assert(!preempt_disabled[cpu_id]);
    }

    trace_task_api(task, "suspend");

    task->wait_start = total_ticks;
    task->wait_timeout = ticks;

    switch (task->task_status) {
    case it_task_status_delete:
        goto unlock;
    case it_task_status_suspend:
    case it_task_status_wait:
    case it_task_status_ready:
        task_set_status_suspend(task);
        goto unlock;
    case it_task_status_running:
        task->task_next_status = it_task_status_suspend;
        task_yield(task, task->cpu_id, "yield-suspend");
        break;
    default:
        assert(0);
    }

unlock:
    port_it_task_exit_critical();
}

static void yield_if_priority_is_higher(int task_priority)
{
    if (task_priority <= min_running_priority)
        return;

    struct it_task *t = list_get_task(&running_tasks[min_running_priority]);
    task_yield(t, t->cpu_id, "yield-priority");
}

void it_task_resume(struct it_task *task)
{
    if (task == NULL)
        return;

    port_it_task_enter_critical();

    trace_task_api(task, "resume");

    if (task->task_status == it_task_status_suspend) {
        task_set_status_ready(task);
        yield_if_priority_is_higher(task->task_priority);
    }

    port_it_task_exit_critical();
}

int it_task_wait(unsigned int ticks, unsigned long *event_value)
{
    if (ticks == 0)
        return -1;

    ticks += 1;

    port_it_task_enter_critical();

    int cpu_id = port_it_task_current_cpu_id();
    struct it_task *task = it_current_task[cpu_id];

    /* 当前cpu调度关闭时,不能休眠
     */
    assert(!preempt_disabled[cpu_id]);

    trace_task_api(task, "wait");

    if (!task->wait_is_wakeup) {
        task->wait_start = total_ticks;
        task->wait_timeout = ticks;
        task->task_next_status = it_task_status_wait;
        task_yield(task, cpu_id, "yield-wait");
    }

    port_it_task_exit_critical();

    int ret = -1;
    port_it_task_enter_critical();

    if (task->wait_is_wakeup) {
        ret = 0;
        task->wait_is_wakeup = 0;
        if (event_value)
            *event_value = task->wait_event_value;
    }

    port_it_task_exit_critical();

    return ret;
}

void it_task_wakeup(struct it_task *task, unsigned long event_value)
{
    if (task == NULL)
        return;

    port_it_task_enter_critical();

    trace_task_api(task, "wakeup");

    if (task->task_status != it_task_status_delete) {
        task->wait_is_wakeup = 1;
        task->wait_event_value = event_value;
        if (task->task_status == it_task_status_wait) {
            task_set_status_ready(task);
            yield_if_priority_is_higher(task->task_priority);
        }
    }

    port_it_task_exit_critical();
}

void it_task_yield(void)
{
    port_it_task_enter_critical();

    int cpu_id = port_it_task_current_cpu_id();
    struct it_task *task = it_current_task[cpu_id];

    trace_task_api(task, "yield");

    task->task_next_status = it_task_status_ready;
    task_yield(task, cpu_id, "yield-user");

    port_it_task_exit_critical();
}

void it_task_set_priority(struct it_task *task, int task_priority)
{
    assert(0 <= task_priority && task_priority < config_it_task_priority_nums);

    port_it_task_enter_critical();

    if (task == NULL) {
        int cpu_id = port_it_task_current_cpu_id();
        task = it_current_task[cpu_id];
    }

    trace_task_api(task, "set-priority");

    if (task->task_priority == task_priority)
        goto unlock;

    switch (task->task_priority) {
    case it_task_status_running: {
        task_cancel_status_running(task);
        list_del_init(&task->link);
        task->task_priority = task_priority;
        task_set_status_running(task, task->cpu_id);
        if (task_priority < max_ready_priority)
            task_yield(task, task->cpu_id, "yield-priority");
        break;
    }
    case it_task_status_ready:
        task_cancel_status_ready(task);
        list_del_init(&task->link);
        task->task_priority = task_priority;
        task_set_status_ready(task);
        if (is_started)
            yield_if_priority_is_higher(task_priority);
        break;
    default:
        task->task_priority = task_priority;
        break;
    }

unlock:
    port_it_task_exit_critical();
}

void it_task_disable_preempt(void)
{
    port_it_task_enter_critical();

    int cpu_id = port_it_task_current_cpu_id();

    preempt_disabled[cpu_id]++;

    assert(preempt_disabled[cpu_id]);

    port_it_task_exit_critical();
}

void it_task_enable_preempt(void)
{
    port_it_task_enter_critical();

    int cpu_id = port_it_task_current_cpu_id();

    if (!preempt_disabled[cpu_id])
        goto unlock;

    if (--preempt_disabled[cpu_id])
        goto unlock;

    if (yield_pending[cpu_id]) {
        yield_pending[cpu_id] = 0;
        struct it_task *task = it_current_task[cpu_id];
        task_yield(task, cpu_id, "yield-pending");
    }

unlock:
    port_it_task_exit_critical();
}

int it_task_preempt_is_enable(void)
{
    int ret;

    port_it_task_enter_critical();

    int cpu_id = port_it_task_current_cpu_id();

    ret = !preempt_disabled[cpu_id];

    port_it_task_exit_critical();

    return ret;
}

void it_task_lock_tasks(void)
{
    port_it_task_enter_critical();
    task_locked++;
    port_it_task_exit_critical();
}

void it_task_unlock_tasks(void)
{
    port_it_task_enter_critical();
    task_locked--;
    assert(task_locked >= 0);
    port_it_task_exit_critical();
}

int it_task_kernel_is_running(void)
{
    return is_started;
}

struct it_task *it_task_get_current(int cpu_id)
{
    if (cpu_id < -1 || cpu_id >= config_it_task_cpu_nums)
        return NULL;

    struct it_task *task;

    port_it_task_enter_critical();

    if (cpu_id == -1)
        cpu_id = port_it_task_current_cpu_id();

    task = it_current_task[cpu_id];

    port_it_task_exit_critical();

    return task;
}

static void *it_task_start_entry(void *userdata)
{
    port_it_task_enter_critical();

    int cpu_id = port_it_task_current_cpu_id();

    struct it_task *task = it_current_task[cpu_id];

    if (task->task_status == it_task_status_first_runing) {
        task_set_status_running(task, cpu_id);
        runtime_count[cpu_id] = systick_get_time_us();
        cpu_status[cpu_id] = cpu_st_online;

        if (ready_tasks_mark)
            yield_if_priority_is_higher(max_ready_priority);
    }

    port_it_task_exit_critical();

    task->task_entry(userdata);

#ifdef CONFIG_OS
    thread_delete(task);
#else
    it_task_delete(task);
#endif

    return NULL;
}

static void it_task_init_kenrel(void);

static struct it_task *it_task_create_inner(
    const char *task_name, int task_priority, it_task_entry task_entry, 
    int stack_size, void *userdata)
{
    struct it_task *task;

    assert(task_name);
    assert(task_entry);
    assert(stack_size);

    if (stack_size < config_it_task_min_stack_size)
        stack_size = config_it_task_min_stack_size;

    task = memalign(config_it_task_task_size_align, sizeof(*task));
    if (!task) {
        printf("it_task: failed to alloc task memory for %s\n", task_name);
        return NULL;
    }

    memset(task, 0, sizeof(*task));

    stack_size = ALIGN(stack_size, config_it_task_stack_size_align);
    task->stack_base = memalign(config_it_task_stack_size_align, stack_size);
    if (!task->stack_base) {
        printf("it_task: failed to alloc stack memory for %s, size: %d\n", task_name, stack_size);
        free(task);
        return NULL;
    }

    memset(task->stack_base, 0xa5, stack_size);

    task->stack_size = stack_size;
    task->stack_pointer = task->stack_base + stack_size;

    task->task_name = task_name;
    task->task_entry = task_entry;
    task->task_priority = task_priority;
    task->cpu_id = 0;
    task->task_id = max_task_id++;

    total_task_nums++;

    INIT_LIST_HEAD(&task->link);

    task->stack_pointer = 
        port_it_task_init_stack(task->stack_pointer, it_task_start_entry, userdata);

    soc_init_cpu_regs(&task->cpu_regs);

    port_it_task_init_task_data(&task->task_data);

    os_enter_critical();
    trace_task_api(task, "create");
    os_exit_critical();

    it_task_init_kenrel();

    return task;
}

static void it_task_delete_inner(struct it_task *task)
{
    total_task_nums--;

    port_it_task_deinit_task_data(&task->task_data);

    free(task->stack_base);
    free(task);
}

struct it_task *it_task_create(
    const char *task_name, int task_priority, it_task_entry task_entry, 
    int stack_size, void *userdata)
{
    struct it_task *task = it_task_create_inner(
        task_name, task_priority, task_entry, stack_size, userdata);

    if (!task)
        return NULL;

    port_it_task_enter_critical();

    task_set_status_ready(task);

    if (is_started)
        yield_if_priority_is_higher(task->task_priority);

    port_it_task_exit_critical();

    return task;
}

void it_task_delete(struct it_task *task)
{
    port_it_task_enter_critical();

    if (task == NULL) {
        int cpu_id = port_it_task_current_cpu_id();
        task = it_current_task[cpu_id];
    }

    trace_task_api(task, "delete");

    switch (task->task_status) {
    case it_task_status_running:
        task->task_next_status = it_task_status_delete;
        task_yield(task, task->cpu_id, "yield-delete");
        break;
    case it_task_status_ready:
        task_cancel_status_ready(task);
        task_set_status_delete(task);
        if (!is_started)
            it_task_wakeup(bg_task, 0);
        break;
    case it_task_status_suspend:
    case it_task_status_wait:
        task_set_status_delete(task);
        if (!is_started)
            it_task_wakeup(bg_task, 0);
        break;
    default:
        break;
    }

    port_it_task_exit_critical();
}

static void delete_pending_tasks(void)
{
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &delete_tasks) {
        struct it_task *task = list_entry(pos, struct it_task, link);
        list_del(pos);
        it_task_delete_inner(task);
    }
}

static unsigned int get_idle_timeout(void)
{
    if (list_empty(&delayed_tasks))
        return -1;

    struct it_task *task = list_get_task(&delayed_tasks);
    if (task->wait_timeout == 0)
        return -1;

    return task_timeout(task);
}

static void *it_idle_task_entry(void *data)
{
    while (1) {
        unsigned int timeout;

        port_it_task_disable_local_irq();

        port_it_task_enter_critical();

        timeout = get_idle_timeout();

        port_it_task_exit_critical();

        port_it_task_enter_idle(timeout);

        port_it_task_enable_local_irq();
    }

    return NULL;
}

static void *it_bg_task_entry(void *data)
{
    int need_rescan = 1;

    while (1) {
        it_task_wait(need_rescan ? 1 : -1, NULL);

        port_it_task_enter_critical();

        if (task_locked) {
            need_rescan = 1;
            goto unlock;
        }

        /* 检查是否有需要被删除的任务
         */
        delete_pending_tasks();

        need_rescan = 0;

unlock:
        port_it_task_exit_critical();
    }

    return NULL;
}

static void it_task_init_kenrel(void)
{
    if (is_inited)
        return;

    is_inited = 1;

    int i;
    for (i = 0; i < config_it_task_priority_nums; i++) {
        INIT_LIST_HEAD(&ready_tasks[i]);
        INIT_LIST_HEAD(&running_tasks[i]);
    }

    INIT_LIST_HEAD(&delayed_tasks);
    INIT_LIST_HEAD(&delete_tasks);
}

void it_task_start_kernel(int cpu_id)
{
    port_it_task_enter_critical();

    if (!bg_task) {
        bg_task = it_task_create(
            "bg-task", config_it_task_priority_nums-1,
            it_bg_task_entry, config_it_task_min_stack_size, NULL);
    }

    is_started = 1;

    struct it_task *task = it_task_create_inner(
        "idle", 0, it_idle_task_entry, config_it_task_min_stack_size, NULL);
    assert(task);
    task->task_status = it_task_status_first_runing;
    task->task_next_status = it_task_status_running;

    idle_tasks[cpu_id] = task;
    it_current_task[cpu_id] = task;

    port_it_task_exit_critical();

    port_it_task_start(cpu_id);
}

void it_task_stop_cpu(int cpu_id)
{
    assert_range(cpu_id, 0, config_it_task_cpu_nums);
    assert(!preempt_disabled[cpu_id]);

    os_enter_critical();

    if (cpu_status[cpu_id] == cpu_st_offline) {
        printf("it_task: can't stop cpu%d when it offline\n", cpu_id);
        os_exit_critical();
        return;
    }

    cpu_status[cpu_id] = cpu_st_request_offline;
    task_yield(it_current_task[cpu_id], cpu_id, "yield-cpu-offline");

    os_exit_critical();

    while (cpu_status[cpu_id] != cpu_st_offline);

    os_enter_critical();

    port_it_task_shutdown_cpu(cpu_id);

    os_exit_critical();
}

void it_task_start_cpu(int cpu_id)
{
    if (cpu_status[cpu_id] == cpu_st_online)
        return;

    port_it_task_startup_cpu(cpu_id);

    int64_t start = systick_get_time_us();
    while (cpu_status[cpu_id] != cpu_st_online) {
        if (systick_get_time_us() - start >= 10*1000) {
            printf("it_task: failed to start cpu%d\n", cpu_id);
            break;
        }
    }
}

/**
 * 下列函数是关于任务信息获取的
 */

const char *it_task_get_name(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task->task_name;
}

void it_task_set_name(struct it_task *task, const char *task_name)
{
    assert(task_name);

    if (task == NULL)
        task = it_task_get_current(-1);

    task->task_name = task_name;
}

int it_task_get_priority(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task->task_priority;
}

int it_task_get_id(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task->task_id;
}

int it_task_get_cpu_id(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task->cpu_id;
}

uint64_t it_task_get_run_time(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task->run_time;
}

enum it_task_status it_task_get_status(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task->task_status;
}

struct task_data *it_task_get_task_data(struct it_task *task)
{
    if (task == NULL)
        task = it_task_get_current(-1);

    return task ? &task->task_data : NULL;
}

int it_task_get_task_nums(void)
{
    return total_task_nums;
}

struct it_task **it_task_get_tasks(void)
{
    int n = 0;

    port_it_task_enter_critical();

    struct it_task **array = malloc((total_task_nums+1) * sizeof(array[0]));

    int i;
    for (i = 0; i < ARRAY_SIZE(it_current_task); i++) {
        if (it_current_task[i]) {
            array[n++] = it_current_task[i];
            update_task_runtime(i);
        }
    }

    struct list_head *pos;
    for (i = 0; i < ARRAY_SIZE(ready_tasks); i++) {
        list_for_each(pos, &ready_tasks[i])
            array[n++] = list_entry(pos, struct it_task, link);
    }

    list_for_each(pos, &delayed_tasks)
        array[n++] = list_entry(pos, struct it_task, link);

    list_for_each(pos, &delete_tasks)
        array[n++] = list_entry(pos, struct it_task, link);

    array[n] = NULL;
    assert(n == total_task_nums);

    port_it_task_exit_critical();

    return array;
}

void it_task_free_tasks(struct it_task **array)
{
    free(array);
}

int it_task_get_available_stack_size(unsigned char *stack_base, int size)
{
    int i = 0;
    unsigned char *base_addr = stack_base;

    for (i = 0; i < size; i++)
        if (*stack_base++ != 0xA5)
            break;

    return stack_base - base_addr;
}

static char* run_time_to_string(uint64_t time, char* time_string)
{
    uint64_t time_us, time_ms;

    time_us = time % 1000;
    time_ms = (time / 1000) % 1000;
    time = time / 1000000;

    switch (time)
    {
        case 0:
            sprintf(time_string, "%llu.%03llums", time_ms, time_us);
            break;
        case 1 ... 59:
            sprintf(time_string, "%llu.%03llus", time, time_ms);
            break;
        case 60 ... 3599:
            sprintf(time_string, "%02llu:%02llus", time / 60, time % 60);
            break;
        case 3600 ... 86399:
            sprintf(time_string, "%02llu:%02llu:%02llus", time / 3600, (time % 3600) / 60, time % 60);
            break;
        default:
            sprintf(time_string, "%lluD-%02llu:%02llu:%02llus", time / 86400, (time % 86400) / 3600, (time % 3600) / 60, time % 60);
    }

    return time_string;
}

static char *write_name_to_buffer(char *pc_buffer, const char *pc_task_name, int max_length)
{
    size_t x;

    strcpy(pc_buffer, pc_task_name);

    for (x = strlen(pc_buffer); x < max_length; x++)
        pc_buffer[x] = ' ';

    pc_buffer[x] = 0x00;

    return &(pc_buffer[x]);
}

char it_task_get_char_status(enum it_task_status task_status)
{
    #define tsk_running_char        ( 'X' )
    #define tsk_blocked_char        ( 'B' )
    #define tsk_ready_char          ( 'R' )
    #define tsk_deleted_char        ( 'D' )
    #define tsk_suspended_char      ( 'S' )

    char char_status;
    switch (task_status) {
        case it_task_status_running:
            char_status = tsk_running_char;
            break;

        case it_task_status_ready:
            char_status = tsk_ready_char;
            break;

        case it_task_status_wait:
            char_status = tsk_blocked_char;
            break;

        case it_task_status_suspend:
            char_status = tsk_suspended_char;
            break;

        case it_task_status_delete:
            char_status = tsk_deleted_char;
            break;

        case it_task_status_first_runing:
            char_status = 'F';
            break;

        default:
            char_status = 0x00;
            break;
    }

    return char_status;
}

char *get_thread_list_alloc(void)
{
    int x;
    char *time;
    int size = 0;
    char *buffer = NULL;
    char time_string[32];
    int strlen_max = 0;
    int strlen_tmp = 0;

    it_task_lock_tasks();
    int array_num = it_task_get_task_nums();
    struct it_task **task_array = it_task_get_tasks();
    it_task_unlock_tasks();

    if (task_array != NULL) {
        strlen_max = strlen(task_array[0]->task_name) + 1;
        for (x = 1; x < array_num; x++) {
            strlen_tmp = strlen(task_array[x]->task_name) + 1;
            if (strlen_max < strlen_tmp)
                strlen_max = strlen_tmp;
        }
    }

    int task_array_size = strlen_max + 64;
    buffer = malloc(sizeof(char) * array_num * task_array_size);
    assert(buffer);
    memset(buffer, 0, sizeof(char) * array_num * task_array_size);

    for (x = 0; x < array_num; x++) {
        memset(time_string, 0, sizeof(time_string));
        time = run_time_to_string(it_task_get_run_time(task_array[x]), time_string);
        sprintf(&buffer[size], "%6u%6c\t%4u%10u\t%3u\t%-16s",
                it_task_get_id(task_array[x]),
                it_task_get_char_status(it_task_get_status(task_array[x])),
                it_task_get_priority(task_array[x]),
                it_task_get_available_stack_size(task_array[x]->stack_base, task_array[x]->stack_size),
                it_task_get_cpu_id(task_array[x]),
                time);

        size += strlen(&buffer[size]);
        write_name_to_buffer(&buffer[size], task_array[x]->task_name, strlen_max);
        size += strlen_max - 1;

        sprintf(&buffer[size], "\r\n");
        size += 2;
    }

    return buffer;
}

int get_thread_runtime_stats_alloc(void **buffer)
{
    int x;
    struct it_task **task_array;
    struct it_task_info *tmp_task_array;

    it_task_lock_tasks();
    int array_num = it_task_get_task_nums();
    task_array = it_task_get_tasks();
    it_task_unlock_tasks();
    tmp_task_array = malloc(array_num * sizeof(struct it_task_info));

    for (x = 0; x < array_num; x++) {
        tmp_task_array[x].task_id = task_array[x]->task_id;
        tmp_task_array[x].cpu_id = task_array[x]->cpu_id;
        tmp_task_array[x].task_priority = task_array[x]->task_priority;
        tmp_task_array[x].task_status = task_array[x]->task_status;
        tmp_task_array[x].run_time = task_array[x]->run_time;
        tmp_task_array[x].task_name = strdup(task_array[x]->task_name);
    }

    *buffer = tmp_task_array;
    return array_num;
}

static int compare_thread_runtime(const void *taska, const void *taskb)
{
    struct it_task_info *task1 = (struct it_task_info  *)taska;
    struct it_task_info *task2 = (struct it_task_info  *)taskb;
    return task2->run_time - task1->run_time;
}

static inline char *update_thread_runtime_stats_alloc(uint64_t interval, void *snapshot, int snapshot_num)
{
    int x;
    char *time;
    int size = 0;
    int strlen_max = 0;
    int strlen_tmp = 0;
    char time_string[32];
    char *buffer = NULL;
    int array_num = snapshot_num;
    uint64_t ulTotalTime = interval;
    struct it_task_info *task_array = snapshot;

    qsort(task_array, array_num, sizeof(struct it_task_info), compare_thread_runtime);

    strlen_max = strlen(task_array[0].task_name) + 1;
    for (x = 1; x < array_num; x++) {
        strlen_tmp = strlen(task_array[x].task_name) + 1;
        if (strlen_max < strlen_tmp)
            strlen_max = strlen_tmp;
    }

    int task_run_time_status_size = strlen_max + 68;

    buffer = malloc(sizeof(char) * array_num * task_run_time_status_size);
    assert(buffer);
    memset(buffer, 0, sizeof(char) * array_num * task_run_time_status_size);

    for (x = 0; x < array_num; x++) {
        memset(time_string, 0, sizeof(time_string));
        time = run_time_to_string(task_array[x].run_time, time_string);

        sprintf(&buffer[size], "%6u%6c\t%4u%7u.%02u\t%3u\t%-16s",
                 task_array[x].task_id,
                 it_task_get_char_status(task_array[x].task_status),
                 task_array[x].task_priority,
                (unsigned int) (task_array[x].run_time * 100 / ulTotalTime),
                (unsigned int) (((task_array[x].run_time * 100) % ulTotalTime) * 100 / ulTotalTime),
                task_array[x].cpu_id,
                time);

        size += strlen(&buffer[size]);

        write_name_to_buffer(&buffer[size], task_array[x].task_name, strlen_max);

        size += strlen_max - 1;

        sprintf(&buffer[size], "\r\n");
        size += 2;

    }

    return buffer;
}

static inline char *update_thread_interval_runtime_stats_alloc(int interval_s,
        void *snapshot1, int snapshot1_num,
        void *snapshot2, int snapshot2_num)
{
    int x;
    int y;
    char *buffer = NULL;
    int array_num_old = snapshot1_num;
    int array_num_new = snapshot2_num;
    struct it_task_info *task_array_old = (struct it_task_info *)snapshot1;
    struct it_task_info *task_array_new;

    uint64_t interval_per_us = (uint64_t)interval_s * 1000 * 10;

    if (!interval_per_us)
        return NULL;

    task_array_new = malloc(array_num_new * sizeof(struct it_task_info));
    assert(task_array_new);

    memcpy(task_array_new, snapshot2, array_num_new * sizeof(struct it_task_info));

    for (x = 0; x < array_num_new; x++) {
        int match_id = 0;
        for (y = 0; y < array_num_old; y++) {
            if (task_array_new[x].task_id == task_array_old[y].task_id) {
                match_id = 1;
                break;
            }
        }

        if (match_id)
            task_array_new[x].run_time = task_array_new[x].run_time - task_array_old[y].run_time;
        else
            task_array_new[x].run_time = 0;

    }

    buffer = update_thread_runtime_stats_alloc(interval_s * 1000 * 1000, task_array_new, array_num_new);

    free(task_array_new);

    return buffer;
}

char *get_thread_interval_runtime_stats_alloc(int interval_s,
        void *snapshot1, int snapshot1_num,
        void *snapshot2, int snapshot2_num)
{
    char *buf = NULL;
    uint64_t interval_time_us;

    if (snapshot1 == NULL && snapshot2 == NULL)
        return buf;

    if (snapshot1 && snapshot2) {
        buf = update_thread_interval_runtime_stats_alloc(interval_s,
                snapshot1, snapshot1_num, snapshot2, snapshot2_num);
        return buf;
    }

    interval_time_us = systick_get_time_us();

    if (snapshot1)
        buf = update_thread_runtime_stats_alloc(interval_time_us, snapshot1, snapshot1_num);

    else if (snapshot2)
        buf = update_thread_runtime_stats_alloc(interval_time_us, snapshot2, snapshot2_num);

    return buf;
}

unsigned int *FindThreadByNumber(unsigned long TaskNumber, char **TaskName)
{
    int x;

    it_task_lock_tasks();
    int array_num = it_task_get_task_nums();
    struct it_task **task_array = it_task_get_tasks();
    it_task_unlock_tasks();

    for (x = 0; x < array_num; x++) {
        if (it_task_get_id(task_array[x]) == TaskNumber) {
            *TaskName = (char *)task_array[x]->task_name;
            return task_array[x]->stack_pointer;
        }
    }

    return NULL;
}

struct it_task_current *FindThreadByName(char * pcTaskName, int *thread_count)
{
    int array_num, x, k = 0;

    it_task_lock_tasks();
    array_num = it_task_get_task_nums();
    struct it_task **task_array = it_task_get_tasks();
    it_task_unlock_tasks();
    struct it_task_current *task_array2 = malloc(array_num * sizeof(struct it_task_current));
    assert(task_array2);

    for (x = 0; x < array_num; x++) {
        if (strcmp(it_task_get_name(task_array[x]), pcTaskName) == 0) {
            task_array2[k].task_id = task_array[x]->task_id;
            task_array2[k++].stack_pointer = task_array[x]->stack_pointer;
        }
    }
    *thread_count = k;

    return task_array2;
}

void free_thread_runtime_stats(void *task_array, int array_num)
{
    int x;
    struct it_task_info *task_info = task_array;
    for (x = 0; x < array_num; x++)
        free((char *)task_info[x].task_name);
}
