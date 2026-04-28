#ifndef RVDOS_CAPABILITY_H
#define RVDOS_CAPABILITY_H

#include "types.h"

// --- ABI version ---
#define RVDOS_ABI_V1   1

// --- Capability bitmask (32-bit for now) ---

// FS / Path
#define CAP_FS_BASIC        (1 << 0)  // open/read/write/close
#define CAP_FS_DIR          (1 << 1)  // directory support (mkdir, ls)
#define CAP_FS_CWD          (1 << 2)  // current working directory
#define CAP_FS_RENAME       (1 << 3)

// Process
#define CAP_PROC_BASIC      (1 << 8)  // spawn/exit/wait
#define CAP_PROC_PS         (1 << 9)
#define CAP_PROC_KILL       (1 << 10)
#define CAP_PROC_SLEEP      (1 << 11)

// Memory
#define CAP_MEM_SBRK        (1 << 16) // sbrk()

// System
#define CAP_SYS_TIME        (1 << 24) // get_ticks
#define CAP_SYS_POWER       (1 << 25) // reboot / poweroff

// --- Capability struct ---

typedef struct {
    uint32 abi_version;
    uint32 caps;
} rvdos_abi_info_t;

#endif