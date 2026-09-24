# ptx 命令

## 命令概述
`ptx` 命令用于生成排列索引（permuted index）。它读取输入文件，为每个单词生成包含上下文的索引条目，常用于生成书籍索引或关键词索引。

## 完整参数列表

| 短选项 | 长选项 | 参数 | 说明 |
|--------|--------|------|------|
| `-A` | `--auto-reference` | 无 | 自动生成引用（行号） |
| `-C` | `--copyright` | 无 | 显示版权信息 |
| `-G` | `--traditional` | 无 | 使用传统格式（兼容旧版） |
| `-F` | `--flag-truncation=STRING` | 字符串 | 使用 STRING 标记截断的行 |
| `-M` | `--macro-name=STRING` | 字符串 | 使用 STRING 作为宏名（默认为 xx） |
| `-O` | `--format=roff` | 无 | 生成 roff 格式输出 |
| `-R` | `--right-side-refs` | 无 | 引用放在右侧 |
| `-S` | `--sentence-regexp=REGEXP` | 正则表达式 | 使用 REGEXP 匹配句子结尾 |
| `-T` | `--format=tex` | 无 | 生成 TeX 格式输出 |
| `-W` | `--word-regexp=REGEXP` | 正则表达式 | 使用 REGEXP 匹配单词 |
| `-b` | `--break-file=FILE` | 文件 | 使用 FILE 中的断行字符 |
| `-f` | `--ignore-case` | 无 | 忽略大小写 |
| `-g` | `--gap-size=NUMBER` | 数字 | 设置列间距为 NUMBER 个字符 |
| `-i` | `--ignore-file=FILE` | 文件 | 使用 FILE 中的忽略单词 |
| `-o` | `--only-file=FILE` | 文件 | 使用 FILE 中的仅显示单词 |
| `-r` | `--references` | 无 | 为每行生成引用 |
| `-t` | `--typeset-mode` | 无 | 使用排版模式（默认） |
| `-w` | `--width=NUMBER` | 数字 | 设置输出宽度为 NUMBER 个字符 |
| | `--help` | 无 | 显示帮助信息 |
| | `--version` | 无 | 显示版本信息 |

## 参数详细说明

### 输出格式选项
- `-O`：生成 roff 格式（用于 troff/nroff 排版系统）
- `-T`：生成 TeX 格式（用于 TeX/LaTeX 排版系统）
- 默认：生成普通文本格式

### 引用选项
- `-A`：自动生成行号引用
- `-r`：在输出中包含引用
- `-R`：将引用放在右侧而不是左侧

### 单词匹配选项
- `-W REGEXP`：使用正则表达式定义什么是单词
- `-f`：比较时忽略大小写
- `-b FILE`：从文件读取断行字符
- `-i FILE`：从文件读取忽略单词列表
- `-o FILE`：从文件读取仅显示单词列表

### 输出格式控制
- `-w NUMBER`：设置输出总宽度
- `-g NUMBER`：设置列之间的间距
- `-F STRING`：当行被截断时添加标记字符串
- `-M STRING`：设置宏名（用于格式化输出）

### 句子识别
- `-S REGEXP`：使用正则表达式匹配句子结尾（默认为 `[.?!]`）

## 使用示例

```bash
# 基本使用
ptx filename.txt

# 生成带引用的索引
ptx -r filename.txt

# 忽略大小写
ptx -f filename.txt

# 设置输出宽度
ptx -w 80 filename.txt

# 生成 roff 格式
ptx -O filename.txt > index.roff

# 生成 TeX 格式
ptx -T filename.txt > index.tex

# 使用自动引用
ptx -A filename.txt

# 自定义宽度和间距
ptx -w 100 -g 3 filename.txt

# 使用忽略列表
ptx -i stopwords.txt filename.txt

# 使用仅显示列表
ptx -o keywords.txt filename.txt

# 自定义单词正则表达式
ptx -W '[a-zA-Z]+' filename.txt

# 自定义句子结尾正则表达式
ptx -S '[.!?;]' filename.txt

# 使用传统格式
ptx -G filename.txt

# 截断标记
ptx -F "..." -w 80 filename.txt

# 组合使用
ptx -r -A -f -w 100 -g 2 filename.txt

# 处理多个文件
ptx file1.txt file2.txt file3.txt

# 使用文件作为输入
ptx < input.txt
```

## WinuxCmd 实现状态
**待实现**

## 相关命令
- `sort`：排序命令
- `grep`：文本搜索
- `awk`：文本处理
- `sed`：流编辑器