#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_debugfs.h>
#include "dfs_stat.h"

static struct debugfs_mount debugfs_mount;

static void dfs_debugfs_inode_dump_tree(struct inode *base, int level, unsigned int parent_flag);
static int __debugfs_remove_file(struct inode *base);

static int debugfs_result_to_dfs(DEBUGRESULT result)
{
    int status = EOK;

    switch (result) {
    case DER_OK:
        break;

    case DER_NO_FILE:
    case DER_NO_PATH:
    case DER_NO_FILESYSTEM:
        status = -ENOENT;
        break;

    case DER_INVALID_NAME:
        status = -EINVAL;
        break;

    case DER_EXIST:
    case DER_INVALID_OBJECT:
        status = -EEXIST;
        break;

    case DER_DISK_ERR:
    case DER_NOT_READY:
    case DER_INT_ERR:
        status = -EIO;
        break;

    case DER_WRITE_PROTECTED:
    case DER_DENIED:
        status = -EROFS;
        break;

    case DER_MKFS_ABORTED:
        status = -EINVAL;
        break;

    default:
        status = -1;
        break;
    }

    return status;
}

static int dfs_inode_initialize(struct inode *inode)
{
    memset(inode, 0x00, sizeof(struct inode));
    INIT_LIST_HEAD(&inode->l_child);
    INIT_LIST_HEAD(&inode->l_peer);
    inode->i_parent = NULL;

    /* Update total Memory Info */
    if (debugfs_mount.mnt_root) {
        debugfs_mount.mnt_root->mem_total += sizeof(struct inode);
    }

    return 0;
}

static int dfs_inode_destroy(struct inode *inode)
{
    assert(inode != NULL);

    /* Update total Memory Info */
    if (debugfs_mount.mnt_root) {
        debugfs_mount.mnt_root->mem_total -= sizeof(struct inode);
    }

    list_del(&inode->l_peer);
    free(inode);

    return 0;
}

static inline int dfs_inode_set_name(struct inode *inode, const char *name)
{
    int ret = 0;

    if (strlen(name) < sizeof(inode->name) -1) {
        memset(inode->name, 0x00, sizeof(inode->name));
        sprintf(inode->name, "%s", name);
    } else {
        ret = -1;
        printf("node name(%s) too long(%zu).\n", name, sizeof(inode->name));
    }

    return ret;
}

static int dfs_inode_insert_node(struct inode *parent, struct inode *child)
{
    struct list_head *pos;
    struct inode *inode;
    int ret = 0;
    assert(parent != NULL);
    assert(child != NULL);

    /* Check if Name Already exist */
    list_for_each(pos, &parent->l_child) {
        inode = list_entry(pos, struct inode, l_peer);
        if (!strcmp(child->name, inode->name)) {
            ret = -EEXIST;
            goto cmd_out;
        }
    }

    child->i_parent = parent;
    list_add(&child->l_peer, &parent->l_child);

cmd_out:
    return ret;
}

static int dfs_inode_remove_node(struct inode *base)
{
    assert(base != NULL);

    struct list_head *pos;
    struct list_head *next;
    struct inode *inode;

    list_for_each_safe(pos, next,&base->l_child) {
        inode = list_entry(pos, struct inode, l_peer);
        if (inode) {
            dfs_inode_remove_node(inode);
        }
    }

    dfs_inode_destroy(base);

    return 0;
}

static struct inode *dfs_debugfs_find_inode(struct inode *base, const char *name)
{
    struct list_head *pos;
    struct inode *inode;

    list_for_each(pos, &base->l_child) {
        inode = list_entry(pos, struct inode, l_peer);
        if (!strcmp(name, inode->name)) {
            break;
        }

        inode = NULL;
    }

    return inode;
}

static void dfs_debugfs_inode_dump_tree(struct inode *base, int level, unsigned int parent_flag)
{
    struct list_head *pos;
    struct inode *inode;

    int tree_level_tmp = 0;

    int tree_level_child_count = 0;
    int tree_level_child_walk = 0;

    int tree_level_parent = parent_flag;

    /* record parent flag */
    list_for_each(pos, &base->l_child) {
        tree_level_child_count++;
    }

    if (tree_level_child_count > 1) {
        tree_level_parent |= 1 << level;
    }

    list_for_each(pos, &base->l_child) {
        inode = list_entry(pos, struct inode, l_peer);

        if (inode) {
            /* walk parent node infomation */
            tree_level_tmp = 0;
            while(tree_level_tmp < level) {
                if (parent_flag & (1 << tree_level_tmp)) {
                    printf("│   ");
                } else {
                    printf("    ");
                }
                tree_level_tmp++;
            }

            /* show current node infomation */
            tree_level_child_walk++;
            if ( (tree_level_child_count == 1)
                    || (tree_level_child_count == tree_level_child_walk) ) {
                /* the first node info / the last node info */
                printf("└── ");
            } else {
                printf("├── ");
            }

            printf("%s\n", inode->name);
            dfs_debugfs_inode_dump_tree(inode, level + 1, tree_level_parent);
        }
    } /* end of list_for_each(... */
}

