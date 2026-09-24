# split 命令

## 命令概述

`split` 命令用于将文件分割成多个较小的文件。默认情况下，它将文件按行数分割，每个输出文件包含 1000 行。输出文件名默认以 `x` 开头，后跟两个字母的后缀（如 `xaa`、`xab` 等）。

**命令格式：**
```
split [选项]... [输入 [前缀]]
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--suffix-length=N` | 使用 N 个字母的后缀（默认值为 2） |
| `-b` | `--bytes=SIZE` | 每个输出文件的最大大小为 SIZE 字节 |
| `-C` | `--line-bytes=SIZE` | 每个输出文件的最大行数为 SIZE 字节 |
| `-d` | `--numeric-suffixes` | 使用数字后缀而不是字母后缀 |
| | `--additional-suffix=SUFFIX` | 在文件后缀后追加额外的后缀 SUFFIX |
| `-e` | `--elide-empty-files` | 不生成空文件 |
| `-n` | `--number=CHUNKS` | 生成指定数量的文件块 |
| `-l` | `--lines=NUMBER` | 每个输出文件的最大行数（默认值为 1000） |
| | `--filter=COMMAND` | 将每个文件块通过管道传递给 COMMAND 处理，而不是写入文件 |
| `-t` | `--separator=SEP` | 使用 SEP 作为行分隔符而不是换行符 |
| `-u` | `--unbuffered` | 使用无缓冲的 I/O |
| | `--verbose` | 在创建每个文件前显示诊断信息 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --suffix-length=N`
指定输出文件名后缀的长度。默认值为 2，这意味着可以创建最多 676 个文件（`aa` 到 `zz`）。增加后缀长度可以支持更多文件。

### `-b, --bytes=SIZE`
按大小分割文件，每个输出文件的最大大小为 SIZE 字节。SIZE 可以是一个整数，也可以是以下单位之一：
- `b`：512 字节块
- `kB`：1000 字节
- `K`：1024 字节
- `MB`：1000000 字节
- `M`：1048576 字节
- `GB`：1000000000 字节
- `G`：1073741824 字节
- `TB`：1000000000000 字节
- `T`：1099511627776 字节

### `-C, --line-bytes=SIZE`
按行字节数分割文件，每个输出文件的最大行数为 SIZE 字节。这确保不会在行中间分割，同时尽量使每个文件接近指定大小。

### `-d, --numeric-suffixes`
使用数字后缀（`00`、`01`、`02` 等）而不是字母后缀（`aa`、`ab`、`ac` 等）。

### `--additional-suffix=SUFFIX`
在文件后缀后追加额外的后缀 SUFFIX。例如，如果 SUFFIX 为 `.txt`，则输出文件名为 `xaa.txt`、`xab.txt` 等。

### `-e, --elide-empty-files`
不生成空文件。当输入文件的行数不能被分割行数整除时，可能会产生空文件。

### `-n, --number=CHUNKS`
将输入文件分割成指定数量的文件块。CHUNKS 可以是：
- `N`：分割成 N 个文件
- `K/N`：只输出第 K 个文件块
- `l/N`：分割成 N 个文件，不拆分行
- `l/K/N`：只输出第 K 个文件块，不拆分行
- `r/N`：使用循环分割（round-robin）
- `r/K/N`：只输出第 K 个文件块，使用循环分割

### `-l, --lines=NUMBER`
按行数分割文件，每个输出文件的最大行数为 NUMBER（默认值为 1000）。

### `--filter=COMMAND`
将每个文件块通过管道传递给 COMMAND 处理，而不是写入文件。COMMAND 可以包含 `%` 字符，它会被替换为当前文件块的文件名。

### `-t, --separator=SEP`
使用 SEP 作为行分隔符而不是换行符。SEP 可以是一个字符，也可以是以下特殊序列：
- `\0`：NUL 字符
- `\n`：换行符
- `\r`：回车符
- `\t`：制表符

### `-u, --unbuffered`
使用无缓冲的 I/O，这对于实时处理很有用。

### `--verbose`
在创建每个文件前显示诊断信息，包括文件名和大小。

## 使用示例

1. **按行数分割文件（默认 1000 行）**
   ```bash
   split largefile.txt
   ```

2. **按行数分割文件，每文件 500 行**
   ```bash
   split -l 500 largefile.txt
   ```

3. **按大小分割文件，每文件 1MB**
   ```bash
   split -b 1M largefile.txt
   ```

4. **使用数字后缀分割文件**
   ```bash
   split -d -l 1000 largefile.txt
   ```

5. **指定输出文件名前缀**
   ```bash
   split -l 500 largefile.txt part_
   ```

6. **使用较长的后缀长度**
   ```bash
   split -a 3 -l 1000 largefile.txt
   ```

7. **添加额外的文件后缀**
   ```bash
   split -l 1000 --additional-suffix=.txt largefile.txt
   ```

8. **将文件分割成 5 个部分**
   ```bash
   split -n 5 largefile.txt
   ```

9. **不生成空文件**
   ```bash
   split -e -l 1000 largefile.txt
   ```

10. **使用自定义行分隔符**
    ```bash
    split -t '|' -l 100 largefile.txt
    ```

11. **通过管道处理每个文件块**
    ```bash
    split -l 1000 --filter='gzip > $FILE.gz' largefile.txt
    ```

12. **显示分割过程信息**
    ```bash
    split --verbose -l 1000 largefile.txt
    ```

## WinuxCmd 实现状态

**待实现**