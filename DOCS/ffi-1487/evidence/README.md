# FFI 门①③ 复现证据包（issue #1487）

本目录把 niubash#727 门①（产品路径等价性）与门③（长任务 Ctrl-C）的
原始复现整理成**可重跑、可审计、默认不喷终端**的两条脚本链：

| 门 | 脚本 | 复现内容 |
|---|---|---|
| 门① | `Invoke-Gate1-Redirect.ps1` | FFI 调用不应用 shell 重定向：stdin 丢失、stdout 泄漏/丢失、`/dev/null` 失效（36 MB 洪泛） |
| 门③ | `Invoke-Gate3-Cancel.ps1` + `conpty_ctrl_c.py` | dispatch 期不轮询取消：`sort big.txt` 注入 ^C 后仍运行到自然结束 |

所有子进程的 stdout/stderr 一律重定向到 `_runs/<timestamp>/` 下的文件；
终端只输出摘要。**任何复现步骤都不允许把 applet 输出直接继承到终端。**

## 0. 基线：2026-11-01 原始实测（不可变参照）

环境：Windows 10 19045 x64；niubash `b7cb8f6`（1.4.x 线）+ rubash
`1c93ea87`；安装树
`C:\Users\cmx\AppData\Local\Programs\Niubash`；FFI 树
`winuxcmd\usr\bin\ffi\v1.1.20\{winuxcore.dll,tiers.tsv}` +
`current.txt`；DLL = `feat/ffi-prototype @ d589562f`（ABI v2）。
`NIU_WINUXCMD_FFI=1 niu doctor` → `ready — winuxcmd 1.1.20, ABI 2`。

原始工件：`D:\repo\nbbuild\evidence\727\result\{m1,m3}\`。

### 门① 实际/预期（`sort` 等 5 格 + 洪泛格）

| 用例 | spawn（`NIU_WINUXCMD_FFI=0`）预期/实际 | FFI（`=1`）实际（缺陷） | 修复后预期 |
|---|---|---|---|
| `sort < words.txt > srt.out` | srt.out 44 B，7 行有序 | **srt.out 0 B**，结果丢失 | 44 B |
| `sort words.txt > o.txt` | o.txt 44 B，终端无数据 | **o.txt 0 B**，44 B 排序结果喷到终端 | 44 B |
| `wc -l < words.txt` | `7` | **`0`** | `7` |
| `tee t.out < words.txt` | t.out 44 B，stdout 44 B | **t.out 0 B**，stdout 空 | 44 B / 44 B |
| `tr a-z A-Z < words.txt` | 44 B 大写 | **0 B**（只有脚本自己的 `RC=0`） | 44 B |
| `sort big.txt > /dev/null`（36 MB） | 终端 0 B，约 0.33 s | **终端收到约 36,000,000 B**；^C 后 180.01 s 才回提示符 | 0 B，瞬时返回 |

夹具 `words.txt`（7 行，44 B）：

```
banana
apple
cherry
apple
date
banana
apple
```

排序后：`apple apple apple banana banana cherry date`。
`big.txt`：3,000,000 行 × 12 B（`%09d-<a..h>` + LF）= 36,000,000 B，
由脚本确定性生成，不入库。

### 门③ 实际/预期（`sort big.txt`，3,000,000 行）

| 断言 | 修复前实际 | 修复后预期 |
|---|---|---|
| (a) ^C 可提前终止 | 基线 12.61 s；^C 注入后又跑 **9.01 s** 至自然结束（总 12.96 s ≈ 基线），`c_early=false` | ^C 后 < 2 s 返回，调用报取消（宿主 `Cancelled` → 130） |
| (b) 宿主存活 | PASS，提示符恢复 | PASS |
| (c) 状态干净 | PASS；后续 `wc -l big.txt` = 3,000,000，重排结果 `cmp` 一致 | PASS |
| 行内 rc | `c_rc=null`（行内 echo 被中止） | `c_rc=130`（或宿主取消码对应 rc） |

原始 `gate3-summary.json`：`b_dur=12.6056595`、
`c_file_done_lat=9.0061953`、`c_dur=12.9634005`、`c_early=false`、
`alive=130`、`wc_lines=3000000`、`cmp1=cmp2=0`、`exit_alive=false`。
洪泛变体：`baseline 0.33 s`、`^C-to-prompt=180.01 s`、后续
`echo/wc/复排 rc 全 None`、`exit` 后 `process alive = True`。

## 1. 运行前提（缺一不可）

1. **1.4.x 产品线**：宿主必须仍有 FFI 路由模块
   （`crates/niubash-runtime/src/ffi/`）。niubash 1.5.0 已由
   `25f1966 refactor(ffi): remove the winuxcore FFI route module` 删除该
   模块，因此 1.5.0 安装树**无法复现**；请用 1.4.4/1.4.x 的 `niu.exe`。
   脚本会检查 `<root>\winuxcmd\usr\bin\ffi\current.txt`，缺失即退出 2。
2. **FFI 树**：`<root>\winuxcmd\usr\bin\ffi\<ver>\{winuxcore.dll,tiers.tsv}`
   + `current.txt`。DLL 必须是 ABI v2（`winux_ffi_abi_version()==2`）。
3. **tier2 认领面**：出货 `tiers.tsv` 的 tier2 为空（0 认领），
   原始证据是**临时扩行**取得的。脚本默认只读检查；只有显式
   `-ApplyTier2` 才会备份并临时把用例命令提升为 tier2，`finally`
   恢复原文件；恢复失败会以退出码 3 明确报告，绝不静默留下脏表。
4. PowerShell 7.4+（`Start-Process -Environment`）。
5. 门③ 需要 `pywinpty 3.0.5`（`python -c "import winpty"`）与
   一个可交互的 niu 构建。无 ConPTY 时脚本拒绝运行，不用
   `Start-Process` 重定向冒充 Ctrl+C。
6. 磁盘：`_runs/` 至少预留 500 MB（36 MB 夹具 + 洪泛捕获 + 排序输出）。

## 2. 运行

```powershell
# 只做发现/自检，不执行任何 applet
pwsh -NoProfile -File .\Invoke-Gate1-Redirect.ps1 -PlanOnly
pwsh -NoProfile -File .\Invoke-Gate3-Cancel.ps1 -PlanOnly

