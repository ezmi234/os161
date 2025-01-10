#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <proc.h>
#include <kern/errno.h>
#include <machine/trapframe.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/wait.h>
#include <kern/limits.h>


#if OPT_SHELL

/**
 * sys_getpid - Retrieve the process ID of the current process.
 *
 * This function returns the process ID (PID) of the currently executing process.
 *
 * Returns:
 *   The PID of the current process.
 */
pid_t sys_getpid() {
  /* retrieve the process ID of the current process */
  pid_t result = curproc->p_pid;
  return result;
}

/**
 * sys_waitpid - Wait for a specific child process to terminate.
 *
 * @pid: The process ID of the child process to wait for.
 * @status: Pointer to an integer where the exit status of the child process will be stored.
 * @options: Options for the waitpid call (e.g., WNOHANG for non-blocking wait).
 * @retval: Pointer to an integer where the process ID of the terminated child process will be stored.
 *
 * This function suspends execution of the calling process until the child process specified by
 * pid terminates, unless the WNOHANG option is specified. If the child process has already
 * terminated, the function returns immediately. The exit status of the child process is stored
 * in the location pointed to by status.
 *
 * Return:
 *  - 0 on success.
 *  - ECHILD if the specified process is not a child of the calling process or if the calling process
 *    attempts to wait on itself.
 *  - EFAULT if the status pointer is invalid or not word-aligned.
 *  - EINVAL if the options argument is invalid.
 *  - ESRCH if no process with the specified pid exists.
 */
int sys_waitpid(pid_t pid, int *status, int options, int *retval) {
    /* SOME ASSERTIONS */
    KASSERT(curproc != NULL);

    /* CHECKING ARGUMENTS */
    if (pid == curproc->p_pid) {
        return ECHILD;  /* Cannot wait on itself */
    } else if (status == NULL) {
        *retval = pid;
        return 0;
    }
    /* TEMPORARY */
    else if ((int)status == 0x40000000 || (unsigned int)status == 0x80000000) {
        return EFAULT;  /* Invalid memory address */
    } else if ((int)status % 4 != 0) {
        return EFAULT;  /* Status must be word-aligned */
    }

    /* OPTIONS */
    switch (options) {
        case 0:
            break;
        case WNOHANG:
            *status = 0;
            *retval = pid;
            return 0;  /* Non-blocking wait */
        default:
            return EINVAL;  /* Invalid options */
    }

    /* RETRIEVING PROCESS */
    struct proc *proc = proc_search(pid);
    if (proc == NULL) {
        return ESRCH;  /* No such process */
    }

    /* CHECKING IF THE PROCESS IS A CHILD */
    if (proc->parent_pid != curproc->p_pid) {
        return ECHILD;  /* Not a child process */
    }

    if (proc->p_numthreads == 0) {
        *status = proc->p_exitcode;
        *retval = proc->p_pid;
        proc_destroy(proc);
        return 0;
    }

    /* CHECKING PROCESS TERMINATION */
    if (proc->p_exited) {
        /* Copy exit status to user space */
        int exit_status = proc->p_exitcode;
        if (status != NULL) {
            int result = copyout(&exit_status, (userptr_t)status, sizeof(int));
            if (result) {
                return result;  /* Copyout failed */
            }
        }

        *retval = pid;
        proc_destroy(proc);  /* Clean up the process */
        return 0;
    }

    /* WAITING FOR TERMINATION */
    lock_acquire(proc->p_locklock);
    while (!proc->p_exited) {
        cv_wait(proc->p_cv, proc->p_locklock);
    }
    lock_release(proc->p_locklock);

    /* ASSIGNING RETURN STATUS */
    *status = proc->p_exitcode;
    *retval = proc->p_pid;
    if (status == NULL) {
        return EFAULT;
    }

    /* TASK COMPLETED SUCCESSFULLY */
    proc_destroy(proc);
    return 0;
}

