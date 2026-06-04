# basename 命令

## 命令概述

`basename` 命令用于从完整的文件路径中提取文件名部分。它可以去除目录前缀和可选的文件后缀。

**命令格式：**
```
basename 名称 [后缀]
basename 选项... 名称...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--multiple` | 支持多个参数，每个参数都被当作名称处理 |
| `-s 后缀` | `--suffix=后缀` | 移除指定的后缀 |
| `-z` | `--zero` | 使用 NUL 字符（`\0`）而不是换行符分隔输出 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --multiple`
支持多个名称参数。当使用此选项时，所有参数都被当作文件名处理，而不是将第二个参数当作后缀。

### `-s 后缀, --suffix=后缀`
移除文件名中指定的后缀。这等同于将后缀作为第二个参数传递（在非 `--multiple` 模式下）。

### `-z, --zero`
使用 NUL 字符（`\0`）而不是换行符来分隔输出。这在处理包含换行符的文件名时很有用。

### `--help`
显示 `basename` 命令的使用帮助，包括简要说明和可用选项列表，然后以状态 0 退出。

### `--version`
显示 `basename` 命令的版本信息，然后以状态 0 退出。

## 使用示例

1. **基本使用 - 提取文件名**
   ```bash
   basename /usr/bin/sort    # 输出: sort
   ```

2. **去除文件后缀**
   ```bash
   basename /home/user/file.txt .txt  # 输出: file
   ```

3. **去除文件扩展名**
   ```bash
   basename document.pdf .pdf  # 输出: document
   ```

4. **使用 -s 选项去除后缀**
   ```bash
   basename -s .c source.c    # 输出: source
   ```

5. **处理多个文件**
   ```bash
   basename -a /path/to/file1.txt /path/to/file2.txt
   # 输出:
   # file1.txt
   # file2.txt
   ```

6. **使用 NUL 分隔符**
   ```bash
   basename -az /path/to/file1 /path/to/file2
   ```

7. **在变量赋值中使用**
   ```bash
   filename=$(basename /path/to/document.pdf .pdf)
   echo $filename  # 输出: document
   ```

8. **处理目录路径**
   ```bash
   basename /usr/local/bin/  # 输出: bin
   ```

9. **处理根路径**
   ```bash
   basename /                # 输出: /
   ```

10. **在脚本中使用**
    ```bash
    #!/bin/bash
    for file in /data/*.csv; do
        name=$(basename "$file" .csv)
        echo "Processing: $name"
    done
    ```

11. **去除多个相同后缀的文件**
    ```bash
    basename -a -s .txt file1.txt file2.txt file3.txt
    # 输出:
    # file1
    # file2
    # file3
    ```

## WinuxCmd 实现状态

**已实现**
