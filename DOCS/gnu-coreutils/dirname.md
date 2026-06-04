# dirname 命令

## 命令概述

`dirname` 命令用于从完整的文件路径中提取目录部分（去除最后一个路径组件）。如果路径以斜杠结尾，则去除该斜杠后再处理。

**命令格式：**
```
dirname [选项] 名称...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-z` | `--zero` | 使用 NUL 字符（`\0`）而不是换行符分隔输出 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-z, --zero`
使用 NUL 字符（`\0`）而不是换行符来分隔输出。这在处理包含换行符的路径时很有用。

### `--help`
显示 `dirname` 命令的使用帮助，包括简要说明和可用选项列表，然后以状态 0 退出。

### `--version`
显示 `dirname` 命令的版本信息，然后以状态 0 退出。

## 使用示例

1. **基本使用 - 提取目录路径**
   ```bash
   dirname /usr/bin/sort    # 输出: /usr/bin
   ```

2. **处理相对路径**
   ```bash
   dirname docs/manual.pdf  # 输出: docs
   ```

3. **处理当前目录**
   ```bash
   dirname file.txt         # 输出: .
   ```

4. **处理根路径**
   ```bash
   dirname /                # 输出: /
   ```

5. **处理多个斜杠**
   ```bash
   dirname /usr//bin/       # 输出: /usr/
   ```

6. **处理带斜杠结尾的路径**
   ```bash
   dirname /usr/local/bin/  # 输出: /usr/local
   ```

7. **使用 NUL 分隔符**
   ```bash
   dirname -z /path/to/file1 /path/to/file2
   ```

8. **在变量赋值中使用**
   ```bash
   dir=$(dirname /path/to/file.txt)
   echo $dir  # 输出: /path/to
   ```

9. **处理多个路径**
   ```bash
   dirname /usr/bin/sort /etc/hosts /tmp
   # 输出:
   # /usr/bin
   # /etc
   # .
   ```

10. **在脚本中使用**
    ```bash
    #!/bin/bash
    script_dir=$(dirname "$(readlink -f "$0")")
    echo "脚本所在目录: $script_dir"
    ```

11. **获取脚本所在目录**
    ```bash
    # 获取当前脚本的绝对路径目录
    DIR="$(cd "$(dirname "$0")" && pwd)"
    echo $DIR
    ```

12. **处理特殊字符**
    ```bash
    dirname "/path with spaces/file.txt"  # 输出: /path with spaces
    ```

## WinuxCmd 实现状态

**已实现**