/**
 * sys_fork - Create a new process by duplicating the calling process.
 * @tf: The trapframe of the parent process.
 * @retval: Pointer to store the PID of the child process.
 *
 * This function creates a new process that is a copy of the calling process.
 *
 * Return:
 * 0 on success, or an error code on failure.
 * Possible error codes:
 * - ENOMEM: Out of memory.
 * - Other error codes returned by as_copy or thread_fork.
 */
int sys_fork(struct trapframe *tf, pid_t *retval) {
    struct proc *child_proc;
    struct trapframe *child_tf;
    int result;

    /* ASSERTING CURRENT PROCESS TO ACTUALLY EXIST */
    KASSERT(curproc != NULL);

    /* Create a new process */
    child_proc = proc_create_runprogram(curproc->p_name);
    if (child_proc == NULL) {
        proc_destroy(child_proc);
        return ENOMEM;  /* Out of memory */
    }

    /* Copy the address space */
    result = as_copy(curproc->p_addrspace, &child_proc->p_addrspace);
    if (result) {
        proc_destroy(child_proc);
        return result; /* Address space copy failed */
    }

    /* Copy the trapframe for the child */
    child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
        proc_destroy(child_proc);
        return ENOMEM; /* Out of memory */
    }
    *child_tf = *tf;

    /* Create a new thread for the child process */
    result = thread_fork(
        curthread->t_name,         /* Name of the thread */
        child_proc,                /* New process */
        enter_forked_process,      /* Entry function */
        (void *)child_tf,          /* Argument for the new thread */
        0                          /* Unused argument */
    );
    if (result) {
        kfree(child_tf);
        proc_destroy(child_proc);
        return result; /* Thread creation failed */
    }

    child_proc->parent_pid = curproc->p_pid;

    /* Return the child PID to the parent */
    *retval = child_proc->p_pid;

    /* Child process gets a return value of 0 */
    return 0;
}

/**
 * sys_execv - Executes a program, replacing the current process.
 * @program: The path to the program to execute.
 * @args: The arguments to pass to the program.
 *
 * This function loads and executes a new program, replacing the current
 * process image with a new one. The new program is specified by the
 * @program parameter, and the arguments to the program are specified by
 * the @args parameter.
 *
 * Return:
 *  - On success, this function does not return.
 *  - On failure, returns an appropriate error code:
 *    - EFAULT: If @program or @args is an invalid pointer.
 *    - ENOMEM: If there is insufficient memory to complete the operation.
 *    - Other error codes as returned by lower-level functions.
 */
