#include "rvdos.h"

void main() {
    uint32 start, end;
    int count = 1000000;

    print_str("Starting benchmark: 1000000 sys_trap calls...\n");

    start = get_ticks();
    for (int i = 0; i < count; i++) {
        sys_trap();
    }
    end = get_ticks();

    print_str("Done.\n");
    print_str("Start ticks: ");
    print_int(start);
    print_str("\nEnd ticks: ");
    print_int(end);
    print_str("\nTotal ticks for 100 calls: ");
    print_int(end - start);
    print_str("\n");

    exit_process(0);
}
