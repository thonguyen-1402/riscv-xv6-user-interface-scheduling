#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;
// newly added fields  + code 


int sched_policy = SCHED_RR;
static const int DEFAULT_WEIGHT = 1;
static const int DEFAULT_SLICE  = 5; // for example, 5 ticks
static int rr_default_slice = DEFAULT_SLICE;
static struct spinlock schedcfg_lock;
#define BIG_STRIDE 100000
#define MLFQ_MIN_PRIO 0
#define MLFQ_MAX_PRIO 2

int mlfq_slice_for_prio(int prio)
{
  if (prio == 2) return 2;   // highest priority: short slice -> prio = 2 -> 2 timeslices
  if (prio == 1) return 4;
  return 8;                 // lowest priority: long slice
}





//---------------------------------
int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&schedcfg_lock, "schedcfg");

  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}
/*NEWLY ADDED CODE 
int
setschedweight(int pid, int weight)
{
  struct proc *p;

  if(weight < 1 || weight > 20)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      p->sched_weight = weight;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
*/
//NEW WEIGHT SETTING, IT HAS SLIDE COMPLY WITH WEIGHT, IF NOT WORKING, DELETE THIS AND USE THE PREVIOUS VERSION
int
setschedweight(int pid, int weight)
{
  struct proc *p;

  if(weight < 1 || weight > 20)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      p->sched_weight = weight;

      // keep stride consistent with weight
      p->stride = BIG_STRIDE / p->sched_weight;
      if (p->stride <= 0)
        p->stride = 1;
      

      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

/*int
setschedslice(int pid, int slice)
{
  struct proc *p;

  if(slice < 1 || slice > 100)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      p->time_slice = slice;
      p->ticks_used = 0;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}*/
//NEWLY-ADDED CODE : SETTIMESLICE ONLY MAKE SENSE IN RR ONLY
int
setschedslice(int pid, int slice)
{
  struct proc *p;

  if (slice < 1 || slice > 1000)
    return -1;

  // IMPORTANT: slice control only makes sense in RR mode.
  if (sched_policy != SCHED_RR)
    return -1;
  
   // pid==0 => set RR template slice for FUTURE processes
  if (pid == 0) {
    acquire(&schedcfg_lock);
    rr_default_slice = slice;
    release(&schedcfg_lock);
    return 0;
  }


  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      p->time_slice = slice;
      p->ticks_used = 0;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

int
setschedpolicy(int policy)
{
  if(policy != SCHED_RR && policy != SCHED_WEIGHTED_RR &&
      policy != SCHED_STRIDE && policy != SCHED_MLFQ)
    return -1;
  sched_policy = policy;
  return 0;
}



//NEWLY-ADDED CODE : TO RELEASE THE ALL THE SILENT WORKERS 
// Kill all processes whose name is "quiet_worker".
// Returns how many were marked killed.
int
release_workers(void)
{
  struct proc *p;
  int count = 0;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED &&
        strncmp(p->name, "quiet_worker", sizeof(p->name)) == 0) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;   // wake so it can see killed and exit
      count++;
    }
    release(&p->lock);
  }
  return count;
}


//-----------------
// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  //NEWLY -ADDED CODE : ASSIGN WEIGHT, TIMESLICE ETC
  p->sched_weight = DEFAULT_WEIGHT;   // e.g., 1
  p->time_slice   = DEFAULT_SLICE;    // e.g., 5
  p->ticks_used   = 0;
  p->total_ticks  = 0;
  //NEWLY-ADDED CODE : FOR STRIDE PASS SCHEDULING 
  // stride scheduling defaults
  p->stride = BIG_STRIDE / p->sched_weight;
  if (p->stride <= 0)
    p->stride = 1;
  p->pass   = 0;
  
  //p->prio = MLFQ_MAX_PRIO;
