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

#define MAX_OPEN_FILES 128 // Define maximum open files per process

#if OPT_SHELL
ssize_t sys_write(int fd, const void *buf, size_t buflen, int32_t *retval) {

    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX) {                                 /* fd should be a valid number */
        return EBADF;
    }
    struct openfile *file_entry = curproc->fileTable[fd];
    if (file_entry == NULL) {                                       /* fd should refer to a valid entry */
        return EBADF;
    }
    if ((file_entry->mode & O_ACCMODE) == O_RDONLY) {               /* fd should be writable */
        return EBADF;
    }

    /* COPYING BUFFER TO KERNEL SIDE */
    char *kbuffer = (char *)kmalloc(buflen);
    if (kbuffer == NULL) {
        return ENOMEM;
    }
    if (copyin((const_userptr_t)buf, kbuffer, buflen)) {
        kfree(kbuffer);
        return EFAULT;
    }

    /* SETUP FOR WRITING */
    struct iovec iov;
    struct uio uio_write;
    struct vnode *vn = file_entry->vn;

    /* ENSURE FILE IS WRITABLE */
    if (vn == NULL) {
        kfree(kbuffer);
        return EIO;
    }

    lock_acquire(file_entry->lock);
    uio_kinit(&iov, &uio_write, kbuffer, buflen, file_entry->offset, UIO_WRITE);
    int error = VOP_WRITE(vn, &uio_write);
    if (error) {
        kfree(kbuffer);
        lock_release(file_entry->lock);
        return error;
    }

    /* UPDATE OFFSET */
    off_t bytes_written = buflen - uio_write.uio_resid;
    *retval = (int32_t)bytes_written;
    file_entry->offset = uio_write.uio_offset;

    /* CLEANUP */
    lock_release(file_entry->lock);
    kfree(kbuffer);

    return 0;
}

#endif

/**
 * @brief sys_read_SHELL() reads up to buflen bytes from the file specified by fd, at the 
 *        location in the file specified by the current seek position of the file, and 
 *        stores them in the space pointed to by buf. The file must be open for reading.
 *  
 *        The current seek position of the file is advanced by the number of bytes read. 
 * 
 * @param fd source file
 * @param buf destination buffer
 * @param count number of bytes to be read
 * @return zero on success. an error value in case of failure
*/
#if OPT_SHELL
ssize_t sys_read(int fd, const void *buf, size_t buflen, int32_t *retval) {

    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX) {                                 /* fd should be a valid number                          */
        return EBADF;       
    } else if (curproc->fileTable[fd] == NULL) {                    /* fd should refer to a valid entry in the fileTable    */
        return EBADF;
    } else if (curproc->fileTable[fd]->mode == O_WRONLY) {     /* fd should refer to a file allowed to be read         */
        return EBADF;
    } else if (buf == NULL) {
        return EFAULT;
    } 

    /* PREPARING KERNEL BUFFER */
    char *kbuffer = (char *) kmalloc((buflen+1) * sizeof(char));
    if (kbuffer == NULL) {
        return ENOMEM;
    }

    /* PERFORMING READING (VOP_READ()) */
    struct openfile *of = curproc->fileTable[fd];
    struct iovec iov;
    struct uio kuio;
    struct vnode *vn = of->vn;
    lock_acquire(of->lock);
    uio_kinit(&iov, &kuio, kbuffer, buflen, of->offset, UIO_READ);
    int err = VOP_READ(vn, &kuio);
    if (err) {
        kfree(kbuffer);
        lock_release(of->lock);
        return err;
    }

    /* REPOSITION OF THE OFFSET */
    of->offset = kuio.uio_offset;
    *retval = buflen - kuio.uio_resid;

    /* COPYING BUFFER TO USER SIDE (COPYOUT()) */
    err = copyout(kbuffer, (userptr_t) buf, *retval);
    if (err) {
        kfree(kbuffer);
        lock_release(of->lock);
        return EFAULT;
    }

    /* FREEING KERNEL BUFFER */
    lock_release(of->lock);
    kfree(kbuffer);

    /* TASK COMPLETED SUCCESSFULLY */
    return 0;
}
#endif

