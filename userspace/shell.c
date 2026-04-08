#include "rvdos.h"

void help() {
    print_str("Available commands:\n");
    print_str("  help         - Show this help message\n");
    print_str("  echo [str]   - Print string to screen\n");
    print_str("  ls           - List files in root directory\n");
    print_str("  cat [file]   - Display file contents\n");
    print_str("  clear        - Clear the screen (simulated)\n");
    print_str("  poweroff     - Shut down the system\n");
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

void main() {
    char buf[128];
    char *redir_file;

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

        if (strcmp(buf, "help") == 0) {
            help();
        } else if (strcmp(buf, "ls") == 0) {
            // Note: LS syscall will now respect STDOUT of shell if we redirect it.
            // But shell is parent. For LS, we'll just implement it here or let kernel do it.
            // Since SYS_LS is a kernel print, we can't easily redirect shell's own output 
            // without changing the kernel's current handle.
            // Let's assume LS is a program for redirection to work properly via spawn.
            pid_t pid = spawn_process("ls", redir_file);
            if (pid < 0) ls(); // fallback
            else wait_process(pid);
        } else if (strcmp(buf, "clear") == 0) {
            for(int i = 0; i < 50; i++) print_str("\n");
        } else if (strcmp(buf, "poweroff") == 0) {
            poweroff();
        } else if (strncmp(buf, "echo ", 5) == 0) {
            char *text = buf + 5;
            handle_t out = STDOUT;
            if (redir_file) {
                out = file_open(redir_file, O_WRONLY | O_CREATE | O_TRUNC);
            }
            file_write(out, text, strlen(text));
            file_write(out, "\n", 1);
            if (redir_file) close_handle(out);
        } else if (strncmp(buf, "cat ", 4) == 0) {
            cat(buf + 4, redir_file);
        } else if (strcmp(buf, "exit") == 0) {
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
