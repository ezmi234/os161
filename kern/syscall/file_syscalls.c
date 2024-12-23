/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <../../../userland/lib/libc/unix/errno.c>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <limits.h>
#include <current.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <vnode.h>
#include <vfs.h>
#include <openfile.h>

/*
 * simple file system calls for write/read
 */
int
sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd!=STDOUT_FILENO && fd!=STDERR_FILENO) {
    kprintf("sys_write supported only to stdout\n");
    return -1;
  }

  for (i=0; i<(int)size; i++) {
    putch(p[i]);
  }

  return (int)size;
}

int
sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd!=STDIN_FILENO) {
    kprintf("sys_read supported only to stdin\n");
    return -1;
  }

  for (i=0; i<(int)size; i++) {
    p[i] = getch();
    if (p[i] < 0) 
      return i;
  }

  return (int)size;
}

#if OPT_SHELL
int
sys_dup2(int oldfd, int newfd)
{
  // preliminary checks
  if (oldfd < 0 || oldfd > OPEN_MAX || newfd < 0 || newfd > OPEN_MAX) {
    errno = EBADF;
    return -1;
  }

  if (curproc->fileTable[oldfd] == NULL) {
    errno = EBADF;
    return -1;
  }

  // special case: oldfd = newfd
  if (oldfd == newfd) {
    return newfd;
  }

  // special case: newfd is already open
  if (curproc->fileTable[newfd] != NULL) {
    // sys_close
    return -1;
  }

  curproc->fileTable[newfd] = curproc->fileTable[oldfd];
  curproc->fileTable[newfd]->count++;
  return newfd;
}
#endif

#if OPT_SHELL
int
sys_chdir(const char *path)
{
  if (path == NULL) {
    errno = EFAULT;
    return -1;
  }

  char kbuf[PATH_MAX];
  struct vnode *new_dir;
  int result = copyinstr((const_userptr_t)path, kbuf, sizeof(kbuf), NULL);
  if (result) {
    errno = result;
    return -1;
  }

  result = vfs_open(kbuf, O_RDONLY, 0, &new_dir);
  if (result) {
    errno = result;
    return -1;
  }

  if (curproc->p_cwd != NULL) {
    vfs_close(curproc->p_cwd);
  }
  curproc->p_cwd = new_dir;
  return 0;
}
#endif

#if OPT_SHELL
char *
sys_getcwd(char buf[], size_t size)
{
  if (buf == NULL || size == 0) {
    errno = EINVAL;
    return NULL;
  }

  struct iovec iovec_buf;
  struct uio cwd_uio;

  uio_kinit(&iovec_buf, &cwd_uio, (userptr_t)buf, size, 0, UIO_READ);

  int result = vfs_getcwd(&cwd_uio);
  if (result) {
    errno = result;
    return NULL;
  }

  return buf;
}
#endif