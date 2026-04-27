#ifndef RVDOS_FS_H
#define RVDOS_FS_H

#include <abi/types.h>

typedef struct file {
    int used;
    int readable;
    int writable;
    uint32 offset;       // Current pointer
    uint32 first_cluster;
    uint32 file_size;
    char name[16];
} file_t;

#endif