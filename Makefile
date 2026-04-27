# --- 工具链配置 ---
CC      = riscv64-linux-gnu-gcc
AS      = riscv64-linux-gnu-gcc
LD      = riscv64-linux-gnu-ld
OBJCOPY = riscv64-linux-gnu-objcopy
OBJDUMP = riscv64-linux-gnu-objdump

GDB = $(shell which riscv64-elf-gdb || which gdb)

# --- 编译选项 ---
CFLAGS = -Wall -Werror -O0 -fno-omit-frame-pointer -ggdb
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -march=rv64gc_zihintpause -mabi=lp64
CFLAGS += -fno-stack-protector
CFLAGS += -fno-pie -no-pie

# 头文件路径（你这次拆分是正确的）
CFLAGS += -Iinclude -Ikernelspace/include

LDFLAGS = -z max-page-size=4096

# --- 目录 ---
K = kernelspace

# --- 自动收集源码 ---
C_SRCS := $(shell find $(K) -name "*.c")
S_SRCS := $(shell find $(K) -name "*.S")

OBJS := $(C_SRCS:.c=.o) $(S_SRCS:.S=.o)

# --- 构建 ---
all: kernel.elf kernel.asm

# --- 编译规则 ---
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(CFLAGS) -c $< -o $@

# --- 链接 ---
kernel.elf: $(OBJS) $(K)/kernel.ld
	$(LD) $(LDFLAGS) -T $(K)/kernel.ld -o $@ $(OBJS)

kernel.asm: kernel.elf
	$(OBJDUMP) -S kernel.elf > $@

# --- 清理 ---
clean:
	find $(K) -name "*.o" -delete
	rm -f kernel.elf kernel.asm

# --- 运行 ---
QEMU_OPTS = -machine virt -bios none -kernel kernel.elf -m 128M -smp 2 \
            -nographic -serial mon:stdio \
            -drive file=fs.img,if=none,format=raw,id=x0 \
            -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 

run: kernel.elf
	qemu-system-riscv64 $(QEMU_OPTS)

debug_s: kernel.elf
	qemu-system-riscv64 $(QEMU_OPTS) -S -gdb tcp::1234

debug_c: kernel.elf
	$(GDB) kernel.elf -ex "target remote :1234"