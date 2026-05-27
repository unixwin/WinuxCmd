# unexpand - 将空格转换为制表符

## 命令概述

`unexpand` 命令用于将文件中的空格转换为制表符（TAB）。它是 `expand` 命令的逆操作，用于减小文件大小或规范化格式。

## 语法

```bash
unexpand [OPTION]... [FILE]...
```

## 参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-a` | `--all` | - | 转换所有连续空格，而不仅是行首空格 |
| `-t` | `--tabs=NUMBER` | NUMBER | 设置制表符宽度为 NUMBER 个空格（默认为 8） |
| `-t` | `--tabs=LIST` | LIST | 使用 LIST 指定的制表位列表 |
| `--first-only` | - | - | 仅转换行首的空格（当使用 -a 时的限制） |
| `--help` | - | - | 显示帮助信息并退出 |
| `--version` | - | - | 显示版本信息并退出 |

## 参数详细说明

### -a, --all

转换所有连续的空格序列，而不仅仅是行首的空格。默认情况下，只转换行首的空格。

### -t, --tabs=NUMBER

设置制表符宽度为指定数量的空格。默认宽度是 8 个空格。

### -t, --tabs=LIST

使用逗号分隔的制表位列表。格式与 `expand` 相同：
- `N`：在位置 N 设置制表位
- `/N`：从当前位置开始每 N 个字符设置一个制表位
- `N,M`：在位置 N 和 M 设置制表位
- `N,/M`：在位置 N 设置制表位，然后每 M 个字符设置一个
- `N-M`：从位置 N 到 M 每隔一个制表位宽度设置一个

### --first_only

仅转换行首的空格。当与 `-a` 选项一起使用时，限制只转换行首。

## 使用示例

```bash
# 将行首空格转换为制表符（默认）
unexpand file.txt

# 将所有空格转换为制表符
unexpand -a file.txt

# 使用 4 个空格作为制表符宽度
unexpand -t 4 file.txt

# 仅转换行首空格
unexpand -a --first-only file.txt

# 使用自定义制表位
unexpand -t 10,20,30 file.txt

# 每 4 个字符设置一个制表位
unexpand -t /4 file.txt

# 处理多个文件
unexpand file1.txt file2.txt

# 将结果重定向到新文件
unexpand -a input.txt > output.txt

# 处理标准输入
cat file.txt | unexpand -t 4

# 转换 Python 缩进
unexpand -t 4 --first-only script.py > script_tabs.py

# 处理带有行首缩进的文件
unexpand -t 2 file.txt
```

## 转换规则

`unexpand` 按以下规则将空格转换为制表符：

1. **行首空格**（默认）：只转换行首的连续空格
2. **所有空格**（-a）：转换所有位置的连续空格
3. **制表位对齐**：只有当空格序列可以对齐到下一个制表位时才转换

例如，使用默认制表位（8）：

```
输入:  "        hello"  (8个空格)
输出:  "\thello"        (1个制表符)

输入:  "    hello"      (4个空格)
输出:  "    hello"      (保持不变，因为无法对齐到制表位)
```

## WinuxCmd 实现状态

**已实现**

---

*文档基于 GNU Coreutils 9.11*
