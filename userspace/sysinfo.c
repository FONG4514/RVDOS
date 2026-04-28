#include "rvdos.h"
#include <stdio.h>

int main() {
    rvdos_abi_info_t info;
    char ver[32];

    if (get_version(ver, 32) != 0) {
        printf("Failed to get kernel version\n");
    } else {
        printf("Kernel: %s\n", ver);
    }

    if (get_abi_info(&info) != 0) {
        printf("Failed to get ABI info\n");
        return -1;
    }

    printf("ABI Version: %d\n", info.abi_version);
    printf("Supported Capabilities:\n");

    printf("[%s] FS_BASIC\n", (info.caps & CAP_FS_BASIC) ? "X" : " ");
    printf("[%s] FS_DIR\n", (info.caps & CAP_FS_DIR) ? "X" : " ");
    printf("[%s] FS_CWD\n", (info.caps & CAP_FS_CWD) ? "X" : " ");
    printf("[%s] FS_RENAME\n", (info.caps & CAP_FS_RENAME) ? "X" : " ");
    printf("[%s] PROC_BASIC\n", (info.caps & CAP_PROC_BASIC) ? "X" : " ");
    printf("[%s] PROC_PS\n", (info.caps & CAP_PROC_PS) ? "X" : " ");
    printf("[%s] MEM_SBRK\n", (info.caps & CAP_MEM_SBRK) ? "X" : " ");
    printf("[%s] SYS_TIME\n", (info.caps & CAP_SYS_TIME) ? "X" : " ");
    printf("[%s] SYS_POWER\n", (info.caps & CAP_SYS_POWER) ? "X" : " ");

    return 0;
}
