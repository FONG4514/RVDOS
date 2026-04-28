#ifndef USER_STDIO_H
#define USER_STDIO_H

#include <abi/types.h>
#include <abi/abi.h>


void     printf(const char *fmt, ...);
char*    gets(char *buf, int max);
void     print_str(const char *s);
void     print_int(int32 n);
void     print_hex(uint64 n);

#endif