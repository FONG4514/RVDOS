#ifndef RVDOS_CAPABILITY_H
#define RVDOS_CAPABILITY_H

#include "types.h"

// --- ABI version ---
#define RVDOS_ABI_VER   2

// --- Capability bitmask (32-bit for now) ---

// FS / Path
#define CAP_FS_READ         (1ULL << 0)  // open/read/close
#define CAP_FS_WRITE        (1ULL << 1)  // write
#define CAP_FS_DIR          (1ULL << 2)  // directory support (mkdir, ls)
#define CAP_FS_CWD          (1ULL << 3)  // current working directory
#define CAP_FS_RENAME       (1ULL << 4)

// Process
#define CAP_PROC_BASIC      (1ULL << 8)  // spawn/exit/wait
#define CAP_PROC_PS         (1ULL << 9)
#define CAP_PROC_KILL       (1ULL << 10)
#define CAP_PROC_SLEEP      (1ULL << 11)
#define CAP_PROC_TRACE      (1ULL << 12)
#define CAP_PROC_SANDBOX    (1ULL << 13)

// Memory
#define CAP_MEM_SBRK        (1ULL << 16) // sbrk()

// System
#define CAP_SYS_TIME        (1ULL << 24) // get_ticks
#define CAP_SYS_POWER       (1ULL << 25) // reboot / poweroff

// Capability
#define PROC_CAP_ENABLE          (1ULL << 63)

// --- Capability struct ---

typedef struct {
    uint32 abi_version;
    uint64 caps;
} rvdos_abi_info_t;

#endif