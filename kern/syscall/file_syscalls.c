/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>
#include <openfile.h>
#include <uio.h>
#include <vnode.h>
#include <syscall.h>
#include <limits.h>
#include <lib.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <synch.h>

#if OPT_SHELL

#define SYSTEM_OPEN_MAX (10*OPEN_MAX)

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

#define MAX_OPEN_FILES 128 // Define maximum open files per process

/* Open a file */
int sys_open(userptr_t pathname, int openflags, mode_t mode, int *retval) {

    /* CHECKING INPUT ARGUMENTS */
    if (pathname == NULL) {
        return EFAULT;
    }

    /* COPYING PATHNAME TO KERNEL SIDE */
    char *kbuffer = (char *)kmalloc(PATH_MAX * sizeof(char));
    if (kbuffer == NULL) {
        return ENOMEM;
    }
    size_t len;
    int err = copyinstr((const_userptr_t)pathname, kbuffer, PATH_MAX, &len);
    if (err) {
        kfree(kbuffer);
        return EFAULT;
    }

    /* OPENING WITH VFS UTILITY */
    struct vnode *v;
    err = vfs_open(kbuffer, openflags, mode, &v);
    if (err) {
        kfree(kbuffer);
        return err;
    }
    kfree(kbuffer);

    /* RETRIEVING A FREE POSITION IN THE SYSTEM FILETABLE */
    struct openfile *of = NULL;
    for (int index = 0; index < MAX_OPEN_FILES; index++) {
        if (systemFileTable[index].vn == NULL) {
            of = &systemFileTable[index];
            of->vn = v;
            break;
        }
    }

    /* ASSIGNING OPENFILE TO CURRENT PROCESS FILETABLE */
    int fd = 3; // Skipping STDIN, STDOUT, and STDERR
    if (of == NULL) {
        vfs_close(v);
        return ENFILE; // System file table is full
    } else {
        for (; fd < OPEN_MAX; fd++) {
            if (curproc->fileTable[fd] == NULL) {
                curproc->fileTable[fd] = of;
                break;
            }
        }

        if (fd == OPEN_MAX) {
            vfs_close(v);
            return EMFILE; // Process file table is full
        }
    }

    /* MANAGING OFFSET */
    if (openflags & O_APPEND) {
        struct stat filestat;
        err = VOP_STAT(of->vn, &filestat);
        if (err) {
            curproc->fileTable[fd] = NULL;
            vfs_close(v);
            return err;
        }
        curproc->fileTable[fd]->offset = filestat.st_size;
    } else {
        curproc->fileTable[fd]->offset = 0;
    }

    /* MANAGING REFERENCES */
    curproc->fileTable[fd]->count = 1;

    /* MANAGING MODE */
    switch (openflags & O_ACCMODE) {
        case O_RDONLY:
            curproc->fileTable[fd]->mode = O_RDONLY;
            break;
        case O_WRONLY:
            curproc->fileTable[fd]->mode = O_WRONLY;
            break;
        case O_RDWR:
            curproc->fileTable[fd]->mode = O_RDWR;
            break;
        default:
            vfs_close(of->vn);
            curproc->fileTable[fd] = NULL;
            return EINVAL;
    }

    /* CREATING LOCK ON THIS FILE */
    curproc->fileTable[fd]->lock = lock_create("FILE_LOCK");
    if (curproc->fileTable[fd]->lock == NULL) {
        vfs_close(of->vn);
        curproc->fileTable[fd] = NULL;
        return ENOMEM;
    }

    /* TASK COMPLETED SUCCESSFULLY */
    *retval = fd;
    return 0;
}


int sys_close(int fd)
{
  struct openfile *file;

  // Validate the file descriptor
  if (fd < 0 || fd >= OPEN_MAX || curproc->fileTable[fd] == NULL)
  {
    return EBADF; // Invalid file descriptor
  }

  file = curproc->fileTable[fd];
  lock_acquire(file->lock);

  // Decrement reference count and close if no references remain
  file->count--;
  if (file->count == 0)
  {
    vfs_close(file->vn);
    lock_release(file->lock);
    lock_destroy(file->lock);
    kfree(file);
  }
  else
  {
    lock_release(file->lock);
  }

  curproc->fileTable[fd] = NULL;
  return 0; // Success
}

off_t sys_lseek(int fd, off_t offset, int whence)
{
  struct openfile *file;
  struct stat statbuf;
  off_t new_offset;

  // Validate the file descriptor
  if (fd < 0 || fd >= OPEN_MAX || curproc->fileTable[fd] == NULL)
  {
    return -1; // Invalid file descriptor
  }

  file = curproc->fileTable[fd];
  lock_acquire(file->lock);

  // Determine the new offset based on whence
  switch (whence)
  {
  case SEEK_SET:
    if (offset < 0)
    {
      lock_release(file->lock);
      return -1; // Invalid offset
    }
    new_offset = offset;
    break;

  case SEEK_CUR:
    new_offset = file->offset + offset;
    if (new_offset < 0)
    {
      lock_release(file->lock);
      return -1; // Invalid offset
    }
    break;

  case SEEK_END:
    VOP_STAT(file->vn, &statbuf);
    new_offset = statbuf.st_size + offset;
    if (new_offset < 0)
    {
      lock_release(file->lock);
      return -1; // Invalid offset
    }
    break;

  default:
    lock_release(file->lock);
    return -1; // Invalid whence
  }

  file->offset = new_offset;
  lock_release(file->lock);
  return new_offset; // Return the new file offset
}

/*
 * simple file system calls for write/read
 */
int sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
  {
    kprintf("sys_write supported only to stdout\n");
    return -1;
  }

  for (i = 0; i < (int)size; i++)
  {
    putch(p[i]);
  }

  return (int)size;
}

int sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd != STDIN_FILENO)
  {
    kprintf("sys_read supported only to stdin\n");
    return -1;
  }

  for (i = 0; i < (int)size; i++)
  {
    p[i] = getch();
    if (p[i] < 0)
      return i;
  }

  return (int)size;
}

#endif
#if OPT_SHELL
int
sys_dup2(int oldfd, int newfd)
{
  // preliminary checks
  if (oldfd < 0 || oldfd > OPEN_MAX || newfd < 0 || newfd > OPEN_MAX) {
    return EBADF;
    
  }

  if (curproc->fileTable[oldfd] == NULL) {
    return EBADF;
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
    return EFAULT;
  }

  char kbuf[PATH_MAX];
  struct vnode *new_dir;
  int result = copyinstr((const_userptr_t)path, kbuf, sizeof(kbuf), NULL);
  if (result) {
    return result;
  }

  result = vfs_open(kbuf, O_RDONLY, 0, &new_dir);
  if (result) {
    return result;
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
    //errno = EINVAL;
    return NULL;
  }

  struct iovec iovec_buf;
  struct uio cwd_uio;

  uio_kinit(&iovec_buf, &cwd_uio, (userptr_t)buf, size, 0, UIO_READ);

  int result = vfs_getcwd(&cwd_uio);
  if (result) {
    //errno = result;
    return NULL;
  }

  return buf;
}
#endif