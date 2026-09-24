# pinky - 轻量级用户信息工具

## 命令概述

`pinky` 命令是一个轻量级的用户信息工具，用于显示登录用户的信息。它是 `finger` 命令的替代品，提供更简洁的用户信息显示。`pinky` 命令在 GNU Coreutils 中提供，用于查看用户的基本信息。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| `-l` | | 产生长格式输出（默认） |
| `-b` | | 省略用户主目录和 shell 的长格式输出 |
| `-h` | | 省略用户项目文件的长格式输出 |
| `-p` | | 省略 `.plan` 文件的长格式输出 |
| `-s` | | 产生短格式输出 |
| `-q` | | 产生短格式输出，但省略登录时间、空闲时间和进程 ID |
| `-w` | | 省略用户名的短格式输出 |
| `-f` | | 省略标题行的短格式输出 |
| `-i` | | 省略空闲时间的短格式输出 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-l`
产生长格式输出（默认）。显示用户的详细信息，包括用户名、终端、登录时间、空闲时间、办公室位置、办公室电话号码等。

### `-b`
在长格式输出中省略用户主目录和 shell 信息。

### `-h`
在长格式输出中省略用户项目文件（`.project`）信息。

### `-p`
在长格式输出中省略 `.plan` 文件信息。

### `-s`
产生短格式输出。显示用户名、终端、登录时间和空闲时间。

### `-q`
产生短格式输出，但省略登录时间、空闲时间和进程 ID。这是最简洁的输出格式。

### `-w`
在短格式输出中省略用户名。

### `-f`
在短格式输出中省略标题行。

### `-i`
在短格式输出中省略空闲时间。

### `--help`
显示 `pinky` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `pinky` 命令的版本信息，然后退出。

## 使用示例

### 基本用法（长格式）
```bash
$ pinky
Login    Name                 TTY      Idle    When           Office
john     John Doe             pts/0    00:05   Jan 15 09:30   Office 101
jane     Jane Smith           pts/1          Jan 15 10:15   Office 102
```

### 短格式输出
```bash
$ pinky -s
Login    TTY      When                    Idle
john     pts/0    Jan 15 09:30            00:05
jane     pts/1    Jan 15 10:15
```

### 最简洁的输出
```bash
$ pinky -q
Login    TTY
john     pts/0
jane     pts/1
```

### 省略特定信息的长格式
```bash
# 省略主目录和 shell 信息
$ pinky -b

# 省略项目文件信息
$ pinky -h

# 省略 .plan 文件信息
$ pinky -p
```

### 省略特定信息的短格式
```bash
# 省略用户名
$ pinky -w

# 省略标题行
$ pinky -f

# 省略空闲时间
$ pinky -i
```

### 查看特定用户信息
```bash
$ pinky john
Login    Name                 TTY      Idle    When           Office
john     John Doe             pts/0    00:05   Jan 15 09:30   Office 101
```

### 在脚本中使用
```bash
#!/bin/bash
# 检查用户是否登录
if pinky -q | grep -q "john"; then
    echo "用户 john 已登录"
else
    echo "用户 john 未登录"
fi

# 获取登录用户数量
user_count=$(pinky -q | wc -l)
echo "当前有 $user_count 个用户登录"

# 显示用户的详细信息
user_info=$(pinky -l john)
echo "用户信息: $user_info"
```

## WinuxCmd 实现状态

**待实现** - `pinky` 命令尚未在 WinuxCmd 中实现。

---

*文档基于 GNU Coreutils 9.11 版本*