#include <stdio.h>
#include <stdlib.h>
#include <rvdos.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: trace program [args...]\n");
        exit_process(0);
    }

    // Enable tracing for this process (and its future children)
    trace(1);

    // Prepare arguments for spawn_process
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
    
    pid_t pid = spawn_process(cmd_path, args);
    if (pid < 0) {
        // Priority 2: Search in current directory (or absolute path)
        pid = spawn_process(argv[1], args);
    }

    if (pid < 0) {
        printf("trace: failed to spawn %s\n", argv[1]);
        exit_process(-1);
    }

    wait_process(pid);
    exit_process(0);
}
