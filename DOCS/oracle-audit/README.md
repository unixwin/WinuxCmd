# Oracle 审计看板（#1462 全命令面行为契约核验）— WP-K 下一波

- 本批命令：`chmod, chown, readlink, touch, dd, split, cksum, uniq, comm, join`（10 条）
- 上一波：`ls, cp, mv, rm, ln, stat, du, df`（`D:/repo/wc-oracle`，248 行 / 19 issues）
- 基线：`origin/main = f0cf6b17`；审计工作树 `D:/repo/wc-auditnext`（detached，未 commit / 未 push / 未改代码）
- 二进制：`build-vs/winuxcmd.exe`，`WinuxCmd 1.2.1`（Release，`WINUXCMD_ENABLE_RUST2=ON`；readlink/touch/join 走 rust2，其余为 C++ applet）
- GNU oracle：WSL2 GNU **coreutils 9.4**（`cksum (GNU coreutils) 9.4`），命令一律经脚本文件（`wsl bash <file>`）
- 上游权威：coreutils 9.4 源码（`src/*.c` + gnulib `parse-datetime.y`/`canonicalize.c`/`userspec.c`），每条差异标注 owning 函数与行号
- 环境：`LC_ALL=C`、`TZ=UTC`、`TERM=dumb`；两侧均显式注入（`win-run.ps1` 的 `EnvironmentVariables`，修掉了上一波“WSL interop 继承控制台/TZ 丢失”的通道伪影）
- 判定口径：compat-gap = 行为/退出码/字节输出不一致；wording = 语义一致但诊断/排版文字不同；noise-platform / noise-locale = Windows 文件系统、身份模型、reparse 链接、locale 导致的不可比噪声

## 一、总看板

| 命令 | 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 | 新 issue |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| chmod | 36 | 31 | 0 | 2 | 3 | 0 | 0 | 0 |
| chown | 32 | 8 | 9 | 9 | 5 | 1 | 0 | 3 |
| readlink | 26 | 15 | 1 | 1 | 9 | 0 | 0 | 0（`-V` 并入全局） |
| touch | 30 | 19 | 8 | 2 | 1 | 0 | 0 | 3（TZ 并入 #1655） |
| dd | 31 | 22 | 6 | 2 | 1 | 0 | 0 | 2 |
| split | 33 | 30 | 1 | 1 | 1 | 0 | 0 | 1 |
| cksum | 35 | 28 | 4 | 3 | 0 | 0 | 0 | 2（`-V` 并入全局） |
| uniq | 32 | 27 | 1 | 3 | 0 | 0 | 1 | 0（`-V` 并入全局） |
| comm | 27 | 24 | 2 | 1 | 0 | 0 | 0 | 1（`-V` 并入全局） |
| join | 32 | 29 | 2 | 1 | 0 | 0 | 0 | 1（`-V` 并入全局） |
| **合计** | **314** | **233** | **34** | **25** | **20** | **1** | **1** | **14** |

> gap 行 34 ≠ issue 14：同一根因跨多行合并（chown 无变化 6 行 → 1 单；touch TZ 5 行 → #1655；dd conv 3 行 → 1 单；全局 `-V` 5 行 → 1 单）。表中 per-command 新 issue 合计 13，另加 1 张不归属单命令的 dispatcher `-V` issue（#1714）。

## 二、本批立案（14 张新 issue，全部 `compat-gap`）

