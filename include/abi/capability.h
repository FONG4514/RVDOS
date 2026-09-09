#ifndef RVDOS_CAPABILITY_H
#define RVDOS_CAPABILITY_H

#include "types.h"

// --- ABI version ---
#define RVDOS_ABI_VER   3

// --- Capability bitmask ---

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
#define CAP_PROC_SANDBOX    (1ULL << 13) // custom child capability mask

// Memory
#define CAP_MEM_SBRK        (1ULL << 16) // sbrk()

// System
#define CAP_SYS_TIME        (1ULL << 24) // get_ticks
#define CAP_SYS_POWER       (1ULL << 25) // reboot / poweroff / panic

// Control bit for SYS_SPAWN a2: request custom child mask (requires CAP_PROC_SANDBOX)
#define PROC_CAP_ENABLE     (1ULL << 63)

// --- Capability profiles (0.8.1 sandbox model) ---
//
// Inheritance rules (always: child ⊆ parent):
//   SYS_SPAWN  (a2 == 0):           child = parent ∩ CAP_PROFILE_USER_DEFAULT
//   SYS_SPAWN  (a2 & PROC_CAP_ENABLE): requires CAP_PROC_SANDBOX;
//                                      child = parent ∩ (a2 without bit63)
//   SYS_SANDBOX:                    requires CAP_PROC_BASIC|CAP_PROC_SANDBOX;
//                                      child = parent ∩ requested_mask
//
// Boot chain:
//   kernel → syscontroller (CAP_PROFILE_SYSCONTROLLER)
//         → shell          (CAP_PROFILE_SHELL_NORMAL | SHELL_MAINT)
//         → user programs  (default USER_DEFAULT, or elevated by shell)

#define CAP_ALL ( \
    CAP_FS_READ | CAP_FS_WRITE | CAP_FS_DIR | CAP_FS_CWD | CAP_FS_RENAME | \
    CAP_PROC_BASIC | CAP_PROC_PS | CAP_PROC_KILL | CAP_PROC_SLEEP | \
    CAP_PROC_TRACE | CAP_PROC_SANDBOX | CAP_MEM_SBRK | CAP_SYS_TIME | CAP_SYS_POWER)

// Ordinary programs launched without an explicit mask
#define CAP_PROFILE_USER_DEFAULT ( \
    CAP_FS_READ | CAP_FS_WRITE | CAP_FS_DIR | CAP_FS_CWD | CAP_FS_RENAME | \
    CAP_PROC_BASIC | CAP_PROC_SLEEP | CAP_MEM_SBRK | CAP_SYS_TIME)

// Interactive shell (normal): no power control; can elevate children via sandbox
#define CAP_PROFILE_SHELL_NORMAL ( \
    CAP_PROFILE_USER_DEFAULT | CAP_PROC_PS | CAP_PROC_KILL | \
    CAP_PROC_TRACE | CAP_PROC_SANDBOX)

// Interactive shell (maint): normal + poweroff/reboot/panic
#define CAP_PROFILE_SHELL_MAINT ( \
    CAP_PROFILE_SHELL_NORMAL | CAP_SYS_POWER)

// init / syscontroller: full set; grants shell a mode profile
#define CAP_PROFILE_SYSCONTROLLER CAP_ALL

// --- Capability struct ---

typedef struct {
    uint32 abi_version;
    uint64 caps;
} rvdos_abi_info_t;

#endif
