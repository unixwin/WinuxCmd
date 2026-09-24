# paste - 合并文件的行

## 命令概述

`paste` 命令用于将多个文件的对应行合并在一起。它从每个文件中读取一行，使用指定的分隔符连接这些行，然后输出结果。默认使用 TAB 作为分隔符。

## 语法

```bash
paste [OPTION]... [FILE]...
```

## 参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-d` | `--delimiters=LIST` | LIST | 使用 LIST 中的字符循环作为分隔符（而不是 TAB） |
| `-s` | `--serial` | - | 串行处理：一次粘贴一个文件的所有行 |
| - | `--zero-terminated` | - | 使用 NUL 而不是换行符作为行分隔符 |
| `--help` | - | - | 显示帮助信息并退出 |
| `--version` | - | - | 显示版本信息并退出 |

## 参数详细说明

### -d, --delimiters=LIST

指定一个或多个字符作为输出分隔符。这些字符会循环使用：
- 第一个分隔符用于第一和第二个文件之间
- 第二个分隔符用于第二和第三个文件之间
- 依此类推，用完后重新开始

如果不使用此选项，默认分隔符是 TAB。

### -s, --serial

串行模式：依次处理每个文件，将单个文件的所有行粘贴为一行（用分隔符连接），然后处理下一个文件。

默认行为是并行模式：同时从所有文件中读取行。

### --zero-terminated

使用 NUL 字符（\0）作为行分隔符，而不是换行符（\n）。

## 使用示例

```bash
# 并行合并两个文件（默认 TAB 分隔）
paste file1.txt file2.txt

# 使用逗号作为分隔符
paste -d ',' file1.txt file2.txt

# 使用多个分隔符循环
paste -d '|:' file1.txt file2.txt file3.txt

# 串行模式：将一个文件的所有行合并为一行
paste -s file.txt

# 串行模式并使用逗号分隔
paste -s -d ',' file.txt

# 合并三列数据
paste col1.txt col2.txt col3.txt

# 将文件转换为 CSV 格式
paste -d ',' file1.txt file2.txt file3.txt > output.csv

# 使用连字符作为分隔符
paste -d '-' file1.txt file2.txt

# 串行模式：将行转为列
paste -s -d '\n' file.txt
```

## WinuxCmd 实现状态

**已实现**

---

*文档基于 GNU Coreutils 9.11*
