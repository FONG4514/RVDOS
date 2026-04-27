#ifndef RVDOS_LOCK_H
#define RVDOS_LOCK_H

#include <abi/types.h>

typedef struct lock {
    volatile uint32 locked; 
    char *lock_name;
    uint32 lock_count;
} spinlock_t;

#endif