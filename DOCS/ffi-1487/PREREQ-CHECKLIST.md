# 复活前置 7 条逐条核对（issue #1487 / niubash#727）

## 0. 原文出处

7 条原文来自 **niubash#727 评论 `6558317263`（2026-11-01T13:03:31Z）§五
“复活前置条件（供下周期排期，按序）”**：

1. WinuxCmd：StdBorrow 补缓存失效 + CRT fd 借还 + flush/cin 纪律
   （ffi.cpp / console.cppm / file_io.cppm）
2. WinuxCmd：`read_all_stdin` 句柄直读；58+9+2 文件 IO 习语收敛
3. WinuxCmd：每调用复位 pipe-closed/write-error 标志
4. WinuxCmd：取消全局 + 读写检查点 + 取消错误码贯通
5. WinuxCmd：tiers-manifest 重审，产出非空且可辩护的 tier2 集
   （长任务/无界输出/纯计算留 spawn）
6. 门：tier2 全集 × {重定向、stdin、heredoc、`$()` 捕获、`/dev/null`、
   多次认领序列、ConPTY 交互} + 门③重跑；**在出货 tiers.tsv 上测**，
   不再靠临时扩行
7. niubash/rubash：零必改

同评论的裁决段：**“FFI 不在 1.5.0 复活”**，维持
`DEFAULT_ENABLED=false`、`TIER1_FFI_CLAIMABLE=false`、tier2 空清单。

## 1. 结论先行

| 状态 | 条数 | 条目 |
|---|---|---|
| **已满足**（在 main / 出货构建上成立） | **0 / 7** | — |
| 未满足，但原型分支已有候选实现 | 3 / 7 | 1、3、4（均只到 PR #1599，未进 main、无 CI、无产品路径实证） |
| 未满足，且没有可验收实现 | 4 / 7 | 2、5、6、7（第 7 条在 1.5.0 后反而新增了宿主改动） |

**没有任何一条达到“已满足”。** “代码已写”不等于“已满足”：这 7 条是
门，不是任务清单；每条都要求产品路径证据或出货构建上的可辩护结论。

## 2. 逐条核对

### 第 1 条 — StdBorrow 缓存失效 + CRT fd 借还 + flush/cin 纪律

**状态：未满足（原型分支有候选实现，未验收）**

- 候选实现：PR #1599 `d4a1a9f8`（2026-11-12），已合入
  `feat/ffi-prototype` 的 `4a20d9a1`（合并提交 `fd31e05b`）。
- 代码锚点（`origin/feat/ffi-prototype:src/ffi/ffi.cpp`）：
  - `_close(fd)` + `_open_osfhandle(dup)` 重绑 fd 0/1/2（`StdBorrow::rebind_fd`，
    约 :310-380）；
  - `invalidateCachedHandles()` + `clear_pipe_closed_flags()`（:403-404）；
  - `_flushall()` 跨借还边界（:96、:327-370 的 restore 路径）。
- 为什么仍判未满足：
  - `src/ffi/` 不在 `main`；PR #1426 仍是 open draft，目标是 `main`，
    head 才到 `4a20d9a1`。
  - PR #1599 **0 个 CI check**（`gh pr checks 1599` →
    `no checks reported`），没有 MSVC 真机构建；
    PR 自述 “the real MSVC build still needs Windows CI”。
  - 没有产品路径复跑；#1487 仍 OPEN。
- 证据锚点：PR #1599 body/文件列表、
  `git show origin/feat/ffi-prototype:src/ffi/ffi.cpp`、
  `gh pr checks 1599`。

### 第 2 条 — read_all_stdin 句柄直读 + 58+9+2 文件 IO 习语收敛

**状态：未满足**

- `read_all_stdin()` 仍是 `std::cin.read` + `_fileno(stdin)`：
  `origin/feat/ffi-prototype:src/utils/file_io.cppm:264-290`。
