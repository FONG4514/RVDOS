#include "rvdos.h"
#include <stdio.h>
#include <string.h>

void print_spaces(int n) {
    for (int i = 0; i < n; i++) {
        print_str(" ");
    }
}

void print_str_padded(const char *s, int width) {
    int len = strlen(s);
    print_str(s);
    if (len < width) {
        print_spaces(width - len);
    }
}

void print_int_padded(int n, int width) {
    int len = 0;
    int temp = n;
    
    if (temp == 0) {
        len = 1;
    } else {
        if (temp < 0) {
            len++;
            temp = -temp;
        }
        while (temp > 0) {
            len++;
            temp /= 10;
        }
    }
    
    print_int(n);
    if (len < width) {
        print_spaces(width - len);
    }
}

void main() {
    proc_info_t info[64];
    int32 count = ps(info, 64);
    
    if (count < 0) {
        printf("ps failed\n");
        return;
    }

    print_str_padded("PID", 6);
    print_str_padded("OWNER-PID", 16);
    print_str_padded("NAME", 16);
    print_str_padded("PRIO", 6);
    print_str_padded("E-PRIO", 8);
    print_str_padded("STATE", 12);
    print_str_padded("HANDLES", 8);
    print_str("\n");

    for (int i = 0; i < count; i++) {
        char *state_str;
        switch (info[i].state) {
            case 1: state_str = "SLEEPING"; break;
            case 2: state_str = "RUNNABLE"; break;
            case 3: state_str = "RUNNING"; break;
            case 4: state_str = "ZOMBIE"; break;
            default: state_str = "UNKNOWN"; break;
        }
        
        print_int_padded(info[i].pid, 6);
        print_int_padded(info[i].owner_pid, 16);
        print_str_padded(info[i].name[0] ? info[i].name : "[init]", 16);
        print_int_padded(info[i].priority, 6);
        print_int_padded(info[i].effective_priority, 8);
        print_str_padded(state_str, 12);
        print_int_padded(info[i].handle_count, 8);
        print_str("\n");
    }
}
