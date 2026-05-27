# chmod 命令

## 命令概述

`chmod` 命令用于更改文件或目录的访问权限（读、写、执行）。

**命令格式：**
```
chmod [选项]... MODE[,MODE]... FILE...
chmod [选项]... OCTAL-MODE FILE...
chmod [选项]... --reference=RFILE FILE...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-c` | `--changes` | 类似 verbose，但仅在进行更改时报告 |
| `-f` | `--silent, --quiet` | 抑制大多数错误消息 |
| `-v` | `--verbose` | 为每个处理的文件输出诊断信息 |
| | `--no-preserve-root` | 不特殊处理 '/'（默认） |
| | `--preserve-root` | 未能递归处理 '/' |
| | `--reference=RFILE` | 使用 RFILE 的模式 |
| `-R` | `--recursive` | 递归更改文件和目录 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 显示版本信息并退出 |

## 参数详细说明

### MODE
权限模式，可以是以下格式：

**符号模式：**
- `[ugoa...][[+-=][perms...]...]`
  - `u`：文件所有者
  - `g`：文件所属组
  - `o`：其他用户
  - `a`：所有用户（等价于 ugo）
  - `+`：添加权限
  - `-`：移除权限
  - `=`：设置精确权限

- 权限字符：
  - `r`：读权限
  - `w`：写权限
  - `x`：执行权限
  - `X`：仅当文件是目录或已有执行权限时设置执行权限
  - `s`：设置用户或组 ID
  - `t`：粘着位

**八进制模式：**
- 4 位八进制数（可选第一位用于设置 UID/GID/粘着位）
  - 第一位：特殊权限（4=设置 UID，2=设置 GID，1=粘着位）
  - 第二位：所有者权限（4=读，2=写，1=执行）
  - 第三位：组权限
  - 第四位：其他用户权限

### `-c, --changes`
类似 verbose，但仅在进行更改时报告。

### `-f, --silent, --quiet`
抑制大多数错误消息。

### `-v, --verbose`
为每个处理的文件输出诊断信息。

### `--no-preserve-root`
不特殊处理 '/'（默认行为）。

### `--preserve-root`
未能递归处理 '/' 目录。

### `--reference=RFILE`
使用 RFILE 的模式，而非指定 MODE。

### `-R, --recursive`
递归更改文件和目录的权限。

## 使用示例

1. **设置文件权限（八进制）**
   ```bash
   chmod 755 script.sh
   ```

2. **设置文件权限（符号模式）**
   ```bash
   chmod u=rwx,go=rx script.sh
   ```

3. **添加执行权限**
   ```bash
   chmod +x script.sh
   ```

4. **移除组和其他用户的写权限**
   ```bash
   chmod go-w file.txt
   ```

5. **递归设置目录权限**
   ```bash
   chmod -R 755 /path/to/dir
   ```

6. **使用参考文件的权限**
   ```bash
   chmod --reference=ref_file target_file
   ```

7. **设置粘着位**
   ```bash
   chmod +t /tmp
   ```

8. **设置 SUID 位**
   ```bash
   chmod u+s program
   ```

9. **显示详细更改信息**
   ```bash
   chmod -v 644 file.txt
   ```

10. **设置目录权限（允许进入）**
    ```bash
    chmod 755 directory
    ```

## WinuxCmd 实现状态

**已实现**