- PR #1599 没有改 `file_io.cppm`；它选择“重绑 CRT fd”来让既有
  `std::cin`/`std::cout`/`fwrite(stdout)` 看到新句柄，从而**绕开**了
  69 个命令文件的 IO 习语收敛。
- 这是合理的替代路线，但它把正确性押在“MSVC 静态 CRT 的 fd 重绑 +
  缓冲语义”上；该路线**无 MSVC 真机验证**，所以第 2 条不能算完成，
  只能记为“路线变更、待证”。
- 证据锚点：上述 `file_io.cppm`、PR #1599 文件列表（仅
  `src/commands/sort.cpp` 一个命令文件被改）、#727 评论 §三 的
  58+9+2 计数。

### 第 3 条 — 每调用复位 pipe-closed/write-error 标志

**状态：未满足（原型分支代码已备）**

- 代码锚点：`ffi.cpp` 的 `StdBorrow::apply/restore` 每次调用执行
  `invalidateCachedHandles(); clear_pipe_closed_flags();`（:403-404），
  `console.cppm:239` 提供 `clear_pipe_closed_flags()`。
- 仍判未满足：同上，只在未验收的原型分支，且没有“连续多次认领 +
  中途换句柄”的产品路径证据（PR 的 harness redirect mode 是 DLL 级，
  不是 niubash 产品路径）。
- 这是 7 条里最接近可验收的一条；一旦有 MSVC 构建 + 产品路径复跑，
  可以第一个翻绿。

### 第 4 条 — 取消全局 + 读写检查点 + 取消错误码贯通

**状态：未满足（部分实现：全局 watchdog + 错误码有，读写检查点缺）**

- 已有：
  - `CancelWatchdog` 线程每 20 ms 轮询宿主回调（`ffi.cpp` 约 :99-130、
    :610-625）；
  - `WINUX_FFI_ERR_CANCELLED = -1006`（`winux_ffi.h:88`）；
  - `sort.cpp` 在归并/循环边界检查 `ffi_cancel_requested()`（约 8 处，
    :1669/:1695/:1930/:2008/:2069/:2086 等）。
- 缺口：
  - `read_all_stdin`、`console.cppm` 的读写原语没有检查点；取消仍要等
    applet 自己到达下一个检查点；
  - 只有 `sort` 一个 applet 有检查点，其他长任务（`tee`、`cat`、
    `split` 等）没有；
  - “纯计算阶段固有不可取消”仍未用 tier 形状准入解决。
- 原始门③缺口是“dispatch 期零检查点”；PR #1599 把“零”变成了
  “一个 applet + 一个 watchdog”，但没有达到第 4 条写的
  “读写检查点 + 错误码贯通”。
- 证据锚点：`ffi.cpp` watchdog、`winux_ffi.h:88`、
  `git show origin/feat/ffi-prototype:src/commands/sort.cpp | grep
  ffi_cancel_requested`、`file_io.cppm:264-290`。

### 第 5 条 — tiers-manifest 重审，产出非空且可辩护的 tier2 集

**状态：未满足**

- `origin/feat/ffi-prototype:src/ffi/tiers-manifest.tsv` 仍是：
  tier1 × 10（cp/rm/rmdir/mkdir/touch/chmod/cat/sed/mkfifo/tty）、
  conditional × 8（tee/sort/uniq/split/stdbuf/dd/expr/xxd）、
  **tier2 = 0 行**。
- 没有一个命令被重新论证为 tier2；`sort`/`uniq` 仍标注
  “writes std::cout directly”，`tee` 标注 “SIGINT disposition is set
  and never restored”，`dd` 标注 “GetStdHandle usage needs per-site
  review”。
- 因此出货 tiers.tsv 仍是 0 认领；门① 在出货形态下“无处可测”。
- 证据锚点：上述 manifest、`tiers-manifest.tsv:22-27` 的 note、
  #1487 事实 C。

### 第 6 条 — 扩大门（tier2 全集 × 7 形态 + 门③），在出货 tiers.tsv 上测

**状态：未满足**

