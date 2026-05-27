# mktemp 命令

## 命令概述

`mktemp` 命令用于安全地创建临时文件或目录。它生成一个唯一的文件名并创建该文件，避免了临时文件名冲突和安全风险。

**命令格式：**
```
mktemp [选项]... [模板]
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-d` | `--directory` | 创建目录而不是文件 |
| `-u` | `--dry-run` | 仅打印文件名，不创建文件（不安全，不推荐） |
| `-q` | `--quiet` | 静默模式，发生错误时不打印消息 |
| `--suffix=后缀` | | 在模板后追加后缀 |
| `--tmpdir[=目录]` | | 在指定目录或 $TMPDIR 中创建文件 |
| `-p 目录` | `--tmpdir=目录` | 在指定目录中创建文件 |
| `-t` | | 解释模板为单个文件名组件，使用 $TMPDIR |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-d, --directory`
创建一个目录而不是文件。创建的目录权限为 0700（仅所有者可读写执行）。

### `-u, --dry-run`
仅打印生成的临时文件名，不实际创建文件。**注意：此选项不安全**，因为在打印文件名和实际使用之间存在竞争条件。不推荐使用。

### `-q, --quiet`
静默模式。如果发生错误，不打印诊断消息。适用于不希望看到错误输出的脚本。

### `--suffix=后缀`
在模板生成的文件名后追加指定的后缀。后缀可以包含点号（如 `.txt`）。这允许在模板中不包含后缀。

### `--tmpdir[=目录]`
在指定的目录中创建临时文件。如果未指定目录，则使用 `$TMPDIR` 环境变量的值。如果 `$TMPDIR` 也未设置，则使用 `/tmp`。

### `-p 目录, --tmpdir=目录`
在指定的目录中创建临时文件。这是 `--tmpdir=目录` 的简写形式。

### `-t`
将模板解释为单个文件名组件（不允许包含 `/`），并使用 `$TMPDIR` 目录。这是为了与某些系统的 `mktemp` 兼容。

### 模板
模板必须包含至少 3 个连续的 `X` 字符（如 `temp.XXXXXX`）。这些 `X` 字符会被替换为随机字符以生成唯一文件名。如果未提供模板，默认使用 `tmp.XXXXXXXXXX`。

### `--help`
显示 `mktemp` 命令的使用帮助，包括简要说明和可用选项列表，然后以状态 0 退出。

### `--version`
显示 `mktemp` 命令的版本信息，然后以状态 0 退出。

## 使用示例

1. **创建临时文件**
   ```bash
   mktemp
   # 输出: /tmp/tmp.XXXXXXXXXX
   ```

2. **使用自定义模板**
   ```bash
   mktemp myapp.XXXXXX
   # 输出: myapp.a1b2c3
   ```

3. **创建带后缀的临时文件**
   ```bash
   mktemp --suffix=.txt myapp.XXXXXX
   # 输出: myapp.a1b2c3.txt
   ```

4. **创建临时目录**
   ```bash
   mktemp -d
   # 输出: /tmp/tmp.XXXXXXXXXX
   ```

5. **在指定目录创建临时文件**
   ```bash
   mktemp -p /var/tmp myapp.XXXXXX
   ```

6. **使用环境变量 TMPDIR**
   ```bash
   export TMPDIR=/var/tmp
   mktemp
   # 输出: /var/tmp/tmp.XXXXXXXXXX
   ```

7. **静默模式**
   ```bash
   mktemp -q myapp.XXXXXX
   ```

8. **在脚本中使用**
   ```bash
   #!/bin/bash
   tmpfile=$(mktemp)
   echo "临时文件: $tmpfile"
   # 使用临时文件
   echo "data" > "$tmpfile"
   # 清理
   rm -f "$tmpfile"
   ```

9. **使用 trap 清理临时文件**
   ```bash
   #!/bin/bash
   tmpfile=$(mktemp)
   trap "rm -f '$tmpfile'" EXIT
   # 使用临时文件
   echo "Processing..." > "$tmpfile"
   ```

10. **创建临时日志目录**
    ```bash
    tmpdir=$(mktemp -d)
    echo "日志目录: $tmpdir"
    # 使用临时目录
    touch "$tmpdir/app.log"
    ```

11. **使用 --tmpdir 选项**
    ```bash
    mktemp --tmpdir=/var/tmp myapp.XXXXXX
    ```

12. **打印但不创建（不推荐）**
    ```bash
    mktemp -u myapp.XXXXXX
    # 仅输出文件名，不创建文件
    ```

## WinuxCmd 实现状态

**已实现**
