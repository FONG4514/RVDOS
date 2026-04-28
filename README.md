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

## 项目结构
- `include/`
  - `abi/` abi定义
  - `arch/` riscv64封装
- `kernelspace/`: 内核源代码
  - `core/`: 内核核心组建
  - `drivers/`: 内核驱动相关
  - `fs/`: 文件系统相关
  - `include/`: 内核头文件
  - `lib/`: 内核依赖的工具
  - `entry.S`： 内核入口
- `userspace/`: 用户态库与程序
  - `include/`: 用户头文件
  - `....` : 用户程序
- `fs.img`: FAT32 格式的磁盘镜像
- `mkfs.sh` :  文件系统构建脚本