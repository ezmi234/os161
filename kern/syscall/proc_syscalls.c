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

/*
 * simple proc management system calls
 */
void
sys__exit(int status)
{
  /* get address space of current process and destroy */
  struct addrspace *as = proc_getas();
  as_destroy(as);
  /* thread exits. proc data structure will be lost */
  thread_exit();

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
}

#if OPT_SHELL
pid_t sys_getpid() {
  pid_t result = curproc->p_pid;
  return result;
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

    /* Step 5: Return the child PID to the parent */
    *retval = child_proc->p_pid;

    /* Step 6: Child process gets a return value of 0 */
    return 0;
}
#endif