//p->time_slice = mlfq_slice_for_prio(p->prio);
/*NEWLY -ADDED CODE : IF NOT WOKRING , DELETE THIS */
// policy-dependent init for slice/prio
if (sched_policy == SCHED_MLFQ) {
  p->prio = MLFQ_MAX_PRIO;
  p->time_slice = mlfq_slice_for_prio(p->prio);
} else if (sched_policy == SCHED_RR) {
  int s;
  acquire(&schedcfg_lock);
  s = rr_default_slice;
  release(&schedcfg_lock);

  p->prio = 0;          // irrelevant outside MLFQ
  p->time_slice = s;    // <-- RR template applies to *new* processes
} else {
  // WRR / STRIDE: ignore RR template
  p->prio = 0;
  p->time_slice = DEFAULT_SLICE;
}
  
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  //newly-added fields
np->sched_weight = p->sched_weight;

if (sched_policy == SCHED_RR) {
  int s;
  acquire(&schedcfg_lock);
  s = rr_default_slice;
  release(&schedcfg_lock);
  np->time_slice = s;
} else {
  np->time_slice = p->time_slice;
}

np->ticks_used  = 0;
np->total_ticks = 0;

np->stride = BIG_STRIDE / np->sched_weight;
if (np->stride <= 0)
  np->stride = 1;
np->pass = 0;

np->prio = p->prio;


  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
