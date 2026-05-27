# expand - 将制表符转换为空格

## 命令概述

`expand` 命令用于将文件中的制表符（TAB）转换为适当数量的空格。它对于规范化文件格式和确保一致的缩进非常有用。

## 语法

```bash
expand [OPTION]... [FILE]...
```

## 参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-i` | `--initial` | - | 仅转换行首的制表符 |
| `-t` | `--tabs=NUMBER` | NUMBER | 设置制表符宽度为 NUMBER 个空格（默认为 8） |
| `-t` | `--tabs=LIST` | LIST | 使用 LIST 指定的制表位列表 |
| `--help` | - | - | 显示帮助信息并退出 |
| `--version` | - | - | 显示版本信息并退出 |

## 参数详细说明

### -i, --initial

仅转换行首的制表符。行中间的制表符将保持不变。

### -t, --tabs=NUMBER

设置制表符宽度为指定数量的空格。默认宽度是 8 个空格。

### -t, --tabs=LIST

使用逗号分隔的制表位列表。可以使用以下格式：
- `N`：在位置 N 设置制表位
- `/N`：从当前位置开始每 N 个字符设置一个制表位
- `N,M`：在位置 N 和 M 设置制表位
- `N,/M`：在位置 N 设置制表位，然后每 M 个字符设置一个
- `N-M`：从位置 N 到 M 每隔一个制表位宽度设置一个

## 使用示例

```bash
# 将制表符转换为 8 个空格（默认）
expand file.txt

# 将制表符转换为 4 个空格
expand -t 4 file.txt

# 仅转换行首的制表符
expand -i file.txt

# 使用自定义制表位
expand -t 10,20,30 file.txt

# 每 4 个字符设置一个制表位
expand -t /4 file.txt

# 在位置 10 设置制表位，然后每 5 个字符一个
expand -t 10/5 file.txt

# 处理多个文件
expand file1.txt file2.txt

# 将结果重定向到新文件
expand -t 4 input.txt > output.txt

# 处理标准输入
cat file.txt | expand -t 4

# 处理 Python 缩进
expand -t 4 script.py > script_expanded.py
```

## Tab 制表位说明

制表位用于确定制表符应该被转换为多少个空格。`expand` 会计算到下一个制表位的距离：

```
位置:    1  2  3  4  5  6  7  8  9  10
制表位:  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·
                     ^
                默认制表位 (8)
```

例如，如果制表位设置为 4：
- 在位置 1 的制表符将转换为 3 个空格（到达位置 4）
- 在位置 5 的制表符将转换为 3 个空格（到达位置 8）

## WinuxCmd 实现状态

**已实现**

---

*文档基于 GNU Coreutils 9.11*
