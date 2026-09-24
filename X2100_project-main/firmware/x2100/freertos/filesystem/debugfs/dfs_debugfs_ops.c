#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_debugfs.h>
#include "dfs_stat.h"

struct simple_attr {
    int (*get)(void *, u64 *);
    int (*set)(void *, u64);
    char get_buf[24];   /* enough to store a u64 and "\n\0" */
    char set_buf[24];
    void *data;
    const char *fmt;    /* format for read operation */
    struct mutex mutex; /* protects access to these buffers */
};

/*
 * simple_read_from_buffer - copy data from the buffer
 * @to: the buffer to read to
 * @count: the maximum number of bytes to read
 * @ppos: the current position in the buffer
 * @from: the buffer to read from
 * @available: the size of the buffer
 *
 * The simple_read_from_buffer() function reads up to @count bytes from the
 * buffer @from at offset @ppos into the address starting at @to.
 *
 * On success, the number of bytes read is returned and the offset @ppos is
 * advanced by this number, or negative value is returned on error.
 */
ssize_t simple_read_from_buffer(void *to, size_t count, off_t *ppos,
                const void *from, size_t available)
{
    off_t pos = *ppos;

    if (pos < 0)
        return -EINVAL;

    if (pos >= available || !count)
        return 0;

    if (count > available - pos)
        count = available - pos;

    memcpy(to, from + pos, count);

    *ppos = pos + count;

    return count;
}

#if 0
/*
 * simple_write_to_buffer - copy data to the buffer
 * @to: the buffer to write to
 * @available: the size of the buffer
 * @ppos: the current position in the buffer
 * @from: the buffer to read from
 * @count: the maximum number of bytes to read
 *
 * The simple_write_to_buffer() function reads up to @count bytes from the
 * address starting at @from into the buffer @to at offset @ppos.
 *
 * On success, the number of bytes written is returned and the offset @ppos is
 * advanced by this number, or negative value is returned on error.
 */
ssize_t simple_write_to_buffer(void *to, size_t available, off_t *ppos,
        const void *from, size_t count)
{
    off_t pos = *ppos;

    if (pos < 0)
        return -EINVAL;

    if (pos >= available || !count)
        return 0;

    if (count > available - pos)
        count = available - pos;

    memcpy(to + pos, from, count);

    *ppos = pos + count;

    return count;
}
#endif


static int dfs_debugfs_simple_attr_open(struct inode *inode, struct dfs_fd *file,
                    int (*get)(void *, u64 *), int (*set)(void *, u64),
                    const char *fmt)
{
    struct simple_attr *attr;

    attr = malloc(sizeof(struct simple_attr));
    if (!attr) {
        return -ENOMEM;
    }

    attr->get = get;
    attr->set = set;
    attr->data = inode->i_private;
    attr->fmt = fmt;
    mutex_init(&attr->mutex);

    inode->i_attr = attr;

    return 0;
}

static ssize_t dfs_debugfs_simple_attr_read(struct dfs_fd *file, char *buffer, size_t count, off_t *ppos)
{
    struct inode *inode = (struct inode *)file->data;
    struct simple_attr *attr = inode->i_attr;
    size_t size;
    size_t ret;

    if (!attr->get) {
        return -EACCES;
    }

    mutex_lock(&attr->mutex);

    if (*ppos) {
        /* continued read */
        size = strlen(attr->get_buf);
    } else {
        /* first read */
        u64 val;
        ret = attr->get(attr->data, &val);
        if (ret)
            goto out;

        size = snprintf(attr->get_buf, sizeof(attr->get_buf),
                 attr->fmt, (unsigned long long)val);
    }

    ret = simple_read_from_buffer(buffer, count, ppos, attr->get_buf, size);

out:
    mutex_unlock(&attr->mutex);
    return ret;
}

static ssize_t dfs_debugfs_simple_attr_write(struct dfs_fd *file, const char *buffer, size_t count, off_t *ppos)
{
    struct inode *inode = (struct inode *)file->data;
    struct simple_attr *attr = inode->i_attr;
    u64 val;
    size_t size;

    if (!attr->set) {
        return -EACCES;
    }

    mutex_lock(&attr->mutex);

    size = min(sizeof(attr->set_buf) - 1, count);
    memcpy(attr->set_buf, buffer, size);
    attr->set_buf[size] = '\0';
    val = strtol(attr->set_buf, NULL, 0);
    attr->set(attr->data, val);

    mutex_unlock(&attr->mutex);

    return count;
}

static int dfs_debugfs_simple_attr_release(struct inode *inode, struct dfs_fd *file)
{
    if (inode->i_attr) {
        free(inode->i_attr);
    }

    return 0;
}

static void __simple_attr_check_format(const char *fmt, ...)
{
    /* don't do anything, just let the compiler check the arguments; */
}

