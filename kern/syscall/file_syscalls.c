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
#include <kern/limits.h>

#if OPT_SHELL

#define MAX_OPEN_FILES 128 /* Define maximum open files per process */

/**
 * sys_write - Write data to a file descriptor.
 *
 * @fd: The file descriptor to write to.
 * @buf: The buffer containing the data to write.
 * @buflen: The number of bytes to write from the buffer.
 * @retval: Pointer to store the number of bytes written.
 *
 * This function writes up to buflen bytes from the buffer pointed to by buf
 * to the file referred to by the file descriptor fd. The number of bytes
 * written is stored in the location pointed to by retval.
 *
 * Return:
 *  - 0 on success.
 *  - EBADF if fd is not a valid file descriptor or if the file is not open for writing.
 *  - EFAULT if curproc or curproc->fileTable is NULL, or if buf is an invalid pointer.
 *  - ENOMEM if there is insufficient memory to allocate the kernel buffer.
 *  - Other error codes as returned by VOP_WRITE.
 */
ssize_t sys_write(int fd, const void *buf, size_t buflen, int32_t *retval)
{
    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX)
        return EBADF;

    /* CHECKING CURPROC AND FILETABLE */
    if (curproc == NULL || curproc->fileTable == NULL)
        return EFAULT;

    /* CHECKING FILE DESCRIPTOR VALIDITY */
    if (curproc->fileTable[fd] == NULL)
        return EBADF;

    struct openfile *of = curproc->fileTable[fd];

    /* ENSURE THE FILE IS NOT READ-ONLY */
    if ((of->mode & O_ACCMODE) == O_RDONLY)
        return EBADF; /* FILE IS NOT OPEN FOR WRITING */

    if (buf == NULL)
        return EFAULT; /* INVALID BUFFER POINTER */

    /* ALLOCATING KERNEL BUFFER */
    char *kbuffer = (char *)kmalloc(buflen);
    if (kbuffer == NULL)
        return ENOMEM;

    /* COPYING DATA FROM USER SPACE TO KERNEL SPACE */
    int err = copyin((const_userptr_t)buf, kbuffer, buflen);
    if (err)
    {
        kfree(kbuffer);
        return EFAULT;
    }

    /* WRITING DATA TO FILE */
    struct iovec iov;
    struct uio kuio;
    struct vnode *vn = of->vn;

    lock_acquire(of->lock);

    uio_kinit(&iov, &kuio, kbuffer, buflen, of->offset, UIO_WRITE);
    err = VOP_WRITE(vn, &kuio);

    /* NO FREE SPACE REMAINING ON THE FILESYSTEM */
    if (err == ENOSPC)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return ENOSPC;
    }
    /* HARDWARE I/O ERROR OCCURRED */
    if (err == EIO)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return EIO; 
    }
    if (err)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return err; /* ERROR DURING FILE WRITE */
    }

    /* UPDATING OFFSET AND RETURN VALUE */
    off_t nbytes = kuio.uio_offset - of->offset;
    *retval = (int32_t)nbytes;
    of->offset = kuio.uio_offset;

    /* RELEASE LOCK AND FREE KERNEL BUFFER */
    lock_release(of->lock);
    kfree(kbuffer);

    return 0;
}

/**
 * sys_open - System call to open a file.
 * @pathname: The path of the file to open.
 * @flags: Flags indicating the mode in which to open the file.
 * @mode: The mode to use if a new file is created.
 * @retval: Pointer to store the file descriptor of the opened file.
 *
 * This function performs the following steps:
 * 1. Checks if the pathname is NULL and returns EFAULT if it is.
 * 2. Copies the pathname from user space to kernel space to ensure security and prevent vfs_open from destroying the buffer.
 * 3. Opens the file using the vfs_open utility.
 * 4. Retrieves a free position in the system file table.
 * 5. Assigns the open file to the current process's file table.
 * 6. Manages the file offset based on the O_APPEND flag.
 * 7. Manages the reference count for the file.
 * 8. Sets the file mode based on the flags.
 * 9. Creates a lock for the file.
 * 10. Returns the file descriptor through the retval pointer.
 *
 * Return:
 * 0 on success, or an appropriate error code on failure.
 */
