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
    uint32 pid;
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
    uint64 caps;
    int tracing;
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
#define MAXPID 65535


#endif
