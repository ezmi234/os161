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

// ssize_t sys_read(int fd, const void *usr_buf, size_t len, int32_t *retval) {

//     /* VALIDATE FILE DESCRIPTOR */
//     if (fd < 0 || fd >= OPEN_MAX) {                                   /* Check if fd is within valid range                    */
//         return EBADF;
//     }
//     struct openfile *file_entry = curproc->fileTable[fd];
//     if (file_entry == NULL) {                                         /* Ensure fd points to a valid file entry              */
//         return EBADF;
//     }
//     if ((file_entry->mode & O_ACCMODE) == O_WRONLY) {                 /* Ensure the file is not open in write-only mode      */
//         return EBADF;
//     }

//     /* ALLOCATE KERNEL BUFFER */
//     char *kernel_buffer = (char *)kmalloc(len * sizeof(char));
//     if (kernel_buffer == NULL) {                                      /* Check if memory allocation succeeded                */
//         return ENOMEM;
//     }

//     /* PREPARE FOR READ OPERATION */
//     struct iovec iov;
//     struct uio uio_read;
//     struct vnode *file_node = file_entry->vn;

//     lock_acquire(file_entry->lock);
//     uio_kinit(&iov, &uio_read, kernel_buffer, len, file_entry->offset, UIO_READ);
//     int read_error = VOP_READ(file_node, &uio_read);
//     if (read_error) {
//         kfree(kernel_buffer);
//         lock_release(file_entry->lock);
//         return read_error;                                            /* Propagate vnode read error                          */
//     }

//     /* UPDATE OFFSET AND RETURN VALUE */
//     off_t bytes_read = len - uio_read.uio_resid;
//     *retval = (int32_t)bytes_read;
//     file_entry->offset = uio_read.uio_offset;

//     /* COPY DATA BACK TO USER SPACE */
//     int copy_error = copyout(kernel_buffer, (userptr_t)usr_buf, bytes_read);
//     if (copy_error) {
//         kfree(kernel_buffer);
//         lock_release(file_entry->lock);
//         return EFAULT;                                                /* User buffer copy failed                             */
//     }

//     /* CLEANUP */
//     lock_release(file_entry->lock);
//     kfree(kernel_buffer);

//     /* SUCCESS */
//     return 0;
// }

// ssize_t sys_write(int fd, const void *usr_buf, size_t len, int32_t *retval) {

//     /* VALIDATE FILE DESCRIPTOR */
//     if (fd < 0 || fd >= OPEN_MAX) {                                   /* Check if fd is within valid range                    */
//         return EBADF;
//     }
//     struct openfile *file_entry = curproc->fileTable[fd];
//     if (file_entry == NULL) {                                         /* Ensure fd points to a valid file entry              */
//         return EBADF;
//     }
//     if ((file_entry->mode & O_ACCMODE) == O_RDONLY) {                 /* Ensure the file is not open in read-only mode       */
//         return EBADF;
//     }

//     /* COPY DATA FROM USER SPACE */
//     char *kernel_buffer = (char *)kmalloc(len * sizeof(char));
//     if (kernel_buffer == NULL) {                                      /* Check if memory allocation succeeded                */
//         return ENOMEM;
//     }
//     int copy_error = copyin((const_userptr_t)usr_buf, kernel_buffer, len);
//     if (copy_error) {
//         kfree(kernel_buffer);
//         return EFAULT;                                                /* User buffer copy failed                             */
//     }

//     /* PREPARE FOR WRITE OPERATION */
//     struct iovec iov;
//     struct uio uio_write;
//     struct vnode *file_node = file_entry->vn;

//     lock_acquire(file_entry->lock);
//     uio_kinit(&iov, &uio_write, kernel_buffer, len, file_entry->offset, UIO_WRITE);
//     int write_error = VOP_WRITE(file_node, &uio_write);
//     if (write_error) {
//         kfree(kernel_buffer);
//         lock_release(file_entry->lock);
//         return write_error;                                           /* Propagate vnode write error                         */
//     }

//     /* UPDATE OFFSET AND RETURN VALUE */
//     off_t bytes_written = len - uio_write.uio_resid;
//     *retval = (int32_t)bytes_written;
//     file_entry->offset = uio_write.uio_offset;

//     /* CLEANUP */
//     lock_release(file_entry->lock);
//     kfree(kernel_buffer);

