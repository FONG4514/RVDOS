#include <rvdos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void itoa(int n, char s[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        s[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0) s[i++] = '-';
    s[i] = '\0';
    
    // reverse
    int j;
    char c;
    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void cpu_stress(int iterations) {
    printf("Starting CPU stress (%d iterations)...\n", iterations);
    volatile int a = 0;
    for (int i = 0; i < iterations; i++) {
        for (int j = 0; j < 10000; j++) {
            a += i * j;
        }
    }
    printf("CPU stress done.\n");
}

void mem_stress(int blocks, int size) {
    printf("Starting memory stress (%d blocks of %d bytes)...\n", blocks, size);
    void **ptrs = malloc(sizeof(void *) * blocks);
    if (!ptrs) {
        printf("Failed to allocate pointers array\n");
        return;
    }

    for (int i = 0; i < blocks; i++) {
        ptrs[i] = malloc(size);
        if (!ptrs[i]) {
            printf("Failed to allocate block %d\n", i);
            blocks = i;
            break;
        }
        memset(ptrs[i], i & 0xFF, size);
    }

    for (int i = 0; i < blocks; i++) {
        unsigned char *p = (unsigned char *)ptrs[i];
        for (int j = 0; j < size; j++) {
            if (p[j] != (i & 0xFF)) {
                printf("Memory corruption at block %d, offset %d\n", i, j);
                break;
            }
        }
        free(ptrs[i]);
    }
    free(ptrs);
    printf("Memory stress done.\n");
}

void file_stress(int count) {
    printf("Starting file stress (%d files)...\n", count);
    char filename[32];
    char buf[64];
    char read_buf[64];
    
    for (int i = 0; i < count; i++) {
        strcpy(filename, "stress_");
        char num[16];
        itoa(i, num);
        strcat(filename, num);
        strcat(filename, ".tmp");

        handle_t h = file_open(filename, O_CREATE | O_RDWR);
        if (h < 0) {
            printf("Failed to create file %s\n", filename);
            continue;
        }

        itoa(i * 1234, buf);
        int len = strlen(buf);
        file_write(h, buf, len);
        close_handle(h);

        h = file_open(filename, O_RDONLY);
        if (h < 0) {
            printf("Failed to open file %s for reading\n", filename);
            continue;
        }
        memset(read_buf, 0, sizeof(read_buf));
        int n = file_read(h, read_buf, len);
        if (n < 0) {
            printf("Failed to read file %s\n", filename);
        } else {
            read_buf[n] = '\0';
            if (strcmp(buf, read_buf) != 0) {
                printf("File content mismatch in %s: expected %s, got %s\n", filename, buf, read_buf);
            }
        }
        close_handle(h);

        if (unlink(filename) < 0) {
            printf("Failed to unlink %s\n", filename);
        }
    }
    printf("File stress done.\n");
}

void proc_stress(int count) {
    printf("Starting process stress (spawning %d processes)...\n", count);
    for (int i = 0; i < count; i++) {
        pid_t pid = spawn_process("/usr/echo", "stress_child");
        if (pid < 0) {
            printf("Failed to spawn process %d\n", i);
            continue;
        }
        wait_process(pid);
    }

     // Test 2: Spawn and Kill (no wait)
    printf("Testing process kill (spawn /usr/loop and kill)...\n");
    for (int i = 0; i < 128; i++) {
        pid_t pid = spawn_process("/usr/loop", "&");
        if (pid > 0) {
            if (kill_process(pid) < 0) {
                printf("Failed to kill process %d\n", pid);
            } else {
                printf("Killed process %d\n", pid);
            }
        } else {
            printf("Failed to spawn loop process\n");
        }
    }

    printf("Process stress done.\n");
}

int main(int argc, char *argv[]) {
    printf("--- 448OS Stress Test ---\n");
    
    printf("%d\n",1/0);

    cpu_stress(0xf);
    // mem_stress(100, 4096);
    // file_stress(50);
    // proc_stress(64);
    
    printf("--- Stress Test Completed Successfully ---\n");
    return 0;
}