/*
 * simple attribute files
 *
 * Writing to an attribute immediately sets a value, an open file can be
 * written to multiple times.
 *
 * Reading from an attribute creates a buffer from the value that might get
 * read with multiple read calls. When the attribute has been read
 * completely, no further read calls are possible until the file is opened
 * again.
 *
 * All attributes contain a text representation of a numeric value
 * that are accessed with the get() and set() functions.
 */
#define DEFINE_SIMPLE_ATTRIBUTE(__fops, __get, __set, __fmt)                \
static int __fops ## _open(struct inode *inode, struct dfs_fd *file)        \
{                                                                           \
    __simple_attr_check_format(__fmt, 0ull);                                \
    return dfs_debugfs_simple_attr_open(inode, file, __get, __set, __fmt);  \
}                                                                           \
static const struct file_operations __fops = {                              \
    .open    = __fops ## _open,                                             \
    .release = dfs_debugfs_simple_attr_release,                             \
    .read    = dfs_debugfs_simple_attr_read,                                \
    .write   = dfs_debugfs_simple_attr_write,                               \
};

/*
 * u8 ops
 */
static int dfs_debugfs_u8_set(void *data, u64 val)
{
    *(u8 *)data = val;

    return 0;
}

static int dfs_debugfs_u8_get(void *data, u64 *val)
{
    *val = *(u8 *)data;

    return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_u8, dfs_debugfs_u8_get, dfs_debugfs_u8_set, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u8_ro, dfs_debugfs_u8_get, NULL, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u8_wo, NULL, dfs_debugfs_u8_set, "%llu");


