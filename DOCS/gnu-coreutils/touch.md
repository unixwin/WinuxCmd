# touch 命令

## 命令概述

`touch` 命令用于更改文件时间戳。如果文件不存在，除非使用 `-c` 选项，否则会创建一个空文件。

**命令格式：**
```
touch [选项]... FILE...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | | 仅更改访问时间 |
| `-c` | `--no-create` | 不创建任何文件 |
| `-d` | `--date=STRING` | 使用 STRING 而非当前时间 |
| `-f` | | 忽略（用于 BSD 兼容性） |
| `-h` | `--no-dereference` | 影响符号链接而非引用的文件 |
| `-m` | | 仅更改修改时间 |
| `-r` | `--reference=FILE` | 使用此文件的时间 |
| `-t` | `--timestamp=[[CC]YY]MMDDhhmm[.ss]` | 使用指定时间戳 |
| | `--time=WORD` | 使用 WORD 指定的时间：access, atime, use, modify, mtime |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a`
仅更改文件的访问时间。

### `-c, --no-create`
不创建任何文件。如果文件不存在，不创建它。

### `-d, --date=STRING`
使用 STRING 作为时间而非当前时间。STRING 可以是各种格式，如 "2023-01-15 12:30:00" 或 "2 days ago"。

### `-f`
忽略，用于 BSD 兼容性。

### `-h, --no-dereference`
影响符号链接本身而非引用的文件。在不支持更改符号链接时间戳的系统上无效。

### `-m`
仅更改文件的修改时间。

### `-r, --reference=FILE`
使用 FILE 的时间戳而非当前时间。

### `-t, --timestamp=[[CC]YY]MMDDhhmm[.ss]`
使用指定的时间戳：
- CC：世纪（可选）
- YY：年
- MM：月（01-12）
- DD：日（01-31）
- hh：小时（00-23）
- mm：分钟（00-59）
- ss：秒（00-59，可选）

### `--time=WORD`
指定要更改的时间：
- `access, atime, use`：访问时间
- `modify, mtime`：修改时间

## 使用示例

1. **创建空文件**
   ```bash
   touch newfile.txt
   ```

2. **更新文件时间戳为当前时间**
   ```bash
   touch existing_file.txt
   ```

3. **仅更新访问时间**
   ```bash
   touch -a file.txt
   ```

4. **仅更新修改时间**
   ```bash
   touch -m file.txt
   ```

5. **不创建不存在的文件**
   ```bash
   touch -c non_existent_file.txt
   ```

6. **使用指定日期**
   ```bash
   touch -d "2023-01-15 12:30:00" file.txt
   ```

7. **使用参考文件的时间**
   ```bash
   touch -r reference.txt target.txt
   ```

8. **使用时间戳**
   ```bash
   touch -t 202301151230.45 file.txt
   ```

9. **创建多个文件**
   ```bash
   touch file1.txt file2.txt file3.txt
   ```

10. **使用相对日期**
    ```bash
    touch -d "2 days ago" file.txt
    ```

## WinuxCmd 实现状态

**已实现**