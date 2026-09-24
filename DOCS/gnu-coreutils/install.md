# install 命令

## 命令概述

`install` 命令用于复制文件并设置属性。它通常用于在安装程序时复制可执行文件、创建目录和设置权限。

**命令格式：**
```
install [选项]... [-T] 源文件 目标文件
install [选项]... 源文件... 目录
install [选项]... -t 目录 源文件...
install [选项]... -d 目录...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-b` | `--backup` | 为已存在的目标文件创建备份 |
| `-c` | | 忽略（兼容旧版 Unix） |
| `-C` | `--compare` | 比较源文件和目标文件，如果相同则跳过复制 |
| `-d` | `--directory` | 将所有参数视为目录名并创建 |
| `-D` | | 创建目标文件的所有上级目录 |
| `-g` | `--group` | 设置文件所属组 |
| `-m` | `--mode` | 设置文件权限模式 |
| `-o` | `--owner` | 设置文件所有者 |
| `-p` | `--preserve-timestamps` | 保留源文件的访问/修改时间 |
| `-s` | `--strip` | 剥离符号表 |
| | `--debug` | 打印调试信息 |
| | `--strip-program` | 用于剥离二进制文件的程序 |
| `-S` | `--suffix` | 覆盖默认的备份后缀 |
| `-t` | `--target-directory` | 指定目标目录 |
| `-T` | `--no-target-directory` | 不将最后一个操作数特殊视为目录 |
| `-v` | `--verbose` | 创建目录时显示每个目录的名称 |
| | `--preserve-context` | 保留 SELinux 安全上下文 |
| `-Z` | | 设置目标文件的 SELinux 安全上下文为默认类型 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-b, --backup`
为已存在的目标文件创建备份。备份文件名在原文件名后添加备份后缀。

### `-c`
忽略选项，仅为兼容旧版 Unix 版本。

### `-C, --compare`
比较源文件和目标文件。如果两者内容相同，则跳过复制操作，减少不必要的磁盘写入。

### `-d, --directory`
将所有参数视为目录名并创建它们。类似 `mkdir -p`，会创建所有必要的上级目录。

### `-D`
创建目标文件路径中所有不存在的上级目录。仅在复制单个文件时有效。

### `-g, --group=组名`
设置安装文件的所属组。在 Windows 上此选项有限制。

### `-m, --mode=模式`
设置安装文件的权限模式（如 `755`、`644`）。在 Windows 上此选项有限制。

### `-o, --owner=用户名`
设置安装文件的所有者。在 Windows 上此选项有限制。

### `-p, --preserve-timestamps`
将源文件的访问时间和修改时间应用到目标文件。

### `-s, --strip`
剥离安装文件的符号表信息（适用于二进制文件）。

### `--debug`
打印详细的调试信息，包括正在执行的操作。

### `--strip-program=程序`
指定用于剥离二进制文件的程序（默认为 `strip`）。

### `-S, --suffix=后缀`
覆盖默认的备份后缀（默认为 `~`）。

### `-t, --target-directory=目录`
将所有源文件安装到指定的目标目录中。

### `-T, --no-target-directory`
将最后一个参数视为普通文件而非目录。

### `-v, --verbose`
显示正在安装的每个文件或创建的每个目录的名称。

### `--preserve-context`
保留文件的 SELinux 安全上下文。

### `-Z`
设置目标文件的 SELinux 安全上下文为默认类型。

## 使用示例

1. **安装单个文件到目标**
   ```bash
   install source.txt dest.txt
   ```

2. **创建目录结构**
   ```bash
   install -d /usr/local/bin /usr/local/lib
   ```

3. **安装并创建上级目录**
   ```bash
   install -D config.txt /etc/myapp/config.txt
   ```

4. **带备份的安装**
   ```bash
   install -b program.exe /usr/local/bin/
   ```

5. **比较后安装（跳过相同文件）**
   ```bash
   install -C source.txt dest.txt
   ```

6. **保留时间戳安装**
   ```bash
   install -p source.txt dest.txt
   ```

7. **安装多个文件到目录**
   ```bash
   install -v file1.txt file2.txt /destination/
   ```

8. **使用目标目录选项**
   ```bash
   install -t /usr/local/bin/ myprogram
   ```

## WinuxCmd 实现状态

**已实现** - 支持 `-b`/`--backup`、`-c`、`-C`/`--compare`、`-d`/`--directory`、`-D`、`-g`/`--group`、`-m`/`--mode`、`-o`/`--owner`、`-p`/`--preserve-timestamps`、`-s`/`--strip`、`--debug`、`--strip-program`、`-S`/`--suffix`、`-t`/`--target-directory`、`-T`/`--no-target-directory`、`-v`/`--verbose`、`--preserve-context`、`-Z` 选项。支持文件比较跳过、时间戳保留、备份创建等核心功能。
