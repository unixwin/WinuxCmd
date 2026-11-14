# FFI 复活裁定材料汇总（issue #1487 / niubash#727）

**日期**：2026-11-14 ｜ **性质**：只读调研 + 证据脚本 + 裁定材料；
零产品代码改动、零 commit、零 push ｜ **工作树**：
`D:/repo/wc-ffi1487`（detached at `origin/main = b781ff35`）。

> 说明：`D:/repo/wc-ffi1487` 在 `b781ff35` 上不含
> `DOCS/2.0-HANDOVER.md`；「零、续作检查点」取自已提交的
> `D:/repo/wc-docs@2040645f/DOCS/2.0-HANDOVER.md`（与主仓工作树的
> 修改版内容一致）。未修改主仓。

**一句话结论：不建议现在复活 FFI。** 7 条原前置 **0/7 已满足**；且
2026-11-01 之后宿主侧把 FFI 路由模块整块删除了（16 文件 / 5,150 行），
新增了第 8 条“恢复宿主”前置。当前 1.5.0 安装树里既没有
`winuxcore.dll`，也没有 `winuxcmd\usr\bin\ffi`，产品路径根本不具备
复现条件。

---

## 1. 时间线与事实基线

| 时间（UTC） | 事实 | 证据 |
|---|---|---|
| 2026-11-01 12:27 | 门①/门③ 产品路径实证：**双 FAIL**；新发现重定向数据丢失 | niubash#727 评论 `6557940431` |
| 2026-11-01 13:03 | 加大门 18 命令 × 10 形态 = 204 格，**56 格损坏，100% 在 FFI 认领格**；总裁决“1.5.0 不复活”；给出 7 条前置 | niubash#727 评论 `6558317263` |
| 2026-11-01 13:21 | niubash#727 以 `not_planned` 关闭 | GitHub issue timeline |
| 2026-11-01 14:00 | niubash `25f1966` 删除 FFI 路由模块与全部 #727 门测试（16 文件，5,150 删除 / 1 新增）；该提交在 **v1.5.0** 内 | `git show --stat 25f1966`；`git tag --contains 25f1966` |
| 2026-11-12 21:39 | PR #1599 提交 A/B 修复（CRT fd 重绑 + cancel watchdog），目标分支是 `feat/ffi-prototype`，不是 `main` | PR #1599 `d4a1a9f8` |
| 2026-11-13 13:25 | PR #1599 合入 `feat/ffi-prototype`（merge `4a20d9a1`）；**0 checks / 0 reviews** | `gh pr checks 1599` |
| 2026-11-14 | 本机安装 niu 1.5.0 / winuxcmd 1.2.1；`winuxcmd\usr\bin\ffi` 不存在 | `niu --version`、目录检查 |

当前状态：

- `main` 没有 `src/ffi/`；PR #1426 仍是 open draft（head `4a20d9a1`，
  目标 `main`）。
- #1487 仍 OPEN；最新评论（2026-11-12）是修复提案，不是验收。
- ABI v3（`rust2/stdout-stream-v3 @ a3a99abe`）**不在 main**；
  broken→141 归属未裁定。

---

## 2. 门①③ 证据包

**位置**：`DOCS/ffi-1487/evidence/`

| 文件 | 用途 |
|---|---|
| `README.md` | 复现总说明、2026-11-01 预期/实际表、安全边界、退出码 |
| `Invoke-Gate1-Redirect.ps1` | 门①：5 个重定向用例 + 可选 36 MB `/dev/null` 洪泛；默认重定向到文件；`-ApplyTier2` 自动备份/恢复 tiers.tsv |
| `Invoke-Gate3-Cancel.ps1` | 门③：ConPTY 注入 ^C，测取消延迟/存活/状态；可选洪泛 |
| `conpty_ctrl_c.py` | pywinpty 3.0.5 驱动（改编自原始 `gate3_final.py` + `pty_shim.py`），只落盘尾部窗口、计数不打印 |
| `baseline-2026-11-01.json` | 原始数字、路径、环境，机器可读 |
| `_runs/`（运行时生成） | 每次运行的 ledger/summary/原始 stdout/stderr；不入库 |

### 2.1 门① 关键预期/实际

| 用例 | spawn | FFI 实际（缺陷） | 修复后 |
|---|---|---|---|
| `sort < words.txt > srt.out` | srt.out 44 B | **0 B，7 行丢失** | 44 B |
| `sort words.txt > o.txt` | o.txt 44 B，终端 0 | **0 B，7 行喷终端** | 44 B |
| `wc -l < words.txt` | `7` | **`0`** | `7` |
| `tee t.out < words.txt` | 44 B / 44 B | **0 B / 0 B** | 44 B / 44 B |
| `tr a-z A-Z < words.txt` | 44 B | **0 B** | 44 B |
| `sort big.txt > /dev/null` | 0 B，0.33 s | **~36,000,000 B；^C→提示符 180.01 s** | 0 B，瞬时 |