# 门①（默认按“修复前=broken”判定；显式扩行；包含 36MB 洪泛）
pwsh -NoProfile -File .\Invoke-Gate1-Redirect.ps1 `
  -ApplyTier2 -Expect broken -IncludeFlood

# 修复后回归（同一套脚本，换成 fixed 判定）
pwsh -NoProfile -File .\Invoke-Gate1-Redirect.ps1 `
  -ApplyTier2 -Expect fixed -IncludeFlood

# 门③（默认不跑洪泛；加 -IncludeFlood 复现 180 s 终端洪泛）
pwsh -NoProfile -File .\Invoke-Gate3-Cancel.ps1 -Expect broken
pwsh -NoProfile -File .\Invoke-Gate3-Cancel.ps1 -Expect broken -IncludeFlood
```

`-NiuExe` 可指向任意 1.4.x 安装/构建；`-OutDir` 可指定证据落盘目录。
每次运行生成：

```
_runs/<UTC timestamp>/
  environment.txt          niu --version / doctor / FFI 树清单 / 哈希
  gate1-ledger.tsv         逐用例：rc、字节数、认领次数、判定
  gate1-summary.json       机器可读汇总 + 期望 profile 命中情况
  cases/<name>/            夹具与逐用例 stdout/stderr（重定向文件）
  flood/stdout.bin         `/dev/null` 失效时被捕获的 36 MB（不打印）
  gate3-summary.json      门③ 三断言 + 洪泛计时
  gate3-console.log       ConPTY 原始流（仅保留末尾窗口，防止无限增长）
  tier-backup/            临时扩行的 tiers.tsv 原件与 SHA-256
```

## 3. 判定与退出码

| 退出码 | 含义 |
|---|---|
| 0 | 所有用例命中所选 `-Expect` profile |
| 1 | 有用例偏离 profile（缺陷已变/未复现），看 ledger |
| 2 | 环境不满足（无 FFI 树、无 pywinpty、无 ABI v2、tier2 为空且未 `-ApplyTier2`） |
| 3 | 临时 tiers.tsv 恢复失败（人工介入；备份在 `tier-backup/`） |

`broken` profile 是**缺陷复现**，不是通过标准；`fixed` profile 才是
复活门要求的绿线。两个 profile 的数值都来自 2026-11-01 原始工件与
修复方案（PR #1599）的预期，不来自推断。

## 4. 安全边界

- 脚本从不把 applet 输出继承到调用者终端；门① 的每个 `niu` 都带
  `-RedirectStandardOutput/-RedirectStandardError`，门③ 的 ConPTY 流由
  读取线程计数并**只落盘尾部窗口**。
- 36 MB 洪泛用例的输出上限为 `-FloodCapBytes`（默认 64 MiB）；超过即
  杀进程并记为 `flood-capped`，不会无限写盘。
- `-ApplyTier2` 只改 `<FFI 树>\<ver>\tiers.tsv` 一个文件，先做
  `tiers.tsv` 备份与 SHA-256，`finally` 恢复并复核哈希；不触碰
  `winuxcore.dll`、`current.txt`、安装树其他文件，也不改仓库内任何
  文件。
- 门③ 的 ^C 通过 ConPTY 的 `\x03` 注入真实控制台输入；不使用
  `Stop-Process`/`taskkill` 伪造取消。
- 不在 CI 或无人值守环境默认跑 36 MB 洪泛；必须显式 `-IncludeFlood`。
- 每个 FFI 用例必须至少出现一次 `route ... -> Ffi (Claimed)`；0 认领
  直接判失败（否则测到的是 spawn 基线）。`-AllowUnclaimed` 只给
  “假 niu”机制自测用，不得用于真实证据。

## 5. 与原始工件的对应

| 本包用例 | 原始工件 |
|---|---|
| `sort_redirect` | `m1/result/ffi/03_sort_redirect.*`（srt.out 0 B） |
| `sort_stdout` | `m1/result/ffi/26_stdout_redir.*`（o.txt 0 B，终端 7 行） |
| `wc_stdin` | `m1/result/ffi/27_stdin_redir.*`（`0`） |
| `tee_stdin` | `m1/result/ffi/28_tee_direct.*`（t.out 0 B） |
| `tr_stdin` | `m1/result/ffi/10_tr.*`（首段空） |
| `flood` | `m3/transcript.devnull-flood.txt` + `timeline.devnull-flood.txt` |
| 门③ | `m3/gate3-summary.json` + `timeline.txt` + `gate3_final.py` |

原始驱动：`D:\repo\nbbuild\evidence\727\m1\run_matrix.sh`、
`.../m3/gate3_final.py`、`.../m3/pty_shim.py`。本包只做安全化与参数化，
不改判定数值。
