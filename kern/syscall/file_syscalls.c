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
#include <kern/limits.h>

#if OPT_SHELL

#define MAX_OPEN_FILES 128 // Define maximum open files per process

#if OPT_SHELL
ssize_t sys_write(int fd, const void *buf, size_t buflen, int32_t *retval)
{

    /* CHECKING FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX)
    { /* fd should be a valid number */
        return EBADF;
    }
    if (curproc == NULL || curproc->fileTable == NULL)
    {
        return EFAULT; /* curproc should not be NULL */
    }
    if (curproc->fileTable[fd] == NULL)
    { /* fd should refer to a valid entry in the fileTable */
        return EBADF;
    }

    struct openfile *of = curproc->fileTable[fd];

    /* Ensure the file is NOT read-only */
    if ((of->mode & O_ACCMODE) == O_RDONLY)
    {
        return EBADF; /* File is not open for writing */
    }

    if (buf == NULL)
    {
        return EFAULT; /* Invalid buffer pointer */
    }

    /* COPYING BUFFER TO KERNEL SIDE (copyin()) */
    char *kbuffer = (char *)kmalloc(buflen);
    if (kbuffer == NULL)
    {
        return ENOMEM;
    }

    /* Copy user buffer to kernel */
    int err = copyin((const_userptr_t)buf, kbuffer, buflen);
    if (err)
    {
        kfree(kbuffer);
        return EFAULT;
    }

    /* PERFORMING WRITING (VOP_WRITE()) */
    struct iovec iov;
    struct uio kuio;
    struct vnode *vn = of->vn;

    lock_acquire(of->lock);

    uio_kinit(&iov, &kuio, kbuffer, buflen, of->offset, UIO_WRITE);
    err = VOP_WRITE(vn, &kuio);
    if (err)
    {
        lock_release(of->lock);
        kfree(kbuffer);
        return err; /* Error during file write */
    }

    /* REPOSITION OF THE OFFSET */
    off_t nbytes = kuio.uio_offset - of->offset;
    *retval = (int32_t)nbytes;
    of->offset = kuio.uio_offset;

    /* FREEING KERNEL BUFFER */
    lock_release(of->lock);
    kfree(kbuffer);

    /* TASK COMPLETED SUCCESSFULLY */
    return 0;
}

#endif

