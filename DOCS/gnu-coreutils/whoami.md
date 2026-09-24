# whoami - 打印当前有效用户名

## 命令概述

`whoami` 命令用于打印当前有效用户的用户名。这是一个简单的命令，用于确认当前正在以哪个用户身份运行命令，与 `logname` 命令不同，`whoami` 显示的是当前有效用户，而不是登录时的用户。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `--help`
显示 `whoami` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `whoami` 命令的版本信息，然后退出。

## 使用示例

### 基本用法
```bash
$ whoami
john
```

### 在脚本中使用
```bash
#!/bin/bash
# 获取当前用户名
current_user=$(whoami)
echo "当前用户: $current_user"

# 检查是否为 root 用户
if [ "$(whoami)" = "root" ]; then
    echo "当前是 root 用户"
else
    echo "当前不是 root 用户"
fi
```

### 与 logname 的区别
```bash
# 当前用户是 root，但最初以 john 登录
$ whoami
root

$ logname
john
```

### 在 sudo 环境中
```bash
# 以普通用户身份运行 sudo 命令
$ sudo whoami
root

# 以普通用户身份运行
$ whoami
john
```

### 结合其他命令
```bash
# 显示当前用户信息
echo "当前用户: $(whoami)"
echo "用户 ID: $(id -u)"
echo "用户主目录: $HOME"

# 在提示符中显示用户名
PS1="$(whoami)@$(hostname):$(pwd)$ "

# 检查当前用户是否有特定权限
if [ "$(whoami)" = "root" ]; then
    # 执行需要 root 权限的操作
    echo "执行管理任务"
fi
```

### 在不同用户上下文中
```bash
# 以 john 登录，切换到其他用户
$ su - jane
Password:

$ whoami
jane  # 显示当前有效用户

$ logname
john  # 显示原始登录用户
```

## WinuxCmd 实现状态

**已实现** - `whoami` 命令已在 WinuxCmd 中实现，支持当前用户名显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*