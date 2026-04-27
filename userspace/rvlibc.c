#include "rvdos.h"
#include <stdarg.h>

/**
 * rvdos 系统库具体实现
 */

extern int main(int argc, char *argv[]);

// 用户态程序的真正入口
void __attribute__((section(".text.entry"))) _start(int argc, char *argv[]) {
    int ret = main(argc, argv);
    exit_process(ret);
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

int32 get_cwd(void *buf,uint32 len) {
    int32 ret = (int32)syscall(SYS_GETCWD, (uint64)buf, (uint64)len,0);
    if (ret != 0) {
        print_str("get_cwd syscall failed\n");
    }
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

int32 ps(proc_info_t *info, uint32 max) {
    return (int32)syscall(SYS_PS, (uint64)info, (uint64)max, 0);
}

void* sbrk(int n) {
    return (void*)syscall(SYS_SBRK, (uint64)n, 0, 0);
}

// Memory allocator (K&R style simple free-list)
typedef long Align;
union header {
  struct {
    union header *ptr;
    unsigned size;
  } s;
  Align x;
};
typedef union header Header;

static Header base;
static Header *freep;

void free(void *ap) {
  Header *bp, *p;
  bp = (Header *)ap - 1;
  for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if (bp + bp->s.size == p->s.ptr) {
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if (p + p->s.size == bp) {
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header* morecore(unsigned nu) {
  char *cp;
  Header *up;
  if (nu < 4096)
    nu = 4096;
  cp = sbrk(nu * sizeof(Header));
  if (cp == (char *)-1)
    return 0;
  up = (Header *)cp;
  up->s.size = nu;
  free((void *)(up + 1));
  return freep;
}

void* malloc(uint32 nbytes) {
  Header *p, *prevp;
  unsigned nunits;
  nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;
  if ((prevp = freep) == 0) {
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for (p = prevp->s.ptr; ; prevp = p, p = p->s.ptr) {
    if (p->s.size >= nunits) {
      if (p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p = p + p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void *)(p + 1);
    }
    if (p == freep)
      if ((p = morecore(nunits)) == 0)
        return 0;
  }
}

// --- 基础工具函数 ---

int strcmp(const char *p, const char *q) {
  while(*p && *p == *q)
    p++, q++;
  return (uint8)*p - (uint8)*q;
}

int strncmp(const char *p, const char *q, uint32 n) {
  while(n > 0 && *p && *p == *q)
    n--, p++, q++;
  if(n == 0)
    return 0;
  return (uint8)*p - (uint8)*q;
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

void print_hex(uint64 n) {
    char buf[1];
    char *hex = "0123456789abcdef";
    int i;
    
    print_str("0x");
    // Simple version: always print 16 chars for 64-bit
    for (i = 15; i >= 0; i--) {
        buf[0] = hex[(n >> (i * 4)) & 0xf];
        file_write(STDOUT, buf, 1);
    }
}

void printf(const char *fmt, ...) {
  va_list ap;
  int i, c;
  char *s;

  va_start(ap, fmt);
  for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
    if (c != '%') {
      char b[1];
      b[0] = c;
      file_write(STDOUT, b, 1);
      continue;
    }
    
    // Skip flags like '-' or numbers
    i++;
    while (fmt[i] == '-' || (fmt[i] >= '0' && fmt[i] <= '9')) {
        i++;
    }

    c = fmt[i] & 0xff;
    if (c == 0)
      break;
    switch (c) {
    case 'd':
      print_int(va_arg(ap, int));
      break;
    case 'x':
    case 'p': // Add %p support for pointers
      print_hex(va_arg(ap, uint64));
      break;
    case 's':
      if ((s = va_arg(ap, char *)) == 0)
        s = "(null)";
      print_str(s);
      break;
    case '%':
      print_str("%");
      break;
    }
  }
  va_end(ap);
}