### 2.2 门③ 关键预期/实际

| 指标 | 修复前 | 修复后 |
|---|---|---|
| baseline `sort -o`（3M 行） | 12.61 s | 同量级 |
| ^C 后到文件完成 | **9.01 s**（总 12.96 s） | < 2 s 返回 |
| `c_early` | **false** | true |
| 行内 rc | **null**（被中止） | 130（Cancelled） |
| 宿主存活 | 130（PASS） | 130 |
| 之后 `wc -l` | 3,000,000（PASS） | 3,000,000 |
| `cmp` 基线/取消/重排 | 0/0（PASS） | 0/0 |

### 2.3 脚本已验证到什么程度

- 两个 `.ps1` 通过 PowerShell AST 解析；`conpty_ctrl_c.py` 通过
  `py_compile`。
- Gate1 用“假 niu = Git Bash”做了端到端自测（`-AllowUnclaimed`）：
  `-Expect fixed -IncludeFlood` → **0 failures / MATCH**；tier2 扩行后
  哈希恢复成功；`-Expect broken` → 5 个 FAIL、退出码 1。说明测量、
  扩行/恢复、洪泛捕获、判定路径都能工作。真实证据不加
  `-AllowUnclaimed`，0 认领会被直接判失败。
- Gate3 用同一个假 niu 自测：baseline 7.16 s、`wc=3000000`、
  `cmp=0/0`、`exit_alive=false`；唯一不匹配是取消延迟
  （4.44 s > 2 s），因为假 niu 是 winuxcmd `sort.exe`，它本身不响应
  ^C——这恰好证明脚本不会把“没取消”误判成 fixed。
- **真实产品路径未跑**：本机 niu 1.5.0 无 FFI 树；脚本以退出码 2 +
  明确提示拒绝运行，不伪造证据。

---

## 3. 复活前置 7 条核对（详见 `PREREQ-CHECKLIST.md`）

原文：niubash#727 评论 `6558317263` §五。

| # | 前置 | 状态 | 证据锚点 |
|---|---|---|---|
| 1 | StdBorrow 缓存失效 + CRT fd 借还 + flush/cin | **未满足**（原型分支有候选） | PR #1599 `d4a1a9f8`；`ffi.cpp` :310-404；0 CI |
| 2 | `read_all_stdin` 句柄直读 + 58+9+2 IO 习语收敛 | **未满足** | `file_io.cppm:264-290` 仍 `std::cin`；PR 只改 `sort.cpp` |
| 3 | 每调用复位 pipe-closed/write-error | **未满足**（代码已备） | `ffi.cpp:403-404`；`console.cppm:239` |
| 4 | 取消全局 + 读写检查点 + 错误码 | **未满足**（部分） | watchdog + `-1006` 有；检查点只有 `sort.cpp` |
| 5 | tiers-manifest 重审，非空可辩护 tier2 | **未满足** | 原型 manifest：tier1×10、conditional×8、**tier2=0** |
| 6 | tier2 全集 × 7 形态 + 门③，出货 tsv 上测 | **未满足** | 原始门靠临时扩行；#1599 只有 harness 级测试 |
| 7 | niubash/rubash 零必改 | **未满足**（前提失效） | `25f1966` 删除 16 文件/5,150 行，v1.5.0 内 |
| **8** | **恢复宿主 FFI 路由模块并重过宿主门** | **新增，未满足** | 同上；`ffi_route_census.rs` / `issue727_ffi_*` 一并被删 |

**统计：已满足 0/7；未满足 7/7（其中 1、3、4 有原型分支候选实现）；
新增第 8 条。** “代码已写”不等于“已满足”，因为这 7 条都要求产品路径
或出货构建证据。

---

## 4. ABI v3 输出状态归属（详见 `ABI-V3-ATTRIBUTION-MEMO.md`）

**问题**：ABI v3 的 bridge 对任意 stdout `-1` 无条件返回 141
（`kSigpipeDeathStatus`，`rust2_bridge.cppm:89, 366`），覆盖命令自己的
状态。已知冲突：`tee -p`（GNU 0）、`xargs`（123-127）、`grep/sed`
（SIGPIPE 被忽略时 2/4）。

**GNU 先例**（20 个来源）：

