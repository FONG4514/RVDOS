#include "rvdos.h"

void main() {
    proc_info_t info[64];
    int32 count = ps(info, 64);
    
    if (count < 0) {
        printf("ps failed\n");
        return;
    }

    printf("PID   NAME             PRIO       EFF_PRIO   STATE\n");
    for (int i = 0; i < count; i++) {
        char *state_str;
        switch (info[i].state) {
            case 1: state_str = "SLEEPING"; break;
            case 2: state_str = "RUNNABLE"; break;
            case 3: state_str = "RUNNING"; break;
            case 4: state_str = "ZOMBIE"; break;
            default: state_str = "UNKNOWN"; break;
        }
        printf("%d     %s             %d         %d         %s\n", 
               info[i].pid, 
               info[i].name[0] ? info[i].name : "[init]", 
               info[i].priority, 
               info[i].effective_priority,
               state_str);
    }
}
