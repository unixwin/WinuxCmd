# sort 命令

## 命令概述
`sort` 命令用于对文本文件的行进行排序。它支持多种排序方式，包括按字母顺序、数字顺序、月份顺序等，并可以处理多种文件格式和编码。

## 完整参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-b` | `--ignore-leading-blanks` | 无 | 忽略行首的空白字符 |
| `-d` | `--dictionary-order` | 无 | 按字典顺序排序（只考虑空格、字母和数字） |
| `-f` | `--ignore-case` | 无 | 忽略大小写（将小写字母转换为大写） |
| `-g` | `--general-numeric-sort` | 无 | 按通用数值排序（支持科学计数法） |
| `-i` | `--ignore-nonprinting` | 无 | 忽略不可打印字符 |
| `-M` | `--month-sort` | 无 | 按月份排序（JAN < FEB < ... < DEC） |
| `-h` | `--human-numeric-sort` | 无 | 按人类可读数值排序（如 2K, 1M） |
| `-n` | `--numeric-sort` | 无 | 按数值排序 |
| `-R` | `--random-sort` | 无 | 随机排序（但相同行的内容保持在一起） |
| `-r` | `--reverse` | 无 | 反向排序 |
| `-V` | `--version-sort` | 无 | 按版本号排序（如 1.2.3 < 1.10.1） |
| | `--batch-size=NMERGE` | 数字 | 合并最多 NMERGE 个输入 |
| | `--compress-program=PROG` | 程序名 | 使用 PROG 压缩临时文件 |
| | `--debug` | 无 | 为每行标注排序使用的关键字 |
| | `--files0-from=F` | 文件 | 从文件 F 中读取以 NUL 结尾的文件名 |
| | `--key=KEYDEF` | 键定义 | 通过键定义排序（参见 KEYDEF 语法） |
| | `--merge` | 无 | 合并已排序的文件，不进行排序 |
| | `--output=FILE` | 文件名 | 将输出写入 FILE 而不是标准输出 |
| | `--reverse` | 无 | 反向排序（同 -r） |
| | `--sort=WORD` | 排序类型 | 按 WORD 排序：general-numeric, human-numeric, month, numeric, random, version |
| | `--stable` | 无 | 禁用最后的比较排序（稳定排序） |
| | `--temporary-directory=DIR` | 目录 | 使用 DIR 存储临时文件 |
| | `--unique` | 无 | 与 -c 选项一起使用时，检查严格排序 |
| | `--zero-terminated` | 无 | 以 NUL 而不是换行符作为行分隔符 |
| | `--parallel=N` | 数字 | 设置并行排序的线程数 |
| | `--buffer-size=SIZE` | 大小 | 设置主内存缓冲区大小 |
| `-c` | `--check` | 无 | 检查输入是否已排序 |
| `-C` | `--check=quiet/silent` | 无 | 类似 -c，但不报告第一个错误 |
| `-k` | `--key=KEYDEF` | 键定义 | 通过键定义排序（同 --key） |
| `-m` | `--merge` | 无 | 合并已排序的文件（同 --merge） |
| `-o` | `--output=FILE` | 文件名 | 输出到文件（同 --output） |
| `-s` | `--stable` | 无 | 稳定排序（同 --stable） |
| `-S` | `--buffer-size=SIZE` | 大小 | 设置缓冲区大小（同 --buffer-size） |
| `-t` | `--field-separator=SEP` | 分隔符 | 使用 SEP 作为字段分隔符 |
| `-T` | `--temporary-directory=DIR` | 目录 | 临时文件目录（同 --temporary-directory） |
| `-u` | `--unique` | 无 | 只输出唯一的行 |
| `-z` | `--zero-terminated` | 无 | 以 NUL 作为行分隔符（同 --zero-terminated） |
| | `--help` | 无 | 显示帮助信息 |
| | `--version` | 无 | 显示版本信息 |

## 参数详细说明

### 排序类型选项
- `-g` (通用数值排序)：支持科学计数法（如 1.5e3），但比 `-n` 慢
- `-n` (数值排序)：按数值大小排序，不支持科学计数法
- `-h` (人类可读数值排序)：支持 K、M、G 等后缀
- `-M` (月份排序)：识别月份缩写，不区分大小写

### 键定义 (KEYDEF) 语法
格式：`-k POS1[,POS2]`
- POS：`F[.C][OPTS]`，其中 F 是字段号，C 是字符位置
- OPTS：排序选项（bdfghiMnrR），可以覆盖全局排序选项

示例：
- `-k 2`：按第二个字段排序
- `-k 2,2`：只按第二个字段排序
- `-k 2n`：按第二个字段的数值排序
- `-k 2,2n -k 3,3r`：先按第二个字段数值排序，再按第三个字段反向排序

### 特殊功能
- 稳定排序 (`-s`)：保证相等元素的原始顺序
- 检查模式 (`-c`)：验证输入是否已排序，不实际排序
- 合并模式 (`-m`)：合并多个已排序文件，假设输入已排序

## 使用示例

```bash
# 基本排序
sort filename.txt

# 数值排序
sort -n numbers.txt

# 按第二列排序
sort -k 2,2 data.txt

# 反向排序并去重
sort -ru filename.txt

# 检查文件是否已排序
sort -c filename.txt

# 按人类可读大小排序
sort -h sizes.txt

# 按月份排序
sort -M months.txt

# 合并多个已排序文件
sort -m sorted1.txt sorted2.txt sorted3.txt

# 输出到文件
sort -o sorted.txt input.txt

# 忽略大小写排序
sort -f filename.txt

# 按版本号排序
sort -V versions.txt

# 随机排序
sort -R filename.txt

# 多键排序
sort -k 1,1 -k 2n data.txt

# 自定义分隔符
sort -t: -k 3n /etc/passwd

# 并行排序
sort --parallel=4 largefile.txt
```

## WinuxCmd 实现状态
**待实现**

## 相关命令
- `uniq`：报告或省略重复行
- `comm`：逐行比较两个已排序文件
- `join`：基于公共字段连接两个文件