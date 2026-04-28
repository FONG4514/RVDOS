#ifndef RVDOS_ABI_SYSCALL_H
#define RVDOS_ABI_SYSCALL_H

// System call numbers
#define SYS_GET_ABI_INFO  1
#define SYS_GETCAPS       2
#define SYS_GET_VERSION   3

#define SYS_TRAP          10
#define SYS_GET_TICKS     11
#define SYS_SPAWN         12
#define SYS_EXIT          13
#define SYS_GETPID        14
#define SYS_CREATE_FILE   15
#define SYS_READ_FILE     16
#define SYS_WRITE_FILE    17
#define SYS_CLOSE_HANDLE  18
#define SYS_WAIT          19
#define SYS_LS            20
#define SYS_PANIC         21
#define SYS_POWEROFF      22
#define SYS_REBOOT        23
#define SYS_MKDIR         24
#define SYS_CHDIR         25
#define SYS_UNLINK        26
#define SYS_GETCWD        27
#define SYS_RENAME        28
#define SYS_PS            29
#define SYS_SBRK          30
#define SYS_KILL          31
#define SYS_SLEEP         32

#endif