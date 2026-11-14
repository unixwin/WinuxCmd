# Oracle 审计看板（#1462 全命令面行为契约核验）

- 本批命令：`ls, cp, mv, rm, ln, stat, du, df`（8 条）
- 基线：`origin/main = b781ff35`；审计工作树 `D:/repo/wc-oracle`
- 二进制：`build-vs/winuxcmd.exe`，`WinuxCmd 1.2.1`（`winuxcmd --version`，Release）
- GNU oracle：WSL2 GNU **coreutils 9.4**，命令一律经脚本文件（`wsl bash <file>`）执行；`LC_ALL=C`、`TZ=UTC`、stdout 为管道
- 上游权威：coreutils 9.4 `src/*.c`（`ls.c/cp.c/copy.c/mv.c/rm.c/remove.c/ln.c/stat.c/du.c/df.c`），每条差异标注 owning 函数与行号
- 判定口径：compat-gap = 与 GNU 行为/退出码/字节输出不一致；wording = 语义一致但诊断/排版文字不同；noise-platform / noise-locale = Windows 文件系统、身份模型、Unicode/locale 导致的不可比噪声

## 一、总看板

| 命令 | 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 | 新 issue |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ls | 70 | 44 | 13 | 1 | 7 | 5 | 0 | 8 |
| cp | 30 | 16 | 8 | 2 | 1 | 0 | 3 | 2 |
| mv | 22 | 20 | 1 | 1 | 0 | 0 | 0 | 1 |
| rm | 23 | 20 | 1 | 0 | 2 | 0 | 0 | 1 |
| ln | 23 | 10 | 1 | 2 | 10 | 0 | 0 | 1 |
| stat | 28 | 15 | 8 | 1 | 3 | 0 | 1 | 4 |
| du | 30 | 25 | 3 | 0 | 2 | 0 | 0 | 1（另 2 项并入他单） |
| df | 22 | 18 | 2 | 0 | 2 | 0 | 0 | 1 |
| **合计** | **248** | **168** | **37** | **7** | **27** | **5** | **4** | **19** |

> 说明：同一缺陷跨多个契约行的，按“一个根因一张 issue”合并（例如 ls 的符号链接显示 + size 为 #1639；ls/du 的 `--si` 为 #1644；stat/du 的 TZ 为 #1655）。因此 gap 行数 37 ≠ issue 数 19。

## 二、本批立案（19 张，全部 `compat-gap`）

| # | 命令 | 摘要 | 严重度 |
|---|---|---|---|
| 1639 | ls | `-l` 符号链接缺 ` -> target`，size 恒 0 | 中 |
| 1640 | ls | `-L` 无法解引用符号链接（rc=2） | 中高 |
| 1641 | ls | `-s` 大小列不右对齐；`-x` 列距多 1 空格、窄宽度少一列 | 低 |
| 1642 | ls | `--group-directories-first` 漏掉指向目录的链接 | 低 |
| 1643 | ls | `-Z` 报 not supported（GNU 输出 `?`，rc=0） | 低 |
| 1644 | ls/du | `--si` 未生效（1024 基数、大写 K） | 低 |
| 1645 | ls | 非法 `--sort` rc=2 且候选表不同 | 低 |
| 1646 | ls | `-b`/`--quoting-style=shell` 引用字节不同 | 中低 |
| 1647 | cp | 任何符号链接源都无法复制（默认/`-d`/`-L`/`-r`/`-a`） | **高** |
| 1648 | cp | `-i` 拒绝时 rc=0（GNU 1） | 中 |
| 1649 | mv | 目录覆盖普通文件时静默替换（**数据丢失**） | **高** |
| 1650 | rm | 空操作数 `''` 被当成缺操作数 | 低 |
| 1651 | ln | `-i`/`-f` 不遵循 last-wins | 中低 |
| 1652 | stat | `-L` 对符号链接报 ENOENT | 中高 |
| 1653 | stat | `-` 操作数不支持（#1089 回归） | 中 |
| 1654 | stat | `%N`/默认头缺箭头，链接 `%s`=0（#1058/#1611 回归） | 中 |
| 1655 | stat/du | `TZ` 被忽略（恒 +0800） | 中 |
| 1656 | du | `-L` 不跟随目录符号链接参数；错误缺原因 | 中 |
| 1657 | df | `--output` 末列尾随空格、表头未对齐 | 低 |

## 三、回归核对（已关闭 issue 是否被重新打破）

通过（本轮实测仍符合 GNU）：

