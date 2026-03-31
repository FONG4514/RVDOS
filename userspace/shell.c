#include "rvdos.h"

void help() {
    print_str("Available commands:\n");
    print_str("  help         - Show this help message\n");
    print_str("  echo [str]   - Print string to screen\n");
    print_str("  ls           - List files in root directory\n");
    print_str("  cat [file]   - Display file contents\n");
    print_str("  clear        - Clear the screen (simulated)\n");
    print_str("  exit         - Exit the shell\n");
    print_str("  [program]    - Try to execute a program\n");
}

void cat(const char *path) {
    handle_t h = file_open(path);
    if (h == INVALID_HANDLE) {
        print_str("cat: cannot open ");
        print_str(path);
        print_str("\n");
        return;
    }

    char buf[128];
    int32 n;
    while ((n = file_read(h, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print_str(buf);
    }
    print_str("\n");
    close_handle(h);
}

void main() {
    char buf[128];

    print_str("\n--- RVDOS Shell ---\n");
    print_str("Type 'help' for a list of commands.\n");

    while (1) {
        print_str("$ ");
        gets(buf, sizeof(buf));
        
        // Remove trailing newline
        int len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }

        if (buf[0] == '\0') continue;

        if (strcmp(buf, "help") == 0) {
            help();
        } else if (strcmp(buf, "ls") == 0) {
            ls();
        } else if (strcmp(buf, "clear") == 0) {
            for(int i = 0; i < 50; i++) print_str("\n");
        } else if (strncmp(buf, "echo ", 5) == 0) {
            print_str(buf + 5);
            print_str("\n");
        } else if (strncmp(buf, "cat ", 4) == 0) {
            cat(buf + 4);
        } else if (strcmp(buf, "exit") == 0) {
            break;
        } else {
            // Try to spawn the command as a process
            pid_t pid = spawn_process(buf);
            if (pid < 0) {
                print_str("Unknown command: ");
                print_str(buf);
                print_str("\n");
            } else {
                // Wait for the process to exit
                wait_process(pid);
            }
        }
    }

    print_str("Shell exiting...\n");
}
