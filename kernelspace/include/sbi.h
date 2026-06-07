#ifndef RVDOS_SBI_H
#define RVDOS_SBI_H

#include <abi/types.h>

// SBI Extension IDs
#define SBI_EXT_0_1_CONSOLE_PUTCHAR 0x01
#define SBI_EXT_SRST 0x53525354
#define SBI_EXT_TIME 0x54494D45
#define SBI_EXT_HSM 0x48534D

// SBI Function IDs for SRST (System Reset)
#define SBI_FUNC_SRST_RESET 0x0

// SBI Function IDs for HSM (Hart State Management)
#define SBI_FUNC_HSM_HART_START 0x0

// Reset Types
#define SBI_SRST_RESET_TYPE_SHUTDOWN 0x0
#define SBI_SRST_RESET_TYPE_COLD_REBOOT 0x1
#define SBI_SRST_RESET_TYPE_WARM_REBOOT 0x2

// Reset Reasons
#define SBI_SRST_RESET_REASON_NONE 0x0
#define SBI_SRST_RESET_REASON_SYSTEM_FAILURE 0x1

struct sbiret {
    long error;
    long value;
};

// Generic SBI call
struct sbiret sbi_call(uint64 ext, uint64 fid, uint64 arg0, uint64 arg1, uint64 arg2);

// Simplified console output (SBI v0.1)
void sbi_console_putchar(int ch);

// Set timer (SBI v1.0 TIME extension)
void sbi_set_timer(uint64 stime_value);

// System reset (SBI v1.0 SRST extension)
void sbi_system_reset(uint32 type, uint32 reason);

// Start a secondary hart (SBI v1.0 HSM extension)
struct sbiret sbi_hart_start(uint64 hartid, uint64 start_addr, uint64 opaque);

#endif
