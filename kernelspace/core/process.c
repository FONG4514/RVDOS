/*
 * Derived from xv6-riscv (https://github.com/mit-pdos/xv6-riscv)
 * Copyright (c) 2006-2023 Frans Kaashoek, Robert Morris, Russ Cox, 
 *                         Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <kernel.h>

struct cpu cpus[MAXCPUCORE];
PCB* procs[MAXPROCESSES];
struct kmem_cache *pcb_cache;

extern const rvdos_abi_info_t KERNEL_ABI_INFO;


extern void file_close(file_t *f);
uint8 pid_alive[MAXPID];

struct {
  spinlock_t lock;
  uint32 next_pid;
} proc_pool;

void procinit(void) {
  init_lock(&proc_pool.lock, "proc_pool");
  proc_pool.next_pid = 0;
  
  pcb_cache = kmem_cache_create("PCB", sizeof(PCB));
  if (!pcb_cache) panic("procinit: kmem_cache_create failed");

  for(int i = 0; i < MAXPROCESSES; i++) {
    procs[i] = (PCB*)kmem_cache_alloc(pcb_cache);
    if (!procs[i]) panic("procinit: kmem_cache_alloc failed");
    
    init_lock(&procs[i]->lock, "proc");
    procs[i]->state = UNUSED;
    // Allocate kernel stack for each potential process
    procs[i]->kstack = (uint64)kalloc();
    if (!procs[i]->kstack) panic("procinit: kstack kalloc failed");
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
  int pid = -1;
  accquire_lock(&proc_pool.lock);

  int start_scan = proc_pool.next_pid;

  while (1) {
    proc_pool.next_pid++;
    
    if (proc_pool.next_pid > MAXPID) {
      proc_pool.next_pid = 2; 
    }

    if (!pid_alive[proc_pool.next_pid]) {
      pid = proc_pool.next_pid;
      pid_alive[pid] = 1; 
      break;
    }

    if (proc_pool.next_pid == start_scan) {
      release_lock(&proc_pool.lock);
      panic("allocpid: system process limit reached (65534 active procs)");
    }
  }

  release_lock(&proc_pool.lock);
  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
PCB* allocproc(void) {
  PCB *p;

  for(int i = 0; i < MAXPROCESSES; i++) {
    p = procs[i];
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
  p->killed = 0;
  p->owner_pid = 0;
  p->tracing = 0;


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
  p->caps = 0;

  pid_alive[p->pid] = 1;
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
    // 1. 在寻找任务前开启中断，确保可以响应时钟/外设
    intr_on();

    PCB *best = 0;
    for(int i = 0; i < MAXPROCESSES; i++) {
      p = procs[i];

      // --- 优化: 锁前探测 (Double-Checked Locking) ---
      // 先不拿锁，直接看状态。如果不是 RUNNABLE，连锁都不去碰。
      // 这一步是读取，不会导致缓存行在多核间冲突。
      if(p->state != RUNNABLE) continue;

      // accquire_lock 内部通常会调用 push_off() 关闭中断
      accquire_lock(&p->lock);
      if(p->state == RUNNABLE) {
        if(best == 0 || p->effective_priority > best->effective_priority) {
          if(best) release_lock(&best->lock);
          best = p;
          continue; // 保持 best 的锁，此时中断是关闭的
        } else if (p->effective_priority == best->effective_priority && p->pid < best->pid) {
          if(best) release_lock(&best->lock);
          best = p;
          continue; // 保持 best 的锁
        }
      }
      release_lock(&p->lock);
    }


    if(best) {
      // 2. 此时中断已经由于持有 best->lock 而关闭
      p = best;
      p->state = RUNNING;
      c->proc = p;
      
      // 切换页表
      uint64 satp = (8L << 60) | ((uint64)p->pagetable >> 12);
      w_satp(satp);
      sfence_vma();

      // 执行切换
      swtch(&c->context, &p->sched_ctx);

      // --- 进程运行结束回到调度器 ---

      // 切换回内核页表
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

      // 释放锁（此时中断依然是关闭的，直到下一次循环开始的 intr_on）
      release_lock(&p->lock);

      // 动态老化逻辑（可以在关中断下快速完成）
      for (int i = 0; i < MAXPROCESSES; i++) {
          p = procs[i];
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
    } else {
        // 3. 如果没找到任务，可以执行 wfi (Wait For Interrupt) 降低功耗
        // 这会让 CPU 挂起，直到下一个中断（如时钟中断）将其唤醒重新扫描
        asm volatile("wfi");
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

  if(p->killed) {
    exit(-1);
  }
}

void wakeup(void *chan) {
  PCB *p;

  for(int i = 0; i < MAXPROCESSES; i++) {
    p = procs[i];
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
  int is_nonblocking = (pid == WAIT_NONBLOCK_KEY);
  PCB *current_proc = myproc();
  int current_pid = current_proc->pid;

  // 权限检查：只有 PID 1 (shell) 可以使用特殊的非阻塞暗号
  if (is_nonblocking && current_pid != 1) {
    return -1;
  }


  accquire_lock(&proc_pool.lock);
  for (;;) {
    int has_runnable_child = 0; // 记录系统中是否还有属于我的（或我该管的）活着的进程

    for (int i = 0; i < MAXPROCESSES; i++) {
      p = procs[i];
      // skip unused and itself
      if (p->state == UNUSED || p->pid == 0 || p->pid == current_pid) continue;

      int is_my_child = (p->owner_pid == current_pid);
      int is_orphan = 0;

      if (is_nonblocking) {
        if (!is_my_child) {
          int owner_active = pid_alive[p->owner_pid];
          if (!owner_active) is_orphan = 1;
        }
      }

      if ((!is_nonblocking && p->pid == pid && is_my_child) || 
          (is_nonblocking && (is_my_child || is_orphan))) {

        // --- 优化: wait 时的锁前探测 ---
        // 调度器在抢 RUNNABLE，我们在抢 ZOMBIE。
        // 如果状态不对，绝对不去碰锁，给调度器让路；反之亦然。
        if(p->state != ZOMBIE) {
            has_runnable_child = 1;
            continue;
        }

        accquire_lock(&p->lock);

        if (p->state == ZOMBIE) {

          // hit the zombie
          int status = p->exit_status;
          int target_pid = p->pid;

          if (p->pagetable) uvmfree(p->pagetable, p->sz);
          
          if (p->context) kfree((void*)p->context);
          p->context = 0;
          p->pagetable = 0;
          
          for (int i = 0; i < MAX_HANDLES; i++) {
            if (p->handles[i] && p->handles[i] != (file_t*)-1) {
              file_close(p->handles[i]);
            }
            p->handles[i] = 0;
          }

          p->state = UNUSED;
          p->owner_pid = 0;
          p->killed = 0;
          memset(p->name, 0, sizeof(p->name));

          pid_alive[target_pid] = 0;
          p->pid = 0;

          release_lock(&p->lock);
          release_lock(&proc_pool.lock);
          
          // 非阻塞模式返回 PID，阻塞模式返回状态
          return is_nonblocking ? target_pid : status;
        }

        // 走到这里说明找到了符合关系的进程，但它还没死
        has_runnable_child = 1;
        release_lock(&p->lock);
        
        // 如果是普通模式找特定 PID，既然还没死，就没必要看别的槽位了
        if (!is_nonblocking) break;
      }
    }

    // --- 退出与阻塞逻辑 ---

    if (is_nonblocking) {
      release_lock(&proc_pool.lock);
      return 0; // 扫了一圈没发现能收的，直接回
    }

    // 阻塞模式：如果没有相关孩子了，返回 -1 报错
    if (!has_runnable_child) {
      release_lock(&proc_pool.lock);
      return -1;
    }

    // 阻塞模式：有孩子 but 还没死，睡等唤醒
    sleep(&proc_pool, &proc_pool.lock);
  }
}
#define O_WRONLY           1
#define O_CREATE           0x100
#define O_TRUNC            0x200

// Create a new process, load code from file, and start it.
// Returns pid of the new process, or -1 on error.
int spawn(char *path, char *args, uint64 cap) {
  PCB *p;
  uint64 pid;
  struct elfhdr elf;
  struct proghdr ph;
  int i, off;
  pagetable_t pagetable = 0;

  if((p = allocproc()) == 0)
    return -1;

  PCB *parent = myproc();
  if (parent == 0) {
        p->owner_pid = 1; // 或者设为 0，代表它是系统根进程
    } else {
        p->owner_pid = parent->pid;
    }
  if (parent) {
      p->cwd_cluster = parent->cwd_cluster;
      memcpy(p->cwd_path, parent->cwd_path, 128);
      p->tracing = parent->tracing;
  }
  p->caps = cap;

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

  // Allocate one more page for stack, placed at USTACK_TOP - PGSIZE
  char *stack = kalloc();
  if (stack) {
      memset(stack, 0, PGSIZE);
      // Map stack at USTACK_TOP - PGSIZE
      uint64 stack_va = USTACK_TOP - PGSIZE;
      mappages(p->pagetable, stack_va, (uint64)stack, PGSIZE, PTE_W|PTE_R|PTE_U);
      
      // Note: TRAPFRAME_GUARD is between USTACK_TOP and TRAPFRAME,
      // and it remains unmapped (no mappages call for it).

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
      uargs[argc++] = (char*)(stack_va + sp_offset); // Virtual address

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
              uargs[argc++] = (char*)(stack_va + sp_offset);
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

      p->context->sp = stack_va + sp_offset; // Virtual SP
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
  if (p->pagetable) uvmfree(p->pagetable, p->sz);
  if (p->context) kfree((void*)p->context);
  p->context = 0;
  p->pagetable = 0;
  for (int i = 0; i < MAX_HANDLES; i++) {
    if (p->handles[i] && p->handles[i] != (file_t*)-1) {
      file_close(p->handles[i]);
    }
    p->handles[i] = 0;
  }
  p->state = UNUSED;
  release_lock(&p->lock);
  return -1;
}

// Set up first user process.
void userinit(void) {
  if (spawn("shell", 0,KERNEL_ABI_INFO.caps) < 0) {
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

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// from a system call or trap to usermode.
int kill(int pid) {
  PCB *p;

  // 获取全局锁，保护进程池的遍历
  accquire_lock(&proc_pool.lock); 

  for(int i = 0; i < MAXPROCESSES; i++){
    p = procs[i];
    // 注意：这里我们只在必要时拿 p->lock
    if(p->pid == pid){
      accquire_lock(&p->lock);
      
      if(p->state == ZOMBIE || p->state == UNUSED){
        release_lock(&p->lock);
        release_lock(&proc_pool.lock); // 记得释放全局锁
        return -1;
      }

      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
      }

      release_lock(&p->lock);
      
      // --- 关键修复 ---
      // 在调用 wakeup 之前释放全局锁！
      // 因为 wakeup 会遍历所有进程并尝试获取每个 p->lock
      // 保持“先全局后局部”且不长期霸占全局锁是内核安全的准则
      release_lock(&proc_pool.lock);
      
      wakeup(&proc_pool); 
      return 0;
    }
  }
  
  release_lock(&proc_pool.lock);
  return -1;
}

int growproc(int n) {
  uint64 sz;
  PCB *p = myproc();

  accquire_lock(&p->lock);
  sz = p->sz;
  if(n > 0){
    // Boundary check: Heap cannot grow into the stack (at USTACK_TOP - PGSIZE)
    if (sz + n >= USTACK_TOP - PGSIZE) {
      release_lock(&p->lock);
      return -1;
    }
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
