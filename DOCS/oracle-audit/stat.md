# stat 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe stat`（Rust2 实现，`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**，LC_ALL=C、TZ=UTC
- 方法：固定格式串逐字节比较；时间戳带 `TZ=UTC`；证据 `work/out/stat-final.txt`（28 例）

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| 文件状态输出 | `do_stat` | `src/stat.c:1456` |
| `-c` 格式解析/转义 | `print_it` | `src/stat.c:1130` |
| 字段渲染表 | `print_stat` | `src/stat.c:1503` |
| 默认/terse 格式生成 | `default_format` | `src/stat.c:1661` |
| 文件系统 `-f` | `print_statfs` | `src/stat.c:859` |
| 时间字段 | `out_epoch_sec` / `human_time` | `src/stat.c:727` / `src/stat.c:644` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| T01 | 默认 8 行格式 | `Links:` 右对齐、属主/组名为名字 | 数值列宽较窄，组名 `UNKNOWN` | wording |
| T02 | `-c %n` | 原样操作数名 | 一致 | 一致 |
| T03 | `-c %s` | 普通文件字节数 | 一致 | 一致 |
| T04 | `-c %F` | `regular file`/`directory`/`symbolic link` | 一致 | 一致 |
| T05 | `-c %a %A` | 八进制 + rwx 串 | 一致（0644 等） | 一致 |
| T06 | `-c %u %g %U %G` | 数字 + 解析名 | 数字一致；`%G` 为 `UNKNOWN`（Windows 无组库） | noise-platform |
| T07 | `-c %i %h %d %D` | inode/链接数/设备 | 结构一致 | 一致 |
| T08 | `-c %t %T %f %r` | 设备类型/原始 mode | 一致（`0 0 81a4 0`） | 一致 |
| T09 | `-c %y %x %z` | 纳秒精度 + 数字时区；`TZ=UTC` 时 `+0000` | 精度正确，但 `+0800`（**忽略 TZ**） | compat-gap |
| T10 | `-c %b %B %o` | 块数/块大小/IO 块 | 结构一致 | 一致 |
| T11 | `-c %w %W` | 出生时间 + epoch | epoch 一致；显示时区随 T09 | compat-gap |
| T12 | `-c %N` | `'lnk' -> 'a.txt'` | `'lnk'`（缺目标串） | compat-gap |
| T13 | `--printf '%s\n'` | 解释 `\n` 且不追加换行 | 一致 | 一致 |
| T14 | `--printf` 转义 `\t \\ \xHH \NNN` | 全部解释 | 用例只构造出缺操作数路径（双方一致），转义未逐项验证 | 未覆盖 |
| T15 | `-t` terse | 空格分隔 15 列 | 结构一致 | 一致 |
| T16 | `-L` | 解引用符号链接，`%F`=regular file | `stat: cannot statx 'lnk': No such file or directory`，rc=1 | compat-gap |
| T17 | 符号链接默认头 | `File: lnk -> a.txt` | `File: lnk` | compat-gap |
| T18 | 悬空链接 | 头含 ` -> nope`，rc=0 | 无箭头（rc 同为 0） | compat-gap |
| T19 | `-` 操作数（stdin） | `stat -c %F -` → `fifo`，rc=0 | `cannot statx '-': No such file or directory`，rc=1 | compat-gap |
| T20 | 不存在路径 | `cannot statx ...`，rc=1 | 逐字节一致 | 一致 |
| T21 | 未知指令 `%q` | 原样输出 `?`，rc=0 | 一致 | 一致 |
| T22 | `--cached=never` | 接受 | 一致（#1069 回归通过） | 一致 |
| T23 | `-f` 默认输出 | 6 行、字段右对齐 | 结构相同；列间空格/`ID` 进制不同 | noise-platform |
| T24 | `-f -c %T` | `ext2/ext3` 等 | `NTFS` | noise-platform |
| T25 | 非法选项 | rc=1 | 一致 | 一致 |
| T26 | `%` 字面量 | 输出 `%` | 一致 | 一致 |
| T27 | 多操作数 `-c '%n:%s'` | 每行一条 | 一致 | 一致 |
| T28 | 符号链接 `%s|%N` | `5|'lnk' -> 'a.txt'` | `0|'lnk'` | compat-gap |

## 三、差异复现

fixture：`a.txt`（6 字节）、`lnk -> a.txt`、`dangling -> nope`。

| # | 复现 | GNU expected | WinuxCmd actual |
|---|---|---|---|
| 1 | `stat -L -c '%F' lnk` | `regular file`，rc=0 | `stat: cannot statx 'lnk': No such file or directory`，rc=1 |
| 2 | `echo x \| stat -c '%F' -` | `fifo`，rc=0 | `stat: cannot statx '-': No such file or directory`，rc=1 |
| 3 | `stat -c '%N\|%s' lnk` | `'lnk' -> 'a.txt'\|5` | `'lnk'\|0` |
| 4 | `stat lnk` | `File: lnk -> a.txt` | `File: lnk` |
| 5 | `TZ=UTC stat -c '%y' a.txt` | `2020-01-02 03:04:05.000000000 +0000` | `2020-01-02 11:04:05.000000000 +0800` |

复现（PowerShell 原样）：

```powershell
$env:TZ = "UTC"
& "...\stat.exe" -L -c "%F" lnk
"x" | & "...\stat.exe" -c "%F" -
& "...\stat.exe" -c "%N|%s" lnk
```

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1058 | `-L` 被忽略、头缺 `-> target`、链接 size=0 | **回归**：三项均复现（T16/T17/T28） |
| #1089 | `-` 应 fstat stdin | **回归**：现报 ENOENT（T19） |
| #1611 | 符号链接 `%s` 应为目标串长 | **回归**：仍为 0（T28） |
| #1021 | `%c %C %f %r %t %T` 未实现 | 通过（`%t %T %f %r` 正确） |
| #1020 | `%z/%Z` 用错时间 | 通过（结构正确） |
| #1022 | 时间戳缺纳秒/时区 | 纳秒/时区格式已具备，但 TZ 未生效（T09） |
| #1023 | 默认与 `-t` 格式不完整 | 结构可用；仍有列宽差异（T01） |
| #1069 | `--cached` 恒失败 | 通过 |
| #1019 | 诊断写到 stdout | 通过（stderr） |
| #1612 系 | `.exe` 兄弟解析 | 未覆盖 |
| #1244 | 盘符方言 | 未覆盖 |

## 五、计数

- 契约行：**28**
- 一致：**15**　compat-gap：**8**　wording：**1**　noise-platform：**3**　未覆盖：**1**
- 立案：4 条 —— #1652（T16）、#1653（T19）、#1654（T12+T17+T18+T28）、#1655（T09+T11）
