// kernelspace/defs.h
#ifndef DEFS_H
#define DEFS_H

#define MAXCPUCORE 8
#define KERNEL_VERSION "sd_0.5.3_std"

#ifndef __ASSEMBLER__

typedef char int8;
typedef short int16;
typedef int int32;
typedef  long int64;

typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

// uart.c 中定义的函数
void uart_init();
void uart_putc(char c);
int  uart_getc();
void uart_intr();
void printf(char *fmt, ...);

// plic.c (or implemented in trap.c for now)
void plic_init();
void plic_inithart();
int  plic_claim();
void plic_complete(int irq);

// panic.c
void panic(char *s);

typedef struct lock {
    volatile uint32 locked; 
    char *lock_name;
    uint32 lock_count;
} spinlock_t;

typedef uint64 pte_t;
typedef uint64 *pagetable_t;

// Saved registers for kernel context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

typedef struct user_context {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
} user_context_t;

// System call numbers
#define SYS_TRAP          10
#define SYS_GET_TICKS     11
#define SYS_SPAWN         12
#define SYS_EXIT          13
#define SYS_GETPID        14
#define SYS_CREATE_FILE   15
#define SYS_READ_FILE     16
#define SYS_WRITE_FILE    17
#define SYS_CLOSE_HANDLE  18
#define SYS_WAIT          19
#define SYS_LS            20
#define SYS_PANIC         21
#define SYS_POWEROFF      22
#define SYS_REBOOT        23
#define SYS_MKDIR         24
#define SYS_CHDIR         25
#define SYS_UNLINK        26
#define SYS_GETCWD        27
#define SYS_RENAME        28

#define MAX_HANDLES 16
#define STDOUT 1
#define STDERR 2

typedef struct file {
    int used;
    int readable;
    int writable;
    uint32 offset;       // Current pointer
    uint32 first_cluster;
    uint32 file_size;
    char name[16];
} file_t;

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
    int exit_status;
    char name[16];
    uint32 cwd_cluster;
    char cwd_path[128];
    file_t *handles[MAX_HANDLES];
} PCB;


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

// mylibc.c
void*           memcpy(void *dst, const void *src, uint n);
void*           memset(void *dst, int c, uint n);
int             memcmp(const void *v1, const void *v2, uint n);
int             strcmp(const char *p, const char *q);
uint            strlen(const char *s);
int             copyout(pagetable_t pagetable, uint64 dstva, uint8 *src, uint64 len);

// pagetable.c
extern pagetable_t kernel_pagetable;
void            kvminit();
void            kvminithart();
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     kvmcreate();
pagetable_t     uvmcreate(user_context_t *context);
pte_t *         walk(pagetable_t, uint64, int);
void            uvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm);


// inir.c

static inline void intr_off() {
    asm volatile("csrci sstatus, 1 << 1");
}

static inline void intr_on() {
    asm volatile("csrsi sstatus, 1 << 1");
}

int             intr_get();
void            push_off(spinlock_t* lock);
void            pop_off(spinlock_t* lock);

#define PHYSTOP (0x80000000L + 128*1024*1024)

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

#endif // __ASSEMBLER__

#endif // DEFS_H
