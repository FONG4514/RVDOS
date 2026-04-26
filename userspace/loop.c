#include "rvdos.h"

// 在配合 & 使用的时候，字符串输出会乱序

void main() {
    printf("Loop program started. I will run a busy loop and malloc memory.\n");
    
    // Test malloc
    void *ptr = malloc(1024);
    if (ptr) {
        printf("Successfully allocated 1024 bytes at %p\n", (uint64)ptr);
        free(ptr);
    } else {
        printf("Malloc failed!\n");
    }

    // Busy loop
    uint32 count = 0;
    while(1) {
        count++;
        if (count % 10000000 == 0) {
        }
    }
}
