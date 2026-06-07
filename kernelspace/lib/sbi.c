#include <sbi.h>

struct sbiret sbi_call(uint64 ext, uint64 fid, uint64 arg0, uint64 arg1, uint64 arg2) {
    struct sbiret ret;
    register uint64 a0 asm("a0") = arg0;
    register uint64 a1 asm("a1") = arg1;
    register uint64 a2 asm("a2") = arg2;
    register uint64 a6 asm("a6") = fid;
    register uint64 a7 asm("a7") = ext;

    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a6), "r"(a7)
                 : "memory");

    ret.error = a0;
    ret.value = a1;
    return ret;
}

void sbi_console_putchar(int ch) {
    // SBI v0.1 console_putchar
    sbi_call(SBI_EXT_0_1_CONSOLE_PUTCHAR, 0, (uint64)ch, 0, 0);
}

void sbi_set_timer(uint64 stime_value) {
    sbi_call(SBI_EXT_TIME, 0, stime_value, 0, 0);
}

void sbi_system_reset(uint32 type, uint32 reason) {
    sbi_call(SBI_EXT_SRST, SBI_FUNC_SRST_RESET, (uint64)type, (uint64)reason, 0);
}

struct sbiret sbi_hart_start(uint64 hartid, uint64 start_addr, uint64 opaque) {
    // hartid 启动后从 start_addr 进入 S 模式，a0=hartid, a1=opaque
    return sbi_call(SBI_EXT_HSM, SBI_FUNC_HSM_HART_START, hartid, start_addr, opaque);
}