/*void OLD SCHEDULER ( RR ONLY)
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}
switchuvm
// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
*/
/*-NEWLY ADDED CODE 
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // allow interrupts briefly, then turn them off again
    intr_on();
    intr_off();

    int found = 0;

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);

      if(p->state == RUNNABLE) {

        // --- our hook: configure slice based on policy/weight ---
        if(sched_policy == SCHED_WEIGHTED_RR){
          if(p->sched_weight < 1)
            p->sched_weight = 1;      // safety
          p->time_slice = DEFAULT_SLICE * p->sched_weight;
        } else {
          // plain RR: everyone gets the same slice
          p->time_slice = DEFAULT_SLICE;
        }
        p->ticks_used = 0;
        // --- end of our hook ---

        // Switch to chosen process.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }

      release(&p->lock);
    }

    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}
*/
/*NEWLY-ADDED CODE : AFTER FIXING THE TIMESLICE FOR THE SET TIMESLICE PART ( IF UNCORRECT BEHAVIOURS, DELETE THIS) - VERIFIED , WORKING , REFER TO THIS IF NEW VERSION FAILS 
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();
    intr_off();

    int found = 0;

    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);

      if (p->state == RUNNABLE) {

        if (sched_policy == SCHED_WEIGHTED_RR) {
          // WRR: slice derived from weight
          if (p->sched_weight < 1)
            p->sched_weight = 1;
          p->time_slice = DEFAULT_SLICE * p->sched_weight;
        }
        // else: RR -> KEEP time_slice as set by allocproc() or setschedslice()

        p->ticks_used = 0;

        p->state = RUNNING;
        c->proc  = p;
        swtch(&c->context, &p->context);

        c->proc = 0;
        found   = 1;
      }

      release(&p->lock);
    }

    if (!found) {
      asm volatile("wfi");
    }
  }
}
*/
//newly-added code : also has stride 
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();
    intr_off();

    int found = 0;

    if (sched_policy == SCHED_STRIDE) {
      // --- STRIDE SCHEDULER ---
      struct proc *best = 0;

      for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == RUNNABLE) {
          // keep stride sane even if weight changed
          if (p->sched_weight < 1)
            p->sched_weight = 1;
          p->stride = BIG_STRIDE / p->sched_weight;
          if (p->stride <= 0)
            p->stride = 1;

          if (best == 0 || p->pass < best->pass) {
            if (best)
              release(&best->lock);
            best = p;
          } else {
            release(&p->lock);
          }
        } else {
          release(&p->lock);
        }
      }

      if (best) {
        // run best for a fixed small quantum
        best->time_slice = DEFAULT_SLICE;
        best->ticks_used = 0;

        best->state = RUNNING;
        c->proc = best;

        // account one schedule-in
        best->pass += best->stride;

        swtch(&c->context, &best->context);
        c->proc = 0;
        release(&best->lock);
        found = 1;
      }

    } 

     /*else if (sched_policy == SCHED_MLFQ) {
  struct proc *best = 0;
  int best_prio = -1;

  // pick RUNNABLE proc with highest prio
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      if (p->prio > best_prio) {
        if (best)
          release(&best->lock);
        best_prio = p->prio;
        best = p;
      } else {
        release(&p->lock);
      }
    } else {
      release(&p->lock);
    }
  }

  if (best) {
    struct cpu *c = mycpu();

    // make sure slice matches its queue
    best->time_slice = mlfq_slice_for_prio(best->prio);
    best->ticks_used = 0;

    c->proc = best;
    best->state = RUNNING;
    best->num_scheduled++;

    found = 1;
    swtch(&c->context, &best->context);

    c->proc = 0;
    release(&best->lock);
  }
}
*/
else if (sched_policy == SCHED_MLFQ) {
  static int last = 0;
  struct proc *best = 0;
  int best_prio = -1;

  // 1) find highest priority among RUNNABLE
  for (int i = 0; i < NPROC; i++) {
    int idx = (last + i) % NPROC;
    p = &proc[idx];
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      if (p->prio > best_prio) {
        if (best) release(&best->lock);
        best = p;
        best_prio = p->prio;
        // keep lock on best
      } else {
        release(&p->lock);
      }
    } else {
      release(&p->lock);
    }
  }

  if (best) {
    best->time_slice = mlfq_slice_for_prio(best->prio);
    best->ticks_used = 0;

    best->state = RUNNING;
    c->proc = best;

    // 2) advance last so next time we don't always favor the same proc
    last = (best - proc + 1) % NPROC;

    swtch(&c->context, &best->context);

    c->proc = 0;
    release(&best->lock);
    found = 1;
  }
}

    
    else {
      // --- RR / WEIGHTED RR (your existing logic) ---
      for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);

        if(p->state == RUNNABLE) {

          if(sched_policy == SCHED_WEIGHTED_RR){
            if(p->sched_weight < 1)
              p->sched_weight = 1;      // safety
            p->time_slice = DEFAULT_SLICE * p->sched_weight;
          }
          // SCHED_RR: keep whatever time_slice came from allocproc/setschedslice

          p->ticks_used = 0;

          // Switch to chosen process.
          p->state = RUNNING;
          c->proc = p;
          swtch(&c->context, &p->context);

          // Process is done running for now.
          c->proc = 0;
          found = 1;
        }

        release(&p->lock);
      }
    }

    if(found == 0) {
      asm volatile("wfi");
    }
  }
}

//----------------
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lkmlfq_slice_for_prio()

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  
  /*del this if this cause error */
  // MLFQ: if we slept before consuming the whole slice,
  // treat it as interactive behavior and promote.
  if (sched_policy == SCHED_MLFQ) {
    if (p->ticks_used < p->time_slice && p->prio < MLFQ_MAX_PRIO) {
      p->prio++;
    }
    // keep quantum consistent with new queue
    p->time_slice = mlfq_slice_for_prio(p->prio);
    p->ticks_used = 0;
  }


  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
//NEWLY-ADDED CODE : MAKE THE PROC DUNMP MORE VISIBLE 
void
procdump(void)
{ printf("sched_policy=%d rr_default_slice=%d\n", sched_policy, rr_default_slice);
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("\n");
  printf("sched_policy=%d\n", sched_policy);
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    printf("%d %s prio=%d w=%d slice=%d used=%d tot=%d stride=%d pass=%d %s\n",
           p->pid, state,
           p->prio,
           p->sched_weight,
           p->time_slice,
           p->ticks_used,
           p->total_ticks,
           p->stride,
           p->pass, 
           p->name);

    printf("\n");
  }
}
}
