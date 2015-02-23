#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/*
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/*
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *globalsem;
static struct lock *globallock;
static struct lock **bowllocks;
static struct cv *cat_cv;
static struct cv *mouse_cv;
static volatile int num_mice_eating;
static volatile int num_cats_eating;

static volatile int num_mice_waiting;
static volatile int num_cats_waiting;
volatile bool debug = false;

/*
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
    /* replace this default implementation with your own implementation of catmouse_sync_init */
    
    globallock= lock_create("globalLock");
    //globalsem = sem_create("globalCatMouseSem",1);
    //malloc n* locks for bowllocks[]
    bowllocks = kmalloc(bowls * sizeof(struct lock *));
    for (int i=0; i<bowls; i++) {
        bowllocks[i] = lock_create("bowl lock");
        if (bowllocks[i] == NULL ){
            for (int j =0; j<i; j++) {
                lock_destroy(bowllocks[j]);
            }
            kfree(bowllocks);
            lock_destroy(globallock);
            kprintf("lock %d failed ", i);
            KASSERT(false);
        }
    }
    
    cat_cv = cv_create("cats_still_eating");
    mouse_cv = cv_create("mice_still_eating");
    
    num_cats_eating=0;
    num_mice_eating=0;
    
    num_mice_waiting=0;
    num_cats_waiting=0;
    return;
}

/*
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
    lock_destroy(globallock);
    for (int i=0; i<bowls; i++) {
        lock_destroy(bowllocks[i]);
    }
    kfree(bowllocks);
    
    cv_destroy(cat_cv);
    cv_destroy(mouse_cv);
    //sem_destroy(globalsem);
    
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl)
{
    
    if (debug) {kprintf("numcats: %d nummice: %d cat before tries lock_acq(g) \n", num_cats_eating, num_mice_eating);}
    if (lock_do_i_hold(globallock)) {if (debug) {kprintf("cat before has its own glock-------------\n");}}
    lock_acquire(globallock);
    if (debug) {kprintf("cat before glock acquired\n");} //-------------------------------------
    
    //check if there are mice eating || more mice waiting
    while (num_mice_eating /*|| (num_mice_waiting> num_cats_eating)*/){
        if (debug) {kprintf("cat before enters wait, mice waiting:%d cats eating%d\n", num_mice_waiting, num_cats_eating);}
        num_cats_waiting++;
        cv_wait(cat_cv,globallock);
        num_cats_waiting--;
    }
    //go eat
    num_cats_eating++;
    
    lock_release(globallock);
    lock_acquire(bowllocks[bowl-1]);
    //V(globalsem);
    if (debug) {kprintf("cat before: glock will be released\n");} //-------------------------------------
    
    
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl)
{
    if (debug) {kprintf("numcats: %d nummice: %d cat after tries lock_acq(g) \n", num_cats_eating, num_mice_eating);}
    if (lock_do_i_hold(globallock)) {if (debug) {kprintf("cat after has its own glock-------------\n");}}
    lock_acquire(globallock);
    if (debug) {kprintf("cat after glock acquired lk holder: \n");} //-------------------------------------
    lock_release(bowllocks[bowl-1]);
    num_cats_eating--;
    if (!num_cats_eating) {
        //let all mice eat
        cv_broadcast(mouse_cv,globallock);
    }
    if (debug) {kprintf("cat after: glock will be released\n");} //-------------------------------------
    lock_release(globallock);
    
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl)
{
    if (debug) {kprintf("numcats: %d nummice: %d mouse before tries lock_acq(g) \n", num_cats_eating, num_mice_eating);}
    if (lock_do_i_hold(globallock)) {if (debug) {kprintf("mouse before has its own glock-------------\n");}}
    lock_acquire(globallock);
    if (debug) {kprintf("mouse before glock acquired\n");} //-------------------------------------
    
    //check if there are cats eating
    while (num_cats_eating /*|| (num_cats_waiting> num_mice_eating)*/){
        if (debug) {kprintf("mouse before enters wait, cats waiting:%d mice eating%d\n", num_cats_waiting, num_mice_eating);}
        num_mice_waiting++;
        cv_wait(mouse_cv,globallock);
        num_mice_waiting--;
    }
    //go eat
    if (debug) {kprintf("numcats: %d nummice: %d mouse tries to acquired block after \n", num_cats_eating, num_mice_eating);}
    //P(globalsem);
    //if (debug){kprintf("semaphore entered \n");}
    num_mice_eating++;
    lock_release(globallock);
    lock_acquire(bowllocks[bowl-1]);
    //V(globalsem);
    
    
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl)
{
    if (debug) {kprintf("numcats: %d nummice: %d mouse after tries lock_acq(g) \n", num_cats_eating, num_mice_eating);}
    if (lock_do_i_hold(globallock)) {if (debug) {kprintf("mouse after has its own glock-------------\n");}}
    lock_acquire(globallock);
    if (debug) {kprintf("mouse after glock acquired\n");} //-------------------------------------
    
    num_mice_eating--;
    lock_release(bowllocks[bowl-1]);
    if (!num_mice_eating ) {
        //let all cats eat
        cv_broadcast(cat_cv,globallock);
    }
    if (debug) {kprintf("mouse after: glock released\n");} //-------------------------------------
    lock_release(globallock);
    
}

