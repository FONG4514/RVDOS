#ifndef RVDOS_KERNEL_H
#define RVDOS_KERNEL_H

#include <abi/types.h>
#include <abi/abi.h>
#include <abi/syscall.h>
#include <abi/capability.h>
#include <arch/riscv.h>
#include <elf.h>
#include <fs.h>
#include <lock.h>
#include <proc.h>
#include <param.h>
#include <mylibc.h>

// uart.c
void uart_init();
void uart_putc(char c);
int  uart_getc();
void uart_intr();
void printf(char *fmt, ...);

// plic.c
void plic_init();
void plic_inithart();
int  plic_claim();
void plic_complete(int irq);

// panic.c
void panic(char *s);

// kalloc.c
void            kinit();
void*           kalloc();
void            kfree(void *);

// process.c
void            procinit(void);
PCB*            allocproc(void);
void            userinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            yield(void);
PCB*            myproc(void);
void            swtch(struct context*, struct context*);
void            exit(int status);
int             spawn(char *path, char *redir_path);
int             wait(int pid);
void            forkret(void);
void            sleep(void *chan, spinlock_t *lk);
void            wakeup(void *chan);
int             growproc(int n);

// pagetable.c
extern pagetable_t kernel_pagetable;
void            kvminit();
void            kvminithart();
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     kvmcreate();
pagetable_t     uvmcreate(user_context_t *context);
pte_t *         walk(pagetable_t, uint64, int);
void            uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm);

void            push_off(spinlock_t* lock);
void            pop_off(spinlock_t* lock);

// lock.c

void init_lock(spinlock_t * lock , char * lock_name);
void accquire_lock (spinlock_t * lock);
void release_lock (spinlock_t * lock);
int  holding(spinlock_t *lock);

// trap.c
void xsmode_trap_init();
void user_trap_handler();
void user_trap_return();
void kernel_trap_handler();

// fs.c
void fs_init();
void fs_ls();
int fs_read_file(char *filename, uint8 *buf, uint32 max_len);
int fs_read_file_offset(char *filename, uint8 *buf, uint32 offset, uint32 max_len);
int CreateHandler(char *path, int mode);
int ReadFile(int handle, uint8 *buf, uint32 len);
void CloseHandle(int handle);
int MakeDir(char *path);
int ChangeDir(char *path);
int Unlink(char *path);
int mv(char *oldpath, char *newpath);

#endif