# logname - 打印当前登录用户名

## 命令概述

`logname` 命令用于打印当前用户的登录名称。这是一个简单的命令，用于获取用户登录时使用的用户名，与 `whoami` 命令不同，`logname` 显示的是登录时的用户名，而不是当前有效用户。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `--help`
显示 `logname` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `logname` 命令的版本信息，然后退出。

## 使用示例

### 基本用法
```bash
$ logname
john
```

### 在脚本中使用
```bash
#!/bin/bash
# 获取登录用户名
login_name=$(logname)
echo "登录用户名: $login_name"

# 检查登录用户是否为 root
if [ "$(logname)" = "root" ]; then
    echo "以 root 用户登录"
else
    echo "以普通用户登录"
fi
```

### 与 whoami 的区别
```bash
# 当前用户是 root，但最初以 john 登录
$ whoami
root

$ logname
john
```

### 结合其他命令
```bash
# 显示登录用户信息
echo "登录用户: $(logname)"
echo "当前用户: $(whoami)"
echo "用户 ID: $(id -u)"

# 在日志中记录登录用户
echo "$(date): 用户 $(logname) 执行了操作" >> /var/log/user_actions.log
```

### 在 su 或 sudo 环境中
```bash
# 以 john 登录，然后切换到 root
$ su -
Password:

$ whoami
root

$ logname
john  # 仍然显示原始登录用户
```

## WinuxCmd 实现状态

**已实现** - `logname` 命令已在 WinuxCmd 中实现，支持登录用户名显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*