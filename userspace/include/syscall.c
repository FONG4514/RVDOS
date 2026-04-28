#include <rvdos.h>

static inline uint64 syscall(uint64 num, uint64 a0, uint64 a1, uint64 a2) {
    register uint64 a7_reg asm("a7") = num;
    register uint64 a0_reg asm("a0") = a0;
    register uint64 a1_reg asm("a1") = a1;
    register uint64 a2_reg asm("a2") = a2;

    asm volatile("ecall"
                 : "+r"(a0_reg)
                 : "r"(a7_reg), "r"(a0_reg), "r"(a1_reg), "r"(a2_reg)
                 : "memory");
    return a0_reg;
}

// --- 系统调用封装实现 ---

void sys_trap(void) {
    syscall(SYS_TRAP, 0, 0, 0);
}

handle_t file_open(const char *path, int mode) {
    return (handle_t)syscall(SYS_CREATE_FILE, (uint64)path, (uint64)mode, 0);
}

int32 file_read(handle_t h, void *buf, uint32 len) {
    return (int32)syscall(SYS_READ_FILE, (uint64)h, (uint64)buf, (uint64)len);
}

int32 get_cwd(void *buf,uint32 len) {
    int32 ret = (int32)syscall(SYS_GETCWD, (uint64)buf, (uint64)len,0);
    return ret;
}

int32 file_write(handle_t h, const void *buf, uint32 len) {
    return (int32)syscall(SYS_WRITE_FILE, (uint64)h, (uint64)buf, (uint64)len);
}

void close_handle(handle_t h) {
    syscall(SYS_CLOSE_HANDLE, (uint64)h, 0, 0);
}

void exit_process(int32 status) {
    syscall(SYS_EXIT, (uint64)status, 0, 0);
}

uint32 get_ticks(void) {
    return (uint32)syscall(SYS_GET_TICKS, 0, 0, 0);
}

pid_t get_pid(void) {
    return (pid_t)syscall(SYS_GETPID, 0, 0, 0);
}

pid_t spawn_process(const char *path, const char *args) {
    return (pid_t)syscall(SYS_SPAWN, (uint64)path, (uint64)args, 0);
}

int32 wait_process(pid_t pid) {
    return (int32)syscall(SYS_WAIT, (uint64)pid, 0, 0);
}

int32 kill_process(pid_t pid) {
    return (int32)syscall(SYS_KILL, (uint64)pid, 0, 0);
}

void sys_panic() {
    syscall(SYS_PANIC,0,0,0);
}

void ls(void) {
    syscall(SYS_LS, 0, 0, 0);
}

void poweroff(void) {
    syscall(SYS_POWEROFF, 0, 0, 0);
}

void reboot(void) {
    syscall(SYS_REBOOT, 0, 0, 0);
}

void sleep(uint64 time) {
    syscall(SYS_SLEEP, time,0,0);
}

int32 mkdir(const char *path) {
    return (int32)syscall(SYS_MKDIR, (uint64)path, 0, 0);
}

int32 chdir(const char *path) {
    return (int32)syscall(SYS_CHDIR, (uint64)path, 0, 0);
}

int32 unlink(const char *path) {
    return (int32)syscall(SYS_UNLINK, (uint64)path, 0, 0);
}

int32 rename(const char *oldpath, const char *newpath) {
    return (int32)syscall(SYS_RENAME, (uint64)oldpath, (uint64)newpath, 0);
}

int32 get_version(char *buf, uint32 len) {
    return (int32)syscall(SYS_GET_VERSION, (uint64)buf, (uint64)len, 0);
}

int32 ps(proc_info_t *info, uint32 max) {
    return (int32)syscall(SYS_PS, (uint64)info, (uint64)max, 0);
}

int32 get_abi_info(rvdos_abi_info_t *info) {
    return (int32)syscall(SYS_GET_ABI_INFO, 0, (uint64)info, 0);
}

uint32 get_caps(void) {
    return (uint32)syscall(SYS_GETCAPS, 0, 0, 0);
}

void* sbrk(int n) {
    return (void*)syscall(SYS_SBRK, (uint64)n, 0, 0);
}

int32 trace(int enable) {
    return (int32)syscall(SYS_TRACE, (uint64)enable, 0, 0);
}