struct inode *dfs_debugfs_create_u8(const char *name, mode_t mode,
                 struct inode *parent, u8 *value)
{
    /* if there are no write bits set, make read only */
    if (!(mode & S_IWUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u8_ro);

    /* if there are no read bits set, make write only */
    if (!(mode & S_IRUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u8_wo);

    return dfs_debugfs_create_file(name, mode, parent, value, &fops_u8);
}

/*
 * u16 ops
 */
static int dfs_debugfs_u16_set(void *data, u64 val)
{
    *(u16 *)data = val;
    return 0;
}

static int dfs_debugfs_u16_get(void *data, u64 *val)
{
    *val = *(u16 *)data;
    return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_u16, dfs_debugfs_u16_get, dfs_debugfs_u16_set, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u16_ro, dfs_debugfs_u16_get, NULL, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u16_wo, NULL, dfs_debugfs_u16_set, "%llu");


struct inode *dfs_debugfs_create_u16(const char *name, mode_t mode,
                  struct inode *parent, u16 *value)
{
    /* if there are no write bits set, make read only */
    if (!(mode & S_IWUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u16_ro);

    /* if there are no read bits set, make write only */
    if (!(mode & S_IRUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u16_wo);

    return dfs_debugfs_create_file(name, mode, parent, value, &fops_u16);
}

/*
 * u32
 */
static int dfs_debugfs_u32_set(void *data, u64 val)
{
    *(u32 *)data = val;
    return 0;
}

static int dfs_debugfs_u32_get(void *data, u64 *val)
{
    *val = *(u32 *)data;
    return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_u32, dfs_debugfs_u32_get, dfs_debugfs_u32_set, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u32_ro, dfs_debugfs_u32_get, NULL, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u32_wo, NULL, dfs_debugfs_u32_set, "%llu");

struct inode *dfs_debugfs_create_u32(const char *name, mode_t mode,
                 struct inode *parent, u32 *value)
{
    /* if there are no write bits set, make read only */
    if (!(mode & S_IWUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u32_ro);

    /* if there are no read bits set, make write only */
    if (!(mode & S_IRUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u32_wo);

    return dfs_debugfs_create_file(name, mode, parent, value, &fops_u32);
}

/*
 * u64
 */
static int dfs_debugfs_u64_set(void *data, u64 val)
{
    *(u64 *)data = val;
    return 0;
}

static int dfs_debugfs_u64_get(void *data, u64 *val)
{
    *val = *(u64 *)data;
    return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_u64, dfs_debugfs_u64_get, dfs_debugfs_u64_set, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u64_ro, dfs_debugfs_u64_get, NULL, "%llu");
DEFINE_SIMPLE_ATTRIBUTE(fops_u64_wo, NULL, dfs_debugfs_u64_set, "%llu");

struct inode *dfs_debugfs_create_u64(const char *name, mode_t mode,
                 struct inode *parent, u64 *value)
{
    /* if there are no write bits set, make read only */
    if (!(mode & S_IWUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u64_ro);

    /* if there are no read bits set, make write only */
    if (!(mode & S_IRUGO))
        return dfs_debugfs_create_file(name, mode, parent, value, &fops_u64_wo);

    return dfs_debugfs_create_file(name, mode, parent, value, &fops_u64);
}

/*
 * bool
 */
static inline int _strtobool(const char *s, bool *res)
{
    switch (s[0]) {
    case 'y':
    case 'Y':
    case '1':
        *res = true;
        break;
    case 'n':
    case 'N':
    case '0':
        *res = false;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static ssize_t read_file_bool(struct dfs_fd *file, char *user_buf,
                  size_t count, off_t *ppos)
{
    char buf[3];
    struct inode *inode = (struct inode *)file->data;
    u32 *val = inode->i_private;

    if (*val)
        buf[0] = 'Y';
    else
        buf[0] = 'N';

    buf[1] = '\n';
    buf[2] = 0x00;
    return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t write_file_bool(struct dfs_fd *file, const char *user_buf,
                   size_t count, off_t *ppos)
{
    char buf[32];
    size_t buf_size;
    bool bv;
    struct inode *inode = (struct inode *)file->data;
    u32 *val = inode->i_private;

    buf_size = min(count, (sizeof(buf)-1));
    memcpy(buf, user_buf, buf_size);

    if (_strtobool(buf, &bv) == 0)
        *val = bv;

    return count;
}

static const struct file_operations fops_bool = {
    .read       = read_file_bool,
    .write      = write_file_bool,
    .open       = NULL,
    .release    = NULL,
};

struct inode *dfs_debugfs_create_bool(const char *name, mode_t mode,
                   struct inode *parent, u32 *value)
{
    return dfs_debugfs_create_file(name, mode, parent, value, &fops_bool);
}

/*
 * array
 */
struct array_data {
    void *array;
    u32 elements;
};

static size_t __u32_format_array(char *buf, size_t bufsize, u32 *array, int array_size)
{
    size_t ret = 0;

    while (--array_size >= 0) {
        size_t len;
        char term = array_size ? ' ' : '\n';

        len = snprintf(buf, bufsize, "%u%c", *array++, term);
        ret += len;

        buf += len;
        bufsize -= len;
    }

    return ret;
}

static int u32_array_open(struct inode *inode, struct dfs_fd *file)
{
    struct array_data *data = inode->i_private;
    int size, elements = data->elements;
    char *buf;

    /*
     * Max size:
     *  - 10 digits + ' '/'\n' = 11 bytes per number
     *  - terminating NUL character
     */
    size = elements * 11;
    buf = malloc(size + 1);
    if (!buf)
        return -ENOMEM;

    buf[size] = 0;

    inode->i_attr = buf;
    __u32_format_array(buf, size, data->array, data->elements);

    return 0;
}

static ssize_t u32_array_read(struct dfs_fd *file, char *buf, size_t len, off_t *ppos)
{
    struct inode *inode = (struct inode *)file->data;

    size_t size = strlen(inode->i_attr);

    return simple_read_from_buffer(buf, len, ppos, inode->i_attr, size);
}

static ssize_t u32_array_write(struct dfs_fd *file, const char *buffer, size_t count, off_t *ppos)
{
    return -EACCES;
}

static int u32_array_release(struct inode *inode, struct dfs_fd *file)
{
    /* buffer */
    if (inode->i_attr) {
        free(inode->i_attr);
    }

    return 0;
}

static const struct file_operations u32_array_fops = {
    .open       = u32_array_open,
    .release    = u32_array_release,
    .read       = u32_array_read,
    .write      = u32_array_write,
};

struct inode *dfs_debugfs_create_u32_array(const char *name, mode_t mode,
                        struct inode *parent, u32 *array, u32 elements)
{
    struct array_data *data = malloc(sizeof(*data));

    if (data == NULL)
        return NULL;

    data->array = array;
    data->elements = elements;

    return dfs_debugfs_create_file(name, mode, parent, data, &u32_array_fops);
}

/*
 * Module API
 */
#include <module.h>

static ssize_t dfs_debugfs_module_read(struct dfs_fd *file, char *buffer, size_t count, off_t *ppos)
{
    struct inode *inode = (struct inode *)file->data;
    struct kernel_param *param = (struct kernel_param *)inode->i_private;
    size_t ret = 0;

    if (param && param->ops) {
        param->count = count;
        param->ppos = ppos;
        ret = param->ops->get(buffer, param);
    }

    return ret;
}

static ssize_t dfs_debugfs_module_write(struct dfs_fd *file, const char *buffer, size_t count, off_t *ppos)
{
    struct inode *inode = (struct inode *)file->data;
    struct kernel_param *param = (struct kernel_param *)inode->i_private;

    if (param && param->ops) {
        param->count = count;
        param->ppos = ppos;
        param->ops->set(buffer, param);
    }

    return count;
}

static int dfs_debugfs_module_release(struct inode *inode, struct dfs_fd *file)
{
    return 0;
}

static int dfs_debugfs_module_open(struct inode *inode, struct dfs_fd *file)
{
    return 0;
}

static const struct file_operations module_fops = {
    .open       = dfs_debugfs_module_open,
    .release    = dfs_debugfs_module_release,
    .read       = dfs_debugfs_module_read,
    .write      = dfs_debugfs_module_write,
};

struct inode *dfs_debugfs_create_module(const char *name, mode_t mode,
                        struct inode *parent, void *params)
{
    return dfs_debugfs_create_file(name, mode, parent, params, &module_fops);
}