int sys_open(const char *pathname, int flags, mode_t mode, int *retval)
{
    /* CHECKING IF PATHNAME IS NULL */
    if (pathname == NULL)
        return EFAULT;

    /* COPYING PATHNAME FROM USER SPACE TO KERNEL SPACE */
    char *kbuffer = (char *)kmalloc(PATH_MAX * sizeof(char));
    if (kbuffer == NULL)
        return ENOMEM;

    size_t len;
    int err = copyinstr((const_userptr_t)pathname, kbuffer, PATH_MAX, &len); /* MAY RETURN EFAULT */
    if (err)
    {
        kfree(kbuffer);
        return EFAULT;
    }

    /* OPENING FILE */
    struct vnode *v;
    err = vfs_open(kbuffer, flags, mode, &v);
    if (err)
    {
        kfree(kbuffer);
        return err;
    }
    kfree(kbuffer);

    /* FINDING A FREE POSITION IN THE SYSTEM FILE TABLE */
    struct openfile *of = NULL;
    for (int i = 3; i < OPEN_MAX; i++)
    {
        /* FINDING A FREE POSITION IN THE SYSTEM FILE TABLE */
        if (curproc->fileTable[i] == NULL)
        {
            of = (struct openfile *)kmalloc(sizeof(struct openfile));
            if (of == NULL)
            {
                vfs_close(v);
                return ENOMEM;
            }
            of->vn = v;
            of->offset = 0;
            of->mode = flags;
            of->count = 1;
            of->lock = lock_create("FILE_LOCK");
            if (of->lock == NULL)
            {
                vfs_close(v);
                kfree(of);
                return ENOMEM;
            }
            curproc->fileTable[i] = of;
            *retval = i;
            return 0;
        }
    }

    /* SYSTEM FILE TABLE IS FULL */
    int fd = 3; /* 0, 1, 2 ARE RESERVED FOR STDIN, STDOUT, STDERR */
    if (of == NULL)
        return ENFILE; /* SYSTEM FILE TABLE IS FULL */
    else
    {
        for (; fd < OPEN_MAX; fd++)
        {
            if (curproc->fileTable[fd] == NULL)
            {
                curproc->fileTable[fd] = of;
                break;
            }
        }

        if (fd == OPEN_MAX - 1)
            return EMFILE; /* PROCESS FILE TABLE IS FULL */
    }

    /* MANAGING OFFSET */
    if (flags & O_APPEND)
    {
        /* READING FILE SIZE */
        struct stat filestat;
        err = VOP_STAT(curproc->fileTable[fd]->vn, &filestat);
        if (err)
        {
            kfree(curproc->fileTable[fd]);
            curproc->fileTable[fd] = NULL;
            return err;
        }
        curproc->fileTable[fd]->offset = filestat.st_size;
    }
    else
        /* SETTING OFFSET TO 0 */
        curproc->fileTable[fd]->offset = 0;

    /* MANAGING COUNT */
    curproc->fileTable[fd]->count = 1;

    /* MANAGING MODE */
    switch (flags & O_ACCMODE)
    {
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

    /* CREATING LOCK */
    curproc->fileTable[fd]->lock = lock_create("FILE_LOCK");
    if (curproc->fileTable[fd]->lock == NULL)
    {
        vfs_close(curproc->fileTable[fd]->vn);
        kfree(curproc->fileTable[fd]);
        curproc->fileTable[fd] = NULL;
        return ENOMEM;
    }

    *retval = fd;
    return 0;
}

/**
 * sys_close - Close a file descriptor.
 * @fd: The file descriptor to close.
 *
 * This function closes the file descriptor specified by @fd. It first checks
 * if the file descriptor is valid and refers to an open file. If the file
 * descriptor is invalid, it returns EBADF. If the file descriptor is valid,
 * it reduces the reference count of the associated open file. If the reference
 * count drops to zero, it closes the vnode associated with the open file.
 *
 * Return: 0 on success, EBADF if the file descriptor is invalid.
 */
int sys_close(int fd)
{
    /* CHECK IF THE FILE DESCRIPTOR IS VALID */
    if (fd < 0 || fd >= OPEN_MAX)
        return EBADF;

    /* CHECK IF THE FILE DESCRIPTOR IS IN USE */
    if (curproc->fileTable[fd] == NULL)
        return EBADF;

    /* RETRIEVE THE OPEN FILE STRUCTURE */
    struct openfile *of = curproc->fileTable[fd];
    lock_acquire(of->lock);

    /* REMOVE THE FILE DESCRIPTOR FROM THE PROCESS'S FILE TABLE */
    curproc->fileTable[fd] = NULL;

    /* DECREMENT THE REFERENCE COUNT */
    if (--of->count == 0)
    {
        /* IF NO MORE REFERENCES, CLOSE THE VNODE */
        struct vnode *vn = of->vn;
        of->vn = NULL;
        vfs_close(vn);
    }

    lock_release(of->lock);
    return 0;
}

/**
 * sys_lseek - Repositions the offset of the open file associated with the file descriptor.
 *
 * @fd: The file descriptor of the file to seek.
 * @pos: The position to seek to.
 * @whence: The directive for the seek operation. It can be one of the following:
 *          SEEK_SET - Set the offset to pos.
 *          SEEK_CUR - Set the offset to the current location plus pos.
 *          SEEK_END - Set the offset to the size of the file plus pos.
 * @retval_low32: Pointer to store the lower 32 bits of the resulting offset.
 * @retval_upp32: Pointer to store the upper 32 bits of the resulting offset.
 *
 * Returns:
 * 0 on success, or an error code on failure:
 * - EBADF if the file descriptor is invalid or not open.
 * - ESPIPE if the file is not seekable.
 * - EINVAL if the whence argument is invalid or the resulting offset would be negative.
 */
int sys_lseek(int fd, off_t pos, int whence, int32_t *retval_low32, int32_t *retval_upp32)
{

    off_t retval = -1;

    /* VALIDATE FILE DESCRIPTOR */
    KASSERT(curproc != NULL);

    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX)
        return EBADF;
    else if (curproc->fileTable[fd] == NULL)
        return EBADF;

    /* CHECKING IF FILE IS SEEKABLE */
    if (!VOP_ISSEEKABLE(curproc->fileTable[fd]->vn))
        return ESPIPE;

    struct openfile *of = curproc->fileTable[fd];
    int err;
    struct stat info;
    lock_acquire(of->lock);
    retval = of->offset;

    /* SEEK OPERATION */
    switch (whence)
    {
    /* SEEK_SET: SET THE OFFSET TO POS */
    case SEEK_SET:
        if (pos < 0)
        {
            lock_release(of->lock);
            return EINVAL;
        }
        retval = pos;
        break;

    /* SEEK_CUR: SET THE OFFSET TO THE CURRENT LOCATION PLUS POS */
    case SEEK_CUR:
        if (pos < 0 && -pos > of->offset)
        {
            lock_release(of->lock);
            return EINVAL;
        }
        retval = of->offset + pos;
        break;

    /* SEEK_END: SET THE OFFSET TO THE SIZE OF THE FILE PLUS POS */
    case SEEK_END:
        err = VOP_STAT(of->vn, &info);
        if (err)
        {
            lock_release(of->lock);
            return err;
        }
        if (pos < 0 && -pos > info.st_size)
        {
            lock_release(of->lock);
            return EINVAL;
        }
        retval = info.st_size - pos;
        break;

    default:
        lock_release(of->lock);
        return EINVAL;
    }

    of->offset = retval;
    lock_release(of->lock);

    /* SET RETURN VALUES */
    *retval_low32 = (int32_t)(retval >> 32);
    *retval_upp32 = (int32_t)(retval & 0x00000000ffffffff);

    return 0;
}

