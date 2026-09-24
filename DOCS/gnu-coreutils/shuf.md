# shuf 命令

## 命令概述
`shuf` 命令用于随机打乱输入行的顺序。它可以从文件或标准输入中读取行，并以随机顺序输出。常用于随机抽样、生成随机序列等场景。

## 完整参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-e` | `--echo` | 无 | 将每个参数视为输入行 |
| `-i` | `--input-range=LO-HI` | 范围 | 从 LO 到 HI 的整数范围中随机选择 |
| `-n` | `--head-count=COUNT` | 数字 | 最多输出 COUNT 行 |
| `-o` | `--output=FILE` | 文件名 | 输出到 FILE 而不是标准输出 |
| `-r` | `--repeat` | 无 | 允许重复输出行（可重复抽样） |
| | `--random-source=FILE` | 文件 | 使用 FILE 作为随机数源 |
| | `--help` | 无 | 显示帮助信息 |
| | `--version` | 无 | 显示版本信息 |

## 参数详细说明

### 输入源选项
- 文件名参数：默认从指定文件读取输入
- `-e`：将命令行参数作为输入行，每个参数一行
- `-i LO-HI`：生成指定范围的整数，替代文件输入

### 输出控制
- `-n COUNT`：限制输出行数，常用于随机抽样
- `-r`：允许重复输出，实现有放回抽样
- `-o FILE`：重定向输出到文件

### 随机源
- 默认使用系统随机设备（如 /dev/urandom）
- `--random-source` 可指定自定义随机源文件

## 使用示例

```bash
# 随机打乱文件行
shuf filename.txt

# 从 1-10 中随机选择 5 个数字
shuf -i 1-10 -n 5

# 从命令行参数中随机选择
shuf -e apple banana cherry date

# 随机抽样 3 行
shuf -n 3 filename.txt

# 有放回抽样（允许重复）
shuf -r -n 10 -e A B C D

# 输出到文件
shuf -o randomized.txt filename.txt

# 生成 1-100 的随机序列
shuf -i 1-100

# 从范围内随机选择 10 个数字
shuf -i 1-1000 -n 10

# 随机排序并限制输出
shuf -n 20 largefile.txt

# 使用自定义随机源
shuf --random-source=/dev/urandom filename.txt

# 从参数中随机选择并允许重复
shuf -r -n 5 -e red green blue yellow purple

# 生成随机密码（从字符集）
shuf -r -n 12 -e {a..z} {A..Z} {0..9} | tr -d '\n'
```

## WinuxCmd 实现状态
**待实现**

## 相关命令
- `sort -R`：随机排序（但相同行保持在一起）
- `sort`：排序命令
- `head`：输出文件开头部分
- `tail`：输出文件结尾部分