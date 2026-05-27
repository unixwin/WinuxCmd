# dd 命令

## 命令概述

`dd` 命令用于按块转换和复制文件。它可以从标准输入或文件读取数据，进行转换后写入标准输出或文件。常用于创建磁盘映像、备份引导扇区、生成特定大小的文件等。

**命令格式：**
```
dd [操作数]...
```

## 完整参数列表

`dd` 使用操作数（operand）而非传统选项格式，操作数以 `名称=值` 的形式指定。

| 操作数 | 说明 |
|--------|------|
| `if=FILE` | 从指定文件读取（默认标准输入） |
| `of=FILE` | 写入到指定文件（默认标准输出） |
| `ibs=BYTES` | 每次读取的字节数（默认 512） |
| `obs=BYTES` | 每次写入的字节数（默认 512） |
| `bs=BYTES` | 同时设置读取和写入的块大小 |
| `cbs=BYTES` | 转换缓冲区大小 |
| `count=N` | 仅复制 N 个输入块 |
| `skip=N` | 跳过开头 N 个 ibs 大小的输入块 |
| `seek=N` | 跳过开头 N 个 obs 大小的输出块 |
| `conv=CONVS` | 按逗号分隔的符号列表进行转换 |
| `status=LEVEL` | 控制诊断输出 |

## 参数详细说明

### `if=FILE`
指定输入文件。如果未指定，从标准输入读取。

### `of=FILE`
指定输出文件。如果未指定，写入到标准输出。

### `ibs=BYTES`
每次读取操作的块大小，以字节为单位。默认为 512 字节。支持以下后缀：
- `c`：1 字节
- `w`：2 字节
- `b`：512 字节
- `kB`：1000 字节
- `K` / `KiB`：1024 字节
- `MB`：1000000 字节
- `M` / `MiB`：1048576 字节
- `GB`：1000000000 字节
- `G` / `GiB`：1073741824 字节

### `obs=BYTES`
每次写入操作的块大小，以字节为单位。默认为 512 字节。支持的后缀同 `ibs`。

### `bs=BYTES`
同时设置输入和输出的块大小。此选项会覆盖 `ibs` 和 `obs` 的设置。

### `cbs=BYTES`
转换缓冲区大小，用于 `ascii`/`ebcdic` 转换。

### `count=N`
仅复制 N 个输入块后停止。如果为 0，则不限制。

### `skip=N`
在开始复制前，跳过输入文件开头的 N 个 `ibs` 大小的块。

### `seek=N`
在开始写入前，跳过输出文件开头的 N 个 `obs` 大小的块。

### `conv=CONVS`
按逗号分隔的转换符号列表，可选值包括：
- `notrunc`：不截断输出文件
- `sync`：用 NUL 填充每个输入块至 `ibs` 大小
- `noerror`：遇到读取错误时继续

### `status=LEVEL`
控制诊断输出，可选值：
- `none`：不显示传输统计信息
- `noxfer`：不显示最终传输统计信息
- `progress`：定期显示传输进度（默认显示统计信息）

## 使用示例

1. **创建指定大小的空文件**
   ```bash
   dd if=/dev/zero of=output.bin bs=1M count=10
   ```

2. **复制磁盘映像**
   ```bash
   dd if=/dev/sda of=disk.img bs=4M
   ```

3. **备份引导扇区**
   ```bash
   dd if=/dev/sda of=mbr.bin bs=512 count=1
   ```

4. **从文件特定偏移开始读取**
   ```bash
   dd if=largefile.bin of=chunk.bin bs=1 skip=1024 count=256
   ```

5. **写入到输出文件的特定位置**
   ```bash
   dd if=data.bin of=output.bin bs=1 seek=512 conv=notrunc
   ```

6. **生成随机数据文件**
   ```bash
   dd if=/dev/urandom of=random.bin bs=1024 count=1
   ```

7. **显示进度信息**
   ```bash
   dd if=largefile.iso of=/dev/null bs=4M status=progress
   ```

8. **跳过输出文件开头**
   ```bash
   dd if=input.bin of=output.bin bs=512 seek=1
   ```

## WinuxCmd 实现状态

**已实现** - 支持 `if`、`of`、`ibs`、`obs`、`bs`、`cbs`、`count`、`skip`、`seek` 操作数，支持 `conv=notrunc,sync,noerror` 转换选项，支持 `status=none,noxfer,progress` 状态选项。支持 `K`/`KiB`/`M`/`MiB`/`G`/`GiB` 等大小后缀。
