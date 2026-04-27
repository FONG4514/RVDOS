#include <kernel.h>

struct trap_info {
    spinlock_t trap_lock;
} info;

extern void kernel_vector();
extern void user_vector();
extern void user_ret(uint64, uint64);
extern void fast_user_vector();
extern int console_read(uint8 *buf, int n);
extern void uart_putc_no_lock(char c);

extern const rvdos_abi_info_t KERNEL_ABI_INFO;

uint64 ticks = 0;
spinlock_t tick_lock;

// --- Interrupt Handling Table ---

void handle_timer();
void handle_external();

typedef void (*interrupt_handler_t)(void);

interrupt_handler_t interrupt_table[16] = {
    [1] = handle_timer,    // Supervisor Software Interrupt (delegated timer)
    [9] = handle_external, // Supervisor External Interrupt (PLIC)
};

void handle_timer() {
    accquire_lock(&tick_lock);
    ticks++;
    // if (ticks % 100 == 0) printf("Hart %d: tick %d\n", (int)r_tp(), (int)ticks);
    release_lock(&tick_lock);

    // Clear Supervisor Software Interrupt Pending (SSIP)
    w_sip(r_sip() & ~2);
}

void handle_external() {
    int irq = plic_claim();
    if (irq == UART0_IRQ) {
        uart_intr();
    } else if (irq == VIRTIO0_IRQ) {
        // virtio_disk_intr(); // Not implemented yet
    } else if (irq) {
        printf("Unexpected interrupt irq=%d\n", irq);
    }
    if (irq) plic_complete(irq);
}

// --- System Call Table ---

typedef uint64 (*syscall_t)(void);

// Prototype syscalls
uint64 sys_create_file(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    int mode = (int)p->context->a1;
    if (path == 0) return -1;

    char kpath[64];
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    
    // Simple copy from user space
    int i;
    for(i = 0; i < 63; i++) {
        kpath[i] = path[i];
        if(kpath[i] == '\0') break;
    }
    kpath[i] = '\0';
    
    w_sstatus(old_sstatus);
    return CreateHandler(kpath, mode);
}

uint64 sys_read_file(void) {
    PCB *p = myproc();
    int handle = (int)p->context->a0;
    uint8 *buf = (uint8*)p->context->a1;
    uint32 len = (uint32)p->context->a2;
    
    if (handle < 0 || handle >= MAX_HANDLES || p->handles[handle] == 0) return -1;

    uint64 ret;
    if (p->handles[handle] == (file_t *)-1) { // Standard Console
        w_sstatus(r_sstatus() | SSTATUS_SUM);
        ret = console_read(buf, len);
        w_sstatus(r_sstatus() & ~SSTATUS_SUM);
    } else {
        w_sstatus(r_sstatus() | SSTATUS_SUM);
        ret = ReadFile(handle, buf, len);
        w_sstatus(r_sstatus() & ~SSTATUS_SUM);
    }
    
    return ret;
}

uint64 sys_getcwd(void) {
    PCB *p = myproc();
    char *buf = (char*)p->context->a0;
    uint32 len = (uint32)p->context->a1;
    if (buf == 0) {
        return -1;
    }

    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    
    int i;
    for(i = 0; i < len - 1; i++) {
        buf[i] = p->cwd_path[i];
        if(buf[i] == '\0') break;
    }
    buf[i] = '\0';
    
    w_sstatus(old_sstatus);
    // printf("sys_getcwd: returning %s\n", p->cwd_path);
    return 0;
}

uint64 sys_close_handle(void) {
    PCB *p = myproc();
    int handle = (int)p->context->a0;
    CloseHandle(handle);
    return 0;
}

uint64 sys_get_ticks(void) {
    return ticks;
}

uint64 sys_spawn(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    char *redir = (char*)p->context->a1;
    if (path == 0) return -1;

    char kpath[64];
    char kredir[64];
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    
    int i;
    for(i = 0; i < 63; i++) {
        kpath[i] = path[i];
        if(kpath[i] == '\0') break;
    }
    kpath[i] = '\0';

    if (redir) {
        for(i = 0; i < 63; i++) {
            kredir[i] = redir[i];
            if(kredir[i] == '\0') break;
        }
        kredir[i] = '\0';
    }
    
    w_sstatus(old_sstatus);
    return spawn(kpath, redir ? kredir : 0);
}

uint64 sys_wait(void) {
    PCB *p = myproc();
    int pid = (int)p->context->a0;
    return wait(pid);
}

uint64 sys_ls(void) {
    // printf("sys_ls: calling fs_ls()\n");
    fs_ls();
    return 0;
}

uint64 sys_exit(void) {
    PCB *p = myproc();
    int status = (int)p->context->a0;
    exit(status);
    return 0; // Does not reach here
}

uint64 sys_getpid(void) {
    return myproc()->pid;
}