/* Open a file */
int sys_open(const char *pathname, int flags, mode_t mode, int *retval)
{

    /* CHECKING INPUT ARGUMENTS */
    if (pathname == NULL)
    {
        return EFAULT;
    }

    /* COPYING PATHNAME TO KERNEL SIDE */
    // this is done for two reasons:
    // 1) security reason
    // 2) vfs_open may destroy the buffer
    char *kbuffer = (char *)kmalloc(PATH_MAX * sizeof(char));
    if (kbuffer == NULL)
    {
        return ENOMEM;
    }
    size_t len;
    int err = copyinstr((const_userptr_t)pathname, kbuffer, PATH_MAX, &len); // may return EFAULT
    if (err)
    {
        kfree(kbuffer);
        return EFAULT;
    }

    /* OPENING WITH VFS UTILITY */
    struct vnode *v;
    err = vfs_open(kbuffer, flags, mode, &v);
    if (err)
    {
        kfree(kbuffer);
        return err;
    }
    kfree(kbuffer);

    /* RETRIEVING A FREE POSITION IN THE SYSTEM FILETABLE */
    struct openfile *of = NULL;
    for (int i = 3; i < OPEN_MAX; i++)
    {
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

    /* ASSIGNING OPENFILE TO CURRENT PROCESS FILETABLE */
    int fd = 3; // skipping STDIN, STDOUT and STDERR
    if (of == NULL)
    {
        return ENFILE; // system file table is full
    }
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
        {
            return EMFILE; // process file table is full
        }
    }

    /* MANAGING OFFSET */
    // if flag specified O_APPEND, the operation on the file should start at the end
    // otherwise, it should start at the beginning
    if (flags & O_APPEND)
    {

        /* RETRIEVING FILE SIZE */
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
    {

        /* STARTING FROM THE BEGINNING */
        curproc->fileTable[fd]->offset = 0;
    }

    /* MANAGING REFERENCES */
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

    /* CREATING LOCK ON THIS FILE */
    curproc->fileTable[fd]->lock = lock_create("FILE_LOCK");
    if (curproc->fileTable[fd]->lock == NULL)
    {
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
    if (fd < 0 || fd >= OPEN_MAX)
    { /* fd should be a valid number                          */
        return EBADF;
    }
    else if (curproc->fileTable[fd] == NULL)
    { /* fd should refer to a valid entry in the fileTable    */
        return EBADF;
    }

    /* REDUCING REFERENCES */
    struct openfile *of = curproc->fileTable[fd];
    lock_acquire(of->lock);
    curproc->fileTable[fd] = NULL;
    if (--of->count > 0)
    {

        /* THIS FILE IS STILL REFERENCED BY SOME PROCESS */
        lock_release(of->lock);
        return 0;
    }
    else
    {

        /* NO MORE PROCESS REFER TO THIS FILE, CLOSING ALSO VNODE */
        struct vnode *vn = of->vn;
        of->vn = NULL;
        vfs_close(vn);
    }

    lock_release(of->lock);
    return 0;
}

int sys_lseek(int fd, off_t pos, int whence, int32_t *retval_low32, int32_t *retval_upp32) {
    struct openfile *of;
    struct vnode *vn;
    struct stat file_stat;
    off_t new_offset;

    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    if (curproc == NULL || curproc->fileTable == NULL) {
        return EFAULT;
    }
    of = curproc->fileTable[fd];
    if (of == NULL) {
        return EBADF;
    }

    vn = of->vn;
    if (vn == NULL || !VOP_ISSEEKABLE(vn)) {
        return ESPIPE;
    }

    if (fd == STDIN_FILENO) {
        return ESPIPE;
    }

    lock_acquire(of->lock);

    if (VOP_STAT(vn, &file_stat) != 0) {
        lock_release(of->lock);
        return EINVAL;
    }

    switch (whence) {
        case SEEK_SET:
            new_offset = pos;
            break;
        case SEEK_CUR:
            new_offset = of->offset + pos;
            break;
        case SEEK_END:
            new_offset = file_stat.st_size + pos;
            break;
        default:
            lock_release(of->lock);
            return EINVAL;
    }

    if (new_offset < 0) {
        lock_release(of->lock);
        return EINVAL;
    }

    of->offset = new_offset;
    *retval_low32 = (int32_t)(new_offset & 0xFFFFFFFF);
    *retval_upp32 = (int32_t)((new_offset >> 32) & 0xFFFFFFFF);

    lock_release(of->lock);
    return 0;
}

ssize_t sys_read(int fd, const void *buf, size_t buflen, int32_t *retval)
{
    struct openfile *of;
    struct iovec iov;
    struct uio kuio;
    struct vnode *vn;
    int err;

    /* VALIDATE FILE DESCRIPTOR */
    if (fd < 0 || fd >= OPEN_MAX)
    {
        return EBADF; /* Invalid file descriptor */
    }
    if (curproc == NULL || curproc->fileTable == NULL)
    {
        return EFAULT; /* curproc should not be NULL */
    }

    of = curproc->fileTable[fd];

    if (of == NULL)
    {
        return EBADF; /* File descriptor is not in use */
    }

    /* Ensure the file is NOT write-only */
    if ((of->mode & O_ACCMODE) == O_WRONLY)
    {
        return EBADF; /* File is not open for reading */
    }

    if (buf == NULL)
    {
        return EFAULT; /* Invalid buffer pointer */
    }

    /* HANDLE READING FROM STDIN */
    if (fd == STDIN_FILENO)
    {
        char *kbuffer = (char *)kmalloc(buflen);
        if (kbuffer == NULL)
        {
            return ENOMEM; /* Memory allocation failed */
        }

        /* READ FROM STDIN */
        size_t i = 0;
        for (; i < buflen; i++)
        {
            kbuffer[i] = getch();
            if (kbuffer[i] == '\n')
            { /* Stop at newline */
                i++;
                break;
            }
        }

        /* COPY BUFFER TO USER SPACE */
        err = copyout(kbuffer, (userptr_t)buf, i); // Keep buf as const void *
        kfree(kbuffer);
        if (err)
        {
            return EFAULT; /* Error copying data to user space */
        }

        *retval = i;
        return 0; /* Success */
    }

    /* ALLOCATE KERNEL BUFFER */
    char *kbuffer = (char *)kmalloc(buflen);
    if (kbuffer == NULL)
    {
        return ENOMEM; /* Memory allocation failed */
    }

    /* RETRIEVE FILE OBJECT */
    vn = of->vn;
    if (vn == NULL)
    {
        kfree(kbuffer);
        return EBADF; /* Invalid vnode */
    }

    /* ACQUIRE FILE LOCK */
    lock_acquire(of->lock);

    /* INITIALIZE UIO FOR KERNEL BUFFER */
    uio_kinit(&iov, &kuio, kbuffer, buflen, of->offset, UIO_READ);

    /* PERFORM READ OPERATION */
    err = VOP_READ(vn, &kuio);
    if (err)
    {
        kfree(kbuffer);
        lock_release(of->lock);
        return err; /* Error during file read */
    }

    /* UPDATE FILE OFFSET */
    of->offset = kuio.uio_offset;

    /* SET RETURN VALUE (BYTES READ) */
    *retval = buflen - kuio.uio_resid;

    /* COPY BUFFER TO USER SPACE */
    err = copyout(kbuffer, (userptr_t)buf, *retval); // Keep buf as const void *
    kfree(kbuffer);
    lock_release(of->lock);

    if (err)
    {
        return EFAULT; /* Copying to user space failed */
    }

    return 0; /* Success */
}

#endif
#if OPT_SHELL
int sys_dup2(int oldfd, int newfd)
{
    // preliminary checks
    if (oldfd < 0 || oldfd > OPEN_MAX || newfd < 0 || newfd > OPEN_MAX)
    {
        return EBADF;
    }

    if (curproc->fileTable[oldfd] == NULL)
    {
        return EBADF;
    }

    // special case: oldfd = newfd
    if (oldfd == newfd)
    {
        return newfd;
    }

    // special case: newfd is already open
    if (curproc->fileTable[newfd] != NULL)
    {
        // sys_close
        return -1;
    }

    curproc->fileTable[newfd] = curproc->fileTable[oldfd];
    curproc->fileTable[newfd]->count++;
    return newfd;
}
#endif

#if OPT_SHELL
int sys_chdir(const char *path)
{
    if (path == NULL)
    {
        return EFAULT;
    }

    char kbuf[PATH_MAX];
    struct vnode *new_dir;
    int result = copyinstr((const_userptr_t)path, kbuf, sizeof(kbuf), NULL);
    if (result)
    {
        return result;
    }

    result = vfs_open(kbuf, O_RDONLY, 0, &new_dir);
    if (result)
    {
        return result;
    }

    if (curproc->p_cwd != NULL)
    {
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
    if (buf == NULL || size == 0)
    {
        // errno = EINVAL;
        return NULL;
    }

    struct iovec iovec_buf;
    struct uio cwd_uio;

    uio_kinit(&iovec_buf, &cwd_uio, (userptr_t)buf, size, 0, UIO_READ);

    int result = vfs_getcwd(&cwd_uio);
    if (result)
    {
        // errno = result;
        return NULL;
    }

    return buf;
}
#endif

#if OPT_SHELL
int sys_remove(const char *pathname)
{

    /* NOT IMPLEMENTED (YET?) */
    (void)pathname;

    /* TASK COMPLETED SUCCESSFULLY */
    return 0;
}
#endif