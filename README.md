# RVDOS

RVDOS 是一个基于 RISC-V 架构的简易操作系统（当前 **Alpha-0.8.1**）。

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
  - `abi/` abi定义（含 capability profiles）
  - `arch/` riscv64封装
- `kernelspace/`: 内核源代码
  - `core/`: 内核核心组建
  - `drivers/`: 内核驱动相关
  - `fs/`: 文件系统相关
  - `include/`: 内核头文件
  - `lib/`: 内核依赖的工具
  - `entry.S`： 内核入口
- `userspace/`: 用户态库与程序
  - `syscontroller`（镜像内文件名 `sysctl`，受 FAT 8.3 限制）: init
  - `shell`: 交互 shell
  - `include/`: 用户头文件
- `fs.img`: FAT32 格式的磁盘镜像
- `mkfs.sh` :  文件系统构建脚本

## 0.8.1 沙盒与能力模型

启动链：

```text
kernel → sysctl/syscontroller (CAP_PROFILE_SYSCONTROLLER)
       → shell          (normal: SHELL_NORMAL / maint: SHELL_MAINT)
       → 用户程序       (默认 USER_DEFAULT ∩ 父进程)
```

能力规则（始终 **child ⊆ parent**）：

| 调用 | 行为 |
|------|------|
| `spawn`（a2=0） | `child = parent ∩ CAP_PROFILE_USER_DEFAULT` |
| `spawn`（a2 含 `PROC_CAP_ENABLE`） | 需要 `CAP_PROC_SANDBOX`；`child = parent ∩ mask` |
| `sandbox`（SYS_SANDBOX） | 需要 `CAP_PROC_BASIC \| CAP_PROC_SANDBOX`；`child = parent ∩ mask` |

模式：

- **normal**（默认）：shell 无 `CAP_SYS_POWER`（`poweroff`/`reboot` 会被拒绝）
- **maint**：shell 额外拥有电源能力

切换 maint：修改内核 `userinit()` 中 `spawn("sysctl", "maint", ...)`，或后续由配置扩展。

注意：根目录入口文件必须是 **8.3 短名**（当前 FS 无 LFN）；`syscontroller` 在盘上名为 `sysctl`。

Shell 对 `ps` / `kill` / `trace` / `sandbox` / `poweroff` 等特权工具会用 `sandbox()` 在自身子集内抬权；普通命令走默认 `spawn`。

内建命令 `caps` 可查看当前 shell 能力。

## TODO
- `下一个大版本` 实现用户与权限，fat32加入用户-文件权限，用户使用cap定义权限能力，root特别判断
- `潜在问题` 由于只是使用重分配pid，在进程数量爆炸等情况下容易炸，比如pid大概率在极端情况下出现A进程退出，B进程马上分配pid，但是A没有释放干净，内核认为是一个进程的情况
