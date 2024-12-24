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
#include <lib.h>
#include <../../../userland/lib/libc/unix/errno.c>

#if OPT_SHELL

#define MAX_OPEN_FILES 128 // Define maximum open files per process

/* Open a file */
int sys_open(const char *pathname, int flags, mode_t mode)
{
  struct vnode *vn;
  struct openfile *file;
  char kpath[PATH_MAX];
  int fd, result;

  // Copy the pathname from user space
  result = copyinstr((userptr_t)pathname, kpath, sizeof(kpath), NULL);
  if (result)
  {
    return result; // Return error code
  }

  // Open the vnode using vfs_open
  result = vfs_open(kpath, flags, mode, &vn);
  if (result)
  {
    return result; // Return error code
  }

  // Allocate and initialize an openfile structure
  file = kmalloc(sizeof(struct openfile));
  if (file == NULL)
  {
    vfs_close(vn);
    return ENOMEM;
  }

  file->vn = vn;
  file->offset = 0;
  file->mode = flags;
  file->count = 1;
  file->lock = lock_create("openfile lock");
  if (file->lock == NULL)
  {
    vfs_close(vn);
    kfree(file);
    return ENOMEM;
  }

  // Find an available file descriptor
  for (fd = 0; fd < OPEN_MAX; fd++)
  {
    if (curproc->fileTable[fd] == NULL)
    {
      curproc->fileTable[fd] = file;
      return fd; // Return file descriptor
    }
  }

  // No free file descriptors
  lock_destroy(file->lock);
  vfs_close(vn);
  kfree(file);
  return EMFILE; // Too many open files
}

/* Close a file */
#include <openfile.h>
#include <kern/errno.h>
#include <proc.h>
#include <current.h>

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

/* Seek within a file */
#include <openfile.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <vnode.h>
#include <proc.h>
#include <current.h>
#include <kern/errno.h>

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