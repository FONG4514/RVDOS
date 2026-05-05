// include/abi/abi.h
#ifndef RVDOS_ABI_ABI_H   // 注意这里的宏名要唯一
#define RVDOS_ABI_ABI_H

#include <abi/types.h>    // 必须包含类型定义，因为结构体里用到了 uint64 等
#include <abi/capability.h>

typedef struct proc_info {
    int pid;
    char name[16];
    int priority;
    int effective_priority;
    int state;
} proc_info_t;

#define MAX_HANDLES 64
#define STDIN  0
#define STDOUT 1
#define STDERR 2

#define PROC_STATE_UNUSED   0
#define PROC_STATE_SLEEPING 1
#define PROC_STATE_RUNNABLE 2
#define PROC_STATE_RUNNING  3
#define PROC_STATE_ZOMBIE   4

#define O_RDONLY           0
#define O_WRONLY           1
#define O_RDWR             2
#define O_CREATE           0x100
#define O_TRUNC            0x200

#define WAIT_NONBLOCK_KEY  -0x1145
#define WITHOUT_CAP        -0x114514

#endif