/* Open a file */
int sys_open(const char *pathname, int flags, mode_t mode, int *retval) {

    /* CHECKING INPUT ARGUMENTS */
    if (pathname == NULL) {
        return EFAULT;
    }

    /* COPYING PATHNAME TO KERNEL SIDE */
    // this is done for two reasons:
    // 1) security reason
    // 2) vfs_open may destroy the buffer
    char *kbuffer = (char *) kmalloc(PATH_MAX * sizeof(char));
    if (kbuffer == NULL) {
        return ENOMEM;
    }
    size_t len;
    int err = copyinstr((const_userptr_t) pathname, kbuffer, PATH_MAX, &len); // may return EFAULT
    if (err) {
        kfree(kbuffer);
        return EFAULT;
    }

    /* OPENING WITH VFS UTILITY */
    struct vnode *v;
    err = vfs_open(kbuffer, flags, mode, &v);
    if (err) {
        kfree(kbuffer);
        return err;
    }
    kfree(kbuffer);

    /* RETRIEVING A FREE POSITION IN THE SYSTEM FILETABLE */
    struct openfile *of = NULL;
    for (int i = 3; i < OPEN_MAX; i++) {
        if (curproc->fileTable[i] == NULL) {
            of = (struct openfile *) kmalloc(sizeof(struct openfile));
            if (of == NULL) {
                vfs_close(v);
                return ENOMEM;
            }
            of->vn = v;
            of->offset = 0;
            of->mode = flags;
            of->count = 1;
            of->lock = lock_create("FILE_LOCK");
            if (of->lock == NULL) {
                vfs_close(v);
                kfree(of);
                return ENOMEM;
            }
            curproc->fileTable[i] = of;
            *retval = i;
            return 0;
        }
    }

    /* ASSIGNING OPENFILE TO CURRENT PROCESS FILETABLE */
    int fd = 3;     // skipping STDIN, STDOUT and STDERR
    if (of == NULL) {
        return ENFILE;  // system file table is full
    } else {
        for (; fd < OPEN_MAX; fd++) {
            if (curproc->fileTable[fd] == NULL) {
                curproc->fileTable[fd] = of;
                break;
            }
        }

        if (fd == OPEN_MAX - 1) {
            return EMFILE;  // process file table is full
        }

    }

    /* MANAGING OFFSET */
    // if flag specified O_APPEND, the operation on the file should start at the end
    // otherwise, it should start at the beginning
    if (flags & O_APPEND) {

            /* RETRIEVING FILE SIZE */
            struct stat filestat;
            err = VOP_STAT(curproc->fileTable[fd]->vn, &filestat);
            if (err) {
                kfree(curproc->fileTable[fd]);
                curproc->fileTable[fd] = NULL;
                return err;
            }
            curproc->fileTable[fd]->offset = filestat.st_size;
    } else {

            /* STARTING FROM THE BEGINNING */
            curproc->fileTable[fd]->offset = 0;
    }

    /* MANAGING REFERENCES */
    curproc->fileTable[fd]->count = 1;

    /* MANAGING MODE */
    switch(flags & O_ACCMODE){
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
			vfs_close(curproc->fileTable[fd]->vn);
			kfree(curproc->fileTable[fd]);
			curproc->fileTable[fd] = NULL;
			return EINVAL;
	}

    /* CREATING LOCK ON THIS FILE */
    curproc->fileTable[fd]->lock = lock_create("FILE_LOCK");
    if (curproc->fileTable[fd]->lock == NULL) {
        vfs_close(curproc->fileTable[fd]->vn);
        kfree(curproc->fileTable[fd]);
        curproc->fileTable[fd] = NULL;
        return ENOMEM;
    }

    /* TASK COMPLETED SUCCESSFULLY */
    *retval = fd;
    return 0;
}

int sys_close(int fd)
{
    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX) {                                 /* fd should be a valid number                          */
        return EBADF;       
    } else if (curproc->fileTable[fd] == NULL) {                    /* fd should refer to a valid entry in the fileTable    */
        return EBADF;
    }

    /* REDUCING REFERENCES */
    struct openfile *of = curproc->fileTable[fd];
    lock_acquire(of->lock);
    curproc->fileTable[fd] = NULL;
    if (--of->count > 0) {

        /* THIS FILE IS STILL REFERENCED BY SOME PROCESS */
        lock_release(of->lock);
        return 0;
    } else {

        /* NO MORE PROCESS REFER TO THIS FILE, CLOSING ALSO VNODE */
        struct vnode *vn = of->vn;
        of->vn = NULL;
        vfs_close(vn);
    }

    lock_release(of->lock);
    return 0;
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