#!/bin/bash

# 配置
IMG_NAME="fs.img"
IMG_SIZE_MB=64
USER_DIR="userspace"
LIB_DIR="userspace/include"
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
mkfs.fat -F 32 -S 512 ${IMG_NAME} > /dev/null

# 创建 /usr 目录
mmd -i ${IMG_NAME} ::/usr

# 编译用户态程序
echo "正在编译用户态程序..."
mkdir -p ${BUILD_DIR}

CC="riscv64-linux-gnu-gcc"
CFLAGS="-Wall -O0 -ffreestanding -nostdlib -fno-common -mcmodel=medany -mno-relax -march=rv64gc -mabi=lp64 -I. -Iuserspace/include -Iinclude"

# 编译库文件
echo "编译用户态基础库..."
LIBS=("crt0" "syscall" "stdio" "stdlib" "string")
LIB_OBJS=""
for LIB in "${LIBS[@]}"; do
    ${CC} ${CFLAGS} -c ${LIB_DIR}/${LIB}.c -o ${BUILD_DIR}/${LIB}.o
    if [ "$LIB" != "crt0" ]; then
        LIB_OBJS="${LIB_OBJS} ${BUILD_DIR}/${LIB}.o"
    fi
done

# 定义所有要编译的程序
PROGRAMS=("shell" "panic" "ls" "bench" "mkdir" "rm" "mv" "cat" "echo" "clear" "poweroff" "reboot" "ps" "loop" "sysinfo" "kill" "sleep" "trace" "sandbox")

for PROG in "${PROGRAMS[@]}"; do
    echo "编译 ${PROG}..."
    ${CC} ${CFLAGS} -c ${USER_DIR}/${PROG}.c -o ${BUILD_DIR}/${PROG}.o
    # 链接时 crt0.o 放在最前面
    ${CC} ${CFLAGS} -T ${USER_DIR}/user.ld -nostartfiles \
        ${BUILD_DIR}/crt0.o ${BUILD_DIR}/${PROG}.o ${LIB_OBJS} \
        -o ${BUILD_DIR}/${PROG}.elf
done

# 4. 将文件塞入镜像
echo "正在将文件存入镜像..."
# 创建一个测试文件供 Demo 使用
echo "Hello from RVDOS File System!" > ${BUILD_DIR}/README.TXT
echo "Persistence Test File - Overwrite me!" > ${BUILD_DIR}/TEST.TXT

# shell 放在根目录，作为入口
mcopy -i ${IMG_NAME} ${BUILD_DIR}/shell.elf ::/shell

# 其他工具放在 /usr/
for PROG in "${PROGRAMS[@]}"; do
    if [ "$PROG" != "shell" ]; then
        mcopy -i ${IMG_NAME} ${BUILD_DIR}/${PROG}.elf ::/usr/${PROG}
    fi
done

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
mdir -i ${IMG_NAME} -/ ::

echo "完成!"
