#include <kernel.h>

void* memcpy(void *dst, const void *src, uint32 n) {
  char *d = dst;
  const char *s = src;
  while(n-- > 0)
    *d++ = *s++;
  return dst;
}

void* memset(void *dst, int c, uint32 n) {
  char *d = dst;
  while(n-- > 0)
    *d++ = c;
  return dst;
}

int memcmp(const void *v1, const void *v2, uint32 n) {
  const uint8 *s1, *s2;

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
  return (uint8)*p - (uint8)*q;
}

uint32 strlen(const char *s) {
    uint32 n = 0;
    while (s[n]) n++;
    return n;
}
