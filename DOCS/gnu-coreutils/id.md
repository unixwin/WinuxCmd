# id - 打印用户和组信息

## 命令概述

`id` 命令用于打印指定用户或当前用户的用户 ID（UID）、组 ID（GID）以及所属的附加组信息。这是一个系统管理命令，常用于查看用户身份和权限信息。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| `-a` | | 忽略，仅用于与其他版本兼容 |
| `-G` | `--groups` | 显示所有组 ID |
| `-g` | `--group` | 仅显示有效组 ID |
| `-n` | `--name` | 显示名称而非数字（需与 `-u`、`-g` 或 `-G` 一起使用） |
| `-r` | `--real` | 显示真实 ID 而非有效 ID（需与 `-u`、`-g` 或 `-G` 一起使用） |
| `-u` | `--user` | 仅显示有效用户 ID |
| `-z` | `--zero` | 以空字符分隔输出（而不是空格） |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a`
忽略。此选项仅用于与其他版本的 `id` 命令兼容，无实际作用。

### `-G, --groups`
显示当前用户所属的所有组 ID（包括主组和附加组）。

### `-g, --group`
仅显示当前用户的有效组 ID。

### `-n, --name`
显示用户或组的名称而非数字 ID。必须与 `-u`、`-g` 或 `-G` 选项一起使用。

### `-r, --real`
显示真实 ID 而非有效 ID。必须与 `-u`、`-g` 或 `-G` 选项一起使用。

### `-u, --user`
仅显示当前用户的有效用户 ID。

### `-z, --zero`
以空字符（NUL）而不是空格分隔输出项。这在处理包含空格的用户名或组名时特别有用。

### `--help`
显示 `id` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `id` 命令的版本信息，然后退出。

## 使用示例

### 显示当前用户信息
```bash
$ id
uid=1000(john) gid=1000(john) groups=1000(john),27(sudo),100(users)
```

### 显示指定用户信息
```bash
$ id root
uid=0(root) gid=0(root) groups=0(root)
```

### 仅显示用户 ID
```bash
$ id -u
1000

$ id -u -n
john
```

### 仅显示组 ID
```bash
$ id -g
1000

$ id -g -n
john
```

### 显示所有组 ID
```bash
$ id -G
1000 27 100

$ id -G -n
john sudo users
```

### 显示真实 ID 而非有效 ID
```bash
$ id -u -r
1000

$ id -g -r
1000
```

### 以空字符分隔输出
```bash
$ id -z
uid=1000(john) gid=1000(john) groups=1000(john),27(sudo),100(users)
```

### 在脚本中使用
```bash
#!/bin/bash
# 检查是否为 root 用户
if [ "$(id -u)" -eq 0 ]; then
    echo "当前是 root 用户"
else
    echo "当前不是 root 用户"
fi

# 获取当前用户名
current_user=$(id -un)
echo "当前用户: $current_user"

# 获取当前用户的所有组
all_groups=$(id -Gn)
echo "所属组: $all_groups"
```

## WinuxCmd 实现状态

**已实现** - `id` 命令已在 WinuxCmd 中实现，支持用户和组信息显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*