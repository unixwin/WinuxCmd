# ln 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe ln`（Rust2 实现，`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**
- 方法：每例独立 fixture 树快照；证据 `work/out/ln-final.txt`（23 例）
- **环境限制**：本机未启用开发者模式，普通用户没有 `SeCreateSymbolicLinkPrivilege`。所有 `ln -s` 用例在 WinuxCmd 侧均返回 `Operation not permitted`，因此“符号链接创建成功后的语义”本轮**不可验证**（标 noise-platform，不算兼容缺口）。硬链接与选项解析不受影响。

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| 创建硬/软链接、强制/交互/备份 | `do_link` | `src/ln.c:178` |
| `-f` 的原子替换 | `atomic_link` | `src/ln.c:160` |
| `-r` 相对路径换算 | `convert_abs_rel` | `src/ln.c:124` |
| 选项解析 | `main` | `src/ln.c:466` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| N01 | 硬链接 | 同 inode，nlink+1，`existing` 存在则报 `File exists` | 一致 | 一致 |
| N02 | `-s` 普通目标 | 创建符号链接 | `failed to create symbolic link ...: Operation not permitted`（权限环境） | noise-platform |
| N03 | `-s` 目录目标 | 创建目录符号链接 | 同上 | noise-platform |
| N04 | 目标已存在（无 `-f`） | `failed to create hard link 'x': File exists`，rc=1 | 逐字节一致 | 一致 |
| N05 | `-f` 替换 | 先 unlink 再 link | 一致 | 一致 |
| N06 | `-i -f`（顺序 `-i` 在前） | **后者 `-f` 胜出**：不提示，直接替换，rc=0（`-f -i` 才提示） | 仍提示 `ln: replace 'b.txt'? `，回答 n 后 rc=1、未替换 | compat-gap |
| N07 | `-r` 相对符号链接 | 生成相对目标 | 受 N02 权限限制，未能验证 | noise-platform |
| N08 | `-sv` 输出 | `'sym' -> 'target'`（GNU 用 `->`） | 受权限限制，未到输出阶段 | noise-platform |
| N09 | `-v` 硬链接 | `'hard' => 'target'` | 逐字节一致 | 一致 |
| N10 | `-b` | `existing.txt~` | 一致 | 一致 |
| N11 | `--backup=numbered` | `existing.txt.~1~` | 一致 | 一致 |
| N12 | `-s` 悬空目标 | 允许创建悬空链接 | 权限限制 | noise-platform |
| N13 | `-s` 目标为目录 | 在目录内创建同名链接 | 权限限制（路径 `dir\target.txt` 已正确展开） | noise-platform |
| N14 | 目标是已存在的悬空链接 | `File exists`，rc=1，不覆盖 | 逐字节一致（#1367 回归通过） | 一致 |
| N15 | `-n`（不 deref 目标目录） | 视为普通名 | 权限限制 | noise-platform |
| N16 | `-T -s target dir` | `failed to create symbolic link 'dir': File exists`，rc=1 | `ln: dir: cannot overwrite directory`，rc=1 | wording |
| N17 | `-s -t dir target` | 在目录内创建 | 权限限制 | noise-platform |
| N18 | 目标不存在 | `failed to access 'no-such': No such file or directory`，rc=1 | 一致 | 一致 |
| N19 | 硬链接自身 | `File exists`，rc=1 | 一致 | 一致 |
| N20 | `-s` 绝对目标 | 原样保存绝对目标 | 权限限制 | noise-platform |
| N21 | 单操作数（隐含 `./`） | `failed to create hard link './target.txt'` | `'.\target.txt'`（Windows 分隔符） | wording |
| N22 | 非法选项 | `unrecognized option`，rc=1 | 一致 | 一致 |
| N23 | `-s dir/` 尾斜杠目标 | 原样保存 `dir/` | 权限限制 | noise-platform |

## 三、差异复现

fixture：`a.txt`（`A\n`）、`b.txt`（`B\n`）。

| # | 复现 | GNU expected | WinuxCmd actual |
|---|---|---|---|
| 1 | `printf 'n\n' \| ln -i -f a.txt b.txt` | 不提示；`b.txt` 被替换为硬链接，nlink 均为 2，rc=0 | `ln: replace 'b.txt'? `，回答 n 后 `b.txt` 保持 `B`，nlink 均 1，rc=1 |
| 2 | `ln -T -s a.txt dir`（dir 为目录） | `ln: failed to create symbolic link 'dir': File exists` | `ln: dir: cannot overwrite directory`（rc 同为 1） |
| 3 | `ln a.txt`（单操作数） | `ln: failed to create hard link './a.txt': File exists` | `... '.\a.txt' ...`（路径分隔符） |

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1308 | `--backup` 控制被丢弃；悬空目标 rc=0；`-v` 用 `->` | `--backup`/悬空目标通过；`-v` 受权限限制未复核 |
| #1367 | 悬空链接目标被当成不存在 | 通过 |
| #1609 | `create_symlink` 绝对化链接目标 | 受权限限制未复核 |
| #1101 | `./` 相对目标不可解析 | 受权限限制未复核 |
| #1234 | 裸盘符根拼接 cwd | 未覆盖 |
| #1491 | `ln -s` 无降级 | 现状：明确 `Operation not permitted`（未降级为 junction） |

## 五、计数

- 契约行：**23**
- 一致：**10**　compat-gap：**1**　wording：**2**　noise-platform：**10**
- 立案：1 条 —— #1651（N06）
