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
        printf("Usage: sandbox program mask\n");
        exit_process(0);
    }

    char * bin_mask = argv[2];
    uint64 mask = bin_to_uint64(bin_mask);

    mask = mask | PROC_CAP_ENABLE;

    char args[128] = "";
    int pos = 0;
    for (int i = 2; i < argc; i++) {
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

    // Search logic similar to shell
    char cmd_path[160];
    // Priority 1: Search in /usr/
    cmd_path[0] = '/'; cmd_path[1] = 'u'; cmd_path[2] = 's'; cmd_path[3] = 'r'; cmd_path[4] = '/';
    int j = 5;
    for(int i = 0; argv[1][i] && j < 159; i++) cmd_path[j++] = argv[1][i];
    cmd_path[j] = '\0';
    
    pid_t pid = sandbox(cmd_path, args,mask);
    if (pid < 0) {
        // Priority 2: Search in current directory (or absolute path)
        pid = sandbox(argv[1], args,mask);
    }

    if (pid < 0) {
        printf("sandbox: failed to spawn %s\n", argv[1]);
        exit_process(-1);
    }

    wait_process(pid);
    exit_process(0);
}