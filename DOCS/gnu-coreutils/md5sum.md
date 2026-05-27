# md5sum 命令

## 命令概述

`md5sum` 命令用于计算和检查 MD5（Message-Digest Algorithm 5）消息摘要。MD5 是一种广泛使用的哈希函数，产生 128 位（16 字节）的哈希值，通常用 32 位十六进制数字表示。

**命令格式：**
```
md5sum [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-b` | `--binary` | 以二进制模式读取文件 |
| `-c` | `--check` | 从文件中读取并检查校验和 |
| | `--ignore-missing` | 忽略缺失的文件（仅用于 `--check`） |
| | `--quiet` | 静默模式，不显示 OK 消息（仅用于 `--check`） |
| | `--status` | 不输出任何状态，仅通过退出码表示结果（仅用于 `--check`） |
| | `--strict` | 严格模式，遇到格式错误的行返回非零退出码（仅用于 `--check`） |
| `-w` | `--warn` | 警告格式错误的行（仅用于 `--check`） |
| | `--tag` | 输出 BSD 风格的校验和行 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-b, --binary`
以二进制模式读取文件（在 Windows 系统上很重要）。在 Unix 系统上，此选项无效，因为文件总是以二进制模式读取。

### `-c, --check`
从文件中读取 MD5 校验和，并验证每个文件的校验和是否匹配。文件格式为：每行包含一个 MD5 哈希值、一个空格、一个星号或空格（表示二进制或文本模式）、然后是文件名。

### `--ignore-missing`
在检查模式下，忽略缺失的文件而不报告错误。

### `--quiet`
在检查模式下，不显示 "OK" 或 "FAILED" 消息。

### `--status`
在检查模式下，不输出任何状态信息，仅通过退出码表示结果。退出码 0 表示所有校验和匹配，非零表示有错误。

### `--strict`
在检查模式下，遇到格式错误的行时返回非零退出码。

### `-w, --warn`
在检查模式下，警告格式错误的行，但继续处理。

### `--tag`
输出 BSD 风格的校验和行，格式为：`MD5 (文件名) = 哈希值`。

### `--help`
显示帮助信息并退出。

### `--version`
显示版本信息并退出。

## 使用示例

1. **计算文件的 MD5 校验和**
   ```bash
   md5sum file.txt
   ```

2. **计算多个文件的 MD5 校验和**
   ```bash
   md5sum file1.txt file2.txt file3.txt
   ```

3. **将校验和保存到文件**
   ```bash
   md5sum file1.txt file2.txt > checksums.md5
   ```

4. **验证文件的完整性**
   ```bash
   md5sum -c checksums.md5
   ```

5. **以二进制模式计算校验和**
   ```bash
   md5sum -b file.txt
   ```

6. **使用 BSD 风格输出**
   ```bash
   md5sum --tag file.txt
   ```

7. **静默验证（不显示 OK 消息）**
   ```bash
   md5sum -c --quiet checksums.md5
   ```

8. **严格模式验证**
   ```bash
   md5sum -c --strict checksums.md5
   ```

9. **从标准输入计算校验和**
   ```bash
   echo "Hello World" | md5sum
   ```

10. **计算目录中所有文件的校验和**
    ```bash
    md5sum * > all_checksums.md5
    ```

## WinuxCmd 实现状态

**已实现**