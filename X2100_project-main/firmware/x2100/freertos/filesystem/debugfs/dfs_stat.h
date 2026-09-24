#ifndef __DFS_STAT_H__
#define __DFS_STAT_H__


#ifdef __cplusplus
extern "C" {
#endif


/* read/write permission */
#define S_IRWXUGO                       (S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO                       (S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO                         (S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO                         (S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO                         (S_IXUSR|S_IXGRP|S_IXOTH)



#ifdef __cplusplus
}
#endif

#endif /* __DFS_STAT_H__ */
