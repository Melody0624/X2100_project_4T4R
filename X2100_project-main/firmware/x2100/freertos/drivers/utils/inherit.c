#include <stdio.h>
#include <string.h>
#include <common.h>
#include <os.h>
#include <lib/spl_cmdargs_analysis.h>
#include <driver/inherit.h>

/*
 * 内存布局配置
 *
 * ┌──────────────────┐
 * │ 配置区                             │
 * │    -inherit_global_header          │
 * │    -设备A配置头部（inherit_device）│
 * │    -设备B配置头部（inherit_device）│← 要继承的配置结构体
 * │    -设备A配置数据                  │
 * │    -设备B配置数据                  │
 * ├──────────────────┤
 * │ 工作区                             │
 * │    -设备A运行申请的内存            │← 设备动态申请的内存
 * │    -设备B运行申请的内存            │
 * │ 每个设备申请的内存不一定是连续的   │
 * └──────────────────┘
 *
 * 方式一：配置区独立于SHARE_MEM，工作区在SHARE_MEM
 *   配置区:    存放需要继承的外设配置结构体数据
 *   工作区:    驱动运行过程中动态申请的内存
 *
 * 方式二：配置区与工作区均在SHARE_MEM
 *   配置区:    存放在共享内存的起始部分（默认前32KB）
 *              存放内容与方式一相同，用于继承外设配置结构体数据
 *   工作区:    位于共享内存的剩余部分（从偏移32KB处开始）
 *              用于运行时动态内存分配
 */

#define INHERIT_MAX_DEVICES     8           // 最大支持的设备数量
#define INHERIT_CONFIG_MEM_SIZE (32 * 1024) // 配置区固定32KB大小

/**
 * 设备配置项头部
 */
struct inherit_device {
    unsigned int magic;     // 设备标识
    unsigned int version;   // 配置结构体的版本号
    unsigned int data_size; // 配置数据的实际大小(字节数)
    unsigned int crc32;     // 配置数据的CRC校验值
    void *user_data;        // 配置数据的起始地址
};


/**
 * 全局管理区头部
 * 位于配置区内存起始位置
 */
struct inherit_global_header {
    unsigned int version;                               // 管理结构版本
    unsigned int device_count;                          // 已注册设备数量
    unsigned int config_offset;                         // 配置数据当前偏移
    struct inherit_device devices[INHERIT_MAX_DEVICES]; // 设备描述符表
};


static void *config_mem_start;
static unsigned int config_mem_size;

static unsigned long share_mem_end;
static void *share_mem_ptr;

static struct mutex config_lock;
static struct mutex malloc_lock;

static struct inherit_device *find_device(unsigned int magic)
{
    struct inherit_global_header *global = (struct inherit_global_header *)config_mem_start;
    int i;

    for (i = 0; i < global->device_count; i++) {
        if (global->devices[i].magic == magic)
            return &global->devices[i];
    }
    return NULL;
}

/**
 * inherit_init传的参数分别是mem,size
 * 当mem为NULL的时候，默认使用share_mem作为配置区存放要继承的数据;
 * 当mem!=NULL且size足够大的时候，并且linux和rtos能够同时访问，要继承的数据存放在此处。
 * 比如tcsm区域,调用inherit_init时传入tcsm的起始地址和大小，继承方和被继承方需保持一致。
 */
