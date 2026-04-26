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
  p->priority = 10;     // Default base priority
  p->effective_priority = 10;
  p->skipped_count = 0;
  p->cpu_usage = 0;



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

  // Initialize current working directory to root
  extern uint32 fs_get_root_cluster(void);
  p->cwd_cluster = fs_get_root_cluster();
  p->cwd_path[0] = '/';
  p->cwd_path[1] = '\0';

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

    PCB *best = 0;
    for(p = procs; p < &procs[64]; p++) {
      accquire_lock(&p->lock);
      if(p->state == RUNNABLE) {
        if(best == 0 || p->effective_priority > best->effective_priority) {
          if(best) release_lock(&best->lock);
          best = p;
          continue; // Keep best locked
        } else if (p->effective_priority == best->effective_priority && p->pid < best->pid) {
          if(best) release_lock(&best->lock);
          best = p;
          continue; // Keep best locked
        }
      }
      release_lock(&p->lock);
    }

    if(best) {
      // best is already locked here
      p = best;
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
      p->cpu_usage++;

      if (p->cpu_usage >= 5) {
          if (p->effective_priority > 1)
              p->effective_priority--;

          p->cpu_usage = 0;
      }
      p->skipped_count = 0;

      
      release_lock(&p->lock);

      for (p = procs; p < &procs[64]; p++) {
          if (p->state == RUNNABLE) {
              accquire_lock(&p->lock);

              p->skipped_count++;

              if (p->skipped_count >= 5) {
                  if (p->effective_priority < 100)
                      p->effective_priority++;

                  p->skipped_count = 0;
              }

              if (p->effective_priority < 3)
                p->effective_priority = 3;

              release_lock(&p->lock);
          }
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

        if (p->effective_priority < BASE_EFF_PRIO + 5) {
          p->effective_priority += 1;
        }
        p->skipped_count = 0;

      }

      release_lock(&p->lock);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state.
void exit(int status) {
  PCB *p = myproc();

  // Close all handles
  for(int h = 0; h < MAX_HANDLES; h++) {
    if(p->handles[h]) {
      CloseHandle(h);
    }
  }

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

#define O_WRONLY           1
#define O_CREATE           0x100
#define O_TRUNC            0x200

// Create a new process, load code from file, and start it.
// Returns pid of the new process, or -1 on error.
int spawn(char *path, char *args) {
  PCB *p;
  uint64 pid;
  struct elfhdr elf;
  struct proghdr ph;
  int i, off;
  pagetable_t pagetable = 0;

  if((p = allocproc()) == 0)
    return -1;

  PCB *parent = myproc();
  if (parent) {
      p->cwd_cluster = parent->cwd_cluster;
      memcpy(p->cwd_path, parent->cwd_path, 128);
  }

  pagetable = p->pagetable;

  // Check ELF header
  if(fs_read_file_offset(path, (uint8*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  if(elf.magic != ELF_MAGIC) {
    // printf("spawn: %s is not a valid ELF\n", path);
    goto bad;
  }

  uint64 sz = 0;
  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(fs_read_file_offset(path, (uint8*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    
    // Allocate and map memory for the segment, solve .BSS
    for(uint64 j = 0; j < ph.memsz; j += PGSIZE){
      char *mem = kalloc();
      if(mem == 0) goto bad;
      memset(mem, 0, PGSIZE);
      
      uint64 count = (ph.filesz > j) ? (ph.filesz - j) : 0;
      if (count > PGSIZE) count = PGSIZE;
      
      if(count > 0){
        if(fs_read_file_offset(path, (uint8*)mem, ph.off + j, (uint32)count) != (int)count){
          kfree(mem);
          goto bad;
        }
      }
      
      int perm = PTE_U;
      if(ph.flags & ELF_PROG_FLAG_READ) perm |= PTE_R;
      if(ph.flags & ELF_PROG_FLAG_WRITE) perm |= PTE_W;
      if(ph.flags & ELF_PROG_FLAG_EXEC) perm |= PTE_X;

      if(mappages(pagetable, ph.vaddr + j, (uint64)mem, PGSIZE, perm) < 0){
        kfree(mem);
        goto bad;
      }
    }
    if (ph.vaddr + ph.memsz > sz) sz = ph.vaddr + ph.memsz;
  }

  p->sz = PGROUNDUP(sz);
  p->context->epc = elf.entry;      // user program counter
  
  // Extract filename from path and store in p->name
  int last_slash = -1;
  for(int i = 0; path[i]; i++) if(path[i] == '/') last_slash = i;
  char *filename = path + last_slash + 1;
  int name_len = strlen(filename);
  if(name_len > 15) name_len = 15;
  memcpy(p->name, filename, name_len);
  p->name[name_len] = '\0';

  // Allocate one more page for stack
  char *stack = kalloc();
  if (stack) {
      memset(stack, 0, PGSIZE);
      mappages(p->pagetable, p->sz, (uint64)stack, PGSIZE, PTE_W|PTE_R|PTE_U);
      p->sz += PGSIZE;
      
      // Since kernel is mapped into user space and we use identity mapping for physical memory,
      // we can write to the 'stack' pointer (physical address) directly to set up user stack.
      uint64 sp_offset = PGSIZE; // Start from top of the page
      uint64 argc = 0;
      uint64 *uargv;
      char *uargs[16]; // Max 16 args

      // 1. Copy program name (argv[0])
      int path_len = strlen(path);
      sp_offset -= (path_len + 1);
      memcpy(stack + sp_offset, path, path_len + 1);
      uargs[argc++] = (char*)(p->sz - PGSIZE + sp_offset); // Virtual address

      // 2. Parse and copy arguments, and handle redirection
      char *redir_file = 0;
      if (args && args[0] != '\0') {
          char *s = args;
          while (*s && argc < 15) {
              while (*s == ' ') s++;
              if (*s == '\0') break;
              
              // Check for redirection
              if (*s == '>') {
                  *s = '\0'; // End arguments here for the process
                  s++;
                  while (*s == ' ') s++;
                  if (*s != '\0') redir_file = s;
                  break; 
              }

              char *start = s;
              while (*s && *s != ' ' && *s != '>') s++;
              int len = s - start;
              
              sp_offset -= (len + 1);
              memcpy(stack + sp_offset, start, len);
              stack[sp_offset + len] = '\0';
              uargs[argc++] = (char*)(p->sz - PGSIZE + sp_offset);
              if (*s == '>') continue; // Will be handled in next iteration or break
          }
      }

      // Handle output redirection if found
      if (redir_file) {
          // Trim trailing spaces from redir_file
          char *end = redir_file;
          while (*end && *end != ' ') end++;
          if (*end == ' ') *end = '\0';

          int h = CreateHandler(redir_file, O_WRONLY | O_CREATE | O_TRUNC);
          if (h >= 0) {
              p->handles[STDOUT] = myproc()->handles[h];
              myproc()->handles[h] = 0;
          }
      }

      // 3. Set up argv array (pointers)
      sp_offset -= (argc + 1) * sizeof(uint64);
      sp_offset &= ~0xF; // 16-byte align stack
      uargv = (uint64*)(stack + sp_offset);
      for(int i = 0; i < argc; i++) uargv[i] = (uint64)uargs[i];
      uargv[argc] = 0;

      p->context->sp = p->sz - PGSIZE + sp_offset; // Virtual SP
      p->context->a0 = argc;
      p->context->a1 = p->context->sp;
  } else {
      goto bad;
  }

  p->state = RUNNABLE;
  pid = p->pid;

  release_lock(&p->lock);
  return pid;

bad:
  p->state = UNUSED;
  release_lock(&p->lock);
  return -1;
}

// Set up first user process.
void userinit(void) {
  if (spawn("shell", 0) < 0) {
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

int growproc(int n) {
  uint64 sz;
  PCB *p = myproc();

  accquire_lock(&p->lock);
  sz = p->sz;
  if(n > 0){
    for(uint64 a = PGROUNDUP(sz); a < sz + n; a += PGSIZE){
      char *mem = kalloc();
      if(mem == 0){
        release_lock(&p->lock);
        return -1;
      }
      memset(mem, 0, PGSIZE);
      if(mappages(p->pagetable, a, (uint64)mem, PGSIZE, PTE_W|PTE_R|PTE_U) < 0){
        kfree(mem);
        release_lock(&p->lock);
        return -1;
      }
    }
  }
  p->sz += n;
  release_lock(&p->lock);
  return 0;
}

