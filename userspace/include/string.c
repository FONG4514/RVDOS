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