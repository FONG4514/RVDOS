#include "rvdos.h"

// 由于内核实现的问题，暂时只支持在改名和移动到同级目录下的文件夹

int main(int argc, char *argv[]) {
    if (argc != 3) {
        print_str("Usage: mv old_path new_path");
        exit_process(-1);
    }

    if (rename(argv[1], argv[2]) < 0) {
        print_str("mv: failed to rename '");
        print_str(argv[1]);
        print_str("' to '");
        print_str(argv[2]);
        print_str("'");
        exit_process(-1);
    }

    exit_process(0);
}
