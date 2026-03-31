# RVDOS

RVDOS 是一个基于 RISC-V 架构的简易的操作系统
### 1. 准备环境
确保你的系统中已安装以下工具：
- `riscv64-linux-gnu-gcc` 
- `qemu-system-riscv64` 
- `dosfstools`, `mtools`

### 2. 编译内核
```bash
make
```

### 3. 准备磁盘镜像 (包含用户态程序)
```bash
bash mkfs.sh
```

### 4. 运行
```bash
make run
```

## 📂 项目结构

- `kernelspace/`: 内核源代码
  - `main.c`: 内核入口与初始化
  - `process.c`: 调度器与进程控制
  - `pagetable.c`: 页表管理与内存映射
  - `fs.c`: VirtIO 驱动与 FAT32 实现
  - `trap.c`: 系统调用分发与中断处理
- `userspace/`: 用户态库与程序
  - `shell.c`: 交互式 Shell 实现
  - `rvdos.h`: 标准库的头文件
  - `rvlibc.c`: 系统调用封装与标准库
- `fs.img`: FAT32 格式的磁盘镜像
