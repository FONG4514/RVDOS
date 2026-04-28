#include <rvdos.h>

extern int main(int argc, char *argv[]);

// 用户态程序的真正入口
void __attribute__((section(".text.entry"))) _start(int argc, char *argv[]) {
    int ret = main(argc, argv);
    exit_process(ret);
}