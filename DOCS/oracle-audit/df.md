# df 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe df`（`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**
- 方法：比较表头、列语义、单位、`Use%`、退出码；卷集合与绝对数值随平台不同，按 noise 归一；证据 `work/out/df-final.txt`（22 例）

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| 命令行/卷枚举 | `main` → `get_all_entries` | `src/df.c:1585` / `src/df.c:1485` |
| 单卷取值 | `get_dev` / `get_point` | `src/df.c:1013` / `src/df.c:1373` |
| 表格渲染/对齐 | `print_table` | `src/df.c:377` |
| `--output` 字段解码 | `decode_output_arg` | `src/df.c:431` |
| 合计行 | `add_to_grand_total` | `src/df.c:979` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| F01 | 默认列 | `Filesystem 1K-blocks Used Available Use% Mounted on` | 列名与语义一致；卷集合/设备名为平台差异 | noise-platform |
| F02 | `-h` | 1024 进制 `220G` | 一致 | 一致 |
| F03 | `-i` | inode 统计数字 | NTFS 不支持 → 全 `-` | noise-platform |
| F04 | `-T` / `--print-type` | 追加 `Type` 列 | 一致（`NTFS`） | 一致 |
| F05 | `-k` | 1K 块 | 一致 | 一致 |
| F06 | `-m` | 1M 块 | 一致 | 一致 |
| F07 | `--total` | 末尾 `total` 行，`Use%` 按合计重算 | 一致 | 一致 |
| F08 | `-a` | 含 0 块伪文件系统 | 一致（Windows 无对应项） | 一致 |
| F09 | `-l` | 仅本地文件系统 | 一致 | 一致 |
| F10 | `-P` | POSIX 格式（`1024-blocks`/`Capacity`） | 一致 | 一致 |
| F11 | 不存在路径 | `df: no-such: No such file or directory`，rc=1 | 逐字节一致 | 一致 |
| F12 | `--output=source,fstype,size,...` | 表头/列序一致；**末列后无尾随空格** | 列序一致，但每行目标列右侧补空格（`D:\\        `），表头未按同样宽度补齐 | compat-gap |
| F13 | `-B 1M` | 1M 块 | 一致 | 一致 |
| F14 | `--si` | 1000 进制（220G→237G） | 一致 | 一致 |
| F15 | `--no-sync` | 不做 sync | 一致 | 一致 |
| F16 | `df FILE` | 显示该文件所在卷 | 一致（`D:\` / `/`） | 一致 |
| F17 | `-t ext4`（本机无） | 无匹配 → rc=1 `no file systems processed` | 逐字节一致 | 一致 |
| F18 | `-t xfs`（本机无） | 同上 | 逐字节一致 | 一致 |
| F19 | 非法选项 | `unrecognized option`，rc=1 | 一致 | 一致 |
| F20 | `-h --total` | 合计行人类可读 | 一致 | 一致 |
| F21 | `--output=fstype,target` | 两列，无尾随空格 | 同样带尾随空格 | compat-gap |
| F22 | `--sync` | 先 sync | 一致 | 一致 |

## 三、差异复现

| # | 复现 | GNU expected（`cat -A`） | WinuxCmd actual |
|---|---|---|---|
| 1 | `df --output=source,target /` | `Filesystem     Mounted on$` / `/dev/sdd       /$`（无尾随空格） | `Filesystem Mounted on` / `D:\        D:\       `（目标列右侧补空格；表头列宽不一致） |

PowerShell 原样：

```powershell
& "...\df.exe" --output=source,target .
```

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1078 | 缺 `-F`/`-m` | `-m` 通过；`-F` 本轮未测（GNU 9.4 在 Linux 也无 `-F`，属 Solaris 兼容项） |
| #981 | 忽略 `BLOCK_SIZE` | 未覆盖 |
| #990 | `-h` 列宽与 GNU 不一致 | 通过（表头/间隔一致） |
| #138 | 无参数只显示一个卷 | 通过（枚举 C:–L:） |
| #1230 | 裸盘符根解析 | 未覆盖 |
| #308 | `-m` 失效 | 通过 |

## 五、计数

- 契约行：**22**
- 一致：**18**　compat-gap：**2**　noise-platform：**2**
- 立案：1 条 —— #1657（F12+F21）
