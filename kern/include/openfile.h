#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <vnode.h>
#include <types.h>

struct openfile {
    struct vnode *vn;
    off_t offset;
    int mode;
    int count;
    struct lock *lock;
};

#endif /* _OPENFILE_H_ */