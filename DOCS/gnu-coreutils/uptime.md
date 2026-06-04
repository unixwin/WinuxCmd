# uptime 命令

## 命令概述

`uptime` 命令用于显示系统运行了多长时间，以及当前时间、已登录用户数和系统负载平均值。

**命令格式：**
```
uptime [选项]...
```

## 完整参数列表

| 短选项 | 长选项 | 说明 |
|--------|--------|------|
| `-p` | `--pretty` | 以美观格式显示运行时间 |
| `-s` | `--since` | 显示系统启动时间 |
| | `--help` | 显示帮助信息并退出 |
| | `--version` | 输出版本信息并退出 |

## 参数详细说明

### `-p, --pretty`
以人类可读的美观格式显示运行时间，而不是默认的单行格式。

输出示例：
```
up 3 days, 5 hours, 27 minutes
```

### `-s, --since`
显示系统最后一次启动的时间和日期。

输出示例：
```
2024-01-15 10:30:45
```

## 默认输出格式

默认输出包含以下信息：
```
 HH:MM:SS up D days, H:MM, N users, load average: X.XX, X.XX, X.XX
```

| 字段 | 说明 | 示例 |
|------|------|------|
| 当前时间 | 系统当前时间 | `14:30:45` |
| 运行时间 | 系统已运行的时间 | `up 3 days, 5:27` |
| 已登录用户 | 当前登录的用户数 | `2 users` |
| 负载平均值 | 1/5/15 分钟平均负载 | `0.50, 0.75, 0.60` |

注意：在 Windows 上，负载平均值不可用，通常显示为 `N/A`。

## 使用示例

1. **显示默认 uptime 信息**
   ```bash
   uptime
   ```
   输出示例：` 14:30:45 up 3 days, 5:27, 2 users, load average: N/A, N/A, N/A`

2. **以美观格式显示运行时间**
   ```bash
   uptime -p
   ```
   输出示例：`up 3 days, 5 hours, 27 minutes`

3. **显示系统启动时间**
   ```bash
   uptime -s
   ```
   输出示例：`2024-01-15 10:30:45`

4. **在脚本中获取运行时间**
   ```bash
   UPTIME=$(uptime -p)
   echo "System has been running for: $UPTIME"
   ```

5. **在监控脚本中使用**
   ```bash
   #!/bin/bash
   echo "=== System Status ==="
   uptime
   echo ""
   echo "=== Disk Usage ==="
   df -h
   ```

6. **获取系统启动时间用于日志分析**
   ```bash
   BOOT_TIME=$(uptime -s)
   echo "System booted at: $BOOT_TIME"
   journalctl --since "$BOOT_TIME"
   ```

7. **显示帮助信息**
   ```bash
   uptime --help
   ```

8. **显示版本信息**
   ```bash
   uptime --version
   ```

## 负载平均值说明

负载平均值表示系统在 1、5、15 分钟内的平均进程数（等待运行或正在运行的进程）。

| 负载值 | 含义 |
|--------|------|
| < 1.0 | 系统空闲 |
| 1.0 | 系统满载，但无进程等待 |
| > 1.0 | 有进程在等待 CPU |
| > CPU核心数 | 系统过载 |

注意：Windows 系统不提供负载平均值，WinuxCmd 显示 `N/A`。

## WinuxCmd 实现状态

**已实现**

支持的选项：
- `-p, --pretty` - 美观格式显示运行时间
- `-s, --since` - 显示系统启动时间

实现说明：
- 运行时间通过 `GetTickCount64()` 获取（毫秒精度）
- 启动时间通过当前时间减去运行时间计算
- 负载平均值在 Windows 上不可用，显示为 `N/A`
- 已登录用户数暂未实现

默认输出格式：
```
 HH:MM:SS up D days, H:MM, load average: N/A, N/A, N/A
```
