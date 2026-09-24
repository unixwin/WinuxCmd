# truncate 命令

## 命令概述

`truncate` 命令用于将文件大小调整为指定大小。如果文件大于指定大小，则截断文件；如果文件小于指定大小，则扩展文件并用空字节填充。

**命令格式：**
```
truncate [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-c` | `--no-create` | 不创建不存在的文件 |
| `-o` | `--io-blocks` | 将 SIZE 视为 I/O 块数而非字节数 |
| `-r` | `--reference=RFILE` | 使用参考文件 RFILE 的大小 |
| `-s` | `--size=SIZE` | 设置文件大小为 SIZE |
| `--help` | | 显示帮助信息并退出 |
| `--version` | | 显示版本信息并退出 |

## 参数详细说明

### `-c, --no-create`
如果文件不存在，不创建新文件。默认情况下，如果文件不存在，会创建一个新文件。

### `-o, --io-blocks`
将 SIZE 参数视为 I/O 块数而非字节数。每个块的大小通常为 512 字节。

### `-r, --reference=RFILE`
使用参考文件 RFILE 的大小来调整目标文件的大小。目标文件将被调整为与参考文件相同的大小。

### `-s, --size=SIZE`
设置文件大小为 SIZE。SIZE 可以是以下格式：
- 纯数字：字节数
- 数字 + 后缀：
  - `K` 或 `KB`：千字节（1000 或 1024，取决于上下文）
  - `M` 或 `MB`：兆字节
  - `G` 或 `GB`：千兆字节
  - `T` 或 `TB`：太字节
  - `P` 或 `PB`：拍字节
  - `E` 或 `EB`：艾字节
- 带符号的数字：`+SIZE` 表示增加，`-SIZE` 表示减少
- 特殊值：
  - `0`：清空文件
  - `+0`：增加 0 字节（不改变大小）
  - `-0`：减少 0 字节（不改变大小）

## 使用示例

1. **将文件大小设置为 100 字节**
   ```bash
   truncate -s 100 file.txt
   ```

2. **将文件大小增加 1KB**
   ```bash
   truncate -s +1K file.txt
   ```

3. **将文件大小减少 512 字节**
   ```bash
   truncate -s -512 file.txt
   ```

4. **使用参考文件的大小**
   ```bash
   truncate -r reference.txt target.txt
   ```

5. **清空文件内容**
   ```bash
   truncate -s 0 file.txt
   ```

6. **创建指定大小的空文件**
   ```bash
   truncate -s 10M newfile.bin
   ```

7. **以 I/O 块为单位设置大小**
   ```bash
   truncate -o -s 100 file.txt
   ```

8. **不创建不存在的文件**
   ```bash
   truncate -c -s 100 nonexistent.txt
   ```

9. **将文件扩展到 1GB**
   ```bash
   truncate -s 1G largefile.dat
   ```

10. **将文件截断为 0 字节（保留文件）**
    ```bash
    truncate -s 0 logfile.log
    ```

11. **使用参考文件调整多个文件大小**
    ```bash
    truncate -r template.txt file1.txt file2.txt file3.txt
    ```

12. **将文件大小设置为 1024 个 I/O 块**
    ```bash
    truncate -o -s 1024 file.txt
    ```

## WinuxCmd 实现状态

**待实现**