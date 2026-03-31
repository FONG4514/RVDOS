#!/bin/bash

# 配置
IMG_NAME="fs.img"
IMG_SIZE_MB=64
USER_DIR="userspace"
BUILD_DIR="build_user"

# 检查依赖
if ! command -v mkfs.fat &> /dev/null; then
    echo "错误: 请安装 dosfstools (sudo dnf install dosfstools)"
    exit 1
fi

if ! command -v mcopy &> /dev/null; then
    echo "错误: 请安装 mtools (sudo dnf install mtools)"
    exit 1
fi

# 1. 创建 64MB 的原始镜像文件
echo "正在创建 ${IMG_NAME} (${IMG_SIZE_MB}MB)..."
dd if=/dev/zero of=${IMG_NAME} bs=1M count=${IMG_SIZE_MB} status=none

# 2. 格式化为 FAT32
# -F 32 指定 FAT32, -S 512 指定扇区大小
mkfs.fat -F 32 -S 512 ${IMG_NAME} > /dev/null

# 编译用户态程序
echo "正在编译用户态程序..."
mkdir -p ${BUILD_DIR}

CC="riscv64-linux-gnu-gcc"
CFLAGS="-Wall -O0 -ffreestanding -nostdlib -fno-common -mcmodel=medany -mno-relax -march=rv64gc -mabi=lp64 -I."

# 编译所有 c 文件并链接
${CC} ${CFLAGS} -c ${USER_DIR}/rvlibc.c -o ${BUILD_DIR}/rvlibc.o
${CC} ${CFLAGS} -c ${USER_DIR}/shell.c -o ${BUILD_DIR}/shell.o

# 链接：必须包含 rvlibc.o 才能使用 wait_process 等新函数
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/shell.o -o ${BUILD_DIR}/shell.elf

# 转换为纯二进制文件，剥离 ELF 头，让内核直接加载代码
riscv64-linux-gnu-objcopy -S -O binary ${BUILD_DIR}/shell.elf ${BUILD_DIR}/shell

# 4. 将文件塞入镜像
echo "正在将文件存入镜像..."
# 创建一个测试文件供 Demo 使用
echo "Hello from RVDOS File System!" > ${BUILD_DIR}/README.TXT

mcopy -i ${IMG_NAME} ${BUILD_DIR}/shell ::/shell

# 移动 icon (如果是目录则使用 -s 递归)
if [ -d "icon" ]; then
    mcopy -i ${IMG_NAME} -s icon ::/
elif [ -f "icon" ]; then
    mcopy -i ${IMG_NAME} icon ::/
fi

# 验证
echo "镜像内容如下:"
mdir -i ${IMG_NAME} ::

echo "完成!"
