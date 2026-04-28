#include <rvdos.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: mkdir <dirname>\n");
        return -1;
    }

    if (mkdir(argv[1]) < 0) {
        printf("mkdir failed\n");
        return -1;
    }

    return 0;
}