uint64 sys_write_file(void) {
    PCB *p = myproc();
    int handle = (int)p->context->a0;
    uint8 *buf = (uint8*)p->context->a1;
    uint32 len = (uint32)p->context->a2;
    
    if (handle < 0 || handle >= MAX_HANDLES || p->handles[handle] == 0) return -1;

    // Standard Output/Error
    if (p->handles[handle] == (file_t *)-1) {
        uint64 old_sstatus = r_sstatus();
        w_sstatus(old_sstatus | SSTATUS_SUM);
        extern int WriteFile(int, uint8*, uint32);
        uint64 ret = WriteFile(handle, buf, len);
        w_sstatus(old_sstatus);
        return ret;
    }

    // For disk files, we MUST copy data to kernel space because 
    // VirtIO uses physical addresses (identity mapping) and cannot see user VA.
    if (len > PGSIZE) len = PGSIZE; // Limit single write to one page for simplicity
    
    uint8 *kbuf = kalloc();
    if (!kbuf) return -1;

    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    memcpy(kbuf, buf, len);
    w_sstatus(old_sstatus);

    extern int WriteFile(int, uint8*, uint32);
    uint64 ret = WriteFile(handle, kbuf, len);
    
    kfree(kbuf);
    return ret;
}

uint64 sys_panic (void) {
    panic("sys_panic");
    return -1;
}

uint64 sys_poweroff(void) {
    printf("Powering off...\n");
    // RISC-V Virt machine syscon poweroff
    *(uint32*)SYSCON = 0x5555;
    return 0;
}

uint64 sys_reboot(void) {
    printf("Rebooting...\n");
    // RISC-V Virt machine syscon reboot
    *(uint32*)SYSCON = 0x7777;
    return 0;
}

uint64 sys_mkdir(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    if (path == 0) return -1;

    char kpath[64];
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    int i;
    for(i = 0; i < 63; i++) {
        kpath[i] = path[i];
        if(kpath[i] == '\0') break;
    }
    kpath[i] = '\0';
    w_sstatus(old_sstatus);
    return MakeDir(kpath);
}

uint64 sys_chdir(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    if (path == 0) return -1;

    char kpath[64];
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    int i;
    for(i = 0; i < 63; i++) {
        kpath[i] = path[i];
        if(kpath[i] == '\0') break;
    }
    kpath[i] = '\0';
    w_sstatus(old_sstatus);
    return ChangeDir(kpath);
}

uint64 sys_unlink(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    if (path == 0) return -1;

    char kpath[64];
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    int i;
    for(i = 0; i < 63; i++) {
        kpath[i] = path[i];
        if(kpath[i] == '\0') break;
    }
    kpath[i] = '\0';
    w_sstatus(old_sstatus);
    return Unlink(kpath);
}

uint64 sys_ps(void) {
    PCB *my_p = myproc();
    proc_info_t *user_info = (proc_info_t*)my_p->context->a0;
    uint32 max = (uint32)my_p->context->a1;
    
    if (user_info == 0) return -1;

    proc_info_t kinfo;
    uint32 count = 0;

    extern PCB procs[64];
    for(int i = 0; i < 64 && count < max; i++) {
        accquire_lock(&procs[i].lock);
        if (procs[i].state != UNUSED) {
            kinfo.pid = procs[i].pid;
            memcpy(kinfo.name, procs[i].name, 16);
            kinfo.priority = procs[i].priority;
            kinfo.effective_priority = procs[i].effective_priority;
            kinfo.state = procs[i].state;
            
            // Access user memory safely while keeping interrupt state intact
            uint64 s = r_sstatus();
            w_sstatus(s | SSTATUS_SUM);
            memcpy(&user_info[count], &kinfo, sizeof(proc_info_t));
            w_sstatus(s);
            
            count++;
        }
        release_lock(&procs[i].lock);
    }
    
    return count;
}

uint64 sys_sbrk(void) {
  int n;
  uint64 addr;
  PCB *p = myproc();

  n = (int)p->context->a0;
  addr = p->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64 sys_rename(void) {
    PCB *p = myproc();
    char *oldpath = (char*)p->context->a0;
    char *newpath = (char*)p->context->a1;
    if (oldpath == 0 || newpath == 0) return -1;

    char kold[64], knew[64];
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    int i;
    for(i = 0; i < 63; i++) {
        kold[i] = oldpath[i];
        if(kold[i] == '\0') break;
    }
    kold[i] = '\0';
    for(i = 0; i < 63; i++) {
        knew[i] = newpath[i];
        if(knew[i] == '\0') break;
    }
    knew[i] = '\0';
    w_sstatus(old_sstatus);
    return mv(kold, knew);
}

uint64 sys_trap(void) {
    return 0;
}

uint64 sys_get_abi_info(void) {
    PCB *proc = myproc();
    rvdos_abi_info_t *info = (rvdos_abi_info_t *)proc->context->a1;

    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);

    memcpy(info,&KERNEL_ABI_INFO,sizeof(rvdos_abi_info_t));

    w_sstatus(old_sstatus);
    return 0;
}

