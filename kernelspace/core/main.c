#include <kernel.h>

extern void uart_init();
extern void printf(char *fmt, ...);
extern void kernel_vector();
extern uint64 ticks;
extern spinlock_t tick_lock;

volatile static int started = 0;

const rvdos_abi_info_t KERNEL_ABI_INFO = {
    .abi_version = RVDOS_ABI_VER,
    .caps = CAP_FS_READ | CAP_FS_WRITE | CAP_FS_DIR | CAP_FS_CWD | CAP_FS_RENAME |
            CAP_PROC_BASIC | CAP_MEM_SBRK | CAP_SYS_TIME | CAP_PROC_PS | CAP_SYS_POWER | CAP_PROC_KILL | CAP_PROC_SLEEP | CAP_PROC_TRACE |
            CAP_PROC_SANDBOX
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

void boot_log(char *msg, int status) {
    printf(" [ RVDOS ] ");

    printf("%s", msg);
    
    // 手动补齐空格（确保 [ OK ] 对齐）
    int len = strlen(msg);
    for(int i = 0; i < 35 - len; i++) {
        printf(" ");
    }

    // 3. 打印状态
    if (status == 0) {
        printf("[  OK  ]\n"); 
    } else if (status == 1) {
        printf("[ FAIL ]\n");
    } else {
        printf("[ WARN ]\n");
    }
}

void main() {
  if (r_tp() == 0) {
printf("\n--- Entering RVDOS ---\n");
    printf("Kernel Version: %s\n", KERNEL_VERSION);
    printf("Kernel ABI Version: %d\n\n",KERNEL_ABI_INFO.abi_version);
    // 逐项初始化并打印状态
    kinit();
    boot_log("Physical Memory", 0);
    
    kmalloc_init();
    boot_log("Slab Allocator", 0);
    
    kvminit();
    kvminithart();
    boot_log("Kernel Page Table", 0);
    
    xsmode_trap_init();
    boot_log("Trap Handlers", 0);

    procinit();
    boot_log("Process Manager", 0);

    fs_init();
    boot_log("FAT32 File System", 0);


    // 自检图标
    uint8 *icon_buf = (uint8*)kmalloc(4096);
    if (icon_buf) {
        read_icon(icon_buf);
        kmfree(icon_buf);
    } else {
    }

    printf("\nSystem initialization complete. CPU Hart: %d\n", (int)r_tp());

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
