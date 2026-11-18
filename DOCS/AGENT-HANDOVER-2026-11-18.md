# WinuxCmd 2.0 — Agent 交接提示词（2026-11-18）

> 这份文件是给**接手的 agent** 的作战简报。原文见 `DOCS/2.0-HANDOVER.md`（路线图与技术债），本文只写"现在到哪了、下一步干什么、什么绝对不能碰"。

---

## 一、项目是什么

WinuxCmd 是 Windows 上的 GNU coreutils/util-linux 兼容实现。1.x 是 C++（`src/`，C++23 modules），2.0 正在把每个 applet 一个个改写成 Rust（`rust2/`），改写后删掉 C++ 体、把 dispatcher 转到 Rust ABI。目标 176 个 applet 全部 Rust 化。

**当前三个层次**：
- `origin/main` = 已完成 Rust 化的命令（约 50+ 个已接线）
- open PR = 每个 PR 把一个命令推到 GNU 完全对齐（WP-I parity）
- worktree = 各命令的独立开发树（最多时 70+ 个）

---

## 二、当前精确状态（2026-11-18 09:20 CST 实测，不要相信旧文档）

### 基线
- **`origin/main = 55453ed7`**（PR #1830 df 刚合并）
- 主 checkout `D:/repo/unixwin-winuxcmd` 停在**旧分支** `fix/1496-posix-subexpression-capture`（上游已删，PR 已合并），**没有 `rust2/` 目录**，还有未提交的 `DOCS/2.0-HANDOVER.md` 和未推送的 `3a81a378` docs 提交。
- **接手第一件事就是切回 main**，不要在旧分支上工作。

### 12 个 open PR
| PR | 分支 | 主题 | 状态 |
|---|---|---|---|
| 1831 | fix/parity-date | date --uct/--rfc-822 + rust2 | 待 rebase 55453ed7 |
| 1829 | fix/parity-tty | tty write-failure status 3 | 待 rebase |
| 1828 | fix/parity-shuf | shuf WP-I parity + Keccak 修正 | 待 rebase |
| 1827 | fix/parity-cksum | cksum WP-I parity | 待 rebase |
| 1826 | fix/parity-head | head WP-I parity | 待 rebase |
| 1825 | fix/parity-tail | tail WP-I parity | 待 rebase |
| 1824 | fix/parity-sort | sort WP-I parity | 待 rebase |
| 1823 | fix/parity-csplit-factor | csplit/factor parity | 待 rebase |
| 1821 | fix/wire-lsrm | ls/rm 走 rust2 | CONFLICTING |
| 1819 | docs/cpp-only-triage | 94 个 C++-only applet 分级 | **MERGEABLE**（可直接合） |
| 1818 | fix/def11 | 11 个库缺陷 | CONFLICTING |
| 1817 | test/1x-repin-clean | GNU 9.4 重新钉住 1.x 断言 | **MERGEABLE**（可直接合） |

### 已完成（本会话）
- **i18n 仓 `unixwin/winuxcmd-i18n` 已同步到 v0.6.53**（提交 `8bd1169`）：删除 7 个全分支死 key，release 已发布。main 的 zh-CN drift extra 从 9 降到 2。
- **#1830（df）已合并**；**#1819 gate-shard 失败确认是 WSL 安装步骤 flaky**（重跑即绿）。
- **#1829 tty 的 i18n baseline 已记录并 push**（`53d4169c`）。

---

## 三、下一步（按优先级）

### P0：把 7 个 parity PR 推到可合并
正有一个 agent 在做（分支：shuf/cksum/head/tail/sort/csplit-factor/tty）。若它死了，照下面做：
1. `git fetch origin`，确认 main = `55453ed7` 或更新
2. 每个分支用自己的 worktree（wt-head/wt-tail/wt-csplit/wt-tty 已存在；shuf/cksum/sort 需自建）
3. `git rebase origin/main`，冲突主要是 **CMakeLists.txt 命令列表并集**
4. i18n baseline：**唯一合法路径**是在 PR 分支跑
   `python scripts/check_i18n_drift.py --i18n-repo D:/repo/i18n-pub --update-baseline`
   再跑一次不带 flag 的验证 exit 0。**不要从 zh-CN catalog 删 key**——那些 key 仍被 main 产出，删了 main 会红。
   （i18n 克隆在 `D:/repo/i18n-pub`，需在 `8bd1169`）