- GNU 没有 host 级统一映射；每个程序自己设置 SIGPIPE 处置：
  - `sort`：先 `SIG_IGN`，看到 EPIPE 后按启动处置重新 `raise` →
    默认 141，父进程忽略时 2；
  - `tee`：默认 141；`-p`/`--output-error=warn-nopipe` 设
    `SIG_IGN`，忽略 EPIPE → **0**；
  - `grep`：默认 141；SIGPIPE 被忽略时 `die(2, "write error")`；
  - `sed`：默认 141；SIGPIPE 被忽略时 `panic()` → 4；
  - `xargs`：不用 141，自己 0/1/123/124/125/126/127。
- WSL GNU 实测：`tee` 141、`tee -p` 0、`grep` 141、`sed` 141、
  `seq` 141；`xargs` 123/125/127。

**uutils 做法**：运行时只做 `preserve_inherited_sigpipe` +
`mute_sigpipe_panic`；`tee` 自己 `disable_pipe_errors()` + 每个 writer
忽略 `BrokenPipe`；`seq` 单独特判；`xargs` 显式 123-127。即
“运行时兜底 + 命令 opt-in”，不是 host 一刀切。

**两个方案代价**：

| | A. host 拥有（现状） | B. command 拥有 | B+. host 默认 + command opt-out |
|---|---|---|---|
| 改动 | 0 | ~50 命令 + core + ABI 语义 | ABI 增 1 flag/哨兵 + tee/xargs |
| tee -p | 错（141） | 对（0） | 对（0） |
| xargs 125 | 有覆盖风险 | 对 | 对 |
| grep/sed 2/4 | 错 | 对 | 对 |
| 风险/工期 | 低/已固化 | 高/数周 | 中/数天-2 周 |

**建议（不是裁定）**：B+ 最接近 GNU 且成本可控；A 不能声称 GNU 兼容；
B 需要先接受 50 命令审计排期。**在 owner 裁定归属前，ABI v3 不应被
接受**——与 handover 现行裁定一致。

---

## 5. SignPath 材料（详见 `SIGNPATH-MATERIALS.md`）

- **产品路径必须签整包，不是只签 setup.exe**：`winuxcore.dll` 在
  `winuxcmd/usr/bin/ffi/<ver>/`，必须被 `winuxcmd/**/*.dll` 覆盖。
- **zip 根布局**：根元素 `<zip-file>`；条目必须是包根相对路径
  （`setup.exe`、`niu.exe`、`winuxcmd/...`），**不能**再包一层版本
  目录；`upload-artifact` 默认 ZIP 正是这条约束的来源。
- **提交时显式传 `artifact-configuration-slug`**：installer-only 的
  “Installer zip” 配置不会签 DLL；full-package 配置才能覆盖。
- **VERSIONINFO 是开放项**：niubash README 明确
  `niu.exe`/`winuxcmd.exe`/shim 没有版本资源；本仓
  `resources/winuxcmd.rc` 只有 icon，FFI DLL target 也没有资源步骤。
  Foundation metadata restrictions 暂时不能加。
- **policy 切换**：只改 `SIGNPATH_SIGNING_POLICY_SLUG`（必要时
  `SIGNPATH_API_TOKEN`），不改 workflow；release policy 后验证
  `Get-AuthenticodeSignature` 必须 `Valid`（test 阶段允许非
  `NotSigned`）；签名后重算所有 digest；test 签过的产物不得进 release。
- **不可跳过**：签名不是门①③ 的替代；release policy 未批前，即使
  ①③ 全绿也只能维持实验形态。

---

## 6. 结论：为什么不建议现在复活

1. **证据面**：门① 5 个数据丢失格 + 36 MB/180 s 洪泛、门③ ^C 后仍跑
   9.01 s，都是产品路径实测；PR #1599 的修复只在未验收的原型分支，
   0 CI、0 review、无 MSVC 真机、无产品路径复跑。
2. **前置面**：0/7 已满足；第 2/4/5/6 条没有可验收实现；第 7 条被
   `25f1966` 推翻，宿主现在需要先恢复 5,150 行。
3. **ABI 面**：v3 的 broken→141 归属未裁定，已知覆盖 `tee -p`/`xargs`/
   `grep/sed` 的 applet 级策略；在归属定案前接受 v3 会把“已知偏差”
   固化成 ABI 语义。
4. **出货面**：当前 1.5.0 安装树没有 FFI 树；`DEFAULT_ENABLED=false` +
   tier2 空清单 + `TIER1_FFI_CLAIMABLE=false` 是唯一安全态。
5. **签名面**：release policy 未批、full-package artifact 形态与
   VERSIONINFO 未落；签名门未过。

### 6.1 满足哪些前置后可复议

**全部 8 条同时满足后**，才建议把本单从“不建议复活”改为“可复议”：

