# cut - 删除文件中每行的选定部分

## 命令概述

`cut` 命令用于从文件的每一行中提取选定的部分（字节、字符或字段）。它是一个常用的文本处理工具，特别适用于处理结构化数据，如 CSV 文件或日志文件。

## 语法

```bash
cut [OPTION]... [FILE]...
```

## 参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-b` | `--bytes=LIST` | LIST | 仅显示指定字节位置的内容 |
| `-c` | `--characters=LIST` | LIST | 仅显示指定字符位置的内容 |
| `-d` | `--delimiter=DELIM` | DELIM | 使用 DELIM 作为字段分隔符（默认为 TAB） |
| `-f` | `--fields=LIST` | LIST | 仅显示指定字段的内容 |
| - | `--complement` | - | 补全选中的字节/字符/字段（反选） |
| `-n` | - | - | 不分割多字节字符（与 -b 一起使用） |
| - | `--output-delimiter=STRING` | STRING | 使用 STRING 作为输出分隔符 |
| `-s` | `--only-delimited` | - | 不显示不包含分隔符的行 |
| - | `--zero-terminated` | - | 使用 NUL 而不是换行符作为行分隔符 |
| `--help` | - | - | 显示帮助信息并退出 |
| `--version` | - | - | 显示版本信息并退出 |

## 参数详细说明

### 位置列表 (LIST)

LIST 是一个逗号分隔的数字或范围列表：

- `N`：第 N 个字节/字符/字段（从 1 开始）
- `N-`：从第 N 个到最后一个
- `N-M`：从第 N 个到第 M 个
- `-M`：从第一个到第 M 个

### -b, --bytes=LIST

仅显示指定字节位置的内容。多字节字符会被分割（除非使用 -n）。

### -c, --characters=LIST

仅显示指定字符位置的内容。在多字节字符环境中，这等同于 `-b`。

### -d, --delimiter=DELIM

指定字段分隔符。默认分隔符是 TAB。只能与 `-f` 选项一起使用。

### -f, --fields=LIST

仅显示指定字段的内容。默认输出包含不包含分隔符的行。

### --complement

显示未被选中的字节/字符/字段（反选功能）。

### -n

不分割多字节字符。只能与 `-b` 选项一起使用。

### --output-delimiter=STRING

指定输出时使用的字段分隔符。

### -s, --only-delimited

不显示不包含分隔符的行。只能与 `-f` 选项一起使用。

### --zero-terminated

使用 NUL 字符（\0）作为行分隔符，而不是换行符（\n）。

## 使用示例

```bash
# 提取每行的第 1-5 个字符
cut -c 1-5 file.txt

# 提取每行的第 1、3、5 个字段（以逗号分隔）
cut -d ',' -f 1,3,5 data.csv

# 提取每行的第 2 个字段到最后一个字段
cut -d ':' -f 2- /etc/passwd

# 提取每行的前 10 个字节
cut -b 1-10 file.txt

# 反选：显示除第 3 个字段外的所有字段
cut -d ',' -f 3 --complement data.csv

# 使用自定义输出分隔符
cut -d ',' -f 1,2 --output-delimiter='|' data.csv

# 不显示不包含分隔符的行
cut -d ',' -f 1 -s data.csv

# 提取 UTF-8 字符（不分割多字节字符）
cut -b 1-10 -n unicode.txt
```

## WinuxCmd 实现状态

**已实现**

---

*文档基于 GNU Coreutils 9.11*