int inherit_init(void *mem, unsigned int size)
{
    void *share_mem_start = NULL;
    unsigned int share_mem_size = 0;

    /* 获取共享内存地址和大小 */
    if (cmdargs_mem_info_get("share_mem", &share_mem_start, &share_mem_size) < 0) {
        printf("inherit_init: err, qucikstart cmdline share_mem invalid!");
        return -1;
    }

    /* 判断配置区内存来源 */
    if (mem && size > sizeof(struct inherit_global_header)) {
        config_mem_start = mem;
        config_mem_size = size;
        share_mem_ptr = (unsigned char *)share_mem_start;
    } else {
        config_mem_start = share_mem_start;
        config_mem_size = INHERIT_CONFIG_MEM_SIZE;
        share_mem_ptr = (unsigned char *)share_mem_start + config_mem_size;
    }

    share_mem_end = (unsigned long)share_mem_start + share_mem_size;

    /* 检查配置区 */
    if (!config_mem_start || config_mem_size <= sizeof(struct inherit_global_header)) {
        printf("inherit_init: config mem is %p or size %u too small\n", config_mem_start, config_mem_size);
        return -1;
    }

    /* 初始化全局管理区头部 */
    struct inherit_global_header *global = (struct inherit_global_header *)config_mem_start;
    memset(global, 0, sizeof(struct inherit_global_header));
    global->version = INHERIT_GLOBAL_VERSION;
    global->config_offset = sizeof(struct inherit_global_header);

    /* 初始化锁 */
    mutex_init(&config_lock);
    mutex_init(&malloc_lock);

    return 0;
}

int inherit_export_cfg(unsigned int magic, unsigned int version, void *cfg_data, unsigned int cfg_size)
{
    if (!cfg_data || !cfg_size || cfg_size > config_mem_size) {
        printf("inherit_export: invalid parameters (data=%lx, size=%u)\n", (unsigned long)cfg_data, cfg_size);
        return -1;
    }

    struct inherit_global_header *global = (struct inherit_global_header *)config_mem_start;

    /* 检查空间是否足够 */
    if (global->config_offset + cfg_size > config_mem_size) {
        printf("inherit_export_cfg: config zone full (used=%u, need=%u, total=%u)\n", global->config_offset, cfg_size, config_mem_size);
        return -1;
    }

    if (global->device_count >= INHERIT_MAX_DEVICES) {
        printf("inherit_export_cfg: too many devices (max=%d)\n", INHERIT_MAX_DEVICES);
        return -1;
    }

    mutex_lock(&config_lock);

    struct inherit_device *dev = find_device(magic);
    if (dev) {
        /* 设备已存在 */
        if (dev->data_size != cfg_size) {
            printf("inherit_export_cfg: data size change (old=%u, new=%u)\n", dev->data_size, cfg_size);
            mutex_unlock(&config_lock);
            return -1;
        }
    } else {
        /* 注册新设备 */
        dev = &global->devices[global->device_count++];
        dev->magic = magic;
        dev->version = version;
        dev->data_size = cfg_size;
        dev->user_data = (unsigned char *)config_mem_start + global->config_offset;

        global->config_offset += cfg_size;
    }

    dev->crc32 = crc32(0, (unsigned char *)cfg_data, cfg_size);
    memcpy(dev->user_data, cfg_data, cfg_size);

    mutex_unlock(&config_lock);

    return 0;
}

void *inherit_malloc(unsigned int size, unsigned int align)
{
    if (!share_mem_ptr || !size || !align) {
        printf("inherit_malloc: invalid parameters (share_mem_ptr=%p, size=%u, align=%u\n", share_mem_ptr, size, align);
        return NULL;
    }

    mutex_lock(&malloc_lock);

    unsigned long cur = (unsigned long)share_mem_ptr;
    unsigned long aligned_addr = ALIGN(cur, align);
    unsigned long new_ptr = aligned_addr + size;

    /* 检查是否越界 */
    if (new_ptr > share_mem_end) {
        printf("inherit_malloc: out of memory (request=%u, remain=%lu)\n", size, share_mem_end - cur);
        mutex_unlock(&malloc_lock);
        return NULL;
    }

    share_mem_ptr = (void *)new_ptr;

    mutex_unlock(&malloc_lock);

    return (void *)aligned_addr;
}

unsigned int inherit_mem_available(void)
{
    return (unsigned int)(share_mem_end - (unsigned long)share_mem_ptr);
}
