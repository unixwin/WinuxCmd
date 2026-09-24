# rmdir 命令

## 命令概述

`rmdir` 命令用于删除空目录。如果目录不为空，操作将失败。

**命令格式：**
```
rmdir [选项]... DIRECTORY...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| | `--ignore-fail-on-non-empty` | 忽略因目录非空而导致的失败 |
| `-p` | `--parents` | 删除目录及其父目录（如果它们也为空） |
| `-v` | `--verbose` | 为每个处理的目录输出诊断信息 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### `--ignore-fail-on-non-empty`
忽略因目录非空而导致的失败。默认情况下，如果目录不为空，`rmdir` 会报错。

### `-p, --parents`
删除目录，然后尝试删除其父目录（如果它们也为空）。这允许一次删除整个空目录路径。

### `-v, --verbose`
为每个处理的目录输出诊断信息。

## 使用示例

1. **删除单个空目录**
   ```bash
   rmdir emptydir
   ```

2. **删除多个空目录**
   ```bash
   rmdir dir1 dir2 dir3
   ```

3. **删除空目录链**
   ```bash
   rmdir -p path/to/empty/nested/dir
   ```

4. **显示删除过程**
   ```bash
   rmdir -v emptydir
   ```

5. **忽略非空目录的错误**
   ```bash
   rmdir --ignore-fail-on-non-empty dir1 dir2
   ```

6. **删除嵌套空目录并显示信息**
   ```bash
   rmdir -pv a/b/c
   ```

## WinuxCmd 实现状态

**待实现**