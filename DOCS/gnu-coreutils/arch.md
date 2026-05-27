# arch 命令

## 命令概述

`arch` 命令用于打印机器的硬件名称。它是 `uname -m` 的简写形式，等价于执行 `uname -m`。

**命令格式：**
```
arch [选项]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 输出版本信息并退出 |

## 参数详细说明

`arch` 命令没有特定的操作参数。它总是输出当前系统的机器硬件架构名称。

### 输出值说明

| 输出值 | 说明 |
|--------|------|
| `x86_64` | 64位 x86 架构 (AMD64/Intel 64) |
| `i386` | 32位 x86 架构 |
| `i686` | 32位 x86 架构 (Pentium Pro+) |
| `aarch64` | 64位 ARM 架构 (ARM64) |
| `arm` | 32位 ARM 架构 |
| `ia64` | Intel Itanium 架构 |
| `ppc` | PowerPC 32位 |
| `ppc64` | PowerPC 64位 |
| `ppc64le` | PowerPC 64位小端 |
| `s390` | IBM System/390 |
| `s390x` | IBM z/Architecture |
| `riscv64` | RISC-V 64位 |
| `mips` | MIPS 32位 |
| `mips64` | MIPS 64位 |
| `sparc` | SPARC 32位 |
| `sparc64` | SPARC 64位 |
| `alpha` | Alpha |
| `hppa` | HP PA-RISC |
| `sh4` | SuperH |

## 使用示例

1. **显示机器架构**
   ```bash
   arch
   ```
   输出示例：`x86_64`

2. **等价的 uname 命令**
   ```bash
   uname -m
   ```

3. **在脚本中使用 arch 检测架构**
   ```bash
   if [ "$(arch)" = "x86_64" ]; then
       echo "64-bit system detected"
   else
       echo "32-bit or other architecture"
   fi
   ```

4. **查看架构用于下载正确的软件包**
   ```bash
   wget "https://example.com/software-$(arch).tar.gz"
   ```

5. **显示帮助信息**
   ```bash
   arch --help
   ```

6. **显示版本信息**
   ```bash
   arch --version
   ```

## WinuxCmd 实现状态

**已实现**

WinuxCmd 的 `arch` 命令通过 Windows API (`GetSystemInfo`) 获取处理器架构信息，并映射为标准的 GNU/Linux 架构名称。

支持的输出架构：
- `x86_64` - AMD64/Intel 64 (PROCESSOR_ARCHITECTURE_AMD64)
- `i386` - Intel x86 (PROCESSOR_ARCHITECTURE_INTEL)
- `arm` - ARM 32位 (PROCESSOR_ARCHITECTURE_ARM)
- `aarch64` - ARM 64位 (PROCESSOR_ARCHITECTURE_ARM64)
- `ia64` - Intel Itanium (PROCESSOR_ARCHITECTURE_IA64)
