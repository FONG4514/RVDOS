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

#include <string.h>
#include <abi/types.h>

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

uint32 strlen(const char *s) {
    uint32 n = 0;
    while (s[n]) n++;
    return n;
}

void* memset(void *s, int c, uint32 n) {
  uint8 *p = (uint8*)s;
  while(n > 0) {
    *p = (uint8)c;
    p++;
    n--;
  }
  return s;
}

char* strcpy(char *s, const char *t) {
  char *os = s;
  while((*s++ = *t++) != 0);
  return os;
}

char* strcat(char *s, const char *t) {
  char *os = s;
  while(*s) s++;
  while((*s++ = *t++) != 0);
  return os;
}