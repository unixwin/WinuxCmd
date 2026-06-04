# nohup 命令

## 命令概述

`nohup` 命令用于运行一个不受挂断信号（SIGHUP）影响的命令，使其在用户注销后仍能继续运行。当标准输出是终端时，输出会自动重定向到 `nohup.out` 文件。

**命令格式：**
```
nohup 命令 [参数]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

> **注意：** GNU `nohup` 没有常规选项。上述两个是通用选项，适用于所有 GNU Coreutils 命令。

## 参数详细说明

### 标准输入重定向
如果标准输入是终端，`nohup` 会将其重定向，使终端会话不会错误地认为终端正被该命令使用。替代的文件描述符被设置为不可读，以便错误尝试从标准输入读取的命令能够报告错误。这是 GNU 扩展；需要移植到非 GNU 主机的程序可以使用 `nohup command [arg]... 0>/dev/null` 代替。

### 标准输出重定向
如果标准输出是终端，命令的标准输出会追加到文件 `nohup.out`；如果无法写入，则追加到 `$HOME/nohup.out`；如果仍然无法写入，则不运行命令。创建的 `nohup.out` 或 `$HOME/nohup.out` 文件仅对用户可读写，不受当前 umask 设置影响。

### 标准错误重定向
如果标准错误是终端，通常会重定向到与标准输出相同的文件描述符。但如果标准输出已关闭，标准错误的终端输出会追加到 `nohup.out` 或 `$HOME/nohup.out`。

### 后台运行
`nohup` 不会自动将命令放到后台运行；必须通过在命令行末尾添加 `&` 来显式实现。

### Niceness 调整
`nohup` 不会改变命令的 niceness 值；需要配合 `nice` 使用，例如 `nohup nice command`。

## 使用示例

1. **在后台运行长时间命令**
   ```bash
   nohup long_running_command &
   ```

2. **将输出重定向到指定文件**
   ```bash
   nohup make > build.log &
   ```

3. **与 nice 配合使用以降低优先级**
   ```bash
   nohup nice -n 15 heavy_task &
   ```

4. **运行多个后台命令**
   ```bash
   nohup task1 > task1.log &
   nohup task2 > task2.log &
   ```

5. **在远程服务器上启动持久进程**
   ```bash
   ssh user@server "nohup /opt/app/start.sh > /var/log/app.log 2>&1 &"
   ```

6. **使用 0>/dev/null 代替标准输入重定向（提高可移植性）**
   ```bash
   nohup command 0>/dev/null &
   ```

## WinuxCmd 实现状态

**已实现**

GNU `nohup` 本身没有常规选项。当前实现保留了本地 `-a/--append` 扩展，并通过分离进程模拟挂断隔离。
