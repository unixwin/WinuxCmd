# readlink 命令

## 命令概述

`readlink` 命令用于显示符号链接的值（目标路径）。也可以用于规范化文件名路径。

**命令格式：**
```
readlink [选项]... FILE...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--canonicalize` | 规范化路径中的每个组件 |
| `-e` | `--canonicalize-existing` | 规范化路径，所有组件必须存在 |
| `-f` | `--canonicalize-missing` | 规范化路径，组件不必存在 |
| `-m` | `--canonicalize-missing` | 规范化路径，组件不必存在 |
| `-n` | `--no-newline` | 不输出尾随换行符 |
| `-q` | `--quiet` | 静默模式，不显示错误信息 |
| `-s` | `--silent` | 静默模式，不显示错误信息 |
| `-v` | `--verbose` | 详细模式，显示错误信息 |
| `-z` | `--zero` | 用 NUL 字符分隔输出 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --canonicalize`
规范化路径中的每个组件，解析所有符号链接和 `.`、`..`。所有组件必须存在。

### `-e, --canonicalize-existing`
规范化路径中的每个组件，解析所有符号链接和 `.`、`..`。所有组件必须存在。与 `-a` 相同。

### `-f, --canonicalize-missing`
规范化路径中的每个组件，解析所有符号链接和 `.`、`..`。不要求所有组件都存在。

### `-m, --canonicalize-missing`
与 `-f` 相同，规范化路径中的每个组件，不要求所有组件都存在。

### `-n, --no-newline`
不在输出末尾添加换行符。

### `-q, --quiet`
静默模式，不显示错误信息。

### `-s, --silent`
静默模式，不显示错误信息（与 `-q` 相同）。

### `-v, --verbose`
详细模式，显示错误信息。

### `-z, --zero`
用 NUL 字符（而非换行符）分隔输出项。

## 使用示例

1. **显示符号链接的目标**
   ```bash
   readlink symlink.txt
   ```

2. **规范化路径**
   ```bash
   readlink -f /path/to/./some/../file
   ```

3. **静默模式，不显示错误**
   ```bash
   readlink -q non_existent_link
   ```

4. **不输出换行符**
   ```bash
   readlink -n symlink.txt
   ```

5. **用 NUL 分隔输出**
   ```bash
   readlink -z symlink.txt
   ```

6. **规范化当前目录路径**
   ```bash
   readlink -f .
   ```

7. **显示多个符号链接的目标**
   ```bash
   readlink -z link1 link2 link3
   ```

8. **获取绝对路径**
   ```bash
   readlink -f relative/path
   ```

## WinuxCmd 实现状态

**待实现**