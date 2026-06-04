# pathchk 命令

## 命令概述

`pathchk` 命令用于检查文件路径名的有效性和可移植性。它可以验证路径名是否符合 POSIX 规范或特定系统的限制。

**命令格式：**
```
pathchk [选项]... 名称...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-p` | `--portability` | 检查 POSIX 可移植性（最严格的检查） |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-p, --portability`
检查路径名的 POSIX 可移植性，而不是检查特定系统的限制。这包括：
- 路径名长度不超过 256 字节
- 文件名长度不超过 14 字节
- 只包含 POSIX 可移植文件名字符集中的字符

### 默认检查（无选项）
检查路径名是否：
- 不包含空路径组件
- 不以 `/` 结尾的非目录路径
- 符合系统特定的长度限制

### `--help`
显示 `pathchk` 命令的使用帮助，包括简要说明和可用选项列表，然后以状态 0 退出。

### `--version`
显示 `pathchk` 命令的版本信息，然后以状态 0 退出。

## 使用示例

1. **检查基本路径有效性**
   ```bash
   pathchk /path/to/file.txt
   echo $?  # 输出: 0（有效）
   ```

2. **检查 POSIX 可移植性**
   ```bash
   pathchk -p /path/to/file.txt
   ```

3. **检查包含特殊字符的路径**
   ```bash
   pathchk -p "/path/to/file with spaces.txt"
   ```

4. **检查过长的文件名**
   ```bash
   # 创建一个很长的文件名
   long_name=$(python3 -c "print('a' * 256)")
   pathchk "$long_name"
   ```

5. **检查包含非便携字符的路径**
   ```bash
   pathchk -p "/path/to/file@#$.txt"
   ```

6. **在脚本中使用**
   ```bash
   #!/bin/bash
   for file in "$@"; do
       if pathchk -p "$file"; then
           echo "可移植: $file"
       else
           echo "不可移植: $file"
       fi
   done
   ```

7. **检查空路径**
   ```bash
   pathchk ""
   echo $?  # 输出: 1（无效）
   ```

8. **检查路径组件**
   ```bash
   pathchk "/path//to/file"
   ```

9. **检查当前目录的文件**
   ```bash
   pathchk ./myfile.txt
   ```

10. **批量检查文件**
    ```bash
    pathchk file1.txt file2.txt file3.txt
    ```

## WinuxCmd 实现状态

**已实现**
