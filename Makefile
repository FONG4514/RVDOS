# --- 工具链配置 ---
CC      = riscv64-linux-gnu-gcc
AS      = riscv64-linux-gnu-gcc
LD      = riscv64-linux-gnu-ld
OBJCOPY = riscv64-linux-gnu-objcopy
OBJDUMP = riscv64-linux-gnu-objdump

# 自动检测调试器: 优先使用 multiarch，其次是 linux-gnu
GDB = $(shell which riscv64-elf-gdb || which gdb)
# --- 编译选项 ---
CFLAGS = -Wall -Werror -O0 -fno-omit-frame-pointer -ggdb
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += -march=rv64gc_zihintpause -mabi=lp64
CFLAGS += -fno-stack-protector
CFLAGS += -fno-pie -no-pie

LDFLAGS = -z max-page-size=4096

# --- 目录与文件 ---
K = kernelspace
OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/pagetable.o \
  $K/main.o \
  $K/process.o \
  $K/lock.o \
  $K/intr.o \
  $K/trapvec.o \
  $K/trap.o \
  $K/fs.o \
  $K/panic.o \
  $K/mylibc.o \

# --- 构建规则 ---
all: kernel.elf kernel.asm

$K/%.o: $K/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$K/%.o: $K/%.S
	$(AS) $(CFLAGS) -c -o $@ $<

kernel.elf: $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o kernel.elf $(OBJS)

# 生成反汇编文件，方便你根据 sepc 找代码行
kernel.asm: kernel.elf
	$(OBJDUMP) -S kernel.elf > kernel.asm

clean:
	rm -f $K/*.o kernel.elf kernel.asm

# --- 运行与调试 ---

QEMU_OPTS = -machine virt -bios none -kernel kernel.elf -m 128M -smp 2 \
            -nographic -serial mon:stdio \
            -drive file=fs.img,if=none,format=raw,id=x0 \
            -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 
# 正常运行
run: kernel.elf
	qemu-system-riscv64 $(QEMU_OPTS)

# 带超时运行 (10秒)
run_timeout: kernel.elf
	timeout 10s qemu-system-riscv64 $(QEMU_OPTS) || true

# 启动 QEMU 并等待 GDB 连接 (监听端口 1234)
debug_s: kernel.elf
	qemu-system-riscv64 $(QEMU_OPTS) -S -gdb tcp::1234

# 启动 GDB 调试器
debug_c: kernel.elf
	$(GDB) kernel.elf -ex "target remote :1234"