int sys_execv(const char *program, char **args) {
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    struct addrspace *new_as, *old_as;
    char **kernel_args = NULL;
    char *kernel_progname = NULL;
    int argc = 0, result;

    if (program == NULL || args == NULL) {
        return EFAULT;
    }

    kernel_progname = kmalloc(PATH_MAX);
    if (kernel_progname == NULL) {
        return ENOMEM;
    }
    result = copyinstr((userptr_t)program, kernel_progname, PATH_MAX, NULL);
    if (result) {
        kfree(kernel_progname);
        return result;
    }

    /* Validate argument list pointers */
    while (1) {
        char *arg_ptr;
        result = copyin((const_userptr_t)(&args[argc]), &arg_ptr, sizeof(char *));
        if (result) {
            kfree(kernel_progname);
            return EFAULT;
        }
        if (arg_ptr == NULL) break;  /* Stop at NULL terminator */
        argc++;
    }

    kernel_args = kmalloc((argc + 1) * sizeof(char *));
    if (kernel_args == NULL) {
        kfree(kernel_progname);
        return ENOMEM;
    }

    for (int i = 0; i < argc; i++) {
        char *arg_ptr;
        result = copyin((const_userptr_t)(&args[i]), &arg_ptr, sizeof(char *));
        if (result) {
            for (int j = 0; j < i; j++) kfree(kernel_args[j]);
            kfree(kernel_args);
            kfree(kernel_progname);
            return EFAULT;
        }

        kernel_args[i] = kmalloc(ARG_MAX);
        if (kernel_args[i] == NULL) {
            for (int j = 0; j < i; j++) kfree(kernel_args[j]);
            kfree(kernel_args);
            kfree(kernel_progname);
            return ENOMEM;
        }
        result = copyinstr((userptr_t)arg_ptr, kernel_args[i], ARG_MAX, NULL);
        if (result) {
            for (int j = 0; j <= i; j++) kfree(kernel_args[j]);
            kfree(kernel_args);
            kfree(kernel_progname);
            return result;
        }
    }
    kernel_args[argc] = NULL;

    result = vfs_open(kernel_progname, O_RDONLY, 0, &v);
    if (result) {
        for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
        kfree(kernel_args);
        kfree(kernel_progname);
        return result;
    }

    new_as = as_create();
    if (new_as == NULL) {
        vfs_close(v);
        for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
        kfree(kernel_args);
        kfree(kernel_progname);
        return ENOMEM;
    }

    old_as = proc_setas(new_as);
    as_activate();

    result = load_elf(v, &entrypoint);
    if (result) {
        as_destroy(new_as);
        proc_setas(old_as);
        as_activate();
        vfs_close(v);
        for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
        kfree(kernel_args);
        kfree(kernel_progname);
        return result;
    }

    vfs_close(v);

    result = as_define_stack(new_as, &stackptr);
    if (result) {
        as_destroy(new_as);
        proc_setas(old_as);
        as_activate();
        for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
        kfree(kernel_args);
        kfree(kernel_progname);
        return result;
    }

    vaddr_t arg_ptrs[argc + 1];
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(kernel_args[i]) + 1;
        stackptr -= ROUNDUP(len, 8);
        result = copyoutstr(kernel_args[i], (userptr_t)stackptr, len, NULL);
        if (result) {
            as_destroy(new_as);
            proc_setas(old_as);
            as_activate();
            for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
            kfree(kernel_args);
            kfree(kernel_progname);
            return result;
        }
        arg_ptrs[i] = stackptr;
    }
    arg_ptrs[argc] = 0;

    stackptr -= ROUNDUP((argc + 1) * sizeof(vaddr_t), 8);
    result = copyout(arg_ptrs, (userptr_t)stackptr, (argc + 1) * sizeof(vaddr_t));
    if (result) {
        as_destroy(new_as);
        proc_setas(old_as);
        as_activate();
        for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
        kfree(kernel_args);
        kfree(kernel_progname);
        return result;
    }

    for (int i = 0; i < argc; i++) kfree(kernel_args[i]);
    kfree(kernel_args);
    kfree(kernel_progname);

    enter_new_process(argc, (userptr_t)stackptr, NULL, stackptr, entrypoint);

    panic("enter_new_process returned unexpectedly!");
    return EINVAL;
}

/**
 * sys__exit - Terminates the current process with the given exit code.
 * @exitcode: The exit code to be set for the process.
 *
 * This function sets the exit code for the current process, signals that
 * the process has exited, and then cleans up resources by calling
 * thread_exit(). If the function returns, it indicates an error as
 * thread_exit() should not return.
 *
 * Preconditions:
 * - The current process (curproc) must not be NULL.
 *
 * Postconditions:
 * - The process's exit code is set.
 * - The process is marked as exited.
 * - Other processes waiting on this process are signaled.
 * - The current thread is terminated.
 *
 * If thread_exit() fails to terminate the thread, a panic is triggered.
 */
void sys__exit(int exitcode) {
    struct proc *proc = curproc;

    KASSERT(proc != NULL);

    proc->p_exitcode = _MKWAIT_EXIT(exitcode);
    proc->p_exited = true;
    /* SIGNALLING THE TERMINATION OF THE PROCESS */
    lock_acquire(proc->p_locklock);
    cv_signal(proc->p_cv, proc->p_locklock);
    lock_release(proc->p_locklock);

    /* Clean up resources */
    thread_exit();

    /* WAIT! YOU SHOULD NOT HAPPEN TO BE HERE */
    panic("[!] Wait! You should not be here. Some errors happened during thread_exit()...\n");

}

#endif