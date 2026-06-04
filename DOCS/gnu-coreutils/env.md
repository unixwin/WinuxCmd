# env 命令

## 命令概述

`env` 命令用于在修改的环境中运行命令，或打印当前环境变量。它可以设置、删除环境变量，清空环境，切换工作目录后再执行指定命令。如果没有指定命令，则打印当前环境。

**命令格式：**
```
env [选项]... [-] [名称=值]... [命令 [参数]...]
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-i` | `--ignore-environment` | 从空环境开始，忽略继承的环境变量 |
| `-0` | `--null` | 输出时每行以 NUL 字节结尾而非换行符 |
| `-u` | `--unset=NAME` | 从环境中删除指定变量，可重复使用 |
| `-C` | `--chdir=DIR` | 在执行命令前切换工作目录到 DIR |
| `-a` | `--argv0=arg` | 覆盖传递给被执行命令的第零个参数 |
| `-S` | `--split-string=string` | 将字符串拆分为多个参数（用于 shebang 行） |
| `-v` | `--debug` | 显示每个处理步骤的详细信息 |
| | `--default-signal[=sig]` | 将信号 sig 解除阻塞并重置为默认处理 |
| | `--ignore-signal[=sig]` | 运行程序时忽略信号 sig |
| | `--block-signal[=sig]` | 阻止信号 sig 被传递 |
| | `--list-signal-handling` | 执行命令前列出被阻塞或忽略的信号 |

## 参数详细说明

### `-i, --ignore-environment`
从空环境开始运行命令，完全忽略继承的所有环境变量。常用于在最小化环境中测试程序。

### `-0, --null`
输出环境变量时，每项以 NUL（ASCII 0x00）字节结尾，而不是换行符。便于配合 `xargs -0` 等工具处理包含特殊字符的环境变量值。

### `-u, --unset=NAME`
从环境中删除名为 NAME 的变量。可重复使用以删除多个变量。如果变量不存在则忽略。

### `-C, --chdir=DIR`
在执行命令前将工作目录切换到 DIR。与 shell 内建 `cd` 不同，此选项会以子进程方式启动命令，不影响当前 shell 的工作目录。

### `-a, --argv0=arg`
覆盖传递给被执行命令的第零个参数（即程序名）。默认情况下第零个参数是命令本身的名称。

### `-S, --split-string=string`
将单个字符串拆分为多个参数，主要用于 shebang 行。支持 FreeBSD 语法的转义序列和环境变量扩展：
- `\n` 换行、`\t` 制表、`\r` 回车
- `\"` 双引号、`\'` 单引号、`\\` 反斜杠
- `\c` 忽略剩余字符
- `\$` 字面美元符号
- `\#` 字面井号
- `${VARNAME}` 环境变量扩展
- 双引号内 `\_` 替换为空格

### `-v, --debug`
显示每个处理步骤的详细信息到标准错误，包括取消设置、设置环境变量和执行的命令及参数。

### `--default-signal[=sig]`
将指定信号解除阻塞并重置为默认处理。省略 sig 则处理所有已知信号。多个信号可用逗号分隔。

### `--ignore-signal[=sig]`
运行程序时忽略指定信号。省略 sig 则忽略所有已知信号。多个信号可用逗号分隔。大多数系统不允许忽略 SIGKILL 和 SIGSTOP。

### `--block-signal[=sig]`
阻止指定信号被传递到程序。省略 sig 则阻止所有已知信号。多个信号可用逗号分隔。

### `--list-signal-handling`
在执行命令前，将当前被阻塞或忽略的信号列表输出到标准错误。

## 使用示例

1. **打印当前环境变量**
   ```bash
   env
   ```

2. **在清空环境后运行命令**
   ```bash
   env -i PATH="$PATH" mycommand
   ```

3. **设置环境变量并运行命令**
   ```bash
   env DISPLAY=:0 LOGNAME=user myapp
   ```

4. **删除环境变量后运行命令**
   ```bash
   env -u EDITOR -u TERM mycommand
   ```

5. **切换工作目录后运行命令**
   ```bash
   env --chdir=/tmp mycommand
   ```

6. **在 shebang 行中使用 -S 拆分参数**
   ```bash
   #!/usr/bin/env -S perl -T -w
   print "hello\n";
   ```

7. **使用 -S 和环境变量扩展**
   ```bash
   #!/usr/bin/env -S PYTHONPATH=/opt/modules:${PYTHONPATH} python3
   ```

8. **调试环境变量操作**
   ```bash
   env -v -u TERM A=B uname -s
   ```

## WinuxCmd 实现状态

**已实现**

核心功能（`-i`、`-u`、`-0`、`-C`、`NAME=VALUE`、命令执行）已实现。`-S/--split-string`、`-a/--argv0`、debug 和信号控制（`--default-signal`、`--ignore-signal`、`--block-signal`、`--list-signal-handling`）仍待补齐。
