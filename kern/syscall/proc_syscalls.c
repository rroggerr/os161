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


/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {
    struct addrspace *as;
    struct proc *p = curproc;
    
#if OPT_A2
    //kprintf("sys_exit tries to acq lock\n");
    lock_acquire(p->waitpid_lk);
    //kprintf("sys_exit : acquired\n");
    //struct proc **proctable = get_proctable();
    int *exit_status = get_exit_status();
    //kprintf("sys_exit : exit_status_get\n");
    p->alive = false;
    exit_status[p->currpid-1] = exitcode;
    cv_broadcast(p->waitpid_cv,p->waitpid_lk);
    //kprintf("sys_exit : broadcasted\n");
    cv_destroy(p->waitpid_cv);
    
#else
    /* for now, just include this to keep the compiler from complaining about
     an unused variable */
    (void)exitcode;
#endif //OPT_A2
    
    DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);
    
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
    
#if OPT_A2
#else
    /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
    proc_destroy(p);
#endif //OPT_A2
    lock_release(p->waitpid_lk);
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
        int ascopyerr = as_copy(p->p_addrspace, &(childproc->p_addrspace));
        if (ascopyerr) {
            panic("as_copy failed in sys_fork\n");
        }
        
        //copy over p_cwd pointers
        childproc->p_cwd = p->p_cwd;
        
        //update pc
        new_tf->tf_epc+=4;
        
        int forkerror = thread_fork("child process thread", childproc,
                                    enter_forked_process, new_tf,0);
        if (forkerror) {
            panic("thread_fork failed in sys_fork\n");
        }
        return 0;
    }
    return 0; //should not get here
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
    struct proc **proc_table = get_proctable();
    int *exit_status=get_exit_status();
    KASSERT(proc_table != NULL);
    
    //get number of elems in the array
    int proctable_size = get_array_size();
    
    if (options != 0) {
        return(EINVAL);
    }
    else if (status == NULL){
        return(EFAULT);
    }
    else if (pid > proctable_size){
        exitstatus = _MKWAIT_EXIT(exitstatus);
        copyout((void *)&exitstatus,status,sizeof(int));
        //kprintf("pid: %d,  proctable_size:%d \n ", pid,proctable_size);
        *retval = 0;
        return(ESRCH);
    }
    //check for proc that already exited
    else{
        struct proc *child = proc_table[pid-1];
        
        //make sure pid = retieved pid
        if (child->currpid != pid) {
            panic("pid is wrong: child %d, requested %d \n", child->currpid, pid);
        }
        // make sure caller is parent
        else if (p->currpid != child->parpid) {
            // caller is not parent
            *retval = 0;
            //kprintf("caller is not parent currpid: %d, child parent: %d\n", p->currpid ,child->parpid);
            return(ECHILD);
        }
        // assumptions correct, proceed
        else if (!child->alive){
            exitstatus = exit_status[pid];
            exitstatus = _MKWAIT_EXIT(exitstatus);
            copyout((void *)&exitstatus,status,sizeof(int));
            *retval = pid;
            return(0);
        }
        else {
            //kprintf("waitpid tries to acq lock\n");
            lock_acquire(child->waitpid_lk);
            //kprintf("waitpid : acquired\n");
            while (child->alive) {
                cv_wait(child->waitpid_cv,child->waitpid_lk);
            }
            //kprintf("waitpid got out of wait\n");
            *retval = pid;
            exitstatus = exit_status[pid];
            exitstatus = _MKWAIT_EXIT(exitstatus);
            copyout((void *)&exitstatus,status,sizeof(int));
            lock_release(child->waitpid_lk);
            return(0);
        }
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


