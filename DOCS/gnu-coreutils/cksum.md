# cksum 命令

## 命令概述

`cksum` 命令用于计算文件的 CRC（循环冗余校验）校验和和字节数。它使用 CRC-32 算法来验证文件的完整性。

**命令格式：**
```
cksum [选项]... [文件]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-a` | `--algorithm=类型` | 使用指定的校验和算法 |
| | `--debug` | 输出调试信息 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-a, --algorithm=类型`
使用指定的校验和算法。类型可以是：
- `sysv`：System V 校验和算法
- `bsd`：BSD 校验和算法
- `crc`：CRC 校验和算法（默认）

### `--debug`
输出调试信息，显示算法的详细计算过程。

### `--help`
显示帮助信息并退出。

### `--version`
显示版本信息并退出。

## 使用示例

1. **计算文件的 CRC 校验和（默认）**
   ```bash
   cksum file.txt
   ```

2. **计算文件的 System V 校验和**
   ```bash
   cksum -a sysv file.txt
   ```

3. **计算文件的 BSD 校验和**
   ```bash
   cksum -a bsd file.txt
   ```

4. **计算多个文件的校验和**
   ```bash
   cksum file1.txt file2.txt
   ```

5. **从标准输入计算校验和**
   ```bash
   echo "Hello World" | cksum
   ```

6. **输出调试信息**
   ```bash
   cksum --debug file.txt
   ```

## WinuxCmd 实现状态

**已实现**