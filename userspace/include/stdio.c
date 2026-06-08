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

#include <rvdos.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <abi/abi.h>

void print_str(const char *s) {
    file_write(STDOUT, s, strlen(s));
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
