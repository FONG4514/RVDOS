#include "rvdos.h"

/**
 * rvdos 系统库具体实现
 */

extern void main();

// 用户态程序的真正入口
void __attribute__((section(".text.entry"))) _start() {
    main();
    exit_process(0);
}

// 底层汇编封装，供库函数内部使用
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

pid_t spawn_process(const char *path, const char *redir_path) {
    return (pid_t)syscall(SYS_SPAWN, (uint64)path, (uint64)redir_path, 0);
}

int32 wait_process(pid_t pid) {
    return (int32)syscall(SYS_WAIT, (uint64)pid, 0, 0);
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

// --- 基础工具函数 ---

int strcmp(const char *p, const char *q) {
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

int strncmp(const char *p, const char *q, uint32 n) {
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uchar)*p - (uchar)*q;
}

char* gets(char *buf, int max) {
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = file_read(STDIN, &c, 1);
    if(cc < 1)
      break;
    
    if (c == '\b' || c == 127) {
      if (i > 0) {
        i--;
        // uart_intr handles echo of backspace, but we might need to send it if kernel didn't
      }
      continue;
    }

    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

uint32 strlen(const char *s) {
    uint32 n = 0;
    while (s[n]) n++;
    return n;
}

void print_str(const char *s) {
    file_write(STDOUT, s, strlen(s));
}

void print_int(int32 n) {
    char buf[16];
    char out[16];
    int32 i = 0;
    int32 j = 0;
    
    if (n == 0) {
        print_str("0");
        return;
    }
    
    if (n < 0) {
        out[j++] = '-';
        n = -n;
    }
    
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    
    while (--i >= 0) {
        out[j++] = buf[i];
    }
    
    file_write(STDOUT, out, j);
}
