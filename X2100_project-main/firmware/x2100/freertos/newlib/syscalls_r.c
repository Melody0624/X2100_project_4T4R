/*
 * Reentrant wrappers for newlib system calls
 *
 * Newlib's libc uses reentrant versions of system calls (_xxx_r),
 * but libnosys.a only provides non-reentrant versions (xxx).
 * This file provides the necessary wrappers.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

/* External declarations for non-reentrant versions from libnosys or project */
extern int open(const char *path, int flags, ...);
extern int read(int fd, void *buf, size_t count);
extern int write(int fd, const void *buf, size_t count);
extern off_t lseek(int fd, off_t offset, int whence);
extern int close(int fd);
extern int fstat(int fd, struct stat *buf);
extern int isatty(int fd);
extern int unlink(const char *path);
extern int stat(const char *path, struct stat *buf);
extern int link(const char *old, const char *new);
extern int rename(const char *old, const char *new);
extern int gettimeofday(struct timeval *tv, void *tz);

/* Reentrant wrappers */
int _read_r(void *reent, int fd, void *buf, size_t count)
{
    (void)reent;
    return read(fd, buf, count);
}

int _write_r(void *reent, int fd, const void *buf, size_t count)
{
    (void)reent;
    return write(fd, buf, count);
}

off_t _lseek_r(void *reent, int fd, off_t offset, int whence)
{
    (void)reent;
    return lseek(fd, offset, whence);
}

int _close_r(void *reent, int fd)
{
    (void)reent;
    return close(fd);
}

int _fstat_r(void *reent, int fd, struct stat *buf)
{
    (void)reent;
    return fstat(fd, buf);
}

int _isatty_r(void *reent, int fd)
{
    (void)reent;
    return isatty(fd);
}

int _open_r(void *reent, const char *path, int flags, int mode)
{
    (void)reent;
    (void)mode;  /* mode parameter may not be used by project's open() */
    return open(path, flags);
}

int _unlink_r(void *reent, const char *path)
{
    (void)reent;
    return unlink(path);
}

int _stat_r(void *reent, const char *path, struct stat *buf)
{
    (void)reent;
    return stat(path, buf);
}

int _link_r(void *reent, const char *old, const char *new)
{
    (void)reent;
    return link(old, new);
}

int _rename_r(void *reent, const char *old, const char *new)
{
    (void)reent;
    return rename(old, new);
}

int _gettimeofday_r(void *reent, struct timeval *tv, void *tz)
{
    (void)reent;
    return gettimeofday(tv, tz);
}
