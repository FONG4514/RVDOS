#ifndef USER_RVDOS_H
#define USER_RVDOS_H

#include <abi/types.h>
#include <abi/syscall.h>
#include <abi/abi.h>
#include <abi/capability.h>

pid_t    get_pid(void);
pid_t    spawn_process(const char *path, const char *args);
int32    wait_process(pid_t pid);
int32    kill_process(pid_t pid);
void     exit_process(int32 status);
int32    ps(proc_info_t *info, uint32 max);
void     sleep(uint64 time);


handle_t file_open(const char *path, int mode);
int32    file_read(handle_t h, void *buf, uint32 len);
int32    file_write(handle_t h, const void *buf, uint32 len);
void     close_handle(handle_t h);
int32    mkdir(const char *path);
int32    chdir(const char *path);
int32    unlink(const char *path);
int32    rename(const char *oldpath, const char *newpath);
int32    get_cwd(void *buf, uint32 len);
void     ls(void); 

void* sbrk(int n);

// Capability
int32    get_abi_info(rvdos_abi_info_t *info);
uint32   get_caps(void);
int32    get_version(char *buf, uint32 len);

uint32   get_ticks(void);
void     sys_panic(void);
void     poweroff(void);
void     reboot(void);
void     sys_trap(void);
int32    trace(int enable);

#endif