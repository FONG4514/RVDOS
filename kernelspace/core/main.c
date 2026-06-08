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
#include <sbi.h>

__attribute__ ((aligned (16))) char stack0[4096 * 8]; // Support up to 8 harts

extern void uart_init();
extern void printf(char *fmt, ...);
extern void kernel_vector();
extern void _entry();   // 次核启动入口（汇编）
extern uint64 ticks;
extern spinlock_t tick_lock;

volatile static int started = 0;
volatile int primary_hart = -1;
int is_sbi = 1;  // 仅支持SBI模式

const rvdos_abi_info_t KERNEL_ABI_INFO = {
    .abi_version = RVDOS_ABI_VER,
    .caps = CAP_FS_READ | CAP_FS_WRITE | CAP_FS_DIR | CAP_FS_CWD | CAP_FS_RENAME |
            CAP_PROC_BASIC | CAP_MEM_SBRK | CAP_SYS_TIME | CAP_PROC_PS | CAP_SYS_POWER | CAP_PROC_KILL | CAP_PROC_SLEEP | CAP_PROC_TRACE |
            CAP_PROC_SANDBOX
};

void sbi_timer_init() {
    // 设置第一个定时器中断，约 100ms 后
    uint64 interval = 1000000; 
    sbi_set_timer(r_time() + interval);
}

void read_icon(uint8* icon_buf) {
    if (icon_buf) {
      int n = fs_read_file("ICON", icon_buf, 4096);
      printf("Welcome to RVDOS\n%s\n", (char*)icon_buf);
      if (n > 0) {
          icon_buf[n < 4096 ? n : 4095] = '\0';
      } else {
          printf("Failed to read ICON file!\n");
      }
  }
}

void boot_log(char *msg, int status) {
    printf(" [ RVDOS ] ");

    printf("%s", msg);
    
    int len = strlen(msg);
    for(int i = 0; i < 35 - len; i++) {
        printf(" ");
    }

    if (status == 0) {
        printf("[  OK  ]\n"); 
    } else if (status == 1) {
        printf("[ FAIL ]\n");
    } else {
        printf("[ WARN ]\n");
    }
}

void main(uint64 dtb) {
  int id = (int)r_tp();

  if (__sync_bool_compare_and_swap(&primary_hart, -1, id)) {
    printf("\n--- Entering RVDOS ---\n");
    printf("Kernel Version: %s\n", KERNEL_VERSION);
    printf("Kernel ABI Version: %d\n", KERNEL_ABI_INFO.abi_version);
    printf("Boot Hart ID: %d, Mode: S-mode (SBI)\n\n", id);

    kinit();
    boot_log("Physical Memory", 0);
    
    kmalloc_init();
    boot_log("Slab Allocator", 0);
    
    kvminit();
    kvminithart();
    boot_log("Kernel Page Table", 0);

    uart_init();
    boot_log("UART Driver", 0);

    plic_init();
    plic_inithart(id);
    boot_log("PLIC (Interrupt Controller)", 0);

    xsmode_trap_init();
    boot_log("Trap Handlers", 0);

    procinit();
    boot_log("Process Manager", 0);

    fs_init();
    boot_log("FAT32 File System", 0);

    uint8 *icon_buf = (uint8*)kmalloc(4096);
    if (icon_buf) {
        read_icon(icon_buf);
        kmfree(icon_buf);
    }

    printf("\nSystem initialization complete.\n");

    userinit();

    sbi_timer_init();
    // 在 SBI 模式下，开启 S-mode 定时器中断
    w_sie(r_sie() | SIE_STIE);

    __sync_synchronize();
    started = 1;

    // 通过 SBI HSM 唤醒其余次核，从 _entry 进入内核
    // _entry 在内核物理地址（恒等映射，虚拟==物理）
    for (int h = 0; h < MAXCPUCORE; h++) {
      if (h == id) continue;
      struct sbiret ret = sbi_hart_start(h, (uint64)_entry, 0);
      if (ret.error == 0) {
        printf(" [ RVDOS ] Hart %d started (SBI HSM)\n", h);
      }
    }
  } else {
    while (started == 0)
      ;
    __sync_synchronize();
    w_stvec((uint64)kernel_vector);
    kvminithart();
    plic_inithart(id);
    w_sie(r_sie() | SIE_STIE);
  }

  w_sie(r_sie() | SIE_SSIE | SIE_SEIE);
  intr_on();

  scheduler();
}

