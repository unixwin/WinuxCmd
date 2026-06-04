# join - 连接两个文件的行

## 命令概述

`join` 命令用于根据共同的连接字段将两个文件的行合并在一起。它会读取两个已排序的文件，并将具有相同连接字段的行输出为单行。

## 语法

```bash
join [OPTION]... FILE1 FILE2
```

## 参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-a` | `--unprintable-lines=FILENUM` | FILENUM | 从文件 FILENUM 打印不可配对的行 |
| `-e` | `--empty=STRING` | STRING | 用 STRING 替换缺失的输入字段 |
| `-i` | `--ignore-case` | - | 比较时忽略大小写 |
| `-j` | `--join=FIELD` | FIELD | 等同于 '-1 FIELD -2 FIELD' |
| `-o` | `--autoformat` | - | 按照 FORMAT 构建输出行 |
| `-t` | `--field-separator=CHAR` | CHAR | 使用 CHAR 作为输入和输出字段分隔符 |
| `-v` | `--unjoinable=FILENUM` | FILENUM | 类似 -a，但不输出连接字段 |
| `-1` | `--field1=FIELD` | FIELD | 在文件 1 的第 FIELD 个字段上连接 |
| `-2` | `--field2=FIELD` | FIELD | 在文件 2 的第 FIELD 个字段上连接 |
| `--check-order` | - | - | 检查输入是否正确排序（即使所有行都可配对） |
| `--nocheck-order` | - | - | 不检查输入是否正确排序 |
| `--header` | - | - | 将第一行视为文件头，不进行连接直接输出 |
| `-z` | `--zero-terminated` | - | 使用 NUL 而不是换行符作为行分隔符 |
| `--help` | - | - | 显示帮助信息并退出 |
| `--version` | - | - | 显示版本信息并退出 |

## 参数详细说明

### -a FILENUM

打印来自文件 FILENUM（1 或 2）的不可配对行。通常这些行会被丢弃。

### -e STRING

当输入字段缺失时（因为行不可配对），用 STRING 替换缺失的字段。

### -i, --ignore-case

在比较连接字段时忽略大小写差异。

### -j FIELD

等同于同时指定 `-1 FIELD -2 FIELD`，即在两个文件的相同字段上连接。

### -o FORMAT

按照指定的格式构建输出行。FORMAT 可以包含：
- `0`：连接字段
- `1.FIELD`：文件 1 的指定字段
- `2.FIELD`：文件 2 的指定字段
- 特殊字符：`'\t'`（TAB）、`'\n'`（换行）、`'\\'`（反斜杠）

### -t CHAR

使用 CHAR 作为输入和输出字段分隔符。默认是空白字符（空格或 TAB）。

### -v FILENUM

类似 -a，但不输出连接字段。用于显示无法配对的行。

### -1 FIELD

指定在文件 1 的第 FIELD 个字段上进行连接。

### -2 FIELD

指定在文件 2 的第 FIELD 个字段上进行连接。

### --check-order

检查两个输入文件是否正确排序。即使所有行都能配对，如果排序不正确也会报错。

### --nocheck-order

不检查输入是否正确排序。这是默认行为。

### --header

将每个文件的第一行视为文件头，不进行连接比较，直接输出。

### -z, --zero-terminated

使用 NUL 字符（\0）作为行分隔符，而不是换行符（\n）。

## 使用示例

```bash
# 在默认字段上连接两个已排序的文件
join file1.txt file2.txt

# 使用逗号作为分隔符
join -t ',' file1.csv file2.csv

# 在第 2 个字段上连接
join -1 2 -2 2 file1.txt file2.txt

# 在两个文件的不同字段上连接
join -1 1 -2 3 file1.txt file2.txt

# 打印文件 1 的所有行（包括不可配对的）
join -a 1 file1.txt file2.txt

# 忽略大小写
join -i file1.txt file2.txt

# 用特定字符串替换缺失字段
join -e "N/A" file1.txt file2.txt

# 自定义输出格式
join -o 1.1 2.2 1.3 file1.txt file2.txt

# 处理 CSV 文件
join -t ',' -1 1 -2 1 file1.csv file2.csv

# 显示文件 1 中无法配对的行
join -v 1 file1.txt file2.txt

# 处理带有文件头的文件
join --header file1.txt file2.txt
```

## WinuxCmd 实现状态

**待实现**

---

*文档基于 GNU Coreutils 9.11*