| # | 命令 | 摘要 | 严重度 |
|---|---|---|---|
| 1701 | chown | 无变化 chown（chown $USER / `owner:` / `:` / `--reference` / `-h` / `-R`）返 rc1 `Operation not permitted`，GNU rc0 | 高 |
| 1702 | chown | `-v` 报告写 stderr；`-c` 无变化仍打印 retained（GNU stdout、`-c` 静默） | 中 |
| 1703 | chown | 空 user spec `''` 被拒，GNU 视为不改、rc0 | 低 |
| 1704 | touch | `-a -m` 同时给出只改 mtime，atime 丢失 | 中 |
| 1705 | touch | `-d/-t` 日期语法残差：裸 `fortnight`、`a fortnight`、`EST`、闰秒 `.60`、年 0000 被拒（#1382 残差） | 中低 |
| 1706 | touch | 空操作数 `''` 被误报 `missing file operand` | 低 |
| 1707 | dd | 统计缺 `FULL+PARTIAL` 形状与 `秒, kB/s` 尾段 | 中 |
| 1708 | dd | `conv=ascii/ebcdic/ibm/block/unblock/ucase/lcase/swab` 全部被拒（纯字节变换，无平台理由） | 高 |
| 1709 | cksum | `-c` 接受 CRC/untagged-CRC 校验行，GNU 9.4 报 no properly formatted（过度接受） | 中低 |
| 1710 | cksum | `-a crc32b` 被接受，GNU 9.4 无该算法（需裁定扩展） | 低 |
| 1711 | split | `-a` 溢出诊断丢实参与 ERANGE 后缀（#1524 残差） | 低 |
| 1712 | comm | `comm - -` rc0 无诊断，GNU rc1 `Bad file descriptor`（#429 残差） | 低 |
| 1713 | join | `-z` 仍按换行切记录，GNU 只认 NUL（#1008 残差） | 低 |
| 1714 | dispatcher | 全局把 `-V` 当 `--version`；GNU coreutils 对 cksum/readlink/uniq/comm/join 等报 invalid option | 低 |

### 并入既存 issue

- **#1655（OPEN，TZ 被忽略）**：追加 touch 复现与代码线索（`https://github.com/unixwin/WinuxCmd/issues/1655#issuecomment-6875870912`）。`TZ=UTC touch -d '2020-01-02 03:04:05'` 得 1577905445（+0800），GNU 1577934245；`touch.rs` 的 `finish_fixed()` 直接 `fs_win::from_local`。

## 三、回归核对（已关闭 issue 是否被重新打破）

通过（本轮实测仍符合 GNU）：

- chmod：#1331（`u+rw-x`/`g=u`/`a=`）、#987（`-v` changed/retained）、#347/#327（诊断与递归错误文本主面）
- readlink：#1491（`-f` 反斜杠路径）、#1231（`/d/` 裸盘符折叠）、#1246（`/cygdrive`、`/mnt` 折叠）——四种拼写均折叠为 `D:/repo/...`
- touch：#1382 的 empty / `@epoch.frac` / `next Monday` / `2 days ago` 面、#612（`2026-6-27 13:12`）、#823（多字节 `-t` 不 panic）、#431（闰秒输入不 panic，但拒绝而非接受 → #1705）
- dd：#364（skip 越界诊断）、#1195 同族（Ctrl-Z 不截断在 cksum 验证）、#1374（环境相关，未复测）
- split：#289（非法 chunks）、#1081（过时 `-NUMBER` 与 `---io-blksize`）、#356（空输入不产生空片）、#1306（`-dl` 打包短选项）
- cksum：#1362（`-b/-t` last-wins）、#1195（Ctrl-Z 不截断，`2049426561 9`）、#1084（`-b/-t` 接受）、#1033（md5/sha*/blake2b/sm3 面）、#1411（`-q/-s` 伪选项拒绝）、#1214（`--raw` 不 CRLF 注入）
- uniq：#1341（`-w -1`）、#1010（`--group=both` 单空行）、#1306（`-Dc` 打包；错误文案仍缺 Try → wording）
- comm：#1009（`--check-order` 保留断点前输出）、#1354（CRLF 不再剥离）、#1045（missing operand after FILE）
- join：#1432（行尾反斜杠按数据保留）、#1007（乱序诊断/合并）、#1070/#1411（`-k`/`--empty` 拒绝）、#1045、#1212（Ctrl-Z 不截断）

**发现回归 / 未完全修复**（已在本批新 issue 或看板注明）：

- touch #1382：裸 `fortnight`、`a fortnight` 仍拒绝 → #1705
- touch #355：具名时区 `EST` 仍拒绝 → #1705
- split #1524：`-a` 溢出消息仍缺实参/ERANGE → #1711
- comm #429：不再死锁，但 rc/诊断仍不同 → #1712
- join #1008：NUL 输入已正确，换行输入仍被切 → #1713
- chown #534（upstream-closed）：本仓产品路径仍复现 `-v` 走 stderr → #1702
- dd #366（bytes copied 已显示）：形状/速率仍缺 → #1707

## 四、方法学备注

