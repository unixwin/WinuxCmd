# du 命令

## 命令概述

`du` 命令用于估算文件和目录的磁盘使用空间。它会递归地显示每个目录及其子目录所占用的磁盘空间。

**命令格式：**
```
du [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--all` | 显示所有文件和目录的磁盘使用情况 |
| `-B` | `--block-size=SIZE` | 按指定大小缩放尺寸输出 |
| `-c` | `--total` | 显示总计信息 |
| `-d` | `--max-depth=N` | 仅显示指定深度的目录信息 |
| `-h` | `--human-readable` | 以人类可读格式显示（例如：1K, 234M, 2G） |
| `-H` | `--si` | 以 1000 为基数而非 1024 |
| `-k` | | 类似 `--block-size=1K` |
| `-l` | `--count-links` | 多次计算硬链接文件的大小 |
| `-m` | | 类似 `--block-size=1M` |
| `-s` | `--summarize` | 仅显示总计信息 |
| `-S` | `--separate-dirs` | 不包含子目录的大小 |
| `--apparent-size` | | 显示表面大小而非磁盘使用量 |
| `--files0-from=F` | | 从文件 F 中读取以 NUL 结尾的文件名 |
| `--exclude=PATTERN` | | 排除匹配 PATTERN 的文件 |
| `--max-depth=N` | | 与 `-d N` 相同 |
| `--time` | | 显示最后修改时间 |
| `--time=WORD` | | 显示指定时间：`atime`、`access`、`use`、`ctime`、`status` |
| `--time-style=STYLE` | | 使用指定时间样式：`full-iso`、`long-iso`、`iso`、`+FORMAT` |
| `--help` | | 显示帮助信息并退出 |
| `--version` | | 显示版本信息并退出 |

## 参数详细说明

### `-a, --all`
显示所有文件和目录的磁盘使用情况，而不仅仅是目录。

### `-B, --block-size=SIZE`
按指定大小缩放尺寸输出。SIZE 可以是以下值：
- `KB` = 1000
- `K` = 1024
- `MB` = 1000*1000
- `M` = 1024*1024
- 等等，直到 `EB`、`E`

### `-c, --total`
在输出末尾添加一行总计信息。

### `-d, --max-depth=N`
仅显示指定深度的目录信息。深度为 0 表示仅显示总计，深度为 1 表示仅显示当前目录下的目录，依此类推。

### `-h, --human-readable`
以人类可读格式显示大小，自动选择最合适的单位（K、M、G、T、P、E）。

### `-H, --si`
类似 `-h`，但使用 1000 为基数而非 1024。

### `-k`
等价于 `--block-size=1K`，以 1024 字节为单位显示。

### `-l, --count-links`
多次计算硬链接文件的大小。默认情况下，硬链接文件只计算一次。

### `-m`
等价于 `--block-size=1M`，以 1048576 字节为单位显示。

### `-s, --summarize`
仅显示每个参数的总计信息，不显示子目录的详细信息。

### `-S, --separate-dirs`
在计算目录大小时，不包含子目录的大小。

### `--apparent-size`
显示表面大小而非磁盘使用量。表面大小是文件的实际字节数，而磁盘使用量是文件在磁盘上占用的空间。

### `--files0-from=F`
从文件 F 中读取以 NUL 结尾的文件名列表。如果 F 是 `-`，则从标准输入读取。

### `--exclude=PATTERN`
排除匹配 PATTERN 的文件和目录。PATTERN 是 shell 通配符模式。

### `--max-depth=N`
与 `-d N` 相同，指定显示深度。

### `--time`
显示每个目录的最后修改时间。

### `--time=WORD`
显示指定的时间类型：
- `atime` 或 `access` 或 `use`：最后访问时间
- `ctime` 或 `status`：最后状态更改时间

### `--time-style=STYLE`
使用指定的时间显示样式：
- `full-iso`：完整的 ISO 格式
- `long-iso`：长 ISO 格式
- `iso`：简短 ISO 格式
- `+FORMAT`：自定义格式

## 使用示例

1. **显示当前目录的磁盘使用情况**
   ```bash
   du
   ```

2. **以人类可读格式显示**
   ```bash
   du -h
   ```

3. **显示总计信息**
   ```bash
   du -sh
   ```

4. **显示所有文件的磁盘使用情况**
   ```bash
   du -ah
   ```

5. **仅显示一级子目录的大小**
   ```bash
   du -h --max-depth=1
   ```

6. **显示目录大小并按大小排序**
   ```bash
   du -h --max-depth=1 | sort -hr
   ```

7. **显示最后修改时间**
   ```bash
   du -h --time
   ```

8. **排除特定模式的文件**
   ```bash
   du -h --exclude="*.log"
   ```

9. **显示多个目录的总计**
   ```bash
   du -c dir1 dir2 dir3
   ```

10. **显示表面大小而非磁盘使用量**
    ```bash
    du -h --apparent-size
    ```

11. **显示特定深度的目录信息**
    ```bash
    du -h --max-depth=2 /home/user
    ```

12. **显示最后访问时间**
    ```bash
    du -h --time=atime
    ```

## WinuxCmd 实现状态

**待实现**