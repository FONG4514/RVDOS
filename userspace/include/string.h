#ifndef USER_STRING_H
#define USER_STRING_H

#include <abi/types.h>

uint32   strlen(const char *s);
int      strcmp(const char *p, const char *q);
int      strncmp(const char *p, const char *q, uint32 n);
void*    memset(void *s, int c, uint32 n);

#endif
