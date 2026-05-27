# pwd - 打印当前工作目录名称

## 命令概述

`pwd`（print working directory）命令用于显示当前工作目录的完整绝对路径。这是一个简单但常用的命令，用于确认用户在文件系统中的位置。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| `-L` | `--logical` | 显示逻辑路径（包含符号链接） |
| `-P` | `--physical` | 显示物理路径（解析所有符号链接） |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-L, --logical`
显示当前工作目录的逻辑路径，不解析符号链接。这是默认行为。如果当前目录包含符号链接，`pwd -L` 将显示包含符号链接的路径。

### `-P, --physical`
显示当前工作目录的物理路径，解析所有符号链接。这将显示实际的物理目录路径，而不是符号链接路径。

### `--help`
显示 `pwd` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `pwd` 命令的版本信息，然后退出。

## 使用示例

### 基本用法
```bash
$ pwd
/home/user/documents
```

### 显示逻辑路径（默认）
```bash
$ cd /var/www/html
$ pwd -L
/var/www/html
```

### 显示物理路径（解析符号链接）
```bash
$ cd /var/www/html
$ pwd -P
/var/www/html
```

### 在脚本中使用
```bash
#!/bin/bash
current_dir=$(pwd)
echo "当前工作目录: $current_dir"
```

### 结合其他命令
```bash
# 将当前目录添加到 PATH
export PATH="$PATH:$(pwd)"

# 在当前目录查找文件
find "$(pwd)" -name "*.txt"
```

## WinuxCmd 实现状态

**已实现** - `pwd` 命令已在 WinuxCmd 中实现，支持基本的路径显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*