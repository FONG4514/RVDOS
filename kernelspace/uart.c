#include <stdarg.h>
#include "riscv.h"
#include "defs.h"
#define UART0 0x10000000L
#define REG(reg) ((volatile unsigned char *)(UART0 + reg))

#define RHR 0 // receive holding register (for input bytes)
#define THR 0 // transmit holding register (for output bytes)
#define IER 1 // interrupt enable register
#define FCR 2 // FIFO control register
#define ISR 2 // interrupt status register
#define LCR 3 // line control register
#define MCR 4 // modem control register
#define LSR 5 // line status register
#define MSR 6 // modem status register
#define SPR 7 // scratchpad register

#define LSR_RX_READY (1 << 0)
#define LSR_TX_IDLE (1 << 5)

spinlock_t uart_lock;
int uart_inited = 0;

#define INPUT_BUF_SIZE 128
struct {
  spinlock_t lock;
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;

void uart_init() {
  init_lock(&uart_lock, "uart_lock");
  init_lock(&cons.lock, "cons_lock");

  // disable interrupts.
  *REG(IER) = 0x00;

  // special mode to set baud rate.
  *REG(LCR) = 0x80;

  // LSB for baud rate of 38.4K.
  *REG(0) = 0x03;

  // MSB for baud rate of 38.4K.
  *REG(1) = 0x00;

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  *REG(LCR) = 0x03;

  // reset and enable FIFOs.
  *REG(FCR) = 0x07;

  // enable receive interrupts.
  *REG(IER) = 0x01;

  uart_inited = 1;
}

void uart_putc_no_lock(char c) {
  while((*REG(LSR) & LSR_TX_IDLE) == 0)
    ;
  *REG(THR) = c;
}

void uart_putc(char c) {
  if (uart_inited) accquire_lock(&uart_lock);
  uart_putc_no_lock(c);
  if (uart_inited) release_lock(&uart_lock);
}

void uart_puts(char *s) {
  while(*s){
    uart_putc(*s++);
  }
}

// Read one character from the UART.
// Returns -1 if no character is available.
int uart_getc() {
  if((*REG(LSR) & LSR_RX_READY) != 0){
    return *REG(RHR);
  } else {
    return -1;
  }
}

extern void wakeup(void*);
extern void sleep(void*, spinlock_t*);

// The console read function.
// This is what SYS_READ_FILE with handle 0 should call.
int console_read(uint8 *buf, int n) {
  int i;
  accquire_lock(&cons.lock);
  for(i = 0; i < n; ){
    while(cons.r == cons.w){
      // Wait for input.
      sleep(&cons.r, &cons.lock);
    }
    char c = cons.buf[cons.r++ % INPUT_BUF_SIZE];
    if(c == 4) { // Ctrl-D
      if(i > 0) cons.r--; // Save Ctrl-D for next read
      break;
    }
    
    // buf is a user-space address. Ensure SUM is set.
    // Although the syscall wrapper sets it, sleep() might have cleared it.
    uint64 old_sstatus = r_sstatus();
    w_sstatus(old_sstatus | SSTATUS_SUM);
    buf[i] = c;
    w_sstatus(old_sstatus);

    i++;
    if(c == '\n') {
      break;
    }
  }
  release_lock(&cons.lock);
  return i;
}

// Handle UART interrupts.
void uart_intr() {
  while(1){
    int c = uart_getc();
    if(c == -1) break;

    accquire_lock(&cons.lock);
    switch(c){
    case 13: // Enter -> LF
      c = '\n';
      break;
    case 127: // Backspace
    case 8:
      if(cons.e != cons.w){
        cons.e--;
        uart_putc('\b');
        uart_putc(' ');
        uart_putc('\b');
      }
      release_lock(&cons.lock);
      continue;
    }

    if(c != 0 && cons.e - cons.r < INPUT_BUF_SIZE){
      uart_putc(c); // Echo
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
      if(c == '\n' || c == 4 || cons.e == cons.r + INPUT_BUF_SIZE){
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    release_lock(&cons.lock);
  }
}

static char digits[] = "0123456789abcdef";

// 辅助函数：打印整数
void printint(int xx, int base, int sign) {
  char buf[16];
  int i;
  unsigned int x;

  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    uart_putc_no_lock(buf[i]);
}

// 辅助函数：打印十六进制指针/长整型
void printptr(unsigned long x) {
  uart_putc_no_lock('0');
  uart_putc_no_lock('x');
  for (int i = 0; i < (sizeof(unsigned long) * 2); i++) {
    uart_putc_no_lock(digits[(x >> (sizeof(unsigned long) * 8 - 4)) & 0xf]);
    x <<= 4;
  }
}

// 封装 printf
void printf(char *fmt, ...) {
  va_list ap;
  int i, c;
  char *s;

  if (fmt == 0) return;

  if (uart_inited) accquire_lock(&uart_lock);

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      uart_putc_no_lock(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0) break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 0);
      break;
    case 'p':
      printptr(va_arg(ap, unsigned long));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        uart_putc_no_lock(*s);
      break;
    case '%':
      uart_putc_no_lock('%');
      break;
    default:
      // 未知格式，打印原文
      uart_putc_no_lock('%');
      uart_putc_no_lock(c);
      break;
    }
  }
  va_end(ap);

  if (uart_inited) release_lock(&uart_lock);
}