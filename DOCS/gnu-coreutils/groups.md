# groups - 打印用户所属的组

## 命令概述

`groups` 命令用于打印指定用户或当前用户所属的所有组名。这是一个用于查看用户组成员身份的实用工具，常用于权限管理和系统管理。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `--help`
显示 `groups` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `groups` 命令的版本信息，然后退出。

## 使用示例

### 显示当前用户的组
```bash
$ groups
john sudo users docker
```

### 显示指定用户的组
```bash
$ groups root
root

$ groups john
john sudo users docker
```

### 显示多个用户的组
```bash
$ groups john root jane
john : john sudo users docker
root : root
jane : jane users developers
```

### 在脚本中使用
```bash
#!/bin/bash
# 检查当前用户是否属于特定组
if groups | grep -q "sudo"; then
    echo "当前用户属于 sudo 组"
else
    echo "当前用户不属于 sudo 组"
fi

# 获取当前用户的所有组
user_groups=$(groups)
echo "用户所属组: $user_groups"
```

### 结合其他命令
```bash
# 检查用户是否属于特定组
if id -nG | grep -q "docker"; then
    echo "用户属于 docker 组"
fi

# 列出所有组成员
getent group sudo

# 显示组信息
cat /etc/group | grep "$(whoami)"
```

### 在权限检查中使用
```bash
#!/bin/bash
# 检查是否有访问特定资源的权限
required_group="developers"

if groups | grep -q "$required_group"; then
    echo "有访问权限"
    # 执行需要该组权限的操作
else
    echo "没有访问权限"
    exit 1
fi
```

## WinuxCmd 实现状态

**已实现** - `groups` 命令已在 WinuxCmd 中实现，支持用户组信息显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*