//     /* SUCCESS */
//     return 0;
// }

#if OPT_SHELL
ssize_t sys_write(int fd, const void *buf, size_t buflen, int32_t *retval) {

    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX) {                                 /* fd should be a valid number                          */
        return EBADF;       
    } else if (curproc->fileTable[fd] == NULL) {                    /* fd should refer to a valid entry in the fileTable    */
        return EBADF;
    } else if (curproc->fileTable[fd]->mode == O_RDONLY) {     /* fd should refer to a file allowed to be written      */
        return EBADF;
    }

    /* COPYING BUFFER TO KERNEL SIDE (copyin()) */
    char *kbuffer = (char *) kmalloc(buflen * sizeof(char));
    if (kbuffer == NULL) {
        return ENOMEM;
    } else if (copyin((const_userptr_t) buf, kbuffer, buflen)) {
        kfree(kbuffer);
        return EFAULT;
    }

    /* PERFORMING WRITING (VOP_WRITE()) */
    struct iovec iov;
	struct uio kuio;
    struct openfile *of = curproc->fileTable[fd];
    struct vnode *vn = of->vn;

    lock_acquire(of->lock);
    uio_kinit(&iov, &kuio, kbuffer, buflen, of->offset, UIO_WRITE);
    int error = VOP_WRITE(vn, &kuio);
    if (error) {
        kfree(kbuffer);
        return error;
    }

    /* REPOSITION OF THE OFFSET */
    off_t nbytes = kuio.uio_offset - of->offset;
    *retval = (int32_t) nbytes;
    of->offset = kuio.uio_offset;

    /* FREEING KERNEL BUFFER */
    lock_release(of->lock);
    kfree(kbuffer);

    /* TASK COMPLETED SUCCESSFULLY */
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

int sys_close(int fd) {

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

        /* NO MORE PROCESSES REFER TO THIS FILE, CLOSING ALSO VNODE */
        struct vnode *vn = of->vn;
        of->vn = NULL;
        vfs_close(vn);
    }

    lock_release(of->lock);                                               /* Free the openfile structure                          */
    return 0;
}

/* Open a file */
// int sys_open(userptr_t pathname, int openflags, mode_t mode, int *retval) {

//     /* CHECKING INPUT ARGUMENTS */
//     if (pathname == NULL) {
//         return EFAULT;
//     }

//     /* COPYING PATHNAME TO KERNEL SIDE */
//     char *kbuffer = (char *)kmalloc(PATH_MAX * sizeof(char));
//     if (kbuffer == NULL) {
//         return ENOMEM;
//     }
//     size_t len;
//     int err = copyinstr((const_userptr_t)pathname, kbuffer, PATH_MAX, &len);
//     if (err) {
//         kfree(kbuffer);
//         return EFAULT;
//     }

//     /* OPENING WITH VFS UTILITY */
//     struct vnode *v;
//     err = vfs_open(kbuffer, openflags, mode, &v);
//     if (err) {
//         kfree(kbuffer);
//         return err;
//     }
//     kfree(kbuffer);

//     /* RETRIEVING A FREE POSITION IN THE SYSTEM FILETABLE */
//     struct openfile *of = NULL;
//     for (int index = 0; index < MAX_OPEN_FILES; index++) {
//         if (systemFileTable[index].vn == NULL) {
//             of = &systemFileTable[index];
//             of->vn = v;
//             break;
//         }
//     }

//     /* ASSIGNING OPENFILE TO CURRENT PROCESS FILETABLE */
//     int fd = 3; // Skipping STDIN, STDOUT, and STDERR
//     if (of == NULL) {
//         vfs_close(v);
//         return ENFILE; // System file table is full
//     } else {
//         for (; fd < OPEN_MAX; fd++) {
//             if (curproc->fileTable[fd] == NULL) {
//                 curproc->fileTable[fd] = of;
//                 break;
//             }
//         }

//         if (fd == OPEN_MAX) {
//             vfs_close(v);
//             return EMFILE; // Process file table is full
//         }
//     }

//     /* MANAGING OFFSET */
//     if (openflags & O_APPEND) {
//         struct stat filestat;
//         err = VOP_STAT(of->vn, &filestat);
//         if (err) {
//             curproc->fileTable[fd] = NULL;
//             vfs_close(v);
//             return err;
//         }
//         curproc->fileTable[fd]->offset = filestat.st_size;
//     } else {
//         curproc->fileTable[fd]->offset = 0;
//     }