static struct inode *dfs_debugfs_walk_inode(DEBUGFS *debugfs, const char *path)
{
    char *sub_path;
    struct inode *inode;
    const char *delim = "/";

    assert(debugfs != NULL);
    assert(debugfs->root_node != NULL);

    /* default root node */
    inode = debugfs->root_node;
    sub_path = (char *)path;

    /* find inode */
    sub_path = strtok(sub_path, delim);
    while(sub_path != NULL && inode) {
        inode = dfs_debugfs_find_inode(inode, sub_path);

        sub_path = strtok(NULL, delim);
    }

    return inode;
}

static int dfs_debugfs_open(struct dfs_fd *file)
{
    struct dfs_filesystem *fs = (struct dfs_filesystem *)file->data;
    DEBUGFS *debugfs = (DEBUGFS *)fs->data;
    struct inode *inode;
    int ret = 0;

    inode = dfs_debugfs_walk_inode(debugfs, file->path);
    if (file->flags & O_DIRECTORY) {

        if (inode == NULL) {
            /* not find vaild inode */
            return -EACCES;
        }

        switch (inode->i_mode & S_IFMT) {
        case S_IFDIR:
            break;
        default:
            /* target inode is not directory */
            return -ENOTDIR;
        }

    } else {
        /* debugfs create file Permission denied */
        if (inode == NULL) {
            return -EACCES;
        }
    }

    struct list_head *pos;
    int child_count = 0;

    list_for_each(pos, &inode->l_child) {
        child_count++;
    }

    file->data = inode;
    file->size = child_count;

    if (file->flags & O_APPEND) {
        file->pos = file->size;
    } else {
        file->pos = 0;
    }

    if (inode->i_ops && inode->i_ops->open) {
        ret = inode->i_ops->open(inode, file);
    }

    return ret;
}

static int dfs_debugfs_close(struct dfs_fd *file)
{
    int ret = 0;
    struct inode *inode = file->data;

    if (inode->i_ops && inode->i_ops->release) {
        ret = inode->i_ops->release(inode, file);
        if (ret < 0) {
            printf("inode release failed. ret = %d\n", ret);
            return ret;
        }
    }

    file->data = NULL;
    file->size = 0;
    file->pos = 0;

    return 0;
}

static int dfs_debugfs_ioctl(struct dfs_fd *file, int cmd, void *args)
{
    return 0;
}

static int dfs_debugfs_read(struct dfs_fd *file, void *buf, size_t count)
{
    int ret = 0;
    struct inode *inode = file->data;

    if (inode->i_ops && inode->i_ops->read) {
        ret = inode->i_ops->read(file, buf, count, &file->pos);
    }

    return ret;
}

static int dfs_debugfs_write(struct dfs_fd *file, const void *buf, size_t count)
{
    int ret = 0;
    struct inode *inode = file->data;

    if (inode->i_ops && inode->i_ops->write) {
        ret = inode->i_ops->write(file, buf, count, &file->pos);
    }

    return ret;
}

static int dfs_debugfs_flush(struct dfs_fd *file)
{
    return 0;
}

static int dfs_debugfs_lseek(struct dfs_fd *file, off_t offset)
{
    return 0;
}

static int dfs_debugfs_getdents(struct dfs_fd *file, struct dirent *dirp, uint32_t count)
{
    size_t index, end;
    struct inode *base;
    struct dirent *d;

    /* open-ed directory */
    base = (struct inode *)file->data;

    assert(base != NULL);

    /* make integer count */
    count = (count / sizeof(struct dirent));
    if (count == 0)
        return -EINVAL;

    struct list_head *pos;
    struct inode *inode;

    index = 0;
    count = 0;
    end = 0;
    /* make sure directoty length */
    list_for_each(pos, &base->l_child) {
        end++;
    }

    /* find valid inode infomation */
    list_for_each(pos, &base->l_child) {
        inode = list_entry(pos, struct inode, l_peer);
        if (inode) {
            if (index >= file->pos && index < end) {
                d = dirp ;
                d->d_type = DT_REG;
                d->d_namlen = NODE_NAME_MAX;
                d->d_reclen = (uint16_t)sizeof(struct dirent);
                strncpy(d->d_name, inode->name, NODE_NAME_MAX);

                count = 1;
                file->pos += 1;
                break;
            }
        }

        index += 1;
    } /* end of list_for_each(... */

    return (count * sizeof(struct dirent));
}

