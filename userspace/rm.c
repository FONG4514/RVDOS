#include <rvdos.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: rm <path>\n");
        return -1;
    }

    if (unlink(argv[1]) < 0) {
        printf("rm failed\n");
        return -1;
    }

    return 0;
}