//     /* MANAGING REFERENCES */
//     curproc->fileTable[fd]->count = 1;

//     /* MANAGING MODE */
//     switch (openflags & O_ACCMODE) {
//         case O_RDONLY:
//             curproc->fileTable[fd]->mode = O_RDONLY;
//             break;
//         case O_WRONLY:
//             curproc->fileTable[fd]->mode = O_WRONLY;
//             break;
//         case O_RDWR:
//             curproc->fileTable[fd]->mode = O_RDWR;
//             break;
//         default:
//             vfs_close(of->vn);
//             curproc->fileTable[fd] = NULL;
//             return EINVAL;
//     }

//     /* CREATING LOCK ON THIS FILE */
//     curproc->fileTable[fd]->lock = lock_create("FILE_LOCK");
//     if (curproc->fileTable[fd]->lock == NULL) {
//         vfs_close(of->vn);
//         curproc->fileTable[fd] = NULL;
//         return ENOMEM;
//     }

//     /* TASK COMPLETED SUCCESSFULLY */
//     *retval = fd;
//     return 0;
// }

int sys_open(userptr_t pathname, int openflags, mode_t mode, int32_t *retval) {

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
    err = vfs_open(kbuffer, openflags, mode, &v);   // may return ENOENT, ENXIO, ENODEV
    if (err) {
        kfree(kbuffer);
        return err;
    }
    kfree(kbuffer);

    /* RETRIEVING A FREE POSITION IN THE SYSTEM FILETABLE */
    struct openfile *of = NULL;
    for (int index = 0; index < SYSTEM_OPEN_MAX; index++) {
        if (systemFileTable[index].vn == NULL) {
            of = &systemFileTable[index];
            of->vn = v;
            break;
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
    if (openflags & O_APPEND) {

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
    switch(openflags & O_ACCMODE){
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

int sys_lseek(int fd, off_t pos, int whence, int32_t *retval_low32, int32_t *retval_upp32) {

    /* INITIALIZE VARIABLES */
    off_t new_offset = -1;

    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX) {                                  /* Ensure fd is within valid range                     */
        return EBADF;       
    }
    struct openfile *file_entry = curproc->fileTable[fd];
    if (file_entry == NULL) {                                        /* Ensure fd points to a valid file entry             */
        return EBADF;
    }

    /* CHECKING FILE SEEKABILITY */
    if (!VOP_ISSEEKABLE(file_entry->vn)) {                           /* Ensure file supports seeking                       */
        return ESPIPE;
    }

    /* PREPARE FOR SEEK */
    lock_acquire(file_entry->lock);
    struct stat file_info;
    int stat_error;

    /* HANDLE WHENCE OPTIONS */
    switch (whence) {
        case SEEK_SET:
            if (pos < 0) {                                           /* Negative positions are invalid for SEEK_SET        */
                lock_release(file_entry->lock);
                return EINVAL;
            }
            new_offset = pos;
            break;

        case SEEK_CUR:
            if (pos < 0 && -pos > file_entry->offset) {              /* Ensure offset does not go below 0                 */
                lock_release(file_entry->lock);
                return EINVAL;
            }
            new_offset = file_entry->offset + pos;
            break;

        case SEEK_END:
            stat_error = VOP_STAT(file_entry->vn, &file_info);
            if (stat_error) {                                        /* Check for VOP_STAT errors                         */
                lock_release(file_entry->lock);
                return stat_error;
            }
            if (pos < 0 && -pos > file_info.st_size) {               /* Ensure offset does not go below 0                 */
                lock_release(file_entry->lock);
                return EINVAL;
            }
            new_offset = file_info.st_size + pos;
            break;

        default:
            lock_release(file_entry->lock);                         /* Invalid whence value                              */
            return EINVAL;
    }

    /* UPDATE FILE OFFSET */
    file_entry->offset = new_offset;
    lock_release(file_entry->lock);

    /* SPLIT 64-BIT OFFSET INTO 32-BIT PARTS */
    *retval_low32 = (int32_t)(new_offset >> 32);                     /* Most significant bits                             */
    *retval_upp32 = (int32_t)(new_offset & 0xFFFFFFFF);              /* Least significant bits                            */

    /* SUCCESS */
    return 0;
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