static int dfs_debugfs_mount(struct dfs_filesystem *fs, unsigned long rwflag, const void *data)
{
    DEBUGFS *debugfs;
    DEBUGRESULT ret = DER_OK;

    /* mount point */
    debugfs = (DEBUGFS *)malloc(sizeof(DEBUGFS));
    if (debugfs == NULL) {
        return -ENOMEM;
    }

    memset(debugfs, 0x00, sizeof(DEBUGFS) );
    debugfs->root_node = (struct inode *)malloc(sizeof(struct inode));
    if (debugfs->root_node == NULL) {
        free(debugfs);
        return -ENOMEM;
    }

    debugfs_mount.mnt_root = debugfs;
    debugfs_mount.mnt_flags = 1;

    dfs_inode_initialize(debugfs->root_node);
    dfs_inode_set_name(debugfs->root_node, "root");
    debugfs->mem_total += sizeof(DEBUGFS);
    debugfs->root_node->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;

    fs->data = debugfs;

    return debugfs_result_to_dfs(ret);
}

static int dfs_debugfs_unmount(struct dfs_filesystem *fs)
{
    DEBUGFS *debugfs;

    debugfs = (DEBUGFS *)fs->data;
    assert(debugfs != NULL);

    if (debugfs->root_node) {
        __debugfs_remove_file(debugfs->root_node);
        debugfs->root_node = NULL;
    }

    free(debugfs);

    return 0;
}

static int dfs_debugfs_statfs(struct dfs_filesystem *fs, struct statfs *buf)
{
    DEBUGFS *debugfs;

    debugfs = (DEBUGFS *)fs->data;
    assert(debugfs != NULL);

    buf->f_bfree = 0;
    buf->f_blocks = debugfs->mem_total;
    buf->f_bsize = 1;

    return 0;
}

static int dfs_debugfs_unlink(struct dfs_filesystem *fs, const char *pathname)
{
    return 0;
}

static int dfs_debugfs_stat(struct dfs_filesystem *fs, const char *filename, struct stat *buf)
{
    DEBUGFS *debugfs;
    struct inode *base;

    debugfs = (DEBUGFS *)fs->data;
    base = dfs_debugfs_walk_inode(debugfs, filename);
    if (base == NULL) {
        return -EACCES;
    }

    buf->st_dev = 0;
    buf->st_mode = base->i_mode;
    buf->st_size = 4096;
    buf->st_mtime = 0;

    return 0;
}

static int dfs_debugfs_rename(struct dfs_filesystem *fs, const char *oldpath, const char *newpath)
{
    return 0;
}

struct dfs_file_ops dfs_debugfs_fops = {
    .open           = dfs_debugfs_open,
    .close          = dfs_debugfs_close,
    .ioctl          = dfs_debugfs_ioctl,
    .read           = dfs_debugfs_read,
    .write          = dfs_debugfs_write,
    .flush          = dfs_debugfs_flush,
    .lseek          = dfs_debugfs_lseek,
    .getdents       = dfs_debugfs_getdents,
    .poll           = NULL, /* poll interface */
};

static const struct dfs_filesystem_ops dfs_debugfs = {
    .name           = "debugfs",
    .flags          = DFS_FS_FLAG_DEFAULT,
    .fops           = &dfs_debugfs_fops,

    .mount          = dfs_debugfs_mount,
    .unmount        = dfs_debugfs_unmount,
    .mkfs           = NULL,
    .statfs         = dfs_debugfs_statfs,

    .unlink         = dfs_debugfs_unlink,
    .stat           = dfs_debugfs_stat,
    .rename         = dfs_debugfs_rename,
};

static void dfs_debugfs_lock(struct inode *inode)
{

}

static void dfs_debugfs_unlock(struct inode *inode)
{

}

struct inode *_lookup_alloc_one_node(const char *name, struct inode *base, int len)
{
    struct inode *inode;
    int ret = 0;

    if (!len) {
        return ERR_PTR(-EACCES);
    }

    if (name[0] == '.') {
        if (len < 2 || (len == 2 && name[1] == '.')) {
            return ERR_PTR(-EACCES);
        }
    }

