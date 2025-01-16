#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <vnode.h>
#include <types.h>

/**
 * @struct openfile
 * 
 * @brief Represents an open file in the OS/161 kernel.
 * 
 * This structure contains information about an open file, including:
 * - A pointer to the vnode representing the file.
 * - A lock to ensure thread-safe access to the file.
 * - A reference count indicating how many times the file is open.
 * - The mode in which the file was opened.
 * - The current offset within the file.
 */
struct openfile {
    struct vnode *vn;   
    struct lock *lock;  
    int count;          
    int mode;          
    off_t offset;      
};

#endif /* _OPENFILE_H_ */