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

/**
 * copy_program_name - Copies a program name from user space to kernel space.
 * @user_prog: The program name in user space.
 * @kernel_prog: Pointer to the location where the program name in kernel space will be stored.
 *
 * This function allocates memory in kernel space to store the program name
 * and copies the program name from user space to the allocated memory.
 *
 * Return: 0 on success, or an error code on failure.
 *         - ENOMEM if memory allocation fails.
 *         - Other error codes as returned by copyinstr.
 */
int copy_program_name(const char *user_prog, char **kernel_prog) {
    *kernel_prog = kmalloc(PATH_MAX);
    if (!(*kernel_prog)) {
        return ENOMEM;
    }
    int result = copyinstr((userptr_t)user_prog, *kernel_prog, PATH_MAX, NULL);
    if (result) {
        kfree(*kernel_prog);
    }
    return result;
}

/**
 * copy_arguments - Copies arguments from user space to kernel space.
 * @user_args: A pointer to the array of arguments in user space.
 * @kernel_args: A pointer to the array of arguments in kernel space.
 * @argc: A pointer to an integer that will store the number of arguments.
 *
 * This function copies arguments from user space to kernel space. It first
 * counts the number of arguments, then allocates memory for the kernel
 * argument array, and finally copies each argument from user space to
 * kernel space.
 *
 * Return: 0 on success, or an error code on failure.
 *         - EFAULT if there is an error copying data from user space.
 *         - ENOMEM if there is an error allocating memory.
 */
int copy_arguments(char **user_args, char ***kernel_args, int *argc) {
    int result, i;
    char *arg_ptr;

    /* Count arguments */
    while (1) {
        result = copyin((const_userptr_t)(&user_args[*argc]), &arg_ptr, sizeof(char *));
        if (result) return EFAULT;
        if (arg_ptr == NULL) break;
        (*argc)++;
    }

    /* Allocate memory for kernel argument array */
    *kernel_args = kmalloc((*argc + 1) * sizeof(char *));
    if (!(*kernel_args)) {
        return ENOMEM;
    }

    /* Copy each argument */
    for (i = 0; i < *argc; i++) {
        result = copyin((const_userptr_t)(&user_args[i]), &arg_ptr, sizeof(char *));
        if (result) {
            cleanup_arguments(*kernel_args, i);
            return EFAULT;
        }

        (*kernel_args)[i] = kmalloc(ARG_MAX);
        if (!(*kernel_args)[i]) {
            cleanup_arguments(*kernel_args, i);
            return ENOMEM;
        }

        result = copyinstr((userptr_t)arg_ptr, (*kernel_args)[i], ARG_MAX, NULL);
        if (result) {
            cleanup_arguments(*kernel_args, i + 1);
            return result;
        }
    }
    (*kernel_args)[*argc] = NULL;
    return 0;
}

/**
 * copy_args_to_stack - Copies an array of argument strings to the user stack.
 * @kernel_args: An array of strings containing the arguments to copy.
 * @argc: The number of arguments in the array.
 * @stackptr: A pointer to the current top of the stack, which will be updated.
 *
 * This function copies each argument string from the kernel space to the user
 * stack, ensuring proper alignment. It also creates an array of pointers to
 * these argument strings on the stack, and updates the stack pointer accordingly.
 *
 * Return: 0 on success, or an error code on failure.
 */
int copy_args_to_stack(char **kernel_args, int argc, vaddr_t *stackptr) {
    vaddr_t arg_ptrs[argc + 1];
    int i, result;
    size_t len;

    /* Copy arguments to stack */
    for (i = argc - 1; i >= 0; i--) {
        len = strlen(kernel_args[i]) + 1;
        *stackptr -= ROUNDUP(len, 8);
        result = copyoutstr(kernel_args[i], (userptr_t)(*stackptr), len, NULL);
        if (result) return result;
        arg_ptrs[i] = *stackptr;
    }
    arg_ptrs[argc] = 0;

    /* Copy argument pointers to stack */
    *stackptr -= ROUNDUP((argc + 1) * sizeof(vaddr_t), 8);
    return copyout(arg_ptrs, (userptr_t)(*stackptr), (argc + 1) * sizeof(vaddr_t));
}


/**
 * cleanup_arguments - Frees memory allocated for an array of arguments.
 * @args: A pointer to an array of strings (arguments).
 * @count: The number of arguments in the array.
 *
 * This function iterates through the array of arguments and frees each
 * individual string. After all strings are freed, it frees the array itself.
 */
void cleanup_arguments(char **args, int count) {
    for (int i = 0; i < count; i++) {
        if (args[i]) kfree(args[i]);
    }
    kfree(args);
}

/**
 * restore_old_address_space - Restores the old address space of a process.
 * @old_as: Pointer to the old address space to be restored.
 * @new_as: Pointer to the new address space to be destroyed.
 *
 * This function destroys the new address space, sets the process's address
 * space to the old address space, and activates the old address space.
 */
void restore_old_address_space(struct addrspace *old_as, struct addrspace *new_as) {
    as_destroy(new_as);
    proc_setas(old_as);
    as_activate();
}