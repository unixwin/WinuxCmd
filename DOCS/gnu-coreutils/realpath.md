# realpath 命令

## 命令概述

`realpath` 命令用于输出解析后的绝对路径名。它会解析所有符号链接、相对路径组件（`.` 和 `..`），并返回规范化的绝对路径。

**命令格式：**
```
realpath [选项]... 文件...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-e` | `--canonicalize-existing` | 解析所有组件，所有组件必须存在 |
| `-m` | `--canonicalize-missing` | 解析所有组件，组件不必存在 |
| `-L` | `--logical` | 解析 `..` 组件前先解析符号链接（默认） |
| `-P` | `--physical` | 解析 `..` 组件前先解析符号链接到物理路径 |
| `-q` | `--quiet` | 静默模式，不打印大多数错误消息 |
| `-s` | `--strip` | 不要解析符号链接，仅规范化路径 |
| `-z` | `--zero` | 使用 NUL 字符（`\0`）而不是换行符分隔输出 |
| `--relative-to=目录` | | 输出相对于指定目录的相对路径 |
| `--relative-base=目录` | | 如果路径在基准目录下，输出相对路径 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-e, --canonicalize-existing`
规范化路径并解析所有符号链接。所有组件（包括最终组件）都必须存在。如果任何组件不存在，则报错。

### `-m, --canonicalize-missing`
规范化路径并解析所有符号链接。不要求路径中的组件存在。不存在的组件将按原样处理。

### `-L, --logical`
在解析 `..` 组件之前先解析符号链接。这是默认行为。例如，如果 `link` 是指向 `dir` 的符号链接，则 `link/../file` 解析为 `dir/../file`。

### `-P, --physical`
在解析 `..` 组件之前先解析符号链接到物理路径。例如，如果 `link` 是指向 `dir` 的符号链接，则 `link/../file` 解析为当前目录中的 `file`。

### `-q, --quiet`
静默模式。不打印大多数错误消息。适用于不希望看到错误输出的脚本。

### `-s, --strip`
不要解析符号链接，仅规范化路径（去除 `.`、`..` 和多余的斜杠）。这比完全解析更快。

### `-z, --zero`
使用 NUL 字符（`\0`）而不是换行符来分隔输出。这在处理包含换行符的文件名时很有用。

### `--relative-to=目录`
输出相对于指定目录的相对路径。如果文件不在指定目录下，输出绝对路径。

### `--relative-base=目录`
仅当文件在指定基准目录下时才输出相对路径，否则输出绝对路径。

### `--help`
显示 `realpath` 命令的使用帮助，包括简要说明和可用选项列表，然后以状态 0 退出。

### `--version`
显示 `realpath` 命令的版本信息，然后以状态 0 退出。

## 使用示例

1. **获取绝对路径**
   ```bash
   realpath file.txt
   # 输出: /home/user/file.txt
   ```

2. **解析相对路径**
   ```bash
   realpath ../parent/file.txt
   # 输出: /home/parent/file.txt
   ```

3. **解析符号链接**
   ```bash
   realpath symlink
   # 输出: /path/to/actual/file
   ```

4. **解析当前目录**
   ```bash
   realpath .
   # 输出: /home/user/current
   ```

5. **要求所有组件存在**
   ```bash
   realpath -e /path/to/file
   ```

6. **允许组件不存在**
   ```bash
   realpath -m /path/to/nonexistent/file
   ```

7. **不解析符号链接**
   ```bash
   realpath -s symlink/../other
   ```

8. **使用物理路径解析**
   ```bash
   realpath -P symlink/../file
   ```

9. **输出相对路径**
   ```bash
   realpath --relative-to=/home /home/user/file.txt
   # 输出: user/file.txt
   ```

10. **条件相对路径**
    ```bash
    realpath --relative-base=/home /home/user/file.txt
    # 输出: user/file.txt
    
    realpath --relative-base=/home /etc/hosts
    # 输出: /etc/hosts
    ```

11. **在脚本中获取脚本所在目录**
    ```bash
    #!/bin/bash
    SCRIPT_DIR=$(dirname "$(realpath "$0")")
    echo "脚本目录: $SCRIPT_DIR"
    ```

12. **使用 NUL 分隔符**
    ```bash
    realpath -z file1 file2 file3
    ```

13. **静默模式**
    ```bash
    realpath -q /path/to/file 2>/dev/null
    ```

14. **规范化路径（去除多余的斜杠和 . ）**
    ```bash
    realpath -s /path//to/./file
    # 输出: /path/to/file
    ```

## WinuxCmd 实现状态

**已实现**
