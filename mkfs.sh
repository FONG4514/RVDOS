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
${CC} ${CFLAGS} -c ${USER_DIR}/panic.c -o ${BUILD_DIR}/panic.o
${CC} ${CFLAGS} -c ${USER_DIR}/ls.c -o ${BUILD_DIR}/ls.o
${CC} ${CFLAGS} -c ${USER_DIR}/bench.c -o ${BUILD_DIR}/bench.o
${CC} ${CFLAGS} -c ${USER_DIR}/mkdir.c -o ${BUILD_DIR}/mkdir.o
${CC} ${CFLAGS} -c ${USER_DIR}/rm.c -o ${BUILD_DIR}/rm.o

# 链接：必须包含 rvlibc.o 才能使用 wait_process 等新函数
# 链接 shell 程序
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
    ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/shell.o \
    -o ${BUILD_DIR}/shell.elf

# 链接 panic 程序
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
    ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/panic.o \
    -o ${BUILD_DIR}/panic.elf

# 链接 ls 程序
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
    ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/ls.o \
    -o ${BUILD_DIR}/ls.elf

# 链接 bench 程序
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
    ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/bench.o \
    -o ${BUILD_DIR}/bench.elf

# 链接 mkdir 程序
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
    ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/mkdir.o \
    -o ${BUILD_DIR}/mkdir.elf

# 链接 rm 程序
${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
    ${BUILD_DIR}/rvlibc.o ${BUILD_DIR}/rm.o \
    -o ${BUILD_DIR}/rm.elf
    
# 4. 将文件塞入镜像
echo "正在将文件存入镜像..."
# 创建一个测试文件供 Demo 使用
echo "Hello from RVDOS File System!" > ${BUILD_DIR}/README.TXT
echo "Persistence Test File - Overwrite me!" > ${BUILD_DIR}/TEST.TXT

mcopy -i ${IMG_NAME} ${BUILD_DIR}/shell.elf ::/shell
mcopy -i ${IMG_NAME} ${BUILD_DIR}/panic.elf ::/panic
mcopy -i ${IMG_NAME} ${BUILD_DIR}/ls.elf ::/ls
mcopy -i ${IMG_NAME} ${BUILD_DIR}/bench.elf ::/bench
mcopy -i ${IMG_NAME} ${BUILD_DIR}/mkdir.elf ::/mkdir
mcopy -i ${IMG_NAME} ${BUILD_DIR}/rm.elf ::/rm
mcopy -i ${IMG_NAME} ${BUILD_DIR}/README.TXT ::/README.TXT
mcopy -i ${IMG_NAME} ${BUILD_DIR}/TEST.TXT ::/TEST.TXT

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
