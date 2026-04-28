#include <kernel.h>

extern void uart_init();
extern void printf(char *fmt, ...);
extern void kernel_vector();
extern uint64 ticks;
extern spinlock_t tick_lock;

volatile static int started = 0;

const rvdos_abi_info_t KERNEL_ABI_INFO = {
    .abi_version = RVDOS_ABI_V1,
    .caps = CAP_FS_BASIC | CAP_FS_DIR | CAP_FS_CWD | CAP_FS_RENAME |
            CAP_PROC_BASIC | CAP_MEM_SBRK | CAP_SYS_TIME | CAP_PROC_PS | CAP_SYS_POWER | CAP_PROC_KILL | CAP_PROC_SLEEP | CAP_PROC_TRACE
};

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

void main() {
  if (r_tp() == 0) {
    printf("\n--- Entering RVDOS ---\n");
    printf("\n--- Kernel Version: %s ---\n",KERNEL_VERSION);
    printf("Initializing physical memory...\n");
    kinit();
    
    printf("Initializing kernel page table...\n");
    kvminit();
    
    printf("Enabling paging on Hart 0...\n");
    kvminithart();
    
    printf("Paging enabled on Hart 0!\n");
    printf("Current Hart ID: %d\n", (int)r_tp());

    printf("init trap!\n");
    xsmode_trap_init();

    printf("Initializing processes...\n");
    procinit();

    printf("Initializing file system...\n");
    fs_init();

    uint8 *icon_buf = (uint8*)kalloc();
    read_icon(icon_buf);

     userinit();

    __sync_synchronize();
    started = 1;
  } else {
    while (started == 0)
      ;
    __sync_synchronize();
    w_stvec((uint64)kernel_vector);
    kvminithart();
  }

  // Enable supervisor software interrupts (for timer) and external interrupts (for PLIC)
  w_sie(r_sie() | SIE_SSIE | SIE_SEIE);
  intr_on();

  scheduler();
}