5. `scripts/build-with-vs.ps1` 构建 → 跑受影响测试/gate → `scripts/format.py` → `git push --force-with-lease`

### P1：合并 ready 的 PR
- #1817 / #1819 已 MERGEABLE，只等 captain 批准合并。

### P2：清理（危险，需 captain 确认）
实测积压：
- **76 个 worktree**，其中 **15 个分支已完全合进 main**（可 `git worktree remove` + `git branch -d`）
- **本地 172 分支**：89 已合并（可删）、70 无 PR 历史（多数可用 `git cherry` 确认已进 main）
- **远程 208 分支**：73 已合并（可 `git push --delete`）
- 8 个 worktree 有未提交改动，其中 `wc-finalfix`(3 文件)、`wc-np1726`(7 文件) 分支已合并但改动未提交——需人工判断保留还是丢弃

### P3：i18n 仓基建 bug（预存，非本会话引入）
`unixwin/winuxcmd-i18n` 的 Release workflow 尾步持续失败：
```
ERROR: Command 'git' is provided by both 'git' and 'niugit'
```
导致 wpm 线上 index 停在 **0.6.14**，与已发布的 0.6.53 严重脱节。需修 Release workflow 的 PATH 冲突。

---

## 四、绝对红线

1. **不要添加 `link` 命令** —— 会 shadow MSVC 的 `link.exe`，破坏构建（曾被删过一次 `10b2364`，又被 bot PR #1065 加回来）。
2. **agent 不合并 PR** —— 合并是 captain 权限。agent 只 push 分支 + 报告。
3. **不 push main**。
4. **不动主 checkout 的未提交改动**（`DOCS/2.0-HANDOVER.md`）。
5. **后台 agent 并发上限 = 1** —— 本账号实测，同时派 2 个会把前一个踢死。必须串行。
6. **i18n**：任何用户可见文本改动需要稳定 key + 英文 fallback；改 catalog 必须 bump `VERSION`；WinuxCmd 和 i18n 仓要分别提交。

---

## 五、环境陷阱（踩过的坑）

- **agent 的 bash 是 niubash 系**，会改写 POSIX 参数，`MSYS_NO_PATHCONV`/`MSYS2_ARG_CONV_EXCL` 都无效。
  → 产品契约探针用 **PowerShell 原样交接** / WSL GNU oracle 用**脚本文件**（禁 `wsl -c` 传引号，`\\` 会折叠）/ 路径操作数用**环境变量**。详见 `rust2/gate/PROBE-METHODOLOGY.md`。
- **构建必须用** `scripts/build-with-vs.ps1`；**格式化必须用** `scripts/format.py`。
- **gh 调用注意限额**：日志先 `gh run view --job <id> --log > 文件` 再 grep，别直接管道给 grep。
- **Git Bash 老 bsdtar 解 zip 会写全零文件** —— 手工解压用 Python zipfile。
- **WSL 安装步骤会 flaky 挂 19.5 分钟**导致 gate-shard 被 timeout 取消 —— 先看 main 同 shard 是否绿；是 flaky 就 `gh run rerun --failed`。
- **推送验证用 `ls-remote`**，不要信 "up-to-date"（多 worktree 的 remote ref 会互相污染）。
- **`--force-with-lease` 优先于 `--force`**。

---

## 六、关键文件

| 路径 | 内容 |
|---|---|
| `DOCS/2.0-HANDOVER.md` | 完整路线图、技术债、探针方法论 |
| `rust2/REPLACEMENT-RECIPE.md` | 机械替换配方（每命令 6 步） |
| `rust2/gate/run-gate.sh` | 差分门 runner |
| `rust2/gate/gen-cases.py` | 差分用例生成器（单一真源） |
| `rust2/gate/COVERAGE.md` | 覆盖率看板 |
| `rust2/gate/PROBE-METHODOLOGY.md` | 探针方法论（必读） |
| `scripts/check_i18n_drift.py` | i18n drift 门（`--update-baseline` 是 PR 合法路径） |
| `D:/repo/i18n-pub` | i18n 仓工作克隆 |

---

## 七、给接手 agent 的第一条命令

```bash
cd /d/repo/unixwin-winuxcmd
git fetch origin --prune
git log --oneline -5 origin/main      # 确认基线
gh pr list --state open --json number,headRefName,mergeable
```
然后从「三、下一步」的 P0 开始。有任何不确定就停下来问 captain，不要猜。
