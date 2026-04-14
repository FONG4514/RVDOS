#include "rvdos.h"

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        print_str(argv[i]);
        if (i < argc - 1) print_str(" ");
    }
    print_str("");
    return 0;
}
