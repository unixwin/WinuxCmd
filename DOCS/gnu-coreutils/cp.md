# cp 命令

## 命令概述

`cp` 命令用于复制文件和目录。它可以将一个或多个源文件复制到目标文件或目录中。

**命令格式：**
```
cp [选项]... [-T] 源文件 目标文件
cp [选项]... 源文件... 目录
cp [选项]... -t 目录 源文件...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--archive` | 等价于 `-dR --preserve=all` |
| `-b` | | 类似 `--backup`，但不接受参数 |
| | `--backup` | 为每个已存在的目标文件创建备份 |
| `-d` | | 等价于 `--no-dereference --preserve=links` |
| `-f` | `--force` | 如果无法打开已存在的目标文件，删除它并重试 |
| `-i` | `--interactive` | 覆盖前提示用户确认 |
| `-H` | | 跟随命令行中的符号链接 |
| `-l` | `--link` | 创建硬链接而非复制文件 |
| `-L` | `--dereference` | 始终跟随源文件中的符号链接 |
| `-n` | `--no-clobber` | 不覆盖已存在的文件，也不报错 |
| `-P` | `--no-dereference` | 从不跟随源文件中的符号链接 |
| `-p` | | 等价于 `--preserve=mode,ownership,timestamps` |
| `-R` | `--recursive` | 递归复制目录 |
| `-r` | `--recursive` | 递归复制目录（同 `-R`） |
| `-s` | `--symbolic-link` | 创建符号链接而非复制文件 |
| `-S` | `--suffix` | 覆盖默认的备份后缀 |
| `-t` | `--target-directory` | 将所有源文件复制到指定目录 |
| `-T` | `--no-target-directory` | 将目标视为普通文件而非目录 |
| `-u` | `--update` | 仅在源文件比目标文件新或目标不存在时复制 |
| `-v` | `--verbose` | 显示详细操作信息 |
| `-x` | `--one-file-system` | 不跨越文件系统边界 |
| `-Z` | | 设置目标文件的 SELinux 安全上下文为默认类型 |
| | `--copy-contents` | 在递归复制时复制特殊文件的内容 |
| | `--dereference` | 跟随源文件中的符号链接 |
| | `--no-dereference` | 从不跟随源文件中的符号链接 |
| | `--no-preserve` | 不保留指定的属性 |
| | `--parents` | 在目标目录中使用完整的源文件路径 |
| | `--preserve` | 保留指定的属性 |
| | `--reflink` | 控制克隆/CoW 副本 |
| | `--remove-destination` | 在尝试打开前删除每个已存在的目标文件 |
| | `--sparse` | 控制稀疏文件的创建 |
| | `--strip-trailing-slashes` | 删除每个源文件参数末尾的斜杠 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --archive`
等价于 `-dR --preserve=all`，用于归档模式复制。保留所有文件属性，递归复制目录，不跟随符号链接但保留链接关系。

### `-b`
类似 `--backup` 选项，但不接受参数。使用默认的备份后缀 `~`。

### `--backup[=CONTROL]`
为每个已存在的目标文件创建备份。可选的 CONTROL 参数控制备份行为：
- `none` / `off`：从不创建备份
- `numbered` / `t`：创建编号备份
- `existing` / `nil`：如果编号备份存在则使用编号，否则使用简单备份
- `simple` / `never`：始终创建简单备份

### `-f, --force`
如果无法打开已存在的目标文件，先删除它再重试。此选项与 `-i` 互斥。

### `-i, --interactive`
在覆盖已存在的目标文件之前，提示用户确认。

### `-l, --link`
创建硬链接而非复制文件内容。

### `-L, --dereference`
始终跟随源文件中的符号链接。

### `-n, --no-clobber`
不覆盖已存在的文件，也不报错。此选项与 `--backup` 互斥。

### `-P, --no-dereference`
从不跟随源文件中的符号链接，直接复制符号链接本身。

### `-p`
保留文件的模式、所有者和时间戳。

### `-R, --recursive`
递归复制目录及其内容。`-r` 是同义选项。

### `-s, --symbolic-link`
创建符号链接而非复制文件。

### `-S, --suffix=后缀`
覆盖默认的备份后缀（默认为 `~`）。

### `-t, --target-directory=目录`
将所有源文件参数复制到指定的目录中。

### `-T, --no-target-directory`
将最后一个参数视为普通文件而非目录。

### `-u, --update`
仅在源文件比目标文件新或目标文件不存在时才复制。

### `-v, --verbose`
在复制每个文件前，显示正在执行的操作信息。

### `-x, --one-file-system`
不跨越文件系统边界进行复制。

## 使用示例

1. **复制单个文件**
   ```bash
   cp file1.txt file2.txt
   ```

2. **复制文件到目录**
   ```bash
   cp file.txt /home/user/
   ```

3. **递归复制目录**
   ```bash
   cp -r source_dir/ dest_dir/
   ```

4. **交互式复制（覆盖前提示）**
   ```bash
   cp -i file.txt dest.txt
   ```

5. **保留文件属性复制**
   ```bash
   cp -p file.txt dest.txt
   ```

6. **归档模式复制（保留所有属性）**
   ```bash
   cp -a source_dir/ dest_dir/
   ```

7. **仅在源文件较新时更新**
   ```bash
   cp -u source.txt dest.txt
   ```

8. **显示详细复制信息**
   ```bash
   cp -v file1.txt file2.txt
   ```

9. **复制多个文件到目录**
   ```bash
   cp file1.txt file2.txt file3.txt /destination/
   ```

10. **使用目标目录选项**
    ```bash
    cp -t /destination/ file1.txt file2.txt
    ```

## WinuxCmd 实现状态

**已实现** - 核心选项已实现，包括 `-a`、`-b`、`-d`、`-f`、`-i`、`-H`、`-l`、`-L`、`-n`、`-P`、`-p`、`-R`/`-r`、`-s`、`-S`、`-t`、`-T`、`-u`、`-v`、`-x`、`-Z` 选项以及 `--archive`、`--backup`、`--force`、`--interactive`、`--link`、`--dereference`、`--no-clobber`、`--no-dereference`、`--recursive`、`--symbolic-link`、`--suffix`、`--target-directory`、`--no-target-directory`、`--update`、`--verbose`、`--one-file-system` 长选项。
