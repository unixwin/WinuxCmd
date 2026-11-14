# ABI v3 输出状态归属裁定备忘录（broken→141：host 还是 command？）

**状态：调研材料，不是裁定。** 给 owner 做决定用；本文件不修改任何
产品代码、不改变 ABI v3 的现状。

**问题单**：unixwin/WinuxCmd#1487（跟踪）、unixwin/WinuxCmd#1488
（streaming output ABI v3）、niubash#727（门与总裁决）。

**现状基线**：`rust2/stdout-stream-v3 @ a3a99abe`（**未合入 main**；
`git merge-base --is-ancestor a3a99abe origin/main` 返回 1）。2.0
handover「零、续作检查点」明确：ABI v3 未被接受，通用 stdout
broken→141 覆盖与 applet 级策略冲突（`xargs` 125、`tee -p` 忽略管道），
接受前必须先敲定 host/command 归属。

---

## 1. 现状：ABI v3 把归属放在了 host

### 1.1 ABI 返回的是“命令自己的状态”，写失败不改返回值

`src/shared/rust2_abi.h`（v3 注释，:113-123）：

> Write-callback failures do NOT change the returned status — the host
> saw every callback result and applies its own mapping (the C++ bridge:
> a `-1` anywhere on stdout -> 141, GNU's SIGPIPE death; a `-2` -> the
> closed-stdout "write error" contract, #1368).

Rust 侧 `winuxcmd_run_io` 返回：命令状态 / 124（宿主停止喂 stdin）/
125（panic、未知命令、`CmdError::Io`）。**broken pipe 不在 ABI 返回值
里**，只体现在每个 write 回调的 `-1`。

### 1.2 Bridge 的单一映射：任何 stdout `-1` → 141，无 opt-out

`src/bridge/rust2_bridge.cppm`：

- `OutputState::stdout_broken_pipe`（:269）记录任意一次 stdout 回调
  `-1`；
- `stdout_write_cb`（:280-303）：`is_stdout_pipe_closed()` → `-1`；
  `is_stdout_write_failed()` → `-2`；
- `run_io`（:335-381）：`if (st.stdout_broken_pipe) return
  kSigpipeDeathStatus;`（:366），`kSigpipeDeathStatus = 128 + 13`
  （:89）。注释明说 “whatever the command returned”。

结果是：**命令无法表达“我知道 broken pipe，但按我的策略忽略它”**。
Rust `tee.rs` 已经实现了 GNU 的 `-p` / `--output-error` 策略
（`rust2/crates/winux-cmds/src/tee.rs:42-58, 153-175`：默认模式 EPIPE
返回 1，`WarnNopipe` 静默忽略），但 bridge 在回调层看到 `-1` 就直接
改成 141，`-p` 的策略被覆盖。`tee.rs` 自己的注释也记录了这一点：
“in the default mode the first broken-pipe write kills tee by signal”。

### 1.3 这为什么在 1.x 没暴露

1.x 的 C++ 命令直接 `safePrint`/`std::cout`，写失败由各命令自己处理；
没有统一的 host 状态层。ABI v3 第一次把“写回调结果”集中到 host，
于是把“GNU 进程级 SIGPIPE 处置”简化成了“host 级 141”。

---

## 2. GNU 先例：没有 host 级统一映射，策略在命令/进程

GNU 的做法不是“运行时看到 EPIPE 就 141”，而是每个程序自己设置
SIGPIPE 处置；shell 只负责把“被 SIGPIPE 杀死”翻译成 128+13。

| 程序 | GNU 行为 | 机制锚点 |
|---|---|---|
| `sort` | 启动时 `SIG_IGN`，收到 EPIPE 后若原处置是 `SIG_DFL` 就**重新 raise SIGPIPE** → 141；若父进程本来忽略 SIGPIPE，则报错退出（`SORT_FAILURE=2`） | coreutils 9.11 `src/sort.c:364-365, 406-408, 4800-4803` |
| `tail` | 显式 `raise(SIGPIPE)`，否则 `EXIT_FAILURE` | `src/tail.c:377-383`（`die_pipe`） |
| `split` | 记录启动时 SIGPIPE 是否默认；`--filter` 子进程按需恢复默认处置 | `src/split.c:66-67, 559-560, 1709` |
| `tee` 默认 | 不忽略 SIGPIPE → 第一个 EPIPE 杀死自己 → 141 | `src/tee.c:48-57, 172-173` |
| `tee -p` / `--output-error=warn-nopipe` | `signal(SIGPIPE, SIG_IGN)`；EPIPE 只关闭该输出、不报错、继续；最终 0 | `src/tee.c:73-78, 154-157, 172-173, 210-219` |
| `grep` | 不安装 SIGPIPE 处理器 → 默认 141；若父进程忽略 SIGPIPE，写错误走 `die(EXIT_TROUBLE=2, "write error")` | grep 3.11 `src/grep.c:441-479, 672-675, 1345-1346` |
| `sed` | 默认 141；若 SIGPIPE 被忽略，`ck_fwrite/ck_fclose` → `panic()` → `EXIT_PANIC=4`（"couldn't write"） | sed `utils.c:222-313`、`utils.h:23-25` |
| `seq` | 不安装处理器 → 默认 141（WSL GNU 9.x 实测） | coreutils `src/seq.c`（无 SIGPIPE 代码） |
| `xargs` | 不使用 128+signal 约定；自己的退出码：0/1/123/124/125/126/127 | findutils 4.10 man `xargs(1)` EXIT STATUS |

关键结论：

1. **GNU 的 141 是“进程被 SIGPIPE 杀死”的 shell 翻译，不是程序返回
   的状态。** 程序可以改变这个处置（`tee -p`），也可以自己捕获写错误
   返回 2/4（grep/sed 在 SIGPIPE 被忽略时）。
2. **`sort` 是最强的反例支持“命令拥有策略”**：它先忽略 SIGPIPE 以便
   看到 EPIPE errno，然后**按启动时的处置决定是否重新 raise**。这就是
   “命令内部知道策略，host 不应替它决定”。
3. **`xargs` 完全不用 141**：它把子进程状态折叠成 123-127。把 xargs
   的 stdout broken 统一改成 141，会覆盖它自己的状态契约（尤其 125 =
   子进程被信号杀死）。

### 2.1 本地实测（WSL GNU，2026-11-14）

`seq 1 1000000 | <cmd> | head -1`，`PIPESTATUS` 取中间命令：

| 命令 | 实测状态 |
|---|---|
| `tee`（默认） | **141** |
| `tee -p` | **0** |
| `tee --output-error=warn-nopipe` | **0** |
| `grep .` | **141** |
| `sed ""` | **141** |
| `seq 1 1000000`（直接进 head） | **141** |
| `xargs -n1 sh -c 'exit 3'` | **123** |
| `xargs sh -c 'kill -TERM $$'` | **125** |
| `xargs /no/such/cmd` | **127** |

这组数字与 source 完全一致：`tee -p` 是唯一“忽略管道仍成功”的
常见命令；`grep`/`sed`/`seq` 默认都是 141；xargs 是 123-127。

---

## 3. uutils / uutils-coreutils 的做法

uutils 不是单一策略，而是“运行时兜底 + 命令 opt-in”的混合：

| 层 | 做法 | 锚点 |
|---|---|---|
| 进程入口 | `preserve_inherited_sigpipe()`：只有父进程用 `RUST_SIGPIPE=default` 明确要求时，才把 SIGPIPE 恢复 `SIG_DFL`；否则 Rust 默认 `SIG_IGN` | `uucore 0.12.0 src/lib/mods/panic.rs:45-67`；`src/lib/lib.rs:216-221` |
| panic 兜底 | `mute_sigpipe_panic()`：只静音 BrokenPipe 的 panic 文本，不改命令状态 | `panic.rs:19-43` |
| 通用工具 | `winux` 之外，uutils 用 `UResult`/`set_exit_code` 让**命令**决定退出码 | `uucore src/lib/mods/error.rs:18-46, 93` |
| `tee` | 实现 `-p`/`--output-error`；`disable_pipe_errors()`（`signal(SIGPIPE, SIG_IGN)`）+ 每个 writer 的 `process_error()` 对 `BrokenPipe` 返回 `Ok(())` | `uutils/coreutils main src/uu/tee/src/tee.rs:23,30-51,80-100,280-305`；`uucore src/lib/features/signals.rs:600-615` |
| `seq` | 单独特判：BrokenPipe 时打印 `write error`，**只有**启动时 SIGPIPE 已被忽略才 `set_exit_code(1)`，否则返回 0 | `uutils/coreutils main src/uu/seq/src/seq.rs:282-296` |
| `xargs`（uutils findutils） | 显式 123/124/125/126/127 映射，与 GNU 一致；对 stdin 的 BrokenPipe 有重试 | `D:\repo\findutils\src\xargs\mod.rs:558-581, 1277-1285` |

**uutils 的教训**：

- 它把“默认 141”交给 OS/SIGPIPE 处置，而不是 host 层无条件覆盖；
- 需要忽略管道的命令（tee）自己在命令内 `disable_pipe_errors()`；
- `seq` 的特判说明“命令级策略”会带来与 GNU 的偏差风险（GNU seq 实测
  141，uutils 注释称 GNU 0），所以**策略归属一旦下放，必须逐命令有
  GNU 对拍**；
- uutils 的 `RUST_SIGPIPE=default` 是“父进程显式要求默认处置”的逃生
  口，等价于我们需要的 per-call opt-out 通道。

---

## 4. 两个归属方案

### 方案 A：host 拥有（现状）

Bridge 看到任意 stdout `-1` → 141；命令返回什么都被覆盖。

### 方案 B：command 拥有

命令对 broken pipe 负全责：默认由共享 Rust IO 层把“未处理的
BrokenPipe”映射成 141；命令显式处理（tee -p、xargs）时保留自己的
状态。ABI 需要把“命令已处理/未处理”告诉 host，或干脆让 Rust 返回
最终状态、host 不再覆盖。

### 方案 B+：host 默认 + command opt-out（混合，需 ABI 增量）

ABI 增加一个 per-call 标志或哨兵（例如 `WINUXCMD_IO_SIGPIPE_HANDLED`
/ 返回码 `-1007`），host 只在“命令未声明已处理”时映射 141。默认行为
与今天一致，只有显式 opt-out 的命令改变。

### 4.1 代价对比

估算口径：1 人日 ≈ 一个熟悉 rust2/ABI 的工程师一天；不含等待 CI/
真机。所有数字是量级估算，不是承诺。

| 维度 | A. host 拥有（现状） | B. command 拥有 | B+. host 默认 + command opt-out |
|---|---|---|---|
| 代码改动 | 0（已实现） | Rust 共享 IO 层 + 50 个已接线命令的 EPIPE 审计；ABI 语义变更（host 不再覆盖） | ABI v3 增一 flag 或哨兵（`rust2_abi.h` + `lib.rs` + bridge 3 处）；tee/xargs 声明 opt-out；其余命令零改 |
| 改动文件量级 | 0 | ~50 命令 + core stream + bridge + 测试（数十文件） | 3-6 文件 + tee/xargs 两个命令 + 测试 |
| 与 GNU 默认 141 的一致性 | sort/seq/grep/sed 默认正确 | 需要共享层默认映射，才能保持默认正确；做对了更忠实 | 默认路径与 A 相同；只有显式 opt-out 偏离 |
| `tee -p` | **错**（GNU 0，产品 141） | 正确 | 正确 |
| `tee --output-error=warn*` | **错**（GNU 0/1，产品 141） | 正确 | 正确 |
| `xargs` 123-127 | **有覆盖风险**（xargs 自己写 stdout 时） | 正确（xargs 自己拥有状态） | 正确 |
| `grep` 写错误 2 / `sed` 4（SIGPIPE 被忽略场景） | **错**（被 141 覆盖） | 正确 | 正确 |
| 实现风险 | 低（已跑） | 高：每个命令都要证明“我处理了 EPIPE 且状态对”；漏一个就改变默认语义 | 中：默认路径不动；风险集中在 opt-out 命令 |
| 回归面 | 现有 v3 测试 | 50 命令 × 管道/文件/控制台 + GNU 对拍 | opt-out 命令 × 形态 + 默认路径冒烟 |
| 测试成本 | 低 | 高（数周） | 中（数天到 1-2 周） |
| 可逆性 | 已固化，后续改要动 ABI | 语义变更难回滚 | 增量、可灰度（先 tee，再 xargs） |
| 对 2.0 时间线 | 不阻塞接线，但带已知错误 | 阻塞面大，可能拖 2.0 | 可控；建议作为 v3 的补充而不是重做 |
| 适用判断 | 只适合“所有命令都该 141”的简化世界 | 最忠实 GNU，但成本最高 | **最接近 GNU 且成本可控**；但仍需 owner 裁定 |

### 4.2 具体影响举例

1. **`tee -p`**：GNU 0；A 返回 141；B/B+ 返回 0。
2. **`xargs`**：GNU 子进程被信号杀 → 125；A 若 xargs 自己写 stdout
   失败会返回 141，覆盖 125；B/B+ 保留 125。
3. **`grep`**：GNU 默认 141；SIGPIPE 被忽略时 2。A 永远 141；B/B+ 可以
   区分。
4. **`sort`**：GNU 默认 141；SIGPIPE 被忽略时 2。A 141 正确；B 需要
   共享层默认 141，命令不 opt-out 时也正确。
5. **`yes`/`seq`**：GNU 141；A 正确；B 默认也必须 141，否则会退化。

### 4.3 待 owner 回答的问题

1. 2.0 是否接受“先按 A 出货、把 tee -p/xargs 记为已知偏差”？如果
   接受，需要把这些偏差写进发布说明与 gate 排除表。
2. 如果不接受，是否批准 B+ 的 ABI 增量（一个 flag/哨兵），并把
   `tee`/`xargs` 作为首批 opt-out 命令？
3. 若选 B（完全 command-owned），谁负责 50 个命令的 EPIPE 审计与
   GNU 对拍？这是数周量级，且会改变默认路径的证明责任。
4. `grep`/`sed` 的 2/4 状态是否也要在 Windows 上保真？如果只要
   “默认 141”，A 已经足够；如果要“SIGPIPE 被忽略时 2/4”，必须 B/B+。

---

## 5. 建议（供裁定，不是裁定）

- **不建议**维持 A 并在 2.0 声称“GNU 兼容”：它已知在 `tee -p`、
  `xargs`、`grep/sed` 的 SIGPIPE-ignored 面上偏离，且没有 per-command
  opt-out 通道。
- **成本最低的忠实路径是 B+**：默认仍是 host 141（不破坏 sort/seq/
  yes），只给 `tee`/`xargs` 一个显式 opt-out；ABI 增量小，可先灰度
  一个命令，再推广。
- 如果 owner 选择 B（完全 command-owned），必须先接受“50 命令审计 +
  GNU 对拍”的排期，否则会从“已知偏差”变成“未知偏差”。
- 无论选哪个，**ABI v3 在归属裁定前不应被接受**；这与 handover 的
  现行裁定一致。归属裁定应作为 #1488 的 blocking decision 记录在
  issue 上，而不是只留在代码注释里。

---

## 6. 调研来源（20 项）

1. `rust2/stdout-stream-v3 @ a3a99abe`：`src/shared/rust2_abi.h`
   （v3 注释、返回状态契约）。
2. 同分支 `src/bridge/rust2_bridge.cppm`（`run_io`、`kSigpipeDeathStatus`）。
3. 同分支 `rust2/README.md` §Streaming output ABI (v3)。
4. 同分支 `rust2/crates/winuxcmd-ffi/src/lib.rs`（ABI v3 入口与状态）。
5. 同分支 `rust2/crates/winux-cmds/src/tee.rs`（`-p`/`--output-error`）。
6. 同分支 `rust2/crates/winux-core/src/stream.rs`（`HostWriter` sticky
   BrokenPipe）。
7. GNU coreutils 9.11 `src/tee.c`（本地
   `D:\repo\gnu-coreutils-9.11\coreutils-9.11`）。
8. GNU coreutils 9.11 `src/sort.c`。
9. GNU coreutils 9.11 `src/tail.c`。
10. GNU coreutils 9.11 `src/split.c`。
11. GNU coreutils 9.11 `lib/closeout.c`。
12. GNU grep 3.11 `src/grep.c`（Debian sources）。
13. GNU grep 3.12 manual §Exit Status（gnu.org）。
14. GNU sed `utils.c` / `utils.h`（本地 sed 源码副本
    `D:\repo\wc-sed2\rust2\target\probe`）。
15. GNU findutils 4.10 `xargs(1)` man page（man7.org，2026-08-03 抓取）。
16. uucore 0.12.0 `src/lib/mods/panic.rs`、`src/lib/features/signals.rs`、
    `src/lib/mods/error.rs`（本机 cargo registry）。
17. uutils/coreutils main `src/uu/tee/src/tee.rs`（GitHub raw）。
18. uutils/coreutils main `src/uu/seq/src/seq.rs`（GitHub raw）。
19. uutils/findutils `src/xargs/mod.rs`（本地 `D:\repo\findutils`）。
20. 本地 WSL GNU 实测（2026-11-14）：`tee`/`tee -p`/`grep`/`sed`/`seq`/
    `xargs` 九组状态码。

> 注：第 13 项与第 20 项是行为文档/实测，其余是源码或设计文本。共
> 20 个锚点、19 个独立来源（GNU grep manual 与实测分开计）。