- ls：#1504（管道下 COLUMNS）、#1536（COLUMNS 解码面）、#1542/#1543（网格严格 `<` 拟合）、#993（文件/目录分区）、#1051（通配符/尾斜杠）、#1364（裸 `/`）、#1067（`--hyperlink`/`--dired`）、#1411（伪选项）、#1572（`.exe` 透明语义）、#977（LS_COLORS）
- cp：#1435（自复制前缀）、#1324（`-i` 答案消费）、#1411（`-g`）
- rm：#1433（删目录链接）、#1327（`-i` 文案）、#1328（`-v` 目录/路径）、#1298（`-v` 生效）
- ln：#1367（悬空目标）、#1308 的备份/悬空部分
- stat：#1021（`%t %T %f %r`）、#1020（`%z/%Z`）、#1069（`--cached`）、#1019（诊断走 stderr）
- du：#1372（`--inodes`）、#1351（`-l` 链接计数）、#1159（尾斜杠双斜杠）、#1039（分配块）
- mv：#1325（argv 扫描误判 `-n/-i`）、#1068（`--exchange` 已实现，属 GNU 9.5 语义）
- df：#990（`-h` 列宽）、#138（多卷）、#308（`-m`）

**发现回归**：stat #1058（`-L`/箭头/链接 size）、#1089（`-` 操作数）、#1611（链接 `%s`）；du #1059（`-L` 目录链接）。

## 四、方法学备注（重要）

1. **审计通道伪影**：最初用 WSL interop 直接启动 Windows 版可执行文件时，子进程继承了控制台句柄，`ls` 误判终端宽度、`COLUMNS` 看似失效（曾误判 #1504 回归）。改为 `work/win-run.ps1` 用 .NET `Process` 重定向原始字节、并经参数显式注入环境变量后，`COLUMNS/TZ/LS_COLORS/TIME_STYLE` 全部生效。#1504/#1536/#1542/#1543 实际仍通过。
2. **字节捕获**：Windows 侧用 `StandardOutput.BaseStream.CopyToAsync` 落盘，避免 PowerShell 重定向的 UTF-16 转码；GNU 侧 `subprocess` 直接拿 bytes。
3. **fixture 时间固定**：所有文件的 mtime/atime 固定为 `2020-01-02 03:04:05 UTC`（ls 额外设置 aaa/bbb/ccc 的 atime），使 `-p`/`-a`/`--time` 可逐值比较；执行后对整棵树做快照（路径/类型/size/mtime/hash/硬链接组）比对副作用。
4. **权限模型**：本机未启用开发者模式，`ln -s`/`cp -s` 一律 `Operation not permitted`，因此“符号链接创建后的语义”标 noise-platform，不计 gap；而“读取/复制已有符号链接”的失败（cp #1647、ls #1640、stat #1652）是真实缺陷。
5. **安全边界**：未执行 `rm --preserve-root /`、未跑未隔离全量 ctest、未改主仓 `D:/repo/unixwin-winuxcmd`。

## 五、复现资产（本地，未提交）

- `DOCS/oracle-audit/<cmd>.md`：8 张契约表
- `DOCS/oracle-audit/work/`：
  - `drive.py`（用例驱动）、`specs/<cmd>.json`（用例定义）、`fixtures.py`（双树 fixture）
  - `win-run.ps1`（Windows 原始字节执行器）、`out/<cmd>-final.txt`（原始比对输出）
  - `run.sh`（WSL 入口）、`iss-*.sh|ps1`、`iss2-*.sh|ps1`（issue 最小复现）
  - `file-issues.py`、`issue-bodies/`（立案正文留档）

## 六、未完成项

- cp：`-x`、`--sparse`、`--reflink=always`、`--preserve=mode,timestamps` 未覆盖（需稀疏文件/块克隆/跨卷环境）。
- ln：符号链接创建成功后的 `-r/-v/--backup/-n` 语义未验证（缺 `SeCreateSymbolicLinkPrivilege`）。
- stat：`--printf` 的 `\t \\ \xHH \NNN` 转义未逐项覆盖；`--format` 其它指令（`%c %C %h` 大值）未做边界。
- df：`-F`（Solaris 兼容）、`BLOCK_SIZE` 环境变量未覆盖。
- 跨卷/跨盘符（`D:`↔`E:`）、`>MAX_PATH`、盘符方言（`/d/...`、`/cygdrive`）不在本批 fixture 范围。
- mv：#1326（`-u` 与 `--update=` 顺序）、#1365（跨卷链接变副本）需跨卷环境另测。