/**
 * sys_read - Read data from a file descriptor.
 * @fd: The file descriptor to read from.
 * @buf: The buffer to store the read data.
 * @buflen: The number of bytes to read.
 * @retval: Pointer to store the number of bytes read.
 *
 * This function reads up to @buflen bytes from the file descriptor @fd into
 * the buffer @buf. The actual number of bytes read is stored in @retval.
 *
 * Return:
 *   0 on success,
 *   EBADF if the file descriptor is invalid or not open for reading,
 *   EFAULT if the buffer pointer is invalid or curproc is NULL,
 *   ENOMEM if memory allocation for the kernel buffer fails,
 *   or an error code from VOP_READ if the read operation fails.
 */
ssize_t sys_read(int fd, const void *buf, size_t buflen, int32_t *retval) {

    /* VALIDATE FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX || curproc->fileTable[fd] == NULL || (curproc->fileTable[fd]->mode & O_ACCMODE) == O_WRONLY) {
        return EBADF;
    }

    /* VALIDATE BUFFER */
    if (buf == NULL) {
        return EFAULT;
    }

    /* ALLOCATE KERNEL BUFFER */
    char *kbuf = kmalloc(buflen);
    if (kbuf == NULL) {
        return ENOMEM;
    }

    /* PREPARE FOR READING */
    struct openfile *of = curproc->fileTable[fd];
    struct iovec iov;
    struct uio kuio;
    uio_kinit(&iov, &kuio, kbuf, buflen, of->offset, UIO_READ);

    /* PERFORM READ OPERATION */
    lock_acquire(of->lock);
    int result = VOP_READ(of->vn, &kuio);
    if (result) {
        lock_release(of->lock);
        kfree(kbuf);
        return result;
    }

    /* UPDATE OFFSET */
    of->offset = kuio.uio_offset;
    *retval = buflen - kuio.uio_resid;

    /* COPY DATA TO USER BUFFER */
    result = copyout(kbuf, (userptr_t)buf, *retval);
    if (result) {
        lock_release(of->lock);
        kfree(kbuf);
        return EFAULT;
    }

    /* CLEAN UP */
    lock_release(of->lock);
    kfree(kbuf);

    return 0;
}


