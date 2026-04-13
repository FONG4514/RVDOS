#include "rvdos.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_str("Usage: mkdir <dirname>\n");
        return -1;
    }

    if (mkdir(argv[1]) < 0) {
        print_str("mkdir failed\n");
        return -1;
    }

    return 0;
}
