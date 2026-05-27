# ln 命令

## 命令概述

`ln` 命令用于创建链接（硬链接或符号链接）。默认创建硬链接，使用 `-s` 选项创建符号链接。

**命令格式：**
```
ln [选项]... [-T] TARGET LINK_NAME
ln [选项]... TARGET
ln [选项]... TARGET... DIRECTORY
ln [选项]... -t DIRECTORY TARGET...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-b` | `--backup[=CONTROL]` | 为每个已存在的目标文件创建备份 |
| `-d` | `-F, --directory` | 允许超级用户尝试硬链接目录（不推荐） |
| `-f` | `--force` | 删除已存在的目标文件 |
| `-i` | `--interactive` | 覆盖前询问 |
| `-L` | `--logical` | 引用符号链接的目标 |
| `-n` | `--no-dereference` | 将指向目录的符号链接视为普通文件 |
| `-P` | `--physical` | 直接引用符号链接本身 |
| `-r` | `--relative` | 创建相对于链接位置的符号链接 |
| `-s` | `--symbolic` | 创建符号链接而非硬链接 |
| `-S` | `--suffix=SUFFIX` | 覆盖通常的备份后缀 |
| `-t` | `--target-directory=DIRECTORY` | 指定目标目录 |
| `-T` | `--no-target-directory` | 将 LINK_NAME 视为普通文件 |
| `-v` | `--verbose` | 打印每个已链接文件的名称 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-b, --backup[=CONTROL]`
为每个已存在的目标文件创建备份。可选的 CONTROL 参数指定备份方法：
- `none, off`：从不创建备份
- `numbered, t`：创建编号备份
- `existing, nil`：如果存在编号备份则编号，否则简单备份
- `simple, never`：总是创建简单备份

### `-d, -F, --directory`
允许超级用户尝试创建指向目录的硬链接。仅超级用户有权使用此选项，且可能因系统限制而失败。

### `-f, --force`
如果目标文件已存在，删除它然后创建链接。

### `-i, --interactive`
如果目标文件已存在，提示用户是否覆盖。

### `-L, --logical`
当目标是符号链接时，引用该符号链接的目标而非符号链接本身。

### `-n, --no-dereference`
如果目标是符号链接且指向目录，将其视为普通文件处理。

### `-P, --physical`
直接引用符号链接本身，而非其目标。

### `-r, --relative`
创建相对于链接文件位置的符号链接。

### `-s, --symbolic`
创建符号链接（软链接）而非硬链接。

### `-S, --suffix=SUFFIX`
覆盖通常的备份后缀（默认为 `~`）。

### `-t, --target-directory=DIRECTORY`
指定要在其中创建链接的目录。

### `-T, --no-target-directory`
将 LINK_NAME 视为普通文件（而非目录）。

### `-v, --verbose`
打印每个已链接文件的名称。

## 使用示例

1. **创建硬链接**
   ```bash
   ln file.txt hardlink.txt
   ```

2. **创建符号链接**
   ```bash
   ln -s file.txt symlink.txt
   ```

3. **创建指向目录的符号链接**
   ```bash
   ln -s /path/to/directory link_name
   ```

4. **覆盖已存在的文件**
   ```bash
   ln -sf file.txt existing_link
   ```

5. **在目标目录中创建链接**
   ```bash
   ln -s -t /path/to/target_dir file.txt
   ```

6. **创建相对路径的符号链接**
   ```bash
   ln -sr /path/to/file.txt ./link.txt
   ```

7. **创建带编号备份的链接**
   ```bash
   ln -b --numbered file.txt existing_file
   ```

8. **显示详细操作信息**
   ```bash
   ln -sv file.txt link.txt
   ```

## WinuxCmd 实现状态

**待实现**