    if (name[0] == '/') {
        return ERR_PTR(-EACCES);
    }

    /*
     * Node init
     */
    inode = malloc(sizeof(struct inode));
    if (!inode) {
        printf("malloc node failed\n");
        return ERR_PTR(-ENOMEM);
    }

    dfs_inode_initialize(inode);
    dfs_inode_set_name(inode, name);
    ret = dfs_inode_insert_node(base, inode);
    if (ret < 0) {
        free(inode);
        return ERR_PTR(ret);
    }

    return inode;
}

static ssize_t default_read_file(struct dfs_fd *file, char *buf, size_t count, off_t *ppos)
{
    return 0;
}

static ssize_t default_write_file(struct dfs_fd *file, const char *buf, size_t count, off_t *ppos)
{
    return count;
}

const struct file_operations debugfs_file_operations = {
    .read           = default_read_file,
    .write          = default_write_file,
    .open           = NULL,
    .release        = NULL,
};

static int debugfs_set_inode_ops(struct inode *inode, mode_t mode, void *data,
            const struct file_operations *fops)
{
    assert(inode != NULL);

    inode->i_mode = mode;
    switch (mode & S_IFMT) {
    case S_IFREG:
        inode->i_ops = fops ? fops : &debugfs_file_operations;
        inode->i_private = data;
        break;

    case S_IFLNK:
        /* not Support */
        inode->i_ops = NULL;
        inode->i_private = data;
        break;

    case S_IFDIR:
    default:
        break;
    }

    return 0;
}

static int debugfs_mknod(struct inode *inode, mode_t mode, void *data,
             const struct file_operations *fops)
{
    return debugfs_set_inode_ops(inode, mode, data, fops);
}

static int debugfs_inode_mkdir(struct inode *inode, mode_t mode)
{
    int res;

    mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
    res = debugfs_mknod(inode, mode, NULL, NULL);

    return res;
}

static int debugfs_inode_create_file(struct inode *inode, mode_t mode,
              void *data, const struct file_operations *fops)
{
    int res;

    mode = (mode & S_IALLUGO) | S_IFREG;
    res = debugfs_mknod(inode, mode, data, fops);

    return res;
}

static struct inode *__debugfs_create_file(const char *name, mode_t mode,
                    struct inode *parent, void *data,
                    const struct file_operations *fops)
{
    struct inode *dentry;
    int error = 0;

    if (!debugfs_mount.mnt_flags) {
        printf("DebugFS not mounted, please mount first\n");
        return NULL;
    }

    if (!parent)
        parent = debugfs_mount.mnt_root->root_node;

    dfs_debugfs_lock(parent);
    dentry = _lookup_alloc_one_node(name, parent, strlen(name));
    if (!IS_ERR(dentry)) {
        switch (mode & S_IFMT) {
        case S_IFDIR:
            error = debugfs_inode_mkdir(dentry, mode);

            break;
        case S_IFLNK:
            /* Not Support */
            break;
        default:
            error = debugfs_inode_create_file(dentry, mode, data, fops);
            break;
        }
    } else {
        error = PTR_ERR(dentry);
    }

    dfs_debugfs_unlock(parent);

    return (error < 0) ? NULL : dentry;
}

static int __debugfs_remove_file(struct inode *base)
{
    if (base == NULL) {
        return -EINVAL;
    }

    dfs_debugfs_lock(base);

    dfs_inode_remove_node(base);

    dfs_debugfs_unlock(base);

    return 0;
}

/*
 * Public
 */
void dfs_debugfs_dump_tree(struct inode *base)
{
    assert(base != NULL);

    printf("%s\n", base->name);
    dfs_debugfs_inode_dump_tree(base, 0, 0);
}

struct inode *dfs_debugfs_create_dir(const char *name, struct inode *parent)
{
    return __debugfs_create_file(name, S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
                   parent, NULL, NULL);
}

struct inode *dfs_debugfs_create_file(const char *name, mode_t mode,
                   struct inode *parent, void *data,
                   const struct file_operations *fops)
{
    switch (mode & S_IFMT) {
    case S_IFREG:
    case 0:
        break;
    default:
        hang();
    }

    return __debugfs_create_file(name, mode, parent, data, fops);
}

void dfs_debugfs_remove_recursive(struct inode *inode)
{
    __debugfs_remove_file(inode);
}

int dfs_debugfs_init(void)
{
    /* register fatfs file system */
    dfs_register(&dfs_debugfs);

    return 0;
}
