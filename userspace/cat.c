#include <rvdos.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return -1;
    }

    handle_t h = file_open(argv[1], O_RDONLY);
    if (h == INVALID_HANDLE) {
        printf("cat: cannot open %s\n", argv[1]);
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
