# uname 命令

## 命令概述

`uname` 命令用于打印系统信息。没有选项时，默认行为等同于 `-s`，打印内核名称。

**命令格式：**
```
uname [选项]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--all` | 打印所有信息 |
| `-s` | `--kernel-name` | 打印内核名称（默认） |
| `-n` | `--nodename` | 打印网络节点主机名 |
| `-r` | `--kernel-release` | 打印内核发行版号 |
| `-v` | `--kernel-version` | 打印内核版本 |
| `-m` | `--machine` | 打印机器硬件名称 |
| `-p` | `--processor` | 打印处理器类型 |
| `-i` | `--hardware-platform` | 打印硬件平台 |
| `-o` | `--operating-system` | 打印操作系统名称 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 输出版本信息并退出 |

## 参数详细说明

### `-a, --all`
打印所有可用的系统信息，等同于同时指定 `-s -n -r -v -m -p -i -o`。

### `-s, --kernel-name`
打印内核名称。这是默认行为，当没有指定任何选项时使用。

### `-n, --nodename`
打印网络节点主机名。在 Windows 上对应计算机名称。

### `-r, --kernel-release`
打印内核发行版号。在 WinuxCmd 中返回 Windows 版本号（如 `10.0`）。

### `-v, --kernel-version`
打印内核版本。在 WinuxCmd 中返回 Windows 构建号（如 `19045`）。

### `-m, --machine`
打印机器硬件名称。返回处理器架构信息：
- `x86_64` - 64位 x86
- `i386` - 32位 x86
- `aarch64` - 64位 ARM
- `arm` - 32位 ARM

### `-p, --processor`
打印处理器类型。在某些系统上可能返回 `unknown`。

### `-i, --hardware-platform`
打印硬件平台。在 WinuxCmd 中返回 `PC`。

### `-o, --operating-system`
打印操作系统名称。在 WinuxCmd 中返回 `Windows_NT`。

## 输出示例

### 完整输出 (-a)
```
MSWindows_NT hostname 10.0 19045 x86_64 unknown PC Windows_NT
```

### 各字段说明
| 字段 | 示例值 | 说明 |
|------|--------|------|
| 内核名称 | `MSWindows_NT` | Windows NT 内核 |
| 主机名 | `DESKTOP-ABC123` | 计算机名称 |
| 内核发行版 | `10.0` | Windows 版本 |
| 内核版本 | `19045` | Windows 构建号 |
| 机器架构 | `x86_64` | 处理器架构 |
| 处理器类型 | `unknown` | 处理器详细类型 |
| 硬件平台 | `PC` | 硬件平台类型 |
| 操作系统 | `Windows_NT` | 操作系统名称 |

## 使用示例

1. **打印内核名称（默认）**
   ```bash
   uname
   ```
   输出：`MSWindows_NT`

2. **打印所有系统信息**
   ```bash
   uname -a
   ```

3. **打印机器架构**
   ```bash
   uname -m
   ```
   输出：`x86_64`

4. **打印主机名**
   ```bash
   uname -n
   ```

5. **打印内核发行版**
   ```bash
   uname -r
   ```
   输出：`10.0`

6. **打印内核版本**
   ```bash
   uname -v
   ```
   输出：`19045`

7. **打印操作系统名称**
   ```bash
   uname -o
   ```
   输出：`Windows_NT`

8. **组合多个选项**
   ```bash
   uname -sn
   ```
   输出：`MSWindows_NT hostname`

9. **在脚本中检测操作系统**
   ```bash
   if [ "$(uname -o)" = "Windows_NT" ]; then
       echo "Running on Windows"
   fi
   ```

10. **检测架构用于条件编译**
    ```bash
    ARCH=$(uname -m)
    if [ "$ARCH" = "x86_64" ]; then
        # 64-bit specific code
    fi
    ```

## WinuxCmd 实现状态

**已实现**

支持的选项：
- `-a, --all` - 打印所有信息
- `-s, --kernel-name` - 打印内核名称
- `-n, --nodename` - 打印主机名
- `-r, --kernel-release` - 打印内核发行版
- `-v, --kernel-version` - 打印内核版本
- `-m, --machine` - 打印机器架构
- `-p, --processor` - 打印处理器类型
- `-i, --hardware-platform` - 打印硬件平台
- `-o, --operating-system` - 打印操作系统

实现说明：
- 通过 Windows API 获取系统信息
- 主机名通过 `GetComputerNameW` 获取
- 架构信息通过 `GetNativeSystemInfo` 获取
- 默认行为与 GNU 版本一致（无选项时显示内核名称）
