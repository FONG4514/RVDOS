#ifndef RVDOS_H
#define RVDOS_H

/**
 * rvdos.h - RVDOS standred lib
 */

typedef unsigned char      uint8;
typedef unsigned char      uchar;
typedef unsigned short     uint16;
typedef unsigned int       uint32;
typedef unsigned long      uint64;

typedef int                int32;
typedef long               int64;
typedef unsigned long      size_t;

typedef int32              handle_t;
typedef int32              pid_t;

#define NULL               ((void*)0)
#define INVALID_HANDLE     ((handle_t)-1)

#define STDIN              0
#define STDOUT             1
#define STDERR             2

#define SYS_GET_TICKS     11
#define SYS_SPAWN         12
#define SYS_EXIT          13
#define SYS_GETPID        14
#define SYS_CREATE_FILE   15
#define SYS_READ_FILE     16
#define SYS_WRITE_FILE    17
#define SYS_CLOSE_HANDLE  18

// function
handle_t file_open(const char *path);
int32    file_read(handle_t h, void *buf, uint32 len);
int32    file_write(handle_t h, const void *buf, uint32 len);
void     close_handle(handle_t h);
void     exit_process(int32 status);
uint32   get_ticks(void);
pid_t    get_pid(void);
pid_t    spawn_process(const char *path);

uint32   strlen(const char *s);
void     print_str(const char *s);
void     print_int(int32 n);
int      strcmp(const char *p, const char *q);
int      strncmp(const char *p, const char *q, uint32 n);
char*    gets(char *buf, int max);

#endif // RVDOS_H
