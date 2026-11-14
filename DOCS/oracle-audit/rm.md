# rm 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe rm`（`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**
- 方法：每例独立 fixture 树快照比较；证据 `work/out/rm-final.txt`（23 例）

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| 命令行、保护根、交互策略 | `main` | `src/rm.c:207` |
| 选项默认值 | `rm_option_init` | `src/rm.c:189` |
| 遍历/删除（fts） | `remove_entry` | `src/remove.c` |
| 前导 `-` 诊断 | `diagnose_leading_hyphen` | `src/rm.c:102` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| R01 | 删除普通文件 | rc=0，条目消失 | 一致 | 一致 |
| R02 | `-i` 回答 n | `rm: remove regular file 'a.txt'? `，保留，rc=0 | 逐字节一致 | 一致 |
| R03 | `-i` 回答 y | 删除，rc=0 | 一致 | 一致 |
| R04 | `-f` 缺失操作数 | 静默，rc=0 | 一致 | 一致 |
| R05 | 缺失操作数 | `cannot remove ... No such file or directory`，rc=1 | 一致 | 一致 |
| R06 | `-r` 目录 | 整棵删除 | 一致 | 一致 |
| R07 | 无 `-r` 删目录 | `Is a directory`，rc=1 | 一致 | 一致 |
| R08 | `-v` 文件 | `removed 'a.txt'` | 一致 | 一致 |
| R09 | `-rv` 目录 | 逐项 `removed`/`removed directory`，效果一致；遍历顺序随目录枚举 | 效果一致，输出顺序不同（NTFS 有序） | noise-platform |
| R10 | 符号链接指向目录 | 只删链接，不递归目标 | 一致（#1433 回归通过） | 一致 |
| R11 | `-ri` 交互顺序 | 先 `descend into 'dir'`，再按 fts 顺序逐项询问，含 `remove directory 'dir/sub'` | 询问集合相同但顺序按 NTFS 枚举；answers 足够时最终树一致 | noise-platform |
| R12 | 悬空符号链接 | 删除链接，rc=0 | 一致 | 一致 |
| R13 | 空操作数 `rm ''` | `rm: cannot remove '': No such file or directory`，rc=1 | `rm: missing operand` + `Try 'rm --help'`，rc=1 | compat-gap |
| R14 | `rm -r .` | `refusing to remove '.' or '..'`，rc=1 | 一致 | 一致 |
| R15 | `--` 终止选项 | `-- a.txt` 删除成功 | 一致 | 一致 |
| R16 | `-I` ≤3 项 | 不询问 | 一致 | 一致 |
| R17 | `-I` >3 项 | `rm: remove 4 arguments? ` | 逐字节一致 | 一致 |
| R18 | `--preserve-root`（相对路径） | 不影响普通目录 | 一致 | 一致 |
| R19 | `--no-preserve-root` | 相对目录正常删除 | 一致 | 一致 |
| R20 | `-v` 缺失 | 报错 rc=1，无 verbose 行 | 一致 | 一致 |
| R21 | 非法选项 | `unrecognized option`，rc=1 | 一致 | 一致 |
| R22 | `-d` | 删除空目录；本 fixture 无 `emptydir`，双方同为 ENOENT | 一致（用例弱） | 一致 |
| R23 | `-ri` 全部回答 y | 最终树完全一致 | 一致 | 一致 |

## 三、差异复现

| # | 复现 | GNU expected | WinuxCmd actual |
|---|---|---|---|
| 1 | PowerShell：`rm.exe ""`（或 `rm -- ""`） | `rm: cannot remove '': No such file or directory`，rc=1 | `rm: missing operand` / `Try 'rm --help' for more information.`，rc=1 |

复现（WSL 侧对照）：`rm ''`。注意 `''` 是显式空路径操作数，GNU 把它当路径而不是“缺操作数”。

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1433 | 删除指向目录的符号链接语义 | 通过 |
| #1327 | `-i` 提示文案（`remove regular file` 等） | 通过（逐字节一致） |
| #1328 | `-v` 目录行缺 `directory`、`\\?\` 绝对路径 | 通过（`removed directory 'dir'`，相对路径） |
| #1298 | `-v` 被忽略 | 通过 |
| #962 | `--no-preserve-root` 危险缩写 | 未覆盖（需 root 语义探针，安全边界外） |
| #1083 | 隐藏调试选项 | 未覆盖 |

## 五、计数

- 契约行：**23**
- 一致：**20**　compat-gap：**1**　noise-platform：**2**
- 立案：1 条 —— #1650（R13）
