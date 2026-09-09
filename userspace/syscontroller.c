#include "rvdos.h"
#include <stdio.h>
#include <string.h>

#define MODE_NORMAL 0
#define MODE_MAINT  1

static int parse_mode(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        if (strcmp(argv[i], "maint") == 0 || strcmp(argv[i], "maintenance") == 0)
            return MODE_MAINT;
        if (strcmp(argv[i], "normal") == 0)
            return MODE_NORMAL;
    }
    return MODE_NORMAL;
}

static uint64 shell_caps_for_mode(int mode) {
    if (mode == MODE_MAINT)
        return CAP_PROFILE_SHELL_MAINT;
    return CAP_PROFILE_SHELL_NORMAL;
}

int main(int argc, char *argv[]) {
    int mode = parse_mode(argc, argv);
    uint64 shell_caps = shell_caps_for_mode(mode);
    const char *mode_name = (mode == MODE_MAINT) ? "maint" : "normal";

    printf("\n--- RVDOS syscontroller ---\n");
    printf("mode=%s  shell_caps=%x\n", mode_name, shell_caps);
    printf("Shell will be restarted if it exits.\n\n");

    for (;;) {
        pid_t pid = sandbox("shell", 0, shell_caps);
        if (pid < 0) {
            printf("syscontroller: failed to spawn shell (err=%d)\n", (int)pid);
            sleep(50);
            continue;
        }

        printf("syscontroller: shell started pid=%d\n", pid);
        wait_process(pid);
        printf("syscontroller: shell exited, restarting...\n");
    }

    return 0;
}
