#ifndef __DFS_DEBUGFS_H__
#define __DFS_DEBUGFS_H__

#include <list.h>
#include <dfs_file.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATH_MAX                        (128)
#define NODE_NAME_MAX                   (32)

typedef enum {
    DER_OK = 0,              /* (0) Succeeded */
    DER_DISK_ERR,            /* (1) A hard error occurred in the low level disk I/O layer */
    DER_INT_ERR,             /* (2) Assertion failed */
    DER_NOT_READY,           /* (3) The physical drive cannot work */
    DER_NO_FILE,             /* (4) Could not find the file */
    DER_NO_PATH,             /* (5) Could not find the path */
    DER_INVALID_NAME,        /* (6) The path name format is invalid */
    DER_DENIED,              /* (7) Access denied due to prohibited access or directory full */
    DER_EXIST,               /* (8) Access denied due to prohibited access */
    DER_INVALID_OBJECT,      /* (9) The file/directory object is invalid */
    DER_WRITE_PROTECTED,     /* (10) The physical drive is write protected */
    DER_INVALID_DRIVE,       /* (11) The logical drive number is invalid */
    DER_NOT_ENABLED,         /* (12) The volume has no work area */
    DER_NO_FILESYSTEM,       /* (13) There is no valid FAT volume */
    DER_MKFS_ABORTED,        /* (14) The f_mkfs() aborted due to any problem */
    DER_TIMEOUT,             /* (15) Could not get a grant to access the volume within defined period */
    DER_LOCKED,              /* (16) The operation is rejected according to the file sharing policy */
    DER_NOT_ENOUGH_CORE,     /* (17) LFN working buffer could not be allocated */
    DER_TOO_MANY_OPEN_FILES, /* (18) Number of open files > _FS_LOCK */
    DER_INVALID_PARAMETER    /* (19) Given parameter is invalid */
} DEBUGRESULT;


struct inode;

struct file_operations {
    ssize_t (*read) (struct dfs_fd *, char *, size_t, off_t *);
    ssize_t (*write) (struct dfs_fd *, const char *, size_t, off_t *);
    int (*open) (struct inode *, struct dfs_fd *);
    int (*release) (struct inode *, struct dfs_fd *);
};


struct inode {
    char              name[NODE_NAME_MAX];
    struct inode      *i_parent;    /* Link to parent inode */
    struct list_head  l_peer;       /* Link to same level inode */
    struct list_head  l_child;      /* Link to lower(child) level inode */
    mode_t            i_mode;      /* Access mode flags */
    void              *i_private;  /* Per inode driver private data */
    const struct file_operations  *i_ops;    /* Driver operations for inode */
    void              *i_attr;  /* Per inode driver attribute */
};


/* File system object structure (DebugFS) */
typedef struct {
    struct inode *root_node;
    u64 mem_total;      /* Unit:Bytes */
} DEBUGFS;

struct debugfs_mount {
    DEBUGFS *mnt_root;    /* root of the mounted tree */
    int mnt_flags;
};


/*
 * Public API
 */
void dfs_debugfs_dump_tree(struct inode *base);

struct inode *dfs_debugfs_create_dir(const char *name, struct inode *parent);

struct inode *dfs_debugfs_create_file(const char *name, mode_t mode,
                   struct inode *parent, void *data,
                   const struct file_operations *fops);

void dfs_debugfs_remove_recursive(struct inode *inode);

int dfs_debugfs_init(void);

/*
 * operation types
 */
struct inode *dfs_debugfs_create_u8(const char *name, mode_t mode,
                 struct inode *parent, u8 *value);

struct inode *dfs_debugfs_create_u16(const char *name, mode_t mode,
                  struct inode *parent, u16 *value);

struct inode *dfs_debugfs_create_u32(const char *name, mode_t mode,
                 struct inode *parent, u32 *value);

struct inode *dfs_debugfs_create_u64(const char *name, mode_t mode,
                 struct inode *parent, u64 *value);

struct inode *dfs_debugfs_create_bool(const char *name, mode_t mode,
                   struct inode *parent, u32 *value);

struct inode *dfs_debugfs_create_u32_array(const char *name, mode_t mode,
                        struct inode *parent,
                        u32 *array, u32 elements);

struct inode *dfs_debugfs_create_module(const char *name, mode_t mode,
                        struct inode *parent, void *params);

#ifdef __cplusplus
}
#endif

#endif /* __DFS_DEBUGFS_H__ */
