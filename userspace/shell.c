#include "rvdos.h"

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
    print_str("Available commands:\n");
    print_str("  help         - Show this help message\n");
    print_str("  cd [dir]     - Change current directory\n");
    print_str("  exit         - Exit the shell\n");
    print_str("  [program]    - Execute a program (searches /usr/ first)\n");
}

void main() {
    char buf[128];
    char cwd_buf[128];
    char cmd_path[160];
    char *arg;

    print_str("\n--- RVDOS Shell ---\n");
    print_str("Type 'help' for a list of commands.\n");

    while (1) {
        print_str("[");
        if (get_cwd(cwd_buf, sizeof(cwd_buf)) == 0) {
            print_str(cwd_buf);
        }
        print_str("]");
        print_str(" # ");
        gets(buf, sizeof(buf));
        
        int len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
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
                if (chdir(arg) < 0) print_str("cd failed\n");
            } else {
                chdir("/");
            }
        } else if (strcasecmp(buf, "exit") == 0) {
            break;
        } else {
            // Priority 1: Search in /usr/
            for(int i=0; i<160; i++) cmd_path[i] = 0;
            cmd_path[0] = '/'; cmd_path[1] = 'u'; cmd_path[2] = 's'; cmd_path[3] = 'r'; cmd_path[4] = '/';
            int j = 5;
            for(int i=0; buf[i] && j < 159; i++) cmd_path[j++] = buf[i];
            
            pid_t pid = spawn_process(cmd_path, arg);
            if (pid < 0) {
                // Priority 2: Search in current directory
                pid = spawn_process(buf, arg);
            }

            if (pid < 0) {
                print_str("Unknown command: ");
                print_str(buf);
                print_str("\n");
            } else {
                wait_process(pid);
            }
        }
    }
    print_str("Shell exiting...\n");
}
