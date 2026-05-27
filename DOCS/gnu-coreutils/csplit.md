# csplit 命令

## 命令概述

`csplit` 命令用于根据上下文将文件分割成多个较小的文件。与 `split` 按行数或字节数分割不同，`csplit` 使用正则表达式或行号作为分割点。输出文件名默认以 `xx` 开头，后跟两位数字（如 `xx00`、`xx01` 等）。

**命令格式：**
```
csplit [选项]... 文件 模式...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-b` | `--suffix-format=FORMAT` | 使用 printf 风格的格式化字符串作为文件后缀 |
| `-f` | `--prefix=PREFIX` | 使用 PREFIX 作为输出文件名前缀（默认为 `xx`） |
| `-k` | `--keep-files` | 即使发生错误也保留输出文件 |
| `-m` | `--suppress-matched` | 不包含匹配行到输出文件中 |
| `-n` | `--digits=N` | 使用 N 位数字作为文件后缀（默认值为 2） |
| `-s` | `--quiet` | 不显示文件大小信息 |
| | `--silent` | 不显示文件大小信息 |
| `-z` | `--elide-empty-files` | 不生成空文件 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-b, --suffix-format=FORMAT`
使用 printf 风格的格式化字符串作为文件后缀。默认格式为 `%02d`，即两位数字，不足时补零。例如，`%03d` 会生成 `000`、`001`、`002` 等后缀。

### `-f, --prefix=PREFIX`
指定输出文件名的前缀。默认值为 `xx`，因此输出文件名为 `xx00`、`xx01` 等。

### `-k, --keep-files`
默认情况下，如果分割过程中发生错误，`csplit` 会删除所有已创建的输出文件。使用此选项可以保留已创建的文件。

### `-m, --suppress-matched`
不包含匹配行到输出文件中。默认情况下，匹配分割模式的行会包含在前一个输出文件中。使用此选项后，匹配行不会包含在任何输出文件中。

### `-n, --digits=N`
指定文件后缀的数字位数。默认值为 2，这意味着可以创建最多 100 个文件（`00` 到 `99`）。增加位数可以支持更多文件。

### `-s, --quiet, --silent`
安静模式，不显示每个输出文件的大小信息。

### `-z, --elide-empty-files`
不生成空文件。当两个连续的分割点之间没有内容时，可能会产生空文件。

## 分割模式

`csplit` 使用分割模式来指定分割点。模式可以是以下几种：

### 行号模式
直接指定行号作为分割点：
```
csplit file.txt 5 10 15
```
这会在第 5、10、15 行处分割文件。

### 正则表达式模式
使用正则表达式匹配行作为分割点：
```
csplit file.txt /pattern/
```
这会在第一个匹配 `pattern` 的行处分割文件。

### 偏移模式
在正则表达式匹配的基础上指定行偏移：
```
csplit file.txt /pattern/+3
```
这会在匹配 `pattern` 的行后第 3 行处分割文件。

### 重复模式
使用 `{*}` 重复上一个模式直到文件结束：
```
csplit file.txt /pattern/ {*}
```

### 指定重复次数
使用 `{N}` 指定模式重复 N 次：
```
csplit file.txt /pattern/ {3}
```

### 从指定位置重复
使用 `{N}` 从第 N 次匹配开始重复：
```
csplit file.txt /pattern/ {2}
```

## 使用示例

1. **在第 10 行处分割文件**
   ```bash
   csplit file.txt 10
   ```

2. **在多个行号处分割文件**
   ```bash
   csplit file.txt 10 20 30
   ```

3. **使用正则表达式分割文件**
   ```bash
   csplit file.txt /pattern/
   ```

4. **在匹配行后第 2 行处分割**
   ```bash
   csplit file.txt /pattern/+2
   ```

5. **重复模式直到文件结束**
   ```bash
   csplit file.txt /pattern/ {*}
   ```

6. **指定模式重复次数**
   ```bash
   csplit file.txt /pattern/ {3}
   ```

7. **使用自定义文件名前缀**
   ```bash
   csplit -f part_ file.txt 10 20
   ```

8. **使用三位数字后缀**
   ```bash
   csplit -n 3 file.txt 10 20
   ```

9. **使用自定义后缀格式**
   ```bash
   csplit -b '%03d.txt' file.txt 10 20
   ```

10. **不生成空文件**
    ```bash
    csplit -z file.txt 10 10
    ```

11. **保留文件即使发生错误**
    ```bash
    csplit -k file.txt 10 20 100
    ```

12. **不包含匹配行**
    ```bash
    csplit -m file.txt /pattern/
    ```

13. **安静模式，不显示文件大小**
    ```bash
    csplit -s file.txt 10 20
    ```

14. **分割配置文件**
    ```bash
    csplit config.txt '/^\[section\]/' '{*}'
    ```

15. **分割日志文件**
    ```bash
    csplit syslog.txt '/^Jan/' '{*}'
    ```

## WinuxCmd 实现状态

**待实现**