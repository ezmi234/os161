/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

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

/*
 * sys_exit - Terminates the current process.
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

#if OPT_SHELL
pid_t sys_getpid() {
  pid_t result = curproc->p_pid;
  return result;
}

/*
 * sys_waitpid - Waits for a child process to terminate.
 */
int sys_waitpid(pid_t pid, int *status, int options, pid_t *retval) {
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

    // /* RETRIEVING EXIT STATUS */
    // int exit_status = proc->p_exitcode;
    // if (status != NULL) {
    //     int result = copyout(&exit_status, (userptr_t)status, sizeof(int));
    //     if (result) {
    //         return result;  
    //     }
    // }

    // /* TASK COMPLETED SUCCESSFULLY */
    // *retval = pid;
    // proc_destroy(proc);  // Clean up the process
    // return 0;
}

/*
 * sys_fork - Create a new process as a copy of the current process.
 */
int sys_fork(struct trapframe *tf, pid_t *retval) {
    struct proc *child_proc;
    struct trapframe *child_tf;
    int result;

    /* Step 1: Create a new process */
    child_proc = proc_create_runprogram(curproc->p_name);
    if (child_proc == NULL) {
        return ENOMEM; // Out of memory
    }

    /* Step 2: Copy the address space */
    result = as_copy(curproc->p_addrspace, &child_proc->p_addrspace);
    if (result) {
        proc_destroy(child_proc);
        return result; // Address space copy failed
    }

    /* Step 3: Copy the trapframe for the child */
    child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
        proc_destroy(child_proc);
        return ENOMEM; // Out of memory
    }
    *child_tf = *tf;

    /* Step 4: Create a new thread for the child process */
    result = thread_fork(
        curthread->t_name,         // Name of the thread
        child_proc,                // New process
        enter_forked_process,      // Entry function
        (void *)child_tf,          // Argument for the new thread
        0                          // Unused argument
    );
    if (result) {
        kfree(child_tf);
        proc_destroy(child_proc);
        return result; // Thread creation failed
    }

    child_proc->parent_pid = curproc->p_pid;

    /* Step 5: Return the child PID to the parent */
    *retval = child_proc->p_pid;

    /* Step 6: Child process gets a return value of 0 */
    return 0;
}
#endif