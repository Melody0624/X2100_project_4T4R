#ifndef __DFS_SHELL_H__
#define __DFS_SHELL_H__

void ls(const char *pathname);
void rm(const char *filename);
void cat(const char *filename);
void copy(const char *src, const char *dst);

/**************************************/
void mkfs(const char *fs_name, const char *device_name);
int df(const char *path);

#endif