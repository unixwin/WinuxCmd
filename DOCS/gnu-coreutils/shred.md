# shred 命令

## 命令概述

`shred` 命令用于反复覆盖指定文件，以防止数据恢复。它通过多次写入随机数据来安全擦除文件内容。

**命令格式：**
```
shred [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-f` | `--force` | 忽略写保护 |
| `-n` | `--iterations` | 覆盖 N 次（默认 3 次） |
| `-u` | `--remove` | 覆盖后截断并删除文件 |
| `-z` | `--zero` | 最后用零覆盖一次以隐藏粉碎操作 |
| `-v` | `--verbose` | 显示进度信息 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-f, --force`
必要时更改文件权限以允许写入。例如，对只读文件进行粉碎操作。

### `-n, --iterations=N`
指定覆盖文件内容的次数。默认为 3 次。可以使用 `-n 0` 来仅进行零覆盖（配合 `-z` 使用时）。

### `-u, --remove`
在覆盖操作完成后，截断并删除文件。这是安全删除文件的标准做法。

### `-z, --zero`
在所有随机数据覆盖完成后，再用零（NUL 字节）覆盖一次。这有助于隐藏文件已被粉碎的事实。

### `-v, --verbose`
显示每个文件的粉碎进度，包括每次覆盖的进度信息。

## 使用示例

1. **安全删除单个文件**
   ```bash
   shred -u secret.txt
   ```

2. **覆盖 5 次后删除**
   ```bash
   shred -n 5 -u sensitive_data.dat
   ```

3. **覆盖后用零填充再删除**
   ```bash
   shred -z -u classified.doc
   ```

4. **强制覆盖只读文件**
   ```bash
   shred -f -u readonly_file.txt
   ```

5. **显示详细进度**
   ```bash
   shred -v -n 5 secret.txt
   ```

6. **安全删除多个文件**
   ```bash
   shred -u file1.txt file2.txt file3.txt
   ```

7. **完整安全删除（多次覆盖 + 零填充 + 删除）**
   ```bash
   shred -v -n 5 -z -u important_file.bin
   ```

8. **仅覆盖不删除**
   ```bash
   shred -n 3 file.txt
   ```

## WinuxCmd 实现状态

**已实现** - 支持 `-f`/`--force`、`-n`/`--iterations`、`-u`/`--remove`、`-z`/`--zero`、`-v`/`--verbose` 选项。使用 `CryptGenRandom` 生成随机数据，支持多次覆盖、零填充和文件删除。
