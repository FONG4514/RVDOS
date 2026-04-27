#include <kernel.h>

__attribute__ ((aligned (16))) char stack0[4096 * 8]; // Support up to 8 harts

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[8][5];

void main();
void timerinit();

extern void uart_init();
extern void printf(char *fmt, ...);
extern void timervec();

void timerinit() {
  int id = r_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000; // cycles; about 1/10th second in qemu.
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..2] : space for timervec to save registers.
  // scratch[3] : address of CLINT MTIMECMP register.
  // scratch[4] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  w_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  w_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  w_mie(r_mie() | MIE_MTIE);
}

void start() {
  if (r_mhartid() == 0) {
    uart_init();
  }

  unsigned long x = r_mstatus();
  
  // 1. 清除 MPP 位
  x &= ~MSTATUS_MPP_MASK; 
  
  // 2. 设置 MPP 为 S-mode (通常值是 1)
  // 这样 mret 才会跳转到 S-mode 而不是 U-mode
  x |= MSTATUS_MPP_S; 
  
  w_mstatus(x);

  // delegate all interrupts and exceptions to supervisor mode.
  // except for timer interrupts, which we handle in M-mode to set SSIP.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  // Do NOT delegate timer interrupt (bit 7) if we handle it in M-mode
  w_mideleg(r_mideleg() & ~MIE_MTIE);

  // 3. 设置跳转地址
  w_mepc((uint64)main);

  // 4. 禁用分页（临时），防止跳转后地址空间对不上
  w_satp(0);

  // 允许 S-mode 访问所有物理地址
  w_pmpaddr0(0x3fffffffffffffull); // 覆盖整个 64-bit 地址空间
  w_pmpcfg0(0xf);                  // 设置为 TOR (Top of Range) 并赋予 R/W/X 权限

  // 5. 设置线程指针/硬件 ID
  int id = r_mhartid();
  w_tp(id);

  // 6. 初始化时钟
  timerinit();

  // 此时执行 mret，硬件会将 PC 设为 mepc，特权级设为 MPP
  asm volatile("mret");
}