- 原始证据是在**临时扩行**的 tiers.tsv 上取得的（#727 评论明确记录
  “测后已恢复原版”），不是出货形态。
- PR #1599 增加了 harness 级 redirect/cancel 回归（`smoke.ps1` 3c/3d，
  6 次换句柄 + 12 MB 多次归并；3M 行、300 ms 取消），但：
  - 这是 `examples/ffi-harness` 的 DLL 级测试，不是 niubash 产品路径；
  - 没有 {重定向、stdin、heredoc、`$()` 捕获、`/dev/null`、多次认领
    序列、ConPTY 交互} 的 7 形态全矩阵；
  - 没有在出货 tiers.tsv 上跑（出货 tier2 为空）。
- 扩大矩阵的原始 18 命令 × 10 形态 / 204 格 / 56 格损坏，仍是当前
  唯一的产品路径基线；修复后没有等价的复跑。
- 证据锚点：`D:\repo\nbbuild\evidence\727\result\gate1-ext\`、
  PR #1599 的 `examples/ffi-harness/smoke.ps1` diff、#727 评论 §一。

### 第 7 条 — niubash/rubash：零必改

**状态：未满足（原始前提已失效；宿主侧现在需要恢复被删除的模块）**

- 2026-11-01 裁定时，宿主（b7cb8f6）已接好 FFI 路由，故写“零必改”。
- 2026-11-01 14:00 UTC，niubash `25f1966 refactor(ffi): remove the
  winuxcore FFI route module and its wiring (Refs #727)` 删除了：
  - `crates/niubash-runtime/src/ffi/{backend,config,context,discovery,mod,provider,route,tiers}.rs`
    （约 2,801 行）；
  - `tests/ffi_route_census.rs`、`tests/issue727_ffi_gates.rs`（1,416 行）、
    `tests/issue727_ffi_runtime_faces.rs`、`tests/issue727_ffi_product_path.rs` 等；
  - 合计 16 文件、**5,150 行删除、1 行新增**。
- 该提交在 **v1.5.0 tag 内**（`git tag --contains 25f1966` → `v1.5.0`）。
  当前安装的 niu 1.5.0 已无 FFI 路由，也没有
  `winuxcmd\usr\bin\ffi` 树。
- 结论：复活现在**必须**先 revert/重写这 5,150 行宿主接线，并重新过
  宿主的 `ffi_route_census` / `issue727_ffi_*` 门；“零必改”不再成立。
  rubash 侧没有证据显示需要改，但宿主改动已经足够推翻第 7 条。
- 证据锚点：`git show --stat 25f1966`、`git tag --contains 25f1966`、
  `git log --all --oneline -- crates/niubash-runtime/src/ffi`。

## 3. 新增的第 8 条（本核对发现，必须补进排期）

**第 8 条 — 恢复宿主 FFI 路由模块并以同等测试面重新验收**

- 内容：revert 或重写 `25f1966` 删除的 16 文件/5,150 行；恢复
  `tests/ffi_route_census.rs`、`tests/issue727_ffi_gates.rs` 等门；
  重新接 `ctrl_c.rs` 的取消漏斗与 `shell.rs` 的 warm-up/route seam。
- 为什么单列：第 7 条只写了“零必改”，没有覆盖“模块被删”这一新
  事实；不把恢复工作显式排期，前 6 条修好了也没有宿主去调用 DLL。

## 4. 复议门槛（给 owner 的一句话）

**0/7 已满足。** 最早可复议的路径是：先做第 8 条（恢复宿主）→ 在
MSVC 出货构建上验收第 1/3 条 → 补第 2/4 条（或书面对“CRT fd 重绑
替代 IO 习语收敛”给出 MSVC 证据并改判）→ 第 5 条产出可辩护 tier2 →
第 6 条在出货 tiers.tsv 上跑 7 形态全矩阵 + 门③。四条同时绿之前，
`DEFAULT_ENABLED=false` / `TIER1_FFI_CLAIMABLE=false` 不应改变。