// System call table
// You can expand this by adding entries like [SYS_READ] = sys_read,
syscall_t syscall_table[64] = {
    [SYS_GET_ABI_INFO] = sys_get_abi_info,
    [SYS_TRAP]         = sys_trap,
    [SYS_GET_TICKS]    = sys_get_ticks,
    [SYS_SPAWN]        = sys_spawn,
    [SYS_EXIT]         = sys_exit,
    [SYS_GETPID]       = sys_getpid,
    [SYS_CREATE_FILE]  = sys_create_file,
    [SYS_READ_FILE]    = sys_read_file,
    [SYS_WRITE_FILE]   = sys_write_file,
    [SYS_CLOSE_HANDLE] = sys_close_handle,
    [SYS_WAIT]         = sys_wait,
    [SYS_LS]           = sys_ls,
    [SYS_PANIC]        = sys_panic,
    [SYS_POWEROFF]     = sys_poweroff,
    [SYS_REBOOT]       = sys_reboot,
    [SYS_MKDIR]        = sys_mkdir,
    [SYS_CHDIR]        = sys_chdir,
    [SYS_UNLINK]       = sys_unlink,
    [SYS_GETCWD]       = sys_getcwd,
    [SYS_RENAME]       = sys_rename,
    [SYS_PS]           = sys_ps,
    [SYS_SBRK]         = sys_sbrk
};

void syscall_dispatcher(void) {
    PCB *p = myproc();
    uint64 num = p->context->a7; // Use a7 as syscall number
    if (num > 0 && num < 64 && syscall_table[num]) {
        p->context->a0 = syscall_table[num]();
    } else {
        printf("Unknown syscall %d on Hart %d at EPC %p\n", (int)num, (int)r_tp(), p->context->epc);
        p->context->a0 = -1;
    }
}

// --- Trap Initialization ---

void xsmode_trap_init() {
    if (r_tp() == 0) {
        init_lock(&info.trap_lock, "trap_lock");
        init_lock(&tick_lock, "tick_lock");
        plic_init();
    }
    plic_inithart();
    // Initially, we are in kernel, so set stvec to kernel_vector
    w_stvec((uint64)kernel_vector);
}

// --- Common Trap Handlers ---

void kernel_trap_handler() {
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kernel_trap_handler: not from supervisor mode");

    if (scause & (1L << 63)) {
        // Interrupt
        uint64 which_int = scause & 0xfff;
        if (which_int < 16 && interrupt_table[which_int]) {
            interrupt_table[which_int]();
        } else {
            printf("Unexpected kernel interrupt: %p\n", scause);
        }
    } else {
        // Exception
        printf("Kernel exception: scause %p, sepc %p, stval %p\n", scause, sepc, r_stval());
        panic("kerneltrap");
    }

    w_sepc(sepc);
    w_sstatus(sstatus);
}

void user_trap_handler() {
    uint64 scause = r_scause();
    uint64 sepc = r_sepc();
    PCB *p = myproc();

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("user_trap_handler: not from user mode");

    // Set stvec to kernel_vector while we are in kernel
    w_stvec((uint64)kernel_vector);

    p->context->epc = sepc;

    if (scause == 8) {
        // ecall (System Call)
        p->context->epc += 4;
        intr_on();
        syscall_dispatcher();
    } else if (scause & (1L << 63)) {
        // Interrupt
        uint64 which_int = scause & 0xfff;
        if (which_int < 16 && interrupt_table[which_int]) {
            interrupt_table[which_int]();
            if (which_int == 1 && p->state == RUNNING) {
                yield();
            }
        }
    } else {
        // Exception
        printf("User exception %p, Hart %d, epc %p, tval %p\n", 
               scause, (int)r_tp(), sepc, r_stval());
        while(1);
    }

    user_trap_return();
}

// Return from kernel to user mode
void user_trap_return() {
    PCB *p = myproc();

    intr_off();

    // Set stvec to our fast vector (shadow mapped)
    w_stvec((uint64)fast_user_vector);

    // Set up context for next trap
    p->context->kernel_satp = r_satp();
    p->context->kernel_sp = p->kstack + PGSIZE;
    p->context->kernel_trap = (uint64)user_trap_handler;
    p->context->kernel_hartid = r_tp();

    uint64 x = r_sstatus();
    x &= ~SSTATUS_SPP;   // Return to user mode
    x |= SSTATUS_SPIE;  // Enable interrupts after sret
    w_sstatus(x);

    w_sepc(p->context->epc);

    // Using trampoline user_ret to perform actual return
    // We don't need to switch satp because kernel is mapped in user's page table.
    // The scheduler switched to p->pagetable.
    uint64 fn = TRAMPOLINE + ((uint64)user_ret - (uint64)user_vector);
    ((void (*)(uint64))fn)(TRAPFRAME);
}