1. **TZ 注入修正**：首轮 `drive.py` 只把 `COMMON_ENV` 传给 GNU，未传给产品，导致 touch 的 `-d/-t` 假阳性；修正后重跑确认“TZ 被忽略”仍成立（`TZ=UTC` 显式注入产品进程后 epoch 仍 +8h）。所有命令的最终数字来自修正后的 `*-final.txt`。
2. **符号链接通道**：本机无 `SeCreateSymbolicLinkPrivilege`，WSL 在 DrvFs 上 `ln -s` 创建的是 WSL 私有 reparse tag，Windows 自身也不解析；readlink 的默认/`-f/-e/-m` 在这些对象上按 noise-platform 计。用 `mklink /J` 建 junction 复核：默认/`-f`/`-e`/`-z`/多操作数全部正确。
3. **字节捕获**：产品侧 `win-run.ps1` 用 .NET `Process` 重定向原始字节（`StandardOutput.BaseStream.CopyToAsync`），避免 PowerShell UTF-16 转码；GNU 侧 `subprocess` 直接 bytes。
4. **fixture 时间固定**：全树 mtime/atime 固定为 `2020-01-02 03:04:05 UTC`；比较快照时剔除 1s 级 mtime（Windows 文件时间粒度），只比路径/类型/size/hash/链接组。
5. **安全边界**：未执行 `chmod -R /`、`chown -R /` 或任何真实根递归；`--preserve-root` 只在普通文件/相对目录探测。未跑未隔离全量 ctest，未改主仓 `D:/repo/unixwin-winuxcmd`。
6. **病态输入插曲**：手工探针 `split -n 18446744073709551616` 会让 GNU 与产品都进入长循环创建空片（5s 内 364k/1M+ 文件）；已用 `rd /s /q` 清理并重建 fixture，随后停止该类探针。两侧行为同病态，不计 gap。

## 五、复现资产（本地，未提交）

- `DOCS/oracle-audit/<cmd>.md`：10 张契约表（含 file:line、字节级 expected/actual）
- `DOCS/oracle-audit/work/`：
  - `drive.py`（支持 `gnu_args`/`win_args`、`probe`、`modes`、`rate` 掩码）、`specs/<cmd>.json`（304 例定义）、`fixtures.py`（双树 fixture）
  - `win-run.ps1`（原始字节 + 显式环境注入）、`mkbin.ps1`（硬链接 applet）
  - `out/<cmd>-final.txt`（最终原始比对）、`fx/`（双树 fixture 现场）
  - `issue-bodies/`（14 张 issue 正文留档）
- GNU 9.4 源码只存在 WSL `/tmp/cu94/coreutils-9.4`（未入库，避免 GPL 源码进入本仓）

## 六、未完成项

- chmod：setuid/setgid/sticky 在 Windows 无对应物，只核了“解析 + 报告面”，未核真实位持久化；`--preserve-root` 的根递归门未实测（安全红线）。
- chown：ACL 实际效果（Get-Acl）未逐项核，仅核 rc/报告/诊断；需要管理员令牌的“真正改 owner”路径未覆盖。
- readlink：原生 NTFS 文件符号链接无法创建（无特权），只覆盖 junction；`-f` 多级链接链未覆盖。
- touch：GNU `parse-datetime` 全语法未穷举（抽样约 20 种形式）；`-d 'now'` 的纳秒级“现在”判定未钉。
- dd：`oflag=direct/nocache`、`conv=noerror` 读错误路径、`SIGUSR1` 进度、`bs` 超大值未覆盖。
- split：`--filter` 的 shell 兼容面（引号/`$FILE` 注入）只测了 `cat > out-$FILE`；CRLF 过滤未测。
- cksum：check 模式的 `--ignore-missing/--quiet/--status/--strict/-w` 组合未系统覆盖；`--debug` 输出、sm3 边界未测。
- uniq：#1523 的计数钳位未构造出可复现输入（需 2^64 行）。
- comm/join：仅 `LC_ALL=C`；其它 locale 的 collation 未覆盖。
- 全批：跨盘符（`D:`↔`E:`）、`>MAX_PATH`、非 C locale、无特权 token 的差异未纳入。

---

# 附录：batch-1 看板（ls/cp/mv/rm/ln/stat/du/df）

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
