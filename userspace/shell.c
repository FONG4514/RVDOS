#include "rvdos.h"

void help() {
    print_str("Available commands:\n");
    print_str("  help         - Show this help message\n");
    print_str("  echo [str]   - Print string to screen\n");
    print_str("  ls           - List files in current directory\n");
    print_str("  cat [file]   - Display file contents\n");
    print_str("  cd [dir]     - Change current directory\n");
    print_str("  mkdir [dir]  - Create a new directory\n");
    print_str("  rm [path]    - Remove a file or directory\n");
    print_str("  clear        - Clear the screen (simulated)\n");
    print_str("  poweroff     - Shut down the system\n");
    print_str("  reboot       - Reboot the system\n");
    print_str("  exit         - Exit the shell\n");
    print_str("  [prog] > [f] - Redirect output to file\n");
    print_str("  [program]    - Try to execute a program\n");
}

void cat(const char *path, const char *redir_file) {
    handle_t h = file_open(path, O_RDONLY);
    if (h == INVALID_HANDLE) {
        print_str("cat: cannot open ");
        print_str(path);
        print_str("\n");
        return;
    }

    handle_t out = STDOUT;
    if (redir_file) out = file_open(redir_file, O_WRONLY | O_CREATE | O_TRUNC);

    char buf[128];
    int32 n;
    while ((n = file_read(h, buf, sizeof(buf))) > 0) {
        file_write(out, buf, n);
    }
    file_write(out, "\n", 1);
    
    close_handle(h);
    if (redir_file) close_handle(out);
}

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

void main() {
    char buf[128];
    char cwd_buf[128];
    char *redir_file;
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
        
        // Remove trailing newline
        int len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }

        if (buf[0] == '\0') continue;

        // Simple redirection parsing
        redir_file = NULL;
        for (int i = 0; buf[i]; i++) {
            if (buf[i] == '>') {
                buf[i] = '\0';
                redir_file = buf + i + 1;
                // Skip spaces
                while (*redir_file == ' ') redir_file++;
                // Trim trailing spaces from command
                int j = i - 1;
                while (j >= 0 && buf[j] == ' ') buf[j--] = '\0';
                break;
            }
        }

        // Split command and first argument
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
        } else if (strcasecmp(buf, "ls") == 0) {
            pid_t pid = spawn_process("ls", redir_file);
            if (pid < 0) ls(); 
            else wait_process(pid);
        } else if (strcasecmp(buf, "clear") == 0) {
            for(int i = 0; i < 50; i++) print_str("\n");
        } else if (strcasecmp(buf, "poweroff") == 0) {
            poweroff();
        } else if (strcasecmp(buf, "reboot") == 0) {
            reboot();
        } else if (strcasecmp(buf, "echo") == 0) {
            char *text = arg ? arg : "";
            handle_t out = STDOUT;
            if (redir_file) {
                out = file_open(redir_file, O_WRONLY | O_CREATE | O_TRUNC);
            }
            file_write(out, text, strlen(text));
            file_write(out, "\n", 1);
            if (redir_file) close_handle(out);
        } else if (strcasecmp(buf, "cat") == 0) {
            if (arg) cat(arg, redir_file);
            else print_str("Usage: cat <file>\n");
        } else if (strcasecmp(buf, "cd") == 0) {
            if (arg) {
                if (chdir(arg) < 0) print_str("cd failed\n");
            } else {
                chdir("/");
            }
        } else if (strcasecmp(buf, "mkdir") == 0) {
            if (arg) {
                if (mkdir(arg) < 0) print_str("mkdir failed\n");
            } else {
                print_str("Usage: mkdir <dir>\n");
            }
        } else if (strcasecmp(buf, "rm") == 0) {
            if (arg) {
                if (unlink(arg) < 0) print_str("rm failed\n");
            } else {
                print_str("Usage: rm <path>\n");
            }
        } else if (strcasecmp(buf, "exit") == 0) {
            break;
        } else {
            // Try to spawn the command as a process
            pid_t pid = spawn_process(buf, redir_file);
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
