# numfmt 命令

## 命令概述

`numfmt` 命令用于在人类可读的字符串表示和数值之间进行转换。最常见的用途是在人类可读格式（如 `4G`）和精确数值（如 `4000000000`）之间转换。

**命令格式：**
```
numfmt [选项]... [数字]
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-d` | `--delimiter=X` | 使用 X 作为输入字段分隔符（默认：换行或空白） |
| | `--field=FIELDS` | 转换输入中的指定字段（默认：1） |
| | `--format=FORMAT` | 使用 printf 风格的浮点格式字符串 |
| | `--from=UNIT` | 按 UNIT 自动缩放输入数字 |
| | `--from-unit=N` | 指定输入单位大小（默认 1） |
| | `--grouping` | 按当前 locale 的规则对数字分组 |
| | `--header[=N]` | 原样输出前 N 行（默认 1） |
| | `--invalid=MODE` | 输入错误时的行为模式 |
| | `--padding=N` | 输出填充到 N 个字符 |
| | `--round=METHOD` | 指定舍入方式 |
| | `--suffix=SUFFIX` | 输出时添加后缀，输入时接受可选后缀 |
| | `--to=UNIT` | 按 UNIT 自动缩放输出数字 |
| | `--to-unit=N` | 指定输出单位大小（默认 1） |
| | `--unit-separator=SEP` | 数字和单位之间的分隔符 |
| | `--debug` | 打印可能的错误用法警告 |
| `-z` | `--zero-terminated` | 以 NUL 而非换行符分隔项 |

## 参数详细说明

### `-d, --delimiter=X`
使用字符 X 作为输入字段分隔符，默认是换行或空白。使用非默认分隔符会关闭自动填充。

### `--field=FIELDS`
转换输入中指定字段的数字（默认第 1 个字段）。支持 `cut` 风格的字段范围：
- `N`：第 N 个字段
- `N-`：从第 N 个字段到行尾
- `N-M`：从第 N 个到第 M 个字段（含）
- `-M`：从第 1 个到第 M 个字段
- `-`：所有字段

### `--format=FORMAT`
使用 printf 风格的浮点格式字符串。格式字符串必须包含一个 `%f` 指令，可选地带有 `'`、`-`、`0`、宽度或精度修饰符：
- `'` 修饰符启用 `--grouping`
- `-` 修饰符启用左对齐的 `--padding`
- 宽度修饰符启用右对齐的 `--padding`
- `0` 宽度修饰符（无 `-`）生成前导零
- 精度规范（如 `%.1f`）覆盖从输入数据确定的精度

### `--from=UNIT`
根据 UNIT 自动缩放输入数字。可选值：
- **none**：不缩放，不接受后缀
- **si**：SI 标准（K=1000, M=1000000, ...）
- **iec**：IEC 标准（K=1024, M=1048576, ...）
- **iec-i**：IEC 标准双字母后缀（Ki=1024, Mi=1048576, ...）
- **auto**：自动识别（单字母=SI，双字母=IEC）

### `--from-unit=N`
指定输入单位大小。例如输入数字 `10` 代表 10 个 512 字节的单位时，使用 `--from-unit=512`。

### `--grouping`
按当前 locale 的分组规则对输出数字进行分组（如千位分隔符）。在 POSIX/C locale 中无效。

### `--header[=N]`
原样输出前 N 行（默认 1），不进行任何转换。

### `--invalid=MODE`
输入错误时的行为：
- **abort**（默认）：立即退出，状态码 2
- **fail**：为每个转换错误打印警告，退出状态码 2
- **warn**：即使有转换错误也以状态码 0 退出
- **ignore**：不打印诊断信息

### `--padding=N`
将输出数字填充到 N 个字符。正数右对齐，负数左对齐。默认根据输入行宽度自动对齐。

### `--round=METHOD`
舍入方式：
- **up**：向上舍入
- **down**：向下舍入
- **from-zero**（默认）：远离零舍入
- **towards-zero**：向零舍入
- **nearest**：四舍五入

### `--suffix=SUFFIX`
输出时添加 SUFFIX，输入时接受可选的 SUFFIX。

### `--to=UNIT`
根据 UNIT 自动缩放输出数字。可选值与 `--from` 相同（不包括 `auto`）。

### `--to-unit=N`
指定输出单位大小。

### `--unit-separator=SEP`
在数字和单位之间使用 SEP 分隔符。默认输入时接受空白或不间断空格，输出时不打印分隔符。

## 使用示例

1. **将数字转换为 SI 格式**
   ```bash
   numfmt --to=si 500000
   # 输出: 500k
   ```

2. **将数字转换为 IEC 格式**
   ```bash
   numfmt --to=iec 500000
   # 输出: 489K
   ```

3. **将 IEC 格式转换为数字**
   ```bash
   numfmt --from=si 1M
   # 输出: 1000000
   ```

4. **从 SI 转换为 IEC**
   ```bash
   numfmt --from=si --to=iec 1T
   # 输出: 932G
   ```

5. **格式化 ls 输出的文件大小**
   ```bash
   ls -l | numfmt --field 5 --header --to=si
   ```

6. **格式化 df 输出的磁盘空间**
   ```bash
   df --block-size=1 | numfmt --field 2 --header --to=iec
   ```

7. **使用填充和对齐**
   ```bash
   du -s * | numfmt --to=si --padding=10
   ```

8. **使用自定义格式**
   ```bash
   du -s * | numfmt --to=si --format="%10f"
   ```

9. **数字分组（千位分隔符）**
   ```bash
   LC_ALL=en_US.utf8 numfmt --from=iec --grouping 2G
   # 输出: 2,147,483,648
   ```

10. **自动识别输入单位**
    ```bash
    numfmt --from=auto 1Mi
    # 输出: 1048576
    ```

11. **添加单位分隔符**
    ```bash
    numfmt --to=si --unit-separator=' ' 500000
    # 输出: 500 k
    ```

## WinuxCmd 实现状态

**已实现**

`-d`、`-f`、`--from`、`--to`、`--round`、`--padding` 等常用参数已实现。
