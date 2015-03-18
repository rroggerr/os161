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
            return ENOMEM;
        }
        return 0;
    }
    return 0; //should not get here
}

#endif //OPT_A2

/* write sys_execv here */

#if OPT_A2

int
sys_execv(const char *inprogram, char **inargs){
    
    struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result; // for checking error
    //kmalloc and copy here later
    
    //should copy in the progname and args here
    //(void)inargs;
    
    //int copyinstr(const_userptr_t usersrc, char *dest, size_t len, size_t *got);
    int progname_size = strlen(inprogram);
    progname_size ++;
    char *progname = kmalloc(progname_size * sizeof(char));
    
    size_t got; // i dont think this matters
    result = copyinstr((const_userptr_t)inprogram, progname,
                       progname_size * sizeof(char), &got);
    if (result) {
        kfree(progname);
		return result;
	}
    
    // copy inargs into kern buffer first
    //count number of args --> argc
    int argc =0;
    for (int i=0; inargs[i] != NULL; i++) {
        argc++;
    }
    
    char **arg_pointers = kmalloc(argc * sizeof(char **)); //doesnt include last NULL
    //argc++; //for last NULL
    char **args = kmalloc(sizeof(char **));
    
    //int copyin(const_userptr_t usersrc, void *dest, size_t len);
    result = copyin((const_userptr_t)inargs, args, argc * sizeof(char *));
    if (result) {
        return result;
    }
    
    /* DEBUG PRINT START --------------------
    kprintf("argc= %d", argc);
    for (int i =0;args[i] != NULL; i++) {
        kprintf("[%d]: %s ",i,args[i]);
    }
    kprintf("\n");
    
    DEBUG PRINT END -----------------------*/
    
    //copy into args[]
    for (int i=0; i<argc; i++) {
        //kprintf("i= %d\n", i);
        int tmp_arg_len = strlen(inargs[i]);
        if (tmp_arg_len>ARG_MAX) {
            return E2BIG;
        }
        
        tmp_arg_len++; //for null term strings
        
        //kprintf("i=%d tmp_arg_len = %d\n", i,tmp_arg_len);

        if (inargs[i] != NULL) {
            args[i] = kmalloc(tmp_arg_len * sizeof(char));
            //int copyinstr(const_userptr_t usersrc, char *dest, size_t len, size_t *got);
            /*result = copyinstr((const_userptr_t)inargs[i], args[i], tmp_arg_len * sizeof(char), &got);
            if (result) {
                for (int j=0; j<i; j++) {
                    kfree(args[j]);
                }
                kfree(args);
                kfree(progname);
                kprintf("GG: inargs[%d] = %s\n", i, inargs[i]);
                return result;
            }*/
            
            for (int j=0; j<tmp_arg_len; j++) {   //because yolo
                args[i][j] = inargs[i][j];
            }
        }
        else {
            args[i] = kmalloc(sizeof(int));
            args[i] = NULL;
        }
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
    
	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}
    
    // copy to user stack------- and dont forget to kfree after done
    //TODO: left off here
    //int copyout(const void *src, userptr_t userdest, size_t len);
    
    //loops for each arg in args[i]
    for (int i = 0; args[i] != NULL; i++) {
        int arg_len = 0;  //this is the length of the string + '/0'
        int arg_total_len = 0;  // this is length of string + padding divisible by 4
        char *tmp_arg = NULL;
        
        arg_len = strlen(args[i]);
        arg_len++; //for null of course
        if (arg_len%4 != 0){
            arg_total_len = arg_len + (4 - (arg_len%4));
        }
        else {
            arg_total_len = arg_len;
        }
        
        //decrement stack ptr
        stackptr -= arg_total_len;
        
        //allocate and copy to tmp_arg with padding
        tmp_arg = kmalloc(arg_total_len * sizeof(char));
        for (int j=0; j<arg_total_len; j++) {
            if (j<arg_len){
                tmp_arg[j] = args[i][j];
            }
            else {
                tmp_arg[j]='\0';
            }
            //kprintf("copied: %s\n", tmp_arg);
        }
        //kprintf("tmparg = %s arglen=%d arg_total_len= %d \n", tmp_arg, arg_len, arg_total_len);
        result = copyout((const void *)tmp_arg,
                         (userptr_t)stackptr, arg_total_len * sizeof(char));
        if (result) {
            //dont forget to kfree stuff
            return result;
        }
        arg_pointers[i] = (char *)stackptr;
        kfree(tmp_arg);
    }
    
    //does it reach null?, this is the args[i] == NULL case
    stackptr -= sizeof(int);  //probably works
    
    stackptr -= argc * sizeof(char *);
    result = copyout((const void *)arg_pointers, (userptr_t)stackptr, argc * sizeof(char *));
    
    //cleaning up
    kfree(progname);
    for (int i=0; i<argc; i++) {
        kfree(args[i]);
    }
    kfree(args);
    
	/* Warp to user mode. */
	enter_new_process(argc, (userptr_t)stackptr, stackptr, entrypoint);
	
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




