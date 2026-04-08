#include "defs.h"

void* memcpy(void *dst, const void *src, uint n) {
  char *d = dst;
  const char *s = src;
  while(n-- > 0)
    *d++ = *s++;
  return dst;
}

void* memset(void *dst, int c, uint n) {
  char *d = dst;
  while(n-- > 0)
    *d++ = c;
  return dst;
}

int memcmp(const void *v1, const void *v2, uint n) {
  const uchar *s1, *s2;

  s1 = v1;
  s2 = v2;
  while(n-- > 0){
    if(*s1 != *s2)
      return *s1 - *s2;
    s1++, s2++;
  }

  return 0;
}

int strcmp(const char *p, const char *q) {
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint strlen(const char *s) {
    uint n = 0;
    while (s[n]) n++;
    return n;
}
