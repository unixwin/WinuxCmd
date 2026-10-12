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

### 平台差异（Windows 特有，issue #1143）

GNU glob 语义下，未匹配的模式保持字面量，因此 `touch 'x*x'` 在 GNU/Linux 上会创建名为 `x*x` 的文件并返回退出码 0。NTFS/Win32 文件名禁止 `*`、`?` 等字符，这样的文件在本平台上不可能存在，属本质平台差异，无法对齐：

- `touch 'x*x'`：创建失败，报错并返回退出码 1（GNU 为 0）。
- `touch -c 'x*x'`：与 GNU 一致——不可能存在的名字视同缺失文件，静默跳过并返回退出码 0（issue #1143 修复项）。
- 派生行为（如 `ls 'x*x'`）：按文件缺失处理返回 2；与 GNU 的差异仅源于 GNU 侧 touch 成功创建了该文件。

单元测试 `touch_unmatched_glob_literal_reports_ntfs_error` 在所有平台断言上述 Windows 行为并注明理由，构成对该差异的测试豁免（不按 GNU 行为断言）。