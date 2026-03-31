#include "riscv.h"
#include "defs.h"
#include "proc.h"

struct cpu cpus[MAXCPUCORE];
PCB procs[64]; // Max 64 processes for now

struct {
  spinlock_t lock;
  int next_pid;
} proc_pool;

void procinit(void) {
  init_lock(&proc_pool.lock, "proc_pool");
  proc_pool.next_pid = 1;
  
  for(int i = 0; i < 64; i++) {
    init_lock(&procs[i].lock, "proc");
    procs[i].state = UNUSED;
    // Allocate kernel stack for each potential process
    procs[i].kstack = (uint64)kalloc();
  }
}

// Return the current CPU's struct.
struct cpu* mycpu(void) {
  int id = r_tp();
  return &cpus[id];
}

// Return the current process on this CPU.
PCB* myproc(void) {
  push_off(0);
  struct cpu *c = mycpu();
  PCB *p = c->proc;
  pop_off(0);
  return p;
}

int allocpid() {
  int pid;
  accquire_lock(&proc_pool.lock);
  pid = proc_pool.next_pid++;
  release_lock(&proc_pool.lock);
  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
PCB* allocproc(void) {
  PCB *p;

  for(p = procs; p < &procs[64]; p++) {
    // Optimization: Check state without lock first to avoid panics on multi-core
    if(p->state == UNUSED) {
      accquire_lock(&p->lock);
      if(p->state == UNUSED) {
        goto found;
      }
      release_lock(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = SLEEPING;
  p->chan = 0;          // CRITICAL: Clear chan to avoid premature wakeup
  p->exit_status = 0;
  p->sz = 0;

  // Initialize handles
  for(int i = 0; i < 3; i++) {
    p->handles[i] = (file_t *)-1; // Special value for standard handles
  }
  for(int i = 3; i < MAX_HANDLES; i++) {
    p->handles[i] = 0;
  }

  // Allocate a trapframe page.
  if((p->context = (user_context_t *)kalloc()) == 0){
    release_lock(&p->lock);
    return 0;
  }
  memset(p->context, 0, PGSIZE); // Zero out trapframe

  // An empty page table.
  p->pagetable = uvmcreate(p->context);
  if(p->pagetable == 0){
    // TODO: cleanup
    release_lock(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->sched_ctx, 0, sizeof(p->sched_ctx));
  p->sched_ctx.ra = (uint64)forkret;
  p->sched_ctx.sp = p->kstack + PGSIZE;

  return p;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, allocating a
// process, setting its state to RUNNING, and then
// switching to it.
void scheduler(void) {
  PCB *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring interrupts are enabled.
    intr_on();

    for(p = procs; p < &procs[64]; p++) {
      // ONLY acquire lock if process looks runnable.
      // This prevents CPU A from locking a process currently RUNNING on CPU B.
      if(p->state == RUNNABLE) {
        accquire_lock(&p->lock);
        if(p->state == RUNNABLE) {
          p->state = RUNNING;
          c->proc = p;
          
          // Switch to process's page table
          uint64 satp = (8L << 60) | ((uint64)p->pagetable >> 12);
          w_satp(satp);
          sfence_vma();

          swtch(&c->context, &p->sched_ctx);

          // Process is done running for now.
          // Switch back to kernel page table
          w_satp((8L << 60) | ((uint64)kernel_pagetable >> 12));
          sfence_vma();

          c->proc = 0;
        }
        release_lock(&p->lock);
      }
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed p->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->nlock, but that would
// break in the scheduler, so we save them here.
void sched(void) {
  int intena;
  PCB *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->nlock != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->sched_ctx, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) {
  PCB *p = myproc();
  if(p) {
    accquire_lock(&p->lock);
    p->state = RUNNABLE;
    sched();
    release_lock(&p->lock);
  }
}

void sleep(void *chan, spinlock_t *lk) {
  PCB *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be guaranteed
  // that we won't miss any wakeup (wakeup runs with p->lock).
  // So it's okay to release lk.
  accquire_lock(&p->lock);
  release_lock(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release_lock(&p->lock);
  accquire_lock(lk);
}

void wakeup(void *chan) {
  PCB *p;

  for(p = procs; p < &procs[64]; p++) {
    if(p != myproc()){
      accquire_lock(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release_lock(&p->lock);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state.
void exit(int status) {
  PCB *p = myproc();

  accquire_lock(&p->lock);
  p->exit_status = status;
  p->state = ZOMBIE;

  wakeup(&proc_pool); // Wake up anyone waiting for a process to exit

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

int wait(int pid) {
  PCB *p;
  int found;

  accquire_lock(&proc_pool.lock);
  for(;;){
    found = 0;
    for(p = procs; p < &procs[64]; p++){
      if(p->pid == pid){
        found = 1;
        accquire_lock(&p->lock);
        if(p->state == ZOMBIE){
          int status = p->exit_status;
          p->state = UNUSED;
          p->pid = 0;
          // In a real OS, we would free the page table and trapframe here
          // kfree(p->context);
          // uvmfree(p->pagetable, p->sz);
          release_lock(&p->lock);
          release_lock(&proc_pool.lock);
          return status;
        }
        release_lock(&p->lock);
        break;
      }
    }

    if(!found){
      release_lock(&proc_pool.lock);
      return -1;
    }

    // Wait for a process to exit
    sleep(&proc_pool, &proc_pool.lock);
  }
}

// Create a new process, load code from file, and start it.
// Returns pid of the new process, or -1 on error.
int spawn(char *path) {
  PCB *p;
  uint64 pid;

  if((p = allocproc()) == 0)
    return -1;

  uint64 sz = 0;
  while (1) {
      char *mem = kalloc();
      if (mem == 0) break;
      memset(mem, 0, PGSIZE);
      int n = fs_read_file_offset(path, (uint8*)mem, sz, PGSIZE);
      if (n <= 0) {
          kfree(mem);
          break;
      }
      mappages(p->pagetable, sz, (uint64)mem, PGSIZE, PTE_W|PTE_R|PTE_X|PTE_U);
      sz += PGSIZE;
      if (n < PGSIZE) break;
  }

  if (sz == 0) {
      // TODO: cleanup p
      release_lock(&p->lock);
      return -1;
  }

  p->sz = sz;
  p->context->epc = 0;      // user program counter
  
  // Allocate one more page for stack
  char *stack = kalloc();
  if (stack) {
      memset(stack, 0, PGSIZE);
      mappages(p->pagetable, sz, (uint64)stack, PGSIZE, PTE_W|PTE_R|PTE_U);
      p->sz += PGSIZE;
      p->context->sp = p->sz; // user stack pointer at top of new page
  } else {
      p->context->sp = sz; // Fallback to using last page as stack (dangerous)
  }

  p->state = RUNNABLE;
  pid = p->pid;

  release_lock(&p->lock);
  return pid;
}

// Set up first user process.
void userinit(void) {
  if (spawn("shell") < 0) {
    panic("userinit: failed to spawn shell");
  }
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void) {
  static int first = 1;

  // Still holding p->lock from scheduler.
  release_lock(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    // fs_init(ROOTDEV); // Already done in main
  }

  user_trap_return();
}