/**
 * sys_dup2 - Duplicates a file descriptor.
 *
 * @param oldfd: The file descriptor to duplicate.
 * @param newfd: The file descriptor to overwrite with the duplicate.
 * @param retval: Pointer to store the new file descriptor.
 *
 * @return: 0 on success, or an error code on failure.
 *
 * This function duplicates the file descriptor oldfd to newfd. If newfd is already
 * open, it will be closed before being overwritten. If oldfd is equal to newfd,
 * the function does nothing and returns newfd. The function performs necessary
 * checks to ensure that the file descriptors are valid and that oldfd is open.
 */
int sys_dup2(int oldfd, int newfd, int32_t *retval)
{
    /* check validity of oldfd and newfd */
    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX)
        return EBADF;

    /* check if oldfd is open */
    if (curproc->fileTable[oldfd] == NULL)
        return EBADF;

    /* special case:if oldfd is the same as newfd, do nothing */
    if (oldfd == newfd)
    {
        *retval = newfd;
        return 0;
    }

    /* special case: newfd is already open, close it before reusing it */
    if (curproc->fileTable[newfd] != NULL)
    {
        struct openfile *of = curproc->fileTable[newfd];
        lock_acquire(of->lock);
        curproc->fileTable[newfd] = NULL;
        if (--of->count == 0)
        {
            struct vnode *vn = of->vn;
            of->vn = NULL;
            vfs_close(vn);
        }
        lock_release(of->lock);
    }

    /* duplicate the fd: point newfd to the same file object as oldfd
    increase the reference count for the file object */
    lock_acquire(curproc->fileTable[oldfd]->lock);
    curproc->fileTable[newfd] = curproc->fileTable[oldfd];
    curproc->fileTable[newfd]->count++;
    lock_release(curproc->fileTable[oldfd]->lock);

    *retval = newfd;
    return 0;
}

