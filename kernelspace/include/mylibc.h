#ifndef RVDOS_LIBC_H
#define RVDOS_LIBC_H

#include "abi/types.h"

// mylibc.c
void*           memcpy(void *dst, const void *src, uint32 n);
void*           memset(void *dst, int c, uint32 n);
int             memcmp(const void *v1, const void *v2, uint32 n);
int             strcmp(const char *p, const char *q);
uint32            strlen(const char *s);

#endif