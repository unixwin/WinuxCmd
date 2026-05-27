# tr - 转换或删除字符

## 命令概述

`tr` 命令用于转换、压缩或删除标准输入中的字符，并将结果输出到标准输出。它是一个简单的字符级转换工具。

## 语法

```bash
tr [OPTION]... SET1 [SET2]
```

## 参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-c` | `--complement` | - | 使用 SET1 的补集（所有不在 SET1 中的字符） |
| `-C` | `--complement` | - | 同 -c |
| `-d` | `--delete` | - | 删除 SET1 中的字符，不进行转换 |
| `-s` | `--squeeze-repeats` | - | 将 SET1 中连续重复的字符压缩为单个字符 |
| `-t` | `--truncate-set1` | - | 截断 SET1 使其与 SET2 长度相同 |
| `--help` | - | - | 显示帮助信息并退出 |
| `--version` | - | - | 显示版本信息并退出 |

## 参数详细说明

### SET 格式

SET 可以使用以下格式指定字符集：

| 格式 | 说明 | 示例 |
|------|------|------|
| 字符字面量 | 直接指定字符 | `abc` |
| 范围 | 从字符到字符的范围 | `a-z` |
| 重复 | 字符重复 N 次 | `[a*5]` |
| 字符类 | 预定义字符类 | `[:alpha:]` |
| 转义序列 | 特殊字符 | `\n`, `\t`, `\\` |

### 字符类

| 字符类 | 说明 |
|--------|------|
| `[:alnum:]` | 字母和数字 |
| `[:alpha:]` | 字母 |
| `[:blank:]` | 空格和制表符 |
| `[:cntrl:]` | 控制字符 |
| `[:digit:]` | 数字 |
| `[:graph:]` | 可见字符（不含空格） |
| `[:lower:]` | 小写字母 |
| `[:print:]` | 可见字符（含空格） |
| `[:punct:]` | 标点符号 |
| `[:space:]` | 空白字符 |
| `[:upper:]` | 大写字母 |
| `[:xdigit:]` | 十六进制数字 |

### -c, --complement

使用 SET1 的补集。即选择所有不在 SET1 中的字符。

### -d, --delete

删除标准输入中属于 SET1 的字符。

### -s, --squeeze-repeats

将 SET1 中连续出现的重复字符压缩为单个字符。

### -t, --truncate-set1

当 SET1 比 SET2 长时，截断 SET1 使其与 SET2 长度相同。

## 使用示例

```bash
# 转换小写为大写
echo "hello" | tr 'a-z' 'A-Z'

# 转换大写为小写
echo "HELLO" | tr '[:upper:]' '[:lower:]'

# 删除所有数字
echo "abc123def" | tr -d '[:digit:]'

# 压缩连续空格为单个空格
echo "hello   world" | tr -s ' '

# 将换行符转换为空格
cat file.txt | tr '\n' ' '

# 删除所有空格
echo "hello world" | tr -d ' '

# 替换特殊字符
echo "hello;world" | tr ';' ','

# 使用补集：删除非数字字符
echo "abc123def456" | tr -cd '[:digit:]'

# 压缩连续换行符
cat file.txt | tr -s '\n'

# ROT13 加密
echo "hello" | tr 'a-zA-Z' 'n-za-mN-ZA-M'

# 将多个字符替换为一个
echo "aabbcc" | tr -s 'abc'

# 删除所有标点符号
echo "Hello, World!" | tr -d '[:punct:]'

# 将制表符转换为空格
cat file.txt | tr '\t' ' '
```

## WinuxCmd 实现状态

**已实现**

---

*文档基于 GNU Coreutils 9.11*
