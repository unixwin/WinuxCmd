# tee 命令

## 命令概述

`tee` 命令从标准输入读取数据，并同时写入标准输出和指定的文件。它常用于在管道中保存中间结果的同时继续处理数据。

**命令格式：**
```
tee [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--append` | 追加到文件末尾，而不是覆盖 |
| `-i` | `--ignore-interrupts` | 忽略中断信号 |
| `-p` | `--output-error[=模式]` | 写入时遇到错误的处理方式 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --append`
将输出追加到文件末尾，而不是覆盖文件的现有内容。如果文件不存在，则创建新文件。

### `-i, --ignore-interrupts`
忽略中断信号（SIGINT）。这在 `tee` 作为管道的一部分运行时很有用，可以防止它因中断信号而终止。

### `-p, --output-error[=模式]`
设置写入输出时遇到错误的行为。可选模式：
- `warn`：诊断写入到 stderr 的错误
- `warn-nopipe`：诊断写入到非管道的错误
- `exit`：遇到错误时立即退出
- `exit-nopipe`：遇到非管道错误时立即退出

默认行为取决于是否以管道形式运行。

### `--help`
显示 `tee` 命令的使用帮助，包括简要说明和可用选项列表，然后以状态 0 退出。

### `--version`
显示 `tee` 命令的版本信息，然后以状态 0 退出。

## 使用示例

1. **基本使用 - 同时输出到屏幕和文件**
   ```bash
   echo "Hello World" | tee output.txt
   ```

2. **追加到文件**
   ```bash
   echo "New line" | tee -a output.txt
   ```

3. **同时写入多个文件**
   ```bash
   echo "Data" | tee file1.txt file2.txt file3.txt
   ```

4. **在管道中使用**
   ```bash
   ls -la | tee file_list.txt | grep ".txt"
   ```

5. **使用管道重定向 stderr**
   ```bash
   command 2>&1 | tee output.log
   ```

6. **同时保存 stdout 和 stderr**
   ```bash
   command > >(tee stdout.log) 2> >(tee stderr.log >&2)
   ```

7. **使用追加模式记录日志**
   ```bash
   echo "$(date): New entry" | tee -a log.txt
   ```

8. **在脚本中显示和记录输出**
   ```bash
   #!/bin/bash
   {
       echo "Starting process..."
       some_command
       echo "Process completed."
   } | tee process.log
   ```

9. **忽略中断信号**
   ```bash
   long_running_command | tee -i output.txt
   ```

10. **使用输出错误处理**
    ```bash
    echo "test" | tee -p=output file.txt
    ```

11. **嵌套 tee**
    ```bash
    echo "data" | tee file1.txt | tee file2.txt | cat
    ```

## WinuxCmd 实现状态

**已实现**
