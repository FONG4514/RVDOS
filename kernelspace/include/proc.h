#ifndef RVDOS_PROC_H
#define RVDOS_PROC_H

#include <arch/riscv.h>   
#include <abi/abi.h>     
#include<param.h>
#include<lock.h>

typedef struct lock spinlock_t;
typedef struct file file_t;


typedef struct PCB {
    spinlock_t lock;
    int state;                // enum procstate
    void *chan;               // If non-zero, sleeping on chan
    pagetable_t pagetable;
    uint64 sz;
    uint64 kstack;
    user_context_t *context;  // Trapframe
    struct context sched_ctx; // Swtch context
    int pid;
    int owner_pid;  
    int priority;             // Base priority
    int effective_priority;   // Current priority
    int skipped_count;        // Scheduler skip count for aging
    int cpu_usage;
    int exit_status;
    int killed;               // one means killed and zero means not killed
    char name[16];
    uint32 cwd_cluster;
    char cwd_path[128];
    uint32 caps;
    file_t *handles[MAX_HANDLES];
} PCB;


struct cpu {
  struct PCB *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int nlock;                  // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[MAXCPUCORE];

enum procstate { UNUSED = PROC_STATE_UNUSED, 
                 SLEEPING = PROC_STATE_SLEEPING, 
                 RUNNABLE = PROC_STATE_RUNNABLE, 
                 RUNNING = PROC_STATE_RUNNING, 
                 ZOMBIE = PROC_STATE_ZOMBIE 
                };

#define BASE_EFF_PRIO 10
#define SKIP_THRESHOLD 5
#define MAX_EFF_PRIO 30


#endif
