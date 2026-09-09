#include <stdio.h>
#include <stdlib.h>
#include <rvdos.h>
#include <string.h>

uint64 bin_to_uint64(const char *s) {
    uint64 res = 0;

    if (!s) return 0;

    if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        s += 2;
    }

    while (*s) {
        if (*s == '1') {
            res = (res << 1) | 1;
        } else if (*s == '0') {
            res <<= 1;
        } else {
            break;
        }
        s++;
    }

    return res;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: sandbox <program> <binary_mask> [args...]\n");
        printf("  Child caps = (this process caps) ∩ mask\n");
        printf("  Requires CAP_PROC_SANDBOX.\n");
        exit_process(0);
    }

    char *bin_mask = argv[2];
    uint64 mask = bin_to_uint64(bin_mask);

    // Program args start after mask (argv[3]...)
    char args[128] = "";
    int pos = 0;
    for (int i = 3; i < argc; i++) {
        int len = strlen(argv[i]);
        if (pos + len + 1 < 128) {
            for (int j = 0; j < len; j++) {
                args[pos++] = argv[i][j];
            }
            if (i < argc - 1) {
                args[pos++] = ' ';
            }
        }
    }
    args[pos] = '\0';

    char cmd_path[160];
    cmd_path[0] = '/'; cmd_path[1] = 'u'; cmd_path[2] = 's'; cmd_path[3] = 'r'; cmd_path[4] = '/';
    int j = 5;
    for (int i = 0; argv[1][i] && j < 159; i++) cmd_path[j++] = argv[1][i];
    cmd_path[j] = '\0';

    pid_t pid = sandbox(cmd_path, args[0] ? args : 0, mask);
    if (pid < 0) {
        pid = sandbox(argv[1], args[0] ? args : 0, mask);
    }

    if (pid < 0) {
        if (pid == (pid_t)WITHOUT_CAP)
            printf("sandbox: permission denied (need CAP_PROC_SANDBOX)\n");
        else
            printf("sandbox: failed to spawn %s\n", argv[1]);
        exit_process(-1);
    }

    wait_process(pid);
    exit_process(0);
}
