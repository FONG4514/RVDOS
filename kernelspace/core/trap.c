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

struct trap_info {
    spinlock_t trap_lock;
} info;

extern void kernel_vector();
extern void user_vector();
extern void user_ret(uint64);
extern void fast_user_vector();
extern int console_read(uint8 *buf, int n);
extern void uart_putc_no_lock(char c);

extern const rvdos_abi_info_t KERNEL_ABI_INFO;

extern struct kmem_cache *pcb_cache;
extern struct kmem_cache *file_cache;

uint64 ticks = 0;
spinlock_t tick_lock;
extern int primary_hart;

// --- Interrupt Handling Table ---

void handle_timer();
void handle_external();

typedef void (*interrupt_handler_t)(void);

interrupt_handler_t interrupt_table[16] = {
    [1] = handle_timer,    // Supervisor Software Interrupt (delegated timer)
    [5] = handle_timer,    // Supervisor Timer Interrupt (SBI timer)
    [9] = handle_external, // Supervisor External Interrupt (PLIC)
};

void handle_timer() {
    accquire_lock(&tick_lock);
    ticks++;

    wakeup(&ticks);
    // if (ticks % 100 == 0) printf("Hart %d: tick %d\n", (int)r_tp(), (int)ticks);
    release_lock(&tick_lock);

    // SBI模式：设置下一个定时器中断
    uint64 interval = 1000000;
    sbi_set_timer(r_time() + interval);
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

// Forward decl (defined below with syscall table)
int has_capability(PCB *p, uint64 cap);

// Copy a NUL-terminated user string of at most max-1 bytes into kbuf.
// Returns 0 on success, -1 if path is NULL.
static int copy_user_str(char *kbuf, char *upath, int max) {
    int i;
    if (upath == 0) return -1;
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    for (i = 0; i < max - 1; i++) {
        kbuf[i] = upath[i];
        if (kbuf[i] == '\0') break;
    }
    kbuf[i] = '\0';
    w_sstatus(old_sstatus);
    return 0;
}

// Resolve child capabilities. Always enforces child ⊆ parent.
// custom=0 → USER_DEFAULT ∩ parent
// custom=1 → requires CAP_PROC_SANDBOX; mask ∩ parent (PROC_CAP_ENABLE cleared)
static int resolve_child_caps(PCB *p, uint64 mask, int custom, uint64 *out) {
    if (custom) {
        if (!has_capability(p, CAP_PROC_SANDBOX))
            return -1;
        *out = p->caps & (mask & ~PROC_CAP_ENABLE);
    } else {
        *out = p->caps & CAP_PROFILE_USER_DEFAULT;
    }
    return 0;
}

uint64 sys_spawn(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    char *redir = (char*)p->context->a1;
    uint64 mask = p->context->a2;
    uint64 child_cap = 0;
    int custom = (mask & PROC_CAP_ENABLE) != 0;

    if (resolve_child_caps(p, mask, custom, &child_cap) < 0)
        return (uint64)WITHOUT_CAP;

    if (path == 0) return -1;

    char kpath[64];
    char kredir[64];
    if (copy_user_str(kpath, path, 64) < 0) return -1;

    if (redir) {
        if (copy_user_str(kredir, redir, 64) < 0) return -1;
        return spawn(kpath, kredir, child_cap);
    }
    return spawn(kpath, 0, child_cap);
}

// SYS_SANDBOX: spawn with an explicit capability mask (always custom).
// a0=path, a1=args, a2=requested_caps (PROC_CAP_ENABLE optional/ignored)
uint64 sys_sandbox(void) {
    PCB *p = myproc();
    char *path = (char*)p->context->a0;
    char *redir = (char*)p->context->a1;
    uint64 mask = p->context->a2;
    uint64 child_cap = 0;

    if (resolve_child_caps(p, mask, 1, &child_cap) < 0)
        return (uint64)WITHOUT_CAP;

    if (path == 0) return -1;

    char kpath[64];
    char kredir[64];
    if (copy_user_str(kpath, path, 64) < 0) return -1;

    if (redir) {
        if (copy_user_str(kredir, redir, 64) < 0) return -1;
        return spawn(kpath, kredir, child_cap);
    }
    return spawn(kpath, 0, child_cap);
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
    
    uint8 *kbuf = kmalloc(len);
    if (!kbuf) return -1;

    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    memcpy(kbuf, buf, len);
    w_sstatus(old_sstatus);

    extern int WriteFile(int, uint8*, uint32);
    uint64 ret = WriteFile(handle, kbuf, len);
    
    kmfree(kbuf);
    return ret;
}

uint64 sys_panic (void) {
    panic("sys_panic");
    return -1;
}

uint64 sys_poweroff(void) {
    printf("Powering off...\n");
    if (pcb_cache) kmem_cache_destroy(pcb_cache);
    if (file_cache) kmem_cache_destroy(file_cache);
    sbi_system_reset(SBI_SRST_RESET_TYPE_SHUTDOWN, SBI_SRST_RESET_REASON_NONE);
    // Fallback for QEMU virt test device
    *(uint32*)SYSCON = 0x5555;
    while(1);
    return 0;
}

uint64 sys_reboot(void) {
    printf("Rebooting...\n");
    if (pcb_cache) kmem_cache_destroy(pcb_cache);
    if (file_cache) kmem_cache_destroy(file_cache);
    sbi_system_reset(SBI_SRST_RESET_TYPE_COLD_REBOOT, SBI_SRST_RESET_REASON_NONE);
    // Fallback for QEMU virt test device
    *(uint32*)SYSCON = 0x7777;
    while(1);
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

    extern PCB* procs[MAXPROCESSES];
    for(int i = 0; i < MAXPROCESSES && count < max; i++) {
        accquire_lock(&procs[i]->lock);
        if (procs[i]->state != UNUSED) {
            kinfo.pid = procs[i]->pid;
            kinfo.owner_pid = procs[i]->owner_pid;
            memcpy(kinfo.name, procs[i]->name, 16);
            kinfo.priority = procs[i]->priority;
            kinfo.effective_priority = procs[i]->effective_priority;
            kinfo.state = procs[i]->state;
            
            int handle_count = 0;
            for(int j = 0; j < MAX_HANDLES; j++) {
                if (procs[i]->handles[j] != 0) {
                    handle_count++;
                }
            }
            kinfo.handle_count = handle_count;

            // Access user memory safely while keeping interrupt state intact
            uint64 s = r_sstatus();
            w_sstatus(s | SSTATUS_SUM);
            memcpy(&user_info[count], &kinfo, sizeof(proc_info_t));
            w_sstatus(s);
            
            count++;
        }
        release_lock(&procs[i]->lock);
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

uint64 sys_getcaps(void) {
    PCB *p = myproc();
    return p->caps;
}

uint64 sys_get_abi_info(void) {
    PCB *proc = myproc();
    rvdos_abi_info_t *info = (rvdos_abi_info_t *)proc->context->a1;

    if (info == 0) return -1;

    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);

    memcpy(info, &KERNEL_ABI_INFO, sizeof(rvdos_abi_info_t));

    w_sstatus(old_sstatus);
    return 0;
}

uint64 sys_get_version(void) {
    PCB *proc = myproc();
    char *buf = (char *)proc->context->a0;
    uint32 len = (uint32)proc->context->a1;

    if (buf == 0) return -1;

    uint32 ver_len = strlen(KERNEL_VERSION);
    if (len < ver_len + 1) return -1;

    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);

    memcpy(buf, KERNEL_VERSION, ver_len + 1);

    w_sstatus(old_sstatus);
    return 0;
}

uint64 sys_kill(void) {
    PCB *p = myproc();
    int pid = (int)p->context->a0;
    return kill(pid);
}

uint64 sys_sleep(void) {
    PCB *p = myproc();
    uint64 sleep_time = p->context->a0;

    uint64 ticks0;
    
    accquire_lock(&tick_lock);
    ticks0 = ticks; 

    while(ticks - ticks0 < sleep_time) {
        if(myproc()->killed) {
            release_lock(&tick_lock);
            return -1;
        }
        
        sleep(&ticks, &tick_lock);
    }

    release_lock(&tick_lock);
    return 0;
}

uint64 sys_trace(void) {
    PCB *p = myproc();
    p->tracing = (int)p->context->a0;
    return 0;
}

// Internal function to check if a process has a certain capability
int has_capability(PCB *p, uint64 cap) {
    return (p->caps & cap) == cap;
}

uint64 syscall_caps[64] = {
    [SYS_GET_TICKS]    = CAP_SYS_TIME,
    [SYS_SPAWN]        = CAP_PROC_BASIC,
    [SYS_CREATE_FILE]  = CAP_FS_WRITE,
    [SYS_READ_FILE]    = CAP_FS_READ,
    [SYS_WRITE_FILE]   = CAP_FS_WRITE,
    [SYS_WAIT]         = CAP_PROC_BASIC,
    [SYS_LS]           = CAP_FS_DIR,
    [SYS_PANIC]        = CAP_SYS_POWER,
    [SYS_POWEROFF]     = CAP_SYS_POWER,
    [SYS_REBOOT]       = CAP_SYS_POWER,
    [SYS_MKDIR]        = CAP_FS_DIR,
    [SYS_CHDIR]        = CAP_FS_CWD,
    [SYS_UNLINK]       = CAP_FS_WRITE,
    [SYS_GETCWD]       = CAP_FS_CWD,
    [SYS_RENAME]       = CAP_FS_RENAME,
    [SYS_PS]           = CAP_PROC_PS,
    [SYS_SBRK]         = CAP_MEM_SBRK,
    [SYS_KILL]         = CAP_PROC_KILL,
    [SYS_SLEEP]        = CAP_PROC_SLEEP,
    [SYS_TRACE]        = CAP_PROC_TRACE,
    [SYS_SANDBOX]      = CAP_PROC_BASIC | CAP_PROC_SANDBOX
};

const char *syscall_names[64] = {
    [SYS_GET_ABI_INFO] = "get_abi_info",
    [SYS_GETCAPS]      = "getcaps",
    [SYS_GET_VERSION]  = "get_version",
    [SYS_TRAP]         = "trap",
    [SYS_GET_TICKS]    = "get_ticks",
    [SYS_SPAWN]        = "spawn",
    [SYS_EXIT]         = "exit",
    [SYS_GETPID]       = "getpid",
    [SYS_CREATE_FILE]  = "create_file",
    [SYS_READ_FILE]    = "read_file",
    [SYS_WRITE_FILE]   = "write_file",
    [SYS_CLOSE_HANDLE] = "close_handle",
    [SYS_WAIT]         = "wait",
    [SYS_LS]           = "ls",
    [SYS_PANIC]        = "panic",
    [SYS_POWEROFF]     = "poweroff",
    [SYS_REBOOT]       = "reboot",
    [SYS_MKDIR]        = "mkdir",
    [SYS_CHDIR]        = "chdir",
    [SYS_UNLINK]       = "unlink",
    [SYS_GETCWD]       = "getcwd",
    [SYS_RENAME]       = "rename",
    [SYS_PS]           = "ps",
    [SYS_SBRK]         = "sbrk",
    [SYS_KILL]         = "kill",
    [SYS_SLEEP]        = "sleep",
    [SYS_TRACE]        = "trace",
    [SYS_SANDBOX]      = "sandbox",
};

// System call table
syscall_t syscall_table[64] = {
    [SYS_GET_ABI_INFO] = sys_get_abi_info,
    [SYS_GETCAPS]      = sys_getcaps,
    [SYS_GET_VERSION]  = sys_get_version,
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
    [SYS_SBRK]         = sys_sbrk,
    [SYS_KILL]         = sys_kill,
    [SYS_SLEEP]        = sys_sleep,
    [SYS_TRACE]        = sys_trace,
    [SYS_SANDBOX]      = sys_sandbox
};

void syscall_dispatcher(void) {
    PCB *p = myproc();
    uint64 num = p->context->a7; // Use a7 as syscall number
    if (num > 0 && num < 64 && syscall_table[num]) {
        uint64 cap_needed = syscall_caps[num];
        
        // Special case: writing to STDOUT/STDERR does not require CAP_FS_WRITE
        if (num == SYS_WRITE_FILE) {
            int handle = (int)p->context->a0;
            if (handle == STDOUT || handle == STDERR) {
                cap_needed = 0;
            }
        }

        if (cap_needed != 0 && !has_capability(p, cap_needed)) {
            printf("PID %d: without capability %s (has %d, needs %d)\n", p->pid, syscall_names[num], p->caps, cap_needed);
            p->context->a0 = WITHOUT_CAP;
            return;
        }

        uint64 arg0 = p->context->a0; // Save first argument for tracing

        p->context->a0 = syscall_table[num]();

        if (p->tracing) {
            printf("PID %d: syscall %s(0x%x) -> %d\n", p->pid, syscall_names[num], arg0, (int)p->context->a0);
        }
    } else {
        printf("Unknown syscall %d on Hart %d at EPC %p\n", (int)num, (int)r_tp(), p->context->epc);
        p->context->a0 = -1;
    }
}

// --- Trap Initialization ---

void xsmode_trap_init() {
    if (r_tp() == primary_hart) {
        init_lock(&info.trap_lock, "trap_lock");
        init_lock(&tick_lock, "tick_lock");
    }
    // PLIC 已在 main.c 中初始化
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
            // SBI模式下定时器中断号为5，触发时间片抢占
            if (which_int == 5 && p->state == RUNNING) {
                yield();
            }
        }
    } else {
        // Exception
        printf("PID %d (%s): User exception %p, epc %p, tval %p\n", 
            p->pid, p->name, scause, sepc,r_stval());
        exit(-1);
    }

    user_trap_return();
}

// Return from kernel to user mode
void user_trap_return() {
    PCB *p = myproc();

    if(p->killed) {
        exit(-1);
    }

    intr_off();

    // Set stvec to our user trap vector
    w_stvec((uint64)user_vector);

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

    // Directly call user_ret. Since kernel is shadow mapped, we can jump to it from user page table.
    user_ret(TRAPFRAME);
}
