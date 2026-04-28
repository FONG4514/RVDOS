#include <rvdos.h>
#include <stdio.h>

int main() {
    uint32 start, end;
    int count = 1000000;

    printf("Starting benchmark: 1000000 sys_trap calls...\n");

    start = get_ticks();
    for (int i = 0; i < count; i++) {
        sys_trap();
    }
    end = get_ticks();

    printf("Done.\n");
    printf("Start ticks: %d\n", start);
    printf("End ticks: %d\n", end);
    printf("Total ticks: %d\n", end - start);

    return 0;
}
