#include <stdio.h>
#include <rvdos.h>

int atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: kill pid\n");
        return 1;
    }

    int pid = atoi(argv[1]);
    if (pid <= 0) {
        printf("invalid pid: %s\n", argv[1]);
        return 1;
    }

    if (kill_process(pid) < 0) {
        printf("kill %d failed\n", pid);
        return 1;
    }

    return 0;
}
