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
#include <limits.h>
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

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

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}

#if OPT_A2
//implementation for fork()
int
sys_fork(struct trapframe *tf, pid_t *retval){
    proc *p = curproc;
    if (proc_count+1 >= PID_MAX) {
        return(ENPROC);
    }
    else {
        char childprocname[] = "child";
        proc *childproc = proc_create_runprogram(childprocname);
        struct trapframe *new_tf;
        new_tf= kmalloc(sizeof(struct trapframe));
        KASSERT(new_tf! = NULL);
        memcpy(new_tf,tf,sizeof(struct trapframe));
        
        //return 0 to parent
        new_tf->tf_v0=0;
        //return parent pid to child
        
        tf->tf_v0=curproc->currpid;
        
        // copy addr space over
        int ascopyerr = as_copy(curproc->p_addrspace, struct childproc->p_addrspace);
        if (ascopy) {
            panic("as_copy failed in sys_fork\n");
        }
        
        //copy over p_cwd pointers
        childproc->p_cwd = curproc->p_cwd;
        
        //update pc
        ntf->tf_epc+=4;
        
        int forkerror = thread_fork("child process thread", childproc,
                                    enter_forked_process, ntf,0);
        if (forkerror) {
            panic("thread_fork failed in sys_fork\n");
        }
        return 0;
    }
}

#endif

/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
    proc *p = curproc;
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
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

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
  return(0);
}


