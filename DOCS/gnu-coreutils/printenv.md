# printenv - 打印环境变量

## 命令概述

`printenv` 命令用于打印所有或指定环境变量的值。这是一个用于查看系统环境变量的实用工具，常用于脚本中获取环境配置信息。

## 完整参数列表

| 短选项 | 长选项 | 描述 |
|--------|--------|------|
| `-0` | `--null` | 以空字符（而不是换行符）结束每一行输出 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-0, --null`
以空字符（ASCII NUL，`\0`）而不是换行符结束每一行输出。这在处理包含换行符的变量值时特别有用，可以确保正确解析输出。

### `--help`
显示 `printenv` 命令的帮助信息，包括所有可用选项和简要说明，然后退出。

### `--version`
显示 `printenv` 命令的版本信息，然后退出。

## 使用示例

### 打印所有环境变量
```bash
$ printenv
SHELL=/bin/bash
USER=john
HOME=/home/john
PATH=/usr/local/bin:/usr/bin:/bin
...
```

### 打印特定环境变量
```bash
$ printenv HOME
/home/john

$ printenv PATH
/usr/local/bin:/usr/bin:/bin
```

### 打印多个环境变量
```bash
$ printenv HOME USER SHELL
/home/john
john
/bin/bash
```

### 使用空字符分隔（处理包含换行符的变量）
```bash
$ printenv -0 MY_VAR
value with
newlines
```

### 在脚本中使用
```bash
#!/bin/bash
# 检查变量是否存在
if printenv MY_VAR >/dev/null 2>&1; then
    echo "MY_VAR 已设置: $(printenv MY_VAR)"
else
    echo "MY_VAR 未设置"
fi
```

### 结合其他命令
```bash
# 将环境变量输出保存到文件
printenv > env_backup.txt

# 过滤特定变量
printenv | grep -i path

# 使用空字符分隔处理包含换行符的变量
printenv -0 | tr '\0' '\n'
```

## WinuxCmd 实现状态

**已实现** - `printenv` 命令已在 WinuxCmd 中实现，支持环境变量的显示功能。

---

*文档基于 GNU Coreutils 9.11 版本*