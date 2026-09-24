# comm 命令

## 命令概述
`comm` 命令用于逐行比较两个已排序的文件。它输出三列：仅在第一个文件中的行、仅在第二个文件中的行、两个文件共有的行。可以有选择地抑制某些列的输出。

## 完整参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-1` | 无 | 无 | 不显示仅在第一个文件中的行 |
| `-2` | 无 | 无 | 不显示仅在第二个文件中的行 |
| `-3` | 无 | 无 | 不显示两个文件共有的行 |
| | `--check-order` | 无 | 检查输入是否正确排序 |
| | `--nocheck-order` | 无 | 不检查输入是否正确排序 |
| | `--output-delimiter=STR` | 字符串 | 使用 STR 作为列分隔符（默认为制表符） |
| | `--total` | 无 | 显示汇总行 |
| | `--zero-terminated` | 无 | 以 NUL 而不是换行符作为行分隔符 |
| | `--help` | 无 | 显示帮助信息 |
| | `--version` | 无 | 显示版本信息 |

## 参数详细说明

### 列选择选项
- `-1`：抑制第一列（仅在第一个文件中的行）
- `-2`：抑制第二列（仅在第二个文件中的行）
- `-3`：抑制第三列（两个文件共有的行）
- 可以组合使用，如 `-12` 只显示共有行，`-23` 只显示第一个文件独有的行

### 输出格式
- 默认使用制表符分隔三列
- `--output-delimiter` 可以自定义分隔符
- 列的顺序：第一列（文件1独有）、第二列（文件2独有）、第三列（共有）

### 输入检查
- `--check-order`：验证输入文件是否正确排序，如果未排序则报错
- `--nocheck-order`：跳过排序检查，即使输入未排序也继续处理

### 汇总功能
- `--total`：在末尾添加一行汇总，显示每列的行数

## 使用示例

```bash
# 基本比较
comm file1.txt file2.txt

# 只显示共有行（抑制前两列）
comm -12 file1.txt file2.txt

# 只显示第一个文件独有的行
comm -23 file1.txt file2.txt

# 只显示第二个文件独有的行
comm -13 file1.txt file2.txt

# 显示两个文件的差异（抑制共有行）
comm -3 file1.txt file2.txt

# 自定义分隔符
comm --output-delimiter=" | " file1.txt file2.txt

# 显示汇总
comm --total file1.txt file2.txt

# 检查输入排序
comm --check-order file1.txt file2.txt

# 跳过排序检查
comm --nocheck-order file1.txt file2.txt

# 比较两个已排序的列表
comm -12 <(sort list1.txt) <(sort list2.txt)

# 找出两个目录中的共同文件
comm -12 <(ls dir1 | sort) <(ls dir2 | sort)

# 比较两个已排序的进程列表
comm -23 <(ps aux | awk '{print $11}' | sort -u) <(ps aux | awk '{print $11}' | sort -u)

# 使用 NUL 分隔符
comm -z --zero-terminated file1.txt file2.txt

# 抑制所有列（只显示是否相同）
comm -123 file1.txt file2.txt

# 显示第一个文件独有的行（带行号）
cat -n file1.txt | sort -k2 | comm -23 - <(sort file2.txt)
```

## WinuxCmd 实现状态
**待实现**

## 相关命令
- `sort`：排序命令，comm 要求输入已排序
- `diff`：逐行比较文件，显示差异
- `cmp`：逐字节比较文件
- `uniq`：报告或省略重复行