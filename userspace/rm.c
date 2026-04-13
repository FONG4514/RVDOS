#include "rvdos.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_str("Usage: rm <path>\n");
        return -1;
    }

    if (unlink(argv[1]) < 0) {
        print_str("rm failed\n");
        return -1;
    }

    return 0;
}