/**
 * sys_fstat - Retrieves the status of an open file.
 *
 * @param fildes: The file descriptor of the file.
 * @param buf: Pointer to a stat structure to store the file status.
 *
 * @return: 0 on success, or an error code on failure.
 *
 * This function is a stub and currently does nothing. It is intended to retrieve
 * the status of the file associated with the file descriptor fildes and store it
 * in the stat structure pointed to by buf.
 */
int sys_fstat(int fildes, struct stat *buf)
{
    (void)fildes;
    (void)buf;

    return 0;
}

/**
 * sys_chdir - Change the current working directory of the calling process.
 *
 * @path: The path to the new directory.
 *
 * This function changes the current working directory of the calling process
 * to the directory specified by the path argument. If the path is NULL, it
 * returns EFAULT. It first copies the user-supplied path into a kernel buffer,
 * then attempts to open the directory. If successful, it updates the current
 * process's working directory to the new directory, closing the old one if
 * necessary.
 *
 * Return:
 * 0 on success, or an error code on failure.
 */
int sys_chdir(const char *path)
{
    /* check if the path is NULL */
    if (path == NULL)
        return EFAULT;

    char kbuf[PATH_MAX];
    struct vnode *new_dir;

    /* copy the user-space string path into the kernel buffer */
    int result = copyinstr((const_userptr_t)path, kbuf, sizeof(kbuf), NULL);
    if (result)
        return result;

    /* try to open the target directory specified by the path */
    result = vfs_open(kbuf, O_RDONLY, 0, &new_dir);
    if (result)
        return result;

    /* if the process already has a current working directory, close it */
    if (curproc->p_cwd != NULL)
        vfs_close(curproc->p_cwd);

    /* update the current working directory */
    curproc->p_cwd = new_dir;
    return 0;
}

/**
 * sys_getcwd - Retrieves the current working directory.
 *
 * @param buf: A buffer to store the current working directory path.
 * @param size: The size of the buffer.
 * @param retlen: A pointer to store the length of the retrieved path.
 *
 * @return: 0 on success, or an error code on failure.
 *
 * This function copies the current working directory path into the provided
 * buffer. The length of the path is stored in the location pointed to by retlen.
 * If the size of the buffer is zero, the function returns EINVAL. If there is
 * an error during the copyin or vfs_getcwd operations, the function returns
 * the corresponding error code.
 */
int sys_getcwd(char buf[], size_t size, int32_t *retlen)
{
    /* check if the size of the buffer is valid */
    if (size == 0)
        return EINVAL;

    /* copy the buffer to kernel space to validate the address */
    int result = copyin((const_userptr_t)buf, buf, 1);
    if (result)
        return result;

    struct iovec iovec_buf;
    struct uio cwd_uio;

    /* initialize the uio structure for reading the current working directory */
    uio_kinit(&iovec_buf, &cwd_uio, (userptr_t)buf, size, 0, UIO_READ);

    /* retrieve the current working directory */
    result = vfs_getcwd(&cwd_uio);
    if (result)
        return result;

    /* retrieve the length of the directory path */
    *retlen = size - cwd_uio.uio_resid;
    return 0;
}

/**
 * sys_remove - Remove a file from the filesystem.
 * @pathname: The path of the file to be removed.
 *
 * This function is a placeholder for the system call to remove a file.
 * Currently, it is not implemented and simply returns success.
 *
 * Return: Always returns 0 indicating success.
 */
int sys_remove(const char *pathname)
{

    /* NOT IMPLEMENTED (YET?) */
    (void)pathname;

    /* TASK COMPLETED SUCCESSFULLY */
    return 0;
}

#endif