1. 第 8 条：恢复宿主 FFI 路由模块（revert/重写 `25f1966`）并通过
   宿主 `ffi_route_census` / `issue727_ffi_*` 门；
2. 第 1/3 条：MSVC 出货构建上验收 CRT fd 重绑 + 缓存失效 + 标志复位；
3. 第 2/4 条：要么完成 `read_all_stdin` 句柄直读 + IO 习语收敛，要么
   书面对“CRT fd 重绑替代”给出 MSVC 证据并改判；读写检查点覆盖
   长任务；
4. 第 5 条：tiers-manifest 产出非空、可辩护的 tier2 集；
5. 第 6 条：在**出货 tiers.tsv** 上跑 7 形态全矩阵 + 门③；
6. ABI 归属已裁定并落地（tee -p / xargs / grep/sed 对拍通过）；
7. 签名：release policy 批准 + full-package artifact 配置 + 签名后
   digest/硬链接回写验证；
8. I18N：任何新增 FFI 诊断/帮助文本有稳定 key、英文 fallback、
   catalog 同步 + VERSION bump（当前材料未新增用户可见文本）。

### 6.2 如果 owner 决定强行推进

最小顺序（不建议跳步）：

1. 先恢复宿主（第 8 条）——否则 DLL 修好也没人调用；
2. 在 MSVC 出货构建上复跑本证据包（`-Expect fixed`）与门③；
3. 裁定 ABI 归属（备忘录 §4），再决定是否接受 v3；
4. 产出 tier2 集 + 出货 tsv 全矩阵；
5. 走 SignPath release policy；
6. 最后才改 `DEFAULT_ENABLED`，并保留 `NIU_WINUXCMD_FFI=0` 关停开关。

---

## 7. 交付文件清单

```
DOCS/ffi-1487/
  REVIVAL-DOSSIER.md                  ← 本文件
  PREREQ-CHECKLIST.md                 ← 7 条 + 新增第 8 条逐条核对
  ABI-V3-ATTRIBUTION-MEMO.md          ← host/command 归属裁定材料
  SIGNPATH-MATERIALS.md               ← artifact 形态 + policy secret 切换
  evidence/
    README.md                         ← 复现说明 + 预期/实际 + 安全边界
    Invoke-Gate1-Redirect.ps1         ← 门① 可重跑脚本
    Invoke-Gate3-Cancel.ps1           ← 门③ 可重跑脚本
    conpty_ctrl_c.py                  ← ConPTY 驱动（pywinpty 3.0.5）
    baseline-2026-11-01.json          ← 原始数字/路径（机器可读）
  refs/
    unixwin_niubash-727.json          ← #727 全文 + 4 条评论（原文）
    unixwin_WinuxCmd-1487.json        ← #1487 全文 + 5 条评论（原文）
```

### 7.1 交付统计

| 项 | 数量 |
|---|---|
| 证据脚本 | **3**（Gate1 `.ps1`、Gate3 `.ps1`、ConPTY `.py`） |
| 证据数据/说明 | 3（`README.md`、`baseline-2026-11-01.json`、`.gitignore`） |
| 裁定/核对文档 | 4（本文件、`PREREQ-CHECKLIST.md`、`ABI-V3-ATTRIBUTION-MEMO.md`、`SIGNPATH-MATERIALS.md`） |
| 原始 issue 档案 | 2（#727、#1487 全文 + 评论 JSON） |
| 调研来源 | **20**（19 个独立来源 + GNU 实测一组） |
| 7 条前置 | **已满足 0 / 未满足 7**；新增第 8 条 |

## 8. 未完成项 / 限制

1. **真实产品路径未跑**：本机只有 niu 1.5.0，FFI 模块与 DLL 均不存在；
   脚本以退出码 2 拒绝，是诚实结果，不是通过。要跑必须提供 1.4.x
   `niu.exe` + ABI v2 `winuxcore.dll` + `tiers.tsv`。
2. **tier2 扩行是测试用**：出货 tiers.tsv 仍为空；脚本的
   `-ApplyTier2` 只改一个文件并恢复，不等于第 5 条完成。
3. **门③ 假 niu 自测**验证了脚本机制，不验证 FFI 取消语义；真实
   修复后的 `< 2 s` 阈值要以 MSVC 构建复跑为准。
4. **ABI 备忘录是材料**：没有替 owner 做裁定；两个方案的工期是量级
   估算，不是承诺。
5. **SignPath 是外部依赖**：release policy 审批时间不可控；本文件
   不含任何 secret，也没有触发签名请求。
6. **未 commit/push**：遵守 lane 纪律；以上文件只在
   `D:/repo/wc-ffi1487` 工作树中，待 captain 审阅。
