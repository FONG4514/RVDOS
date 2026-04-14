#include "rvdos.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_str("Usage: cat <file>");
        return -1;
    }

    handle_t h = file_open(argv[1], O_RDONLY);
    if (h == INVALID_HANDLE) {
        print_str("cat: cannot open ");
        print_str(argv[1]);
        print_str("");
        return -1;
    }

    char buf[128];
    int32 n;
    while ((n = file_read(h, buf, sizeof(buf))) > 0) {
        file_write(STDOUT, buf, n);
    }
    
    close_handle(h);
    return 0;
}
