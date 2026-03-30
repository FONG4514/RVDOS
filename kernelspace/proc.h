#ifndef PROC_H
#define PROC_H

#include "defs.h"

// Per-CPU state.
struct cpu {
  struct PCB *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int nlock;                  // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[MAXCPUCORE];

enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

#endif
