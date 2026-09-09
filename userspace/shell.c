#include "rvdos.h"
#include <stdio.h>
#include <string.h>

// Simple case-insensitive comparison helper
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++;
        s2++;
    }
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    return (unsigned char)c1 - (unsigned char)c2;
}

void help() {
    printf("Available commands:\n");
    printf("  help         - Show this help message\n");
    printf("  cd [dir]     - Change current directory\n");
    printf("  caps         - Show this shell's capabilities\n");
    printf("  exit         - Exit the shell (syscontroller restarts it)\n");
    printf("  [cmd] &      - Run command in background\n");
    printf("  [program]    - Execute a program (searches /usr/ first)\n");
    printf("\nCapability model:\n");
    printf("  default spawn → USER_DEFAULT ∩ shell caps\n");
    printf("  privileged tools (ps/kill/...) get elevated subset\n");
}

// Extra caps beyond USER_DEFAULT for known privileged tools.
// Kernel still enforces child ⊆ shell caps.
static uint64 elev_caps_for(const char *cmd) {
    uint64 c = CAP_PROFILE_USER_DEFAULT;

    if (strcasecmp(cmd, "ps") == 0)
        c |= CAP_PROC_PS;
    else if (strcasecmp(cmd, "kill") == 0)
        c |= CAP_PROC_KILL;
    else if (strcasecmp(cmd, "trace") == 0)
        c |= CAP_PROC_TRACE;
    else if (strcasecmp(cmd, "sandbox") == 0)
        c |= CAP_PROC_SANDBOX;
    else if (strcasecmp(cmd, "poweroff") == 0 ||
             strcasecmp(cmd, "reboot") == 0 ||
             strcasecmp(cmd, "panic") == 0)
        c |= CAP_SYS_POWER;
    else
        return 0; // use default spawn (USER_DEFAULT)

    return c;
}

static void print_caps(uint64 caps) {
    printf("shell caps=%x\n", caps);
    printf("  FS_READ=%d FS_WRITE=%d FS_DIR=%d FS_CWD=%d FS_RENAME=%d\n",
           !!(caps & CAP_FS_READ), !!(caps & CAP_FS_WRITE), !!(caps & CAP_FS_DIR),
           !!(caps & CAP_FS_CWD), !!(caps & CAP_FS_RENAME));
    printf("  PROC_BASIC=%d PS=%d KILL=%d SLEEP=%d TRACE=%d SANDBOX=%d\n",
           !!(caps & CAP_PROC_BASIC), !!(caps & CAP_PROC_PS), !!(caps & CAP_PROC_KILL),
           !!(caps & CAP_PROC_SLEEP), !!(caps & CAP_PROC_TRACE), !!(caps & CAP_PROC_SANDBOX));
    printf("  MEM_SBRK=%d SYS_TIME=%d SYS_POWER=%d\n",
           !!(caps & CAP_MEM_SBRK), !!(caps & CAP_SYS_TIME), !!(caps & CAP_SYS_POWER));
}

static pid_t run_program(const char *name, const char *arg) {
    char cmd_path[160];
    uint64 elev = elev_caps_for(name);
    pid_t pid;

    // Priority 1: /usr/<name>
    cmd_path[0] = '/'; cmd_path[1] = 'u'; cmd_path[2] = 's'; cmd_path[3] = 'r'; cmd_path[4] = '/';
    int j = 5;
    for (int i = 0; name[i] && j < 159; i++) cmd_path[j++] = name[i];
    cmd_path[j] = '\0';

    if (elev) {
        pid = sandbox(cmd_path, arg, elev);
        if (pid < 0)
            pid = sandbox(name, arg, elev);
    } else {
        pid = spawn_process(cmd_path, arg);
        if (pid < 0)
            pid = spawn_process(name, arg);
    }
    return pid;
}

void main() {
    char buf[128];
    char cwd_buf[128];
    char *arg;
    int background;

    printf("\n--- RVDOS Shell ---\n");
    printf("Type 'help' for a list of commands.\n");

    while (1) {

        while (wait_process(WAIT_NONBLOCK_KEY) > 0) {
        }

        if (get_cwd(cwd_buf, sizeof(cwd_buf)) == 0) {
            printf("[%s] # ", cwd_buf);
        } else {
            printf("[] # ");
        }

        gets(buf, sizeof(buf));

        int len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }

        if (buf[0] == '\0') continue;

        // Check for background execution
        background = 0;
        if (len > 0 && buf[len-1] == '&') {
            background = 1;
            buf[--len] = '\0';
            while (len > 0 && buf[len-1] == ' ') {
                buf[--len] = '\0';
            }
        }

        if (buf[0] == '\0') continue;

        // Split command and argument string
        arg = NULL;
        for (int i = 0; buf[i]; i++) {
            if (buf[i] == ' ') {
                buf[i] = '\0';
                arg = buf + i + 1;
                while (*arg == ' ') arg++;
                if (*arg == '\0') arg = NULL;
                break;
            }
        }

        if (strcasecmp(buf, "help") == 0) {
            help();
        } else if (strcasecmp(buf, "cd") == 0) {
            if (arg) {
                if (chdir(arg) < 0) printf("cd failed\n");
            } else {
                chdir("/");
            }
        } else if (strcasecmp(buf, "caps") == 0) {
            print_caps(get_caps());
        } else if (strcasecmp(buf, "exit") == 0) {
            break;
        } else {
            pid_t pid = run_program(buf, arg);

            if (pid < 0) {
                if (pid == (pid_t)WITHOUT_CAP)
                    printf("Permission denied (missing capability): %s\n", buf);
                else
                    printf("Unknown command: %s\n", buf);
            } else {
                if (background) {
                    printf("[PID %d] started in background\n", pid);
                } else {
                    wait_process(pid);
                }
            }
        }
    }
    printf("Shell exiting...\n");
}
