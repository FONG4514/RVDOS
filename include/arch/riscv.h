#ifndef RVDOS_ARCH_RISCV_H   // 注意这里的宏名要唯一
#define RVDOS_ARCH_RISCV_H

#include <abi/types.h>

#define MSTATUS_MPP_MASK (3L << 11)
#define MSTATUS_MPP_M    (3L << 11)
#define MSTATUS_MPP_S    (1L << 11)
#define MSTATUS_MPP_U    (0L << 11)
#define MSTATUS_MIE      (1L << 3)

// sstatus 掩码
#define SSTATUS_SPP (1L << 8)   // Previous mode, 1=Supervisor, 0=User
#define SSTATUS_SPIE (1L << 5)  // Supervisor Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)   // Supervisor Interrupt Enable
#define SSTATUS_SUM (1L << 18)  // Supervisor User Memory access

// SV39 分页相关
#define PGSIZE 4096
#define PGSHIFT 12

#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1) // readable
#define PTE_W (1L << 2) // writable
#define PTE_X (1L << 3) // executable
#define PTE_U (1L << 4) // user can access

// 虚拟地址空间布局
#define MAXVA (1L << 38)
#define TRAPFRAME (MAXVA - PGSIZE)
#define TRAPFRAME_GUARD (TRAPFRAME - PGSIZE)  // 保护页，不映射
#define USTACK_TOP TRAPFRAME_GUARD             // 用户栈顶
#define PHYSTOP (0x80000000L + 128*1024*1024)
#define USERBASE PHYSTOP                       // 用户程序基地址
// --- PLIC (Platform Level Interrupt Controller) ---
#define PLIC 0x0c000000L
#define SYSCON 0x100000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

#define UART0_IRQ 10
#define VIRTIO0_IRQ 1

#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // 64-bit register

#ifndef __ASSEMBLER__

// --- 寄存器操作 ---

static inline uint64 r_sstatus() {
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x));
  return x;
}

static inline void w_sstatus(uint64 x) {
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

static inline void intr_on() {
    asm volatile("csrsi sstatus, %0" : : "i"(SSTATUS_SIE));
}

static inline void intr_off() {
    asm volatile("csrci sstatus, %0" : : "i"(SSTATUS_SIE));
}

// 判断当前中断是否开启
static inline int intr_get() {
    return (r_sstatus() & SSTATUS_SIE) != 0;
}

// CLINT 寄存器 (QEMU virt)


static inline uint64 r_mstatus() {
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x));
  return x;
}

static inline void w_mstatus(uint64 x) {
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

static inline void w_mepc(uint64 x) {
  asm volatile("csrw mepc, %0" : : "r" (x));
}

static inline uint64 r_mhartid() {
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r" (x));
  return x;
}

static inline void w_medeleg(uint64 x) {
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

static inline uint64 r_medeleg() {
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x));
  return x;
}

static inline void w_mideleg(uint64 x) {
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

static inline uint64 r_mideleg() {
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x));
  return x;
}

static inline void w_mtvec(uint64 x) {
  asm volatile("csrw mtvec, %0" : : "r" (x));
}

static inline void w_mscratch(uint64 x) {
  asm volatile("csrw mscratch, %0" : : "r" (x));
}

// Machine-mode Interrupt Enable
#define MIE_MEIE (1L << 11) // external
#define MIE_MTIE (1L << 7)  // timer
#define MIE_MSIE (1L << 3)  // software

static inline uint64 r_mie() {
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x));
  return x;
}

static inline void w_mie(uint64 x) {
  asm volatile("csrw mie, %0" : : "r" (x));
}

// --- PMP 寄存器操作 ---

static inline void w_pmpcfg0(uint64 x) {
  asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

static inline void w_pmpaddr0(uint64 x) {
  asm volatile("csrw pmpaddr0, %0" : : "r" (x));
}

// --- Supervisor Mode 寄存器操作 ---

static inline void w_satp(uint64 x) {
  asm volatile("csrw satp, %0" : : "r" (x));
}

static inline uint64 r_satp() {
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x));
  return x;
}

static inline void sfence_vma() {
  asm volatile("sfence.vma zero, zero");
}



static inline void w_stvec(uint64 x) {
  asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64 r_stvec() {
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x));
  return x;
}

static inline uint64 r_scause() {
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x));
  return x;
}

static inline uint64 r_sepc() {
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x));
  return x;
}

static inline void w_sepc(uint64 x) {
  asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64 r_stval() {
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x));
  return x;
}

static inline void w_sscratch(uint64 x) {
  asm volatile("csrw sscratch, %0" : : "r" (x));
}

static inline uint64 r_sscratch() {
  uint64 x;
  asm volatile("csrr %0, sscratch" : "=r" (x));
  return x;
}

static inline uint64 r_sip() {
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x));
  return x;
}

static inline void w_sip(uint64 x) {
  asm volatile("csrw sip, %0" : : "r" (x));
}

static inline void w_tp(uint64 x) {
  asm volatile("mv tp, %0" : : "r" (x));
}

static inline uint64 r_tp() {
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x));
  return x;
}

static inline uint64 r_sp() {
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x));
  return x;
}

// Supervisor-mode Interrupt Enable
#define SIE_SEIE (1L << 9) // external
#define SIE_STIE (1L << 5) // timer
#define SIE_SSIE (1L << 1) // software

static inline uint64 r_sie() {
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x));
  return x;
}

static inline void w_sie(uint64 x) {
  asm volatile("csrw sie, %0" : : "r" (x));
}

// --- 分页辅助宏 (C 专用) ---

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

#define PXMASK          0x1FF
#define PXSHIFT(level)  (PGSHIFT + (9*(level)))
#define PX(level, va) ((((uint64)va) >> PXSHIFT(level)) & PXMASK)

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


#endif
#endif
