# mv 命令

## 命令概述

`mv` 命令用于移动（重命名）文件和目录。它可以将源文件移动到目标位置，或将多个源文件移动到目标目录中。

**命令格式：**
```
mv [选项]... [-T] 源文件 目标文件
mv [选项]... 源文件... 目录
mv [选项]... -t 目录 源文件...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-b` | | 类似 `--backup`，但不接受参数 |
| `-f` | `--force` | 覆盖前不提示确认 |
| `-i` | | 覆盖前提示用户确认 |
| `-n` | `--no-clobber` | 不覆盖已存在的文件 |
| | `--strip-trailing-slashes` | 删除每个源文件参数末尾的斜杠 |
| `-S` | `--suffix` | 覆盖默认的备份后缀 |
| `-t` | `--target-directory` | 将所有源文件移动到指定目录 |
| `-T` | `--no-target-directory` | 将目标视为普通文件而非目录 |
| `-u` | `--update` | 仅在源文件比目标文件新或目标不存在时移动 |
| `-v` | `--verbose` | 显示详细操作信息 |
| `-Z` | `--context` | 设置目标文件的 SELinux 安全上下文为默认类型 |
| | `--backup[=CONTROL]` | 为已存在的目标文件创建备份 |
| | `--force` | 覆盖前不提示确认 |
| | `--interactive` | 根据 WHEN 参数提示：`never`、`once`(`-I`) 或 `always`(`-i`) |
| | `--no-clobber` | 不覆盖已存在的文件 |
| | `--suffix=后缀` | 覆盖默认的备份后缀 |
| | `--target-directory=目录` | 将所有源文件移动到指定目录 |
| | `--no-target-directory` | 将目标视为普通文件而非目录 |
| | `--update` | 仅在源文件比目标文件新或目标不存在时移动 |
| | `--verbose` | 显示详细操作信息 |
| | `--context` | 设置目标文件的 SELinux 安全上下文为默认类型 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-b`
类似 `--backup` 选项，但不接受参数。使用默认的备份后缀 `~`。

### `-f, --force`
如果目标文件已存在，直接覆盖而不提示用户确认。此选项与 `-i` 互斥。

### `-i`
在覆盖已存在的目标文件之前，提示用户确认。此选项与 `-f` 互斥。

### `-n, --no-clobber`
不覆盖已存在的文件。如果目标文件已存在，则跳过该操作，不报错。

### `--strip-trailing-slashes`
删除每个源文件参数末尾的斜杠字符。在处理目录路径时很有用。

### `-S, --suffix=后缀`
覆盖默认的备份后缀（默认为 `~`）。与 `-b` 或 `--backup` 一起使用。

### `-t, --target-directory=目录`
将所有源文件参数移动到指定的目标目录中。此选项与 `-T` 互斥。

### `-T, --no-target-directory`
将最后一个参数视为普通文件而非目录。此选项与 `-t` 互斥。

### `-u, --update`
仅在以下情况移动文件：
- 源文件比目标文件新
- 目标文件不存在

### `-v, --verbose`
在移动每个文件前，显示正在执行的操作信息。

### `-Z, --context`
设置目标文件的 SELinux 安全上下文为默认类型。

## 使用示例

1. **重命名文件**
   ```bash
   mv oldname.txt newname.txt
   ```

2. **移动文件到目录**
   ```bash
   mv file.txt /home/user/
   ```

3. **移动多个文件到目录**
   ```bash
   mv file1.txt file2.txt file3.txt /destination/
   ```

4. **交互式移动（覆盖前提示）**
   ```bash
   mv -i file.txt /existing/dir/
   ```

5. **不覆盖已存在文件**
   ```bash
   mv -n file.txt /destination/
   ```

6. **强制覆盖（不提示）**
   ```bash
   mv -f file.txt /destination/
   ```

7. **仅更新较新的文件**
   ```bash
   mv -u source.txt /destination/
   ```

8. **显示详细移动信息**
   ```bash
   mv -v file1.txt file2.txt /destination/
   ```

9. **使用目标目录选项**
   ```bash
   mv -t /destination/ file1.txt file2.txt
   ```

10. **带备份的移动**
    ```bash
    mv -b file.txt /existing/dir/
    ```

## WinuxCmd 实现状态

**已实现** - 支持 `-b`、`-f`/`--force`、`-i`、`-n`/`--no-clobber`、`--strip-trailing-slashes`、`-S`/`--suffix`、`-t`/`--target-directory`、`-T`/`--no-target-directory`、`-u`/`--update`、`-v`/`--verbose`、`-Z`/`--context`、`--backup`、`--interactive` 选项。支持文件重命名、移动到目录、交互确认、不覆盖、仅更新等核心功能。
