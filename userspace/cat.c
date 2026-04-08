#include "rvdos.h"

void main(int argc, char *argv[]) {
    // Note: Our shell currently doesn't pass argc/argv to main directly via stack properly
    // but it passes the command line buffer. For now, we'll read the command line
    // Or just implement a simple version that captures the first argument from somewhere.
    // In this OS, the shell currently just spawns the process.
    // Let's assume the shell might pass arguments eventually.
    // For now, let's make a hacky cat that works with our current spawn.
}
