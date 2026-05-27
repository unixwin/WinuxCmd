# ls 命令

## 命令概述

`ls` 命令用于列出目录内容。默认情况下，列出当前目录的文件和子目录。如果指定的参数是目录，则列出该目录的内容；如果是文件，则列出该文件的信息。

**命令格式：**
```
ls [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--all` | 不忽略以 `.` 开头的条目 |
| `-A` | `--almost-all` | 不列出隐含的 `.` 和 `..` |
| `-b` | `--escape` | 以 C 风格转义打印非图形字符 |
| `-B` | `--ignore-backups` | 不列出以 `~` 结尾的隐含条目 |
| `-c` | | 配合 `-lt`：按 ctime 排序并显示；配合 `-l`：显示 ctime 并按名称排序；否则按 ctime 排序，最新在前 |
| `-C` | | 按列列出条目 |
| `-d` | `--directory` | 列出目录本身而非其内容 |
| `-f` | | 按目录顺序列出所有条目 |
| `-F` | `--classify` | 在条目后追加指示符（`*/=>@|` 之一） |
| `-g` | | 类似 `-l`，但不列出所有者 |
| `-h` | `--human-readable` | 配合 `-l` 和 `-s`，以 1K 234M 2G 等格式显示大小 |
| `-i` | `--inode` | 打印每个文件的索引号（inode） |
| `-I` | `--ignore` | 忽略匹配 PATTERN 的条目 |
| `-k` | `--kibibytes` | 默认使用 1024 字节块显示文件系统使用情况 |
| `-l` | `--long-list` | 使用长列表格式 |
| `-L` | `--dereference` | 显示符号链接引用的文件信息而非链接本身 |
| `-m` | | 用逗号分隔的列表填充宽度 |
| `-n` | `--numeric-uid-gid` | 类似 `-l`，但列出数字用户和组 ID |
| `-N` | `--literal` | 打印条目名称时不加引号 |
| `-o` | | 类似 `-l`，但不列出组信息 |
| `-p` | | 在目录后追加 `/` 指示符 |
| `-q` | `--hide-control-chars` | 用 `?` 替代非图形字符 |
| `-Q` | `--quote-name` | 将条目名称用双引号括起来 |
| `-r` | `--reverse` | 反转排序顺序 |
| `-R` | `--recursive` | 递归列出子目录 |
| `-s` | `--size` | 以块为单位显示每个文件的分配大小 |
| `-S` | | 按文件大小排序，最大的在前 |
| `-t` | | 按时间排序，最新的在前 |
| `-T` | `--tabsize` | 指定制表符宽度（默认 8） |
| `-u` | | 配合 `-lt`：按访问时间排序并显示；配合 `-l`：显示访问时间；否则按访问时间排序 |
| `-U` | | 不排序，按目录顺序列出条目 |
| `-v` | | 文本中的版本号自然排序 |
| `-w` | `--width` | 设置输出宽度为 COLS（0 表示无限制） |
| `-x` | | 按行而非按列列出条目 |
| `-X` | | 按条目扩展名的字母顺序排序 |
| `-Z` | `--context` | 打印每个文件的安全上下文 |
| `-1` | | 每行列出一个文件 |
| | `--sort=WORD` | 按 WORD 排序：`none`、`size`、`time`、`version`、`extension` |
| | `--format=WORD` | 设置输出格式：`across`/`horizontal`、`commas`、`long`、`single-column`/`single`/`one`、`vertical`/`columns` |
| | `--time=WORD` | 更改时间显示样式：`atime`/`access`/`use`、`ctime`/`status`、`birth`/`creation` |
| | `--block-size=SIZE` | 按 SIZE 缩放大小 |
| | `--quoting-style=WORD` | 使用引号样式 WORD：`literal`、`escape`、`c`、`c-maybe`、`shell`、`shell-always`、`shell-escape`、`shell-escape-always` |
| | `--show-control-chars` | 按原样显示非图形字符 |
| | `--indicator-style=WORD` | 使用 WORD 方式追加指示符：`none`、`slash`、`file-type`、`classify` |
| | `--file-type` | 追加文件类型指示符（不含 `*`） |
| | `--color[=WHEN]` | 彩色输出，WHEN 为 `always`、`auto` 或 `never` |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --all`
显示所有条目，包括以 `.` 开头的隐藏文件和目录。同时显示 `.`（当前目录）和 `..`（上级目录）。

### `-A, --almost-all`
类似 `-a`，但不列出 `.` 和 `..` 条目。

### `-b, --escape`
以 C 风风格转义显示非图形字符（如 `\n`、`\t` 等）。

### `-B, --ignore-backups`
不显示以 `~` 结尾的文件（通常是备份文件）。

### `-C`
以多列格式显示条目，按垂直方向排列。

### `-d, --directory`
列出目录本身的信息，而不是目录中的内容。

### `-f`
不排序，按文件系统目录顺序列出。同时启用 `-a`。

### `-F, --classify`
在条目后追加指示符以标识文件类型：
- `*`：可执行文件
- `/`：目录
- `@`：符号链接
- `|`：FIFO
- `=`：套接字

### `-g`
类似长格式 `-l`，但不显示文件所有者列。

### `-h, --human-readable`
以人类可读的格式显示文件大小（如 `1K`、`234M`、`2G`）。

### `-i, --inode`
在每行开头显示文件的 inode 编号。

### `-I, --ignore=PATTERN`
不显示匹配 shell 模式 PATTERN 的条目。

### `-l`
使用长列表格式显示文件信息，包括权限、链接数、所有者、组、大小、修改时间和文件名。

### `-L, --dereference`
当显示符号链接的信息时，显示链接指向的文件信息而非链接本身。

### `-m`
以逗号分隔的水平列表显示条目。

### `-n, --numeric-uid-gid`
类似 `-l`，但以数字形式显示用户 ID 和组 ID。

### `-o`
类似长格式 `-l`，但不显示组信息列。

### `-p`
在目录名后追加 `/` 指示符。

### `-q, --hide-control-chars`
用 `?` 替换文件名中的非图形控制字符。

### `-Q, --quote-name`
将文件名用双引号括起来。

### `-r, --reverse`
反转排序顺序。

### `-R, --recursive`
递归列出所有子目录的内容。

### `-s, --size`
在每行开头以块为单位显示文件的分配大小。

### `-S`
按文件大小降序排序（最大的在前）。

### `-t`
按修改时间降序排序（最新的在前）。

### `-T, --tabsize=COLS`
指定制表符停止位宽度（默认为 8）。

### `-u`
使用访问时间（atime）而非修改时间（mtime）进行排序和显示。

### `-U`
不排序，按文件系统目录顺序列出。

### `-v`
按版本号自然排序（数字按数值排序而非字典序）。

### `-w, --width=COLS`
设置输出宽度。0 表示无限制。

### `-x`
以水平排列的多列格式显示条目。

### `-X`
按文件扩展名的字母顺序排序。

### `-1`
每行只显示一个文件。

## 使用示例

1. **列出当前目录**
   ```bash
   ls
   ```

2. **长列表格式**
   ```bash
   ls -l
   ```

3. **显示隐藏文件**
   ```bash
   ls -a
   ```

4. **长列表 + 隐藏文件 + 人类可读大小**
   ```bash
   ls -lah
   ```

5. **按大小排序**
   ```bash
   ls -lS
   ```

6. **按时间排序（最新的在前）**
   ```bash
   ls -lt
   ```

7. **递归列出目录**
   ```bash
   ls -R
   ```

8. **显示 inode 编号**
   ```bash
   ls -i
   ```

9. **列出目录本身而非内容**
   ```bash
   ls -ld /home/user
   ```

10. **彩色输出**
    ```bash
    ls --color=always
    ```

11. **按文件类型分类显示**
    ```bash
    ls -F
    ```

12. **每行显示一个文件**
    ```bash
    ls -1
    ```

## WinuxCmd 实现状态

**已实现** - 支持 `-a`/`--all`、`-A`/`--almost-all`、`-b`/`--escape`、`-B`/`--ignore-backups`、`-C`、`-d`/`--directory`、`-f`、`-F`/`--classify`、`-g`、`-h`/`--human-readable`、`-i`/`--inode`、`-I`/`--ignore`、`-l`/`--long-list`、`-n`/`--numeric-uid-gid`、`-o`、`-p`、`-q`/`--hide-control-chars`、`-Q`/`--quote-name`、`-r`/`--reverse`、`-R`/`--recursive`、`-s`/`--size`、`-S`、`-t`、`-T`/`--tabsize`、`-u`、`-U`、`-v`、`-w`/`--width`、`-x`、`-X`、`-1`、`--sort`、`--format`、`--time`、`--block-size`、`--quoting-style`、`--show-control-chars`、`--indicator-style`、`--file-type`、`--color` 选项。支持长列表格式、多种排序模式、彩色输出、权限显示、inode 显示等核心功能。
