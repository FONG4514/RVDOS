#ifndef RVDOS_FS_H
#define RVDOS_FS_H

#include <abi/types.h>
#include <lock.h>

typedef struct file {
    int ref;             // Reference count
    int readable;
    int writable;
    uint32 offset;       // Current pointer
    uint32 first_cluster;
    uint32 file_size;
    char name[16];
    struct lock lock;
} file_t;

#endif