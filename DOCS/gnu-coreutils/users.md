# users - 打印当前登录的用户

## 命令概述

`users` 命令用于打印当前登录系统的用户名列表。这是一个用于查看系统登录状态的实用工具，常用于系统监控和管理。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `--help`
显示 `users` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `users` 命令的版本信息，然后退出。

## 使用示例

### 基本用法
```bash
$ users
john jane root
```

### 显示重复登录的用户
```bash
# 如果同一用户多次登录，会显示多次
$ users
john john jane
```

### 在脚本中使用
```bash
#!/bin/bash
# 检查特定用户是否登录
if users | grep -q "john"; then
    echo "用户 john 已登录"
else
    echo "用户 john 未登录"
fi

# 统计登录用户数量
user_count=$(users | tr ' ' '\n' | sort -u | wc -l)
echo "当前有 $user_count 个不同用户登录"
```

### 结合其他命令
```bash
# 显示登录用户详细信息
who

# 显示登录用户及其活动
w

# 统计登录用户数量
users | tr ' ' '\n' | sort -u | wc -l

# 检查是否有多个用户登录
if [ $(users | tr ' ' '\n' | sort -u | wc -l) -gt 1 ]; then
    echo "有多个用户登录"
fi
```

### 在系统监控中使用
```bash
#!/bin/bash
# 监控登录用户
while true; do
    echo "当前登录用户: $(users)"
    echo "登录用户数量: $(users | tr ' ' '\n' | sort -u | wc -l)"
    sleep 60
done
```

## WinuxCmd 实现状态

**已实现** - `users` 命令已在 WinuxCmd 中实现，支持登录用户显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*