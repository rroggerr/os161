#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include <limits.h>
#include "opt-A2.h"
#include <array.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
    
    struct addrspace *as;
    struct proc *p = curproc;
    
#if OPT_A2
    lock_acquire(p->waitpid_lk);
    p->alive = false;
    p->exit_status =exitcode;
    cv_broadcast(p->waitpid_cv, p->waitpid_lk);
    lock_release(p->waitpid_lk);
#else
    /* for now, just include this to keep the compiler from complaining about
     an unused variable */
    (void)exitcode;
#endif //OPT_A2
    
    DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);
    
    if (p->parpid == 0 || !(proctable[p->parpid-1]->alive)) {
        KASSERT(curproc->p_addrspace != NULL);
        as_deactivate();
        /*
         * clear p_addrspace before calling as_destroy. Otherwise if
         * as_destroy sleeps (which is quite possible) when we
         * come back we'll be calling as_activate on a
         * half-destroyed address space. This tends to be
         * messily fatal.
         */
        as = curproc_setas(NULL);
        as_destroy(as);
        
        /* detach this thread from its process */
        /* note: curproc cannot be used after this call */
        proc_remthread(curthread);
        
        
        //set all the childrean of this poc to dead
        for (int i=0; i<get_array_size(); i++) {
            //find child with the dead parent
            struct proc *tmpchild = proctable[i];
            if (tmpchild->parpid == p->currpid) {
                tmpchild->parpid=0;
            }
        }
        
        /* if this is the last user process in the system, proc_destroy()
         will wake up the kernel menu thread */
        proc_destroy(p);
        
        kfree(p);
    }
    else{
        KASSERT(curproc->p_addrspace != NULL);
        
        as_deactivate();
        /*
         * clear p_addrspace before calling as_destroy. Otherwise if
         * as_destroy sleeps (which is quite possible) when we
         * come back we'll be calling as_activate on a
         * half-destroyed address space. This tends to be
         * messily fatal.
         */
        as = curproc_setas(NULL);
        as_destroy(as);
        
        /* detach this thread from its process */
        /* note: curproc cannot be used after this call */
        proc_remthread(curthread);
        
        /* if this is the last user process in the system, proc_destroy()
         will wake up the kernel menu thread */
        proc_destroy(p);
    }
    
    thread_exit();
    /* thread_exit() does not return, so we should never get here */
    panic("return from thread_exit in sys_exit\n");
}

#if OPT_A2
//implementation for fork()
int
sys_fork(struct trapframe *tf, pid_t *retval){
    struct proc *p = curproc;
    if (proc_count_get()+1 >= PID_MAX) {
        return(ENPROC);
    }
    else {
        struct proc *childproc = proc_create_runprogram("child proc");
        struct trapframe *new_tf = kmalloc(sizeof(struct trapframe));
        KASSERT(new_tf!= NULL);
        memcpy(new_tf,tf,sizeof(struct trapframe));
        
        //return 0 to child
        new_tf->tf_v0 = 0;
        //return child pid to parent
        tf->tf_v0 = childproc->currpid;
        
        *retval = childproc->currpid;
        
        //give parent pid to child
        childproc->parpid = p->currpid;
        
        // copy addr space over
        /*int ascopyerr = */as_copy(p->p_addrspace, &(childproc->p_addrspace));
        /*if (ascopyerr) {
         panic("as_copy failed in sys_fork\n");
         }*/
        
        //copy over p_cwd pointers
        childproc->p_cwd = p->p_cwd;
        
        //update pc
        new_tf->tf_epc+=4;
        
        int forkerror = thread_fork("child process thread", childproc,
                                    enter_forked_process, new_tf,0);
        if (forkerror) {
            return(ENOMEM);
        }
        return 0;
    }
    return 0; //should not get here
}

#endif //OPT_A2

/* write sys_execv here */

#if OPT_A2

int
sys_execv(const char *inprogname, char **inargs){
    
    struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result; // for checking error
    char *progname; //kmalloc and copy here later
    
    
    
    //should copy in the progname and args here
    (void)inprogname;
    (void)inargs;
    
    //int copyinstr(const_userptr_t usersrc, char *dest, size_t len, size_t *got);
    int progname_size = strlen(inprogname);
    progname_size ++;
    progname = kmalloc(progname_size * sizeof(char *));
    
    size_t got; // i dont think this matters
    result = copyinstr((const_userptr_t)inprogname, progname, 128, &got);
    if (result) {
		return result;
	}
    
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}
    
    
	/* Create a new address space. */
    //should destroy current proc addrspace
	if (curproc_getas() != NULL){
        as_destroy(curproc_getas());
    }
    
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}
    
	/* Switch to it and activate it. */
	curproc_setas(as);
	as_activate();
    
	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}
    
	/* Done with the file now. */
	vfs_close(v);
    
    
    // copy to user stack------- and dont forget to kfree after done
    
	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}
    
	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
                      stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

#endif //OPT_A2

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
    /* for now, this is just a stub that always returns a PID of 1 */
    /* you need to fix this to make it work properly */
    *retval = curproc->currpid;
    return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
            userptr_t status,
            int options,
            pid_t *retval)
{
    int exitstatus;
#if OPT_A2
    struct proc *p = curproc;
    KASSERT(proctable != NULL);
    
    //get number of elems in the array
    int proctable_size = get_array_size();
    
    if (options != 0) {
        return(EINVAL);
    }
    else if (status == NULL){
        return(EFAULT);
    }
    else if (pid > proctable_size){
        *retval = 0;
        return(ESRCH);
    }
    
    //check for proc that already exited
    struct proc *child = proctable[pid-1];
    if (!(child->alive) || child->parpid ==0){ //YOLO
        exitstatus = child->exit_status;
        exitstatus = _MKWAIT_EXIT(exitstatus);
        copyout((void *)&exitstatus,status,sizeof(int));
        *retval = pid;
        return(0);
    }
    //make sure pid = retieved pid
    else if (child->currpid != pid) {
        panic("pid is wrong: child %d, requested %d \n", child->currpid, pid);
    }
    // make sure caller is parent
    else if (p->currpid != child->parpid) {
        // caller is not parent
        *retval = 0;
        //kprintf("caller is not parent currpid: %d, child parent: %d\n", p->currpid ,child->parpid);
        
        //debug print
        //kprintf("currpid %d, childparpid %d", p->currpid, child->parpid);
        return(ECHILD);
    }
    // assumptions correct, proceed
    else {
        //kprintf("waitpid tries to acq lock\n");
        lock_acquire(child->waitpid_lk);
        //kprintf("waitpid : acquired\n");
        while (child->alive) {
            cv_wait(child->waitpid_cv,child->waitpid_lk);
        }
        //kprintf("waitpid got out of wait\n");
        *retval = pid;
        exitstatus = child->exit_status;
        exitstatus = _MKWAIT_EXIT(exitstatus);
        copyout((void *)&exitstatus,status,sizeof(int));
        lock_release(child->waitpid_lk);
        
        //do it here as well before kfree
        for (int i=0; i<get_array_size(); i++) {
            //find child with the dead parent
            struct proc *tmpchild = proctable[i];
            if (tmpchild->parpid == p->currpid) {
                tmpchild->parpid=0;
            }
        }
        
        //try thread deatch??
        
        
        kfree(child);
        return(0);
    }
    
#else
    /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.
     
     Fix this!
     */
    int result;
    if (options != 0) {
        return(EINVAL);
    }
    /* for now, just pretend the exitstatus is 0 */
    exitstatus = 0;
    result = copyout((void *)&exitstatus,status,sizeof(int));
    if (result) {
        return(result);
    }
    *retval = pid;
#endif //OPT_A2
    return(0);
}
