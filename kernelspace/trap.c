#include "riscv.h"
#include "defs.h"
#include "proc.h"

struct trap_info {
    spinlock_t trap_lock;
} info;

extern void kernel_vector();
// These are in the trampoline section
extern char trampoline_start[], user_vector[], user_ret[];

extern void fast_user_vector();
extern int console_read(uint8 *buf, int n);
extern void uart_putc_no_lock(char c);

uint64 ticks = 0;
spinlock_t tick_lock;

// --- PLIC implementation ---
void plic_init() {
  // set UART's priority to 1.
  *(uint32*)(PLIC_PRIORITY + UART0_IRQ*4) = 1;
  // set VIRTIO's priority to 1.
  *(uint32*)(PLIC_PRIORITY + VIRTIO0_IRQ*4) = 1;
}

void plic_inithart() {
  int hart = r_tp();
  // set uart's enable bit for this hart's S-mode. 
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);
  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int plic_claim() {
  int hart = r_tp();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void plic_complete(int irq) {
  int hart = r_tp();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}

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
    uint64 user_path = p->context->a0;
    int mode = (int)p->context->a1;
    if (user_path == 0) return -1;

    char kpath[64];
    // Copy string from user space
    for(int i = 0; i < 63; i++) {
        uint64 pa = walkaddr(p->pagetable, user_path + i);
        if(pa == 0) return -1;
        kpath[i] = *(char*)pa;
        if(kpath[i] == '\0') break;
        if(i == 62) kpath[63] = '\0';
    }
    
    return CreateHandler(kpath, mode);
}

uint64 sys_read_file(void) {
    PCB *p = myproc();
    int handle = (int)p->context->a0;
    uint64 user_buf = p->context->a1;
    uint32 len = (uint32)p->context->a2;
    
    if (handle < 0 || handle >= MAX_HANDLES || p->handles[handle] == 0) return -1;

    uint64 ret;
    if (p->handles[handle] == (file_t *)-1) { // Standard Console
        // For console read, we'll read into a kernel buffer first
        uint8 *kbuf = kalloc();
        if(!kbuf) return -1;
        int n = (len > PGSIZE) ? PGSIZE : len;
        ret = console_read(kbuf, n);
        
        // Copy back to user
        for(int i = 0; i < ret; i++) {
            uint64 pa = walkaddr(p->pagetable, user_buf + i);
            if(pa == 0) { kfree(kbuf); return -1; }
            *(uint8*)pa = kbuf[i];
        }
        kfree(kbuf);
    } else {
        // For file read, ReadFile takes a kernel buffer.
        // Similar to console read, we read into kernel memory first.
        uint8 *kbuf = kalloc();
        if(!kbuf) return -1;
        int n = (len > PGSIZE) ? PGSIZE : len;
        ret = ReadFile(handle, kbuf, n);
        
        if(ret > 0) {
            for(int i = 0; i < ret; i++) {
                uint64 pa = walkaddr(p->pagetable, user_buf + i);
                if(pa == 0) { kfree(kbuf); return -1; }
                *(uint8*)pa = kbuf[i];
            }
        }
        kfree(kbuf);
    }
    
    return ret;
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
    uint64 user_path = p->context->a0;
    uint64 user_redir = p->context->a1;
    if (user_path == 0) return -1;

    char kpath[64];
    char kredir[64];
    
    // Copy path from user space
    for(int i = 0; i < 63; i++) {
        uint64 pa = walkaddr(p->pagetable, user_path + i);
        if(pa == 0) return -1;
        kpath[i] = *(char*)pa;
        if(kpath[i] == '\0') break;
        if(i == 62) kpath[63] = '\0';
    }

    if (user_redir) {
        // Copy redir path from user space
        for(int i = 0; i < 63; i++) {
            uint64 pa = walkaddr(p->pagetable, user_redir + i);
            if(pa == 0) return -1;
            kredir[i] = *(char*)pa;
            if(kredir[i] == '\0') break;
            if(i == 62) kredir[63] = '\0';
        }
    }
    
    return spawn(kpath, user_redir ? kredir : 0);
}

uint64 sys_wait(void) {
    PCB *p = myproc();
    int pid = (int)p->context->a0;
    return wait(pid);
}

uint64 sys_ls(void) {
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
    uint64 user_buf = p->context->a1;
    uint32 len = (uint32)p->context->a2;
    
    if (handle < 0 || handle >= MAX_HANDLES || p->handles[handle] == 0) return -1;

    // Standard Output/Error
    if (p->handles[handle] == (file_t *)-1) {
        // Copy data from user to kernel first
        uint8 *kbuf = kalloc();
        if(!kbuf) return -1;
        uint32 total = 0;
        while(total < len) {
            uint32 chunk = (len - total > PGSIZE) ? PGSIZE : (len - total);
            for(uint32 i = 0; i < chunk; i++) {
                uint64 pa = walkaddr(p->pagetable, user_buf + total + i);
                if(pa == 0) { kfree(kbuf); return -1; }
                kbuf[i] = *(uint8*)pa;
            }
            extern int WriteFile(int, uint8*, uint32);
            WriteFile(handle, kbuf, chunk);
            total += chunk;
        }
        kfree(kbuf);
        return total;
    }

    // For disk files
    if (len > PGSIZE) len = PGSIZE; 
    
    uint8 *kbuf = kalloc();
    if (!kbuf) return -1;

    for(uint32 i = 0; i < len; i++) {
        uint64 pa = walkaddr(p->pagetable, user_buf + i);
        if(pa == 0) { kfree(kbuf); return -1; }
        kbuf[i] = *(uint8*)pa;
    }

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

uint64 sys_trap(void) {
    return 0;
}

// System call table
// You can expand this by adding entries like [SYS_READ] = sys_read,
syscall_t syscall_table[64] = {
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
    [SYS_REBOOT]       = sys_reboot
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
        exit(-1);
    }

    user_trap_return();
}

// Return from kernel to user mode
void user_trap_return() {
    PCB *p = myproc();

    intr_off();

    // Set stvec to user_vector (trampoline page)
    // We must use the user-space virtual address of user_vector.
    extern char trampoline_start[], user_vector[];
    uint64 user_vector_va = TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline_start);
    w_stvec(user_vector_va);

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
    // Now we MUST switch satp back to user's page table.
    uint64 satp = (8L << 60) | ((uint64)p->pagetable >> 12);
    uint64 fn = TRAMPOLINE + ((uint64)user_ret - (uint64)trampoline_start);
    
    // printf("Returning to user mode: epc=%p, satp=%p\n", p->context->epc, satp);
    
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

int intr_get() {
    return (r_sstatus() & SSTATUS_SIE) != 0;
}
