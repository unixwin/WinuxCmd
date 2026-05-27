# uniq 命令

## 命令概述
`uniq` 命令用于报告或省略重复行。它通常与 `sort` 命令配合使用，因为 `uniq` 只比较相邻行。可以统计重复次数、只显示重复行、只显示唯一行等。

## 完整参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-c` | `--count` | 无 | 在每行前缀显示重复次数 |
| `-d` | `--repeated` | 无 | 只显示重复的行（每组只显示一次） |
| `-D` | `--all-repeated` | 无 | 显示所有重复的行（每组显示所有行） |
| `-f` | `--skip-fields=N` | 数字 | 跳过前 N 个字段进行比较 |
| `-i` | `--ignore-case` | 无 | 忽略大小写进行比较 |
| `-s` | `--skip-chars=N` | 数字 | 跳过前 N 个字符进行比较 |
| `-u` | `--unique` | 无 | 只显示唯一的行（不重复的行） |
| `-w` | `--check-chars=N` | 数字 | 只比较每行的前 N 个字符 |
| `-z` | `--zero-terminated` | 无 | 以 NUL 而不是换行符作为行分隔符 |
| | `--group` | 分组方式 | 分组显示：separate（默认），prepend，append，both |
| | `--help` | 无 | 显示帮助信息 |
| | `--version` | 无 | 显示版本信息 |

## 参数详细说明

### 显示模式选项
- `-c`：在每行前添加重复次数（右对齐，空格填充）
- `-d`：只显示重复行（每组重复行只显示一次）
- `-D`：显示所有重复行（显示每组重复行的所有行）
- `-u`：只显示不重复的行

### 比较控制选项
- `-f N`：跳过前 N 个字段（字段由空格分隔）
- `-s N`：跳过前 N 个字符
- `-w N`：只比较前 N 个字符
- `-i`：忽略大小写差异

### 分组显示选项 (--group)
- `separate`：组之间用空行分隔（默认）
- `prepend`：每组前添加空行
- `append`：每组后添加空行
- `both`：每组前后都添加空行

### 行分隔符
- `-z`：使用 NUL 字符作为行分隔符，适用于包含换行符的文件名

## 使用示例

```bash
# 去除重复行（需先排序）
sort filename.txt | uniq

# 统计每行出现次数
sort filename.txt | uniq -c

# 只显示重复行
sort filename.txt | uniq -d

# 显示所有重复行
sort filename.txt | uniq -D

# 只显示唯一行
sort filename.txt | uniq -u

# 忽略大小写去重
sort filename.txt | uniq -i

# 统计并按次数排序
sort filename.txt | uniq -c | sort -rn

# 跳过第一个字段比较
uniq -f 1 data.txt

# 只比较前 10 个字符
uniq -w 10 filename.txt

# 跳过前 5 个字符
uniq -s 5 filename.txt

# 分组显示
sort filename.txt | uniq --group=prepend

# 处理文件名（包含换行符）
find . -print0 | sort -z | uniq -z

# 统计日志中的重复错误
grep "ERROR" logfile.txt | sort | uniq -c | sort -rn

# 比较第 2-4 个字段
uniq -f 1 -w 20 data.txt
```

## WinuxCmd 实现状态
**待实现**

## 相关命令
- `sort`：排序命令，通常与 uniq 配合使用
- `comm`：逐行比较两个已排序文件
- `awk`：更强大的文本处理工具
- `grep`：文本搜索命令