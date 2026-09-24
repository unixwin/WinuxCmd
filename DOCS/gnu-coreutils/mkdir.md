# mkdir 命令

## 命令概述

`mkdir` 命令用于创建目录（文件夹）。如果目录已存在，除非使用 `-p` 选项，否则会报错。

**命令格式：**
```
mkdir [选项]... DIRECTORY...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-m` | `--mode=MODE` | 设置文件权限模式（类似 chmod） |
| `-p` | `--parents` | 需要时创建父目录，如果目录已存在不报错 |
| `-v` | `--verbose` | 为每个创建的目录打印消息 |
| `-Z` | `--context[=CTX]` | 设置 SELinux 安全上下文 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `-m, --mode=MODE`
设置创建目录的权限模式。MODE 可以是八进制数（如 755）或符号表示法（如 u=rwx,g=rx,o=rx）。默认情况下，权限由 umask 决定。

### `-p, --parents`
如果需要，创建父目录。如果目录已存在，不报错。这允许一次创建整个目录路径。

### `-v, --verbose`
为每个创建的目录打印一条消息。

### `-Z, --context[=CTX]`
设置创建目录的 SELinux 安全上下文。如果未指定 CTX，则根据当前默认上下文设置。如果没有 SELinux 支持，此选项无效。

## 使用示例

1. **创建单个目录**
   ```bash
   mkdir newdir
   ```

2. **创建多级目录**
   ```bash
   mkdir -p path/to/nested/dir
   ```

3. **设置目录权限**
   ```bash
   mkdir -m 755 mydir
   ```

4. **使用符号权限创建目录**
   ```bash
   mkdir -m u=rwx,g=rx,o=rx mydir
   ```

5. **创建多个目录**
   ```bash
   mkdir dir1 dir2 dir3
   ```

6. **显示创建过程**
   ```bash
   mkdir -v newdir
   ```

7. **创建带父目录的路径并显示信息**
   ```bash
   mkdir -pv /tmp/a/b/c
   ```

8. **在当前目录创建多个子目录**
   ```bash
   mkdir -p project/{src,bin,doc}
   ```

## WinuxCmd 实现状态

**已实现**