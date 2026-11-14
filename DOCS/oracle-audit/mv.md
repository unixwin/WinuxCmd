# mv 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe mv`（`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**
- 方法：每例独立 fixture 快照比较（同 cp）；证据 `work/out/mv-final.txt`（22 例）

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| rename 路径解析、跨卷回退 | `do_move` | `src/mv.c:167` |
| 选项默认值 | `cp_option_init` / `rm_option_init` | `src/mv.c:119` / `src/mv.c:87` |
| 备份/更新/交互 | `main` | `src/mv.c:311` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| M01 | 文件改名 | inode 保留，内容/时间不变 | 一致 | 一致 |
| M02 | 文件移入目录 | `dstdir/one.txt` | 一致 | 一致 |
| M03 | 目录移入目录 | 整棵移动 | 一致 | 一致 |
| M04 | `-i` 回答 n | 提示、不移动，**rc=1** | 同 | 一致 |
| M05 | `-i` 回答 y | 覆盖，rc=0 | 同 | 一致 |
| M06 | `-n` | 不替换，`mv: not replacing 'x'`，rc=1 | 逐字节一致 | 一致 |
| M07 | `-u` 等 mtime | 跳过，rc=0 | 一致 | 一致 |
| M08 | `-v` | `renamed 'a' -> 'b'` | 一致 | 一致 |
| M09 | `-t missing` | `mv: target directory 'missing': No such file or directory`，rc=1 | 一致 | 一致 |
| M10 | `-T` | dest 视为普通名 | 一致 | 一致 |
| M11 | 目录覆盖普通文件 | **拒绝**：`mv: cannot overwrite non-directory 'a.txt' with directory 'dir'`，rc=1，原文件保留 | **静默把 a.txt 替换成目录**，rc=0，原文件内容丢失 | compat-gap |
| M12 | 源不存在 | `cannot stat ...`，rc=1 | 一致 | 一致 |
| M13 | 目录移入自身子目录 | rc=1；消息 `cannot move 'src' to a subdirectory of itself` | rc=1；消息写成 `cannot copy a directory, 'src', into itself`（mv 说 copy） | wording |
| M14 | `--backup=numbered` | `existing.txt.~1~` | 一致 | 一致 |
| M15 | `-b` | `existing.txt~` | 一致 | 一致 |
| M16 | `--exchange` | **9.4 无此选项**（rc=1）；9.5 起支持 | 实现交换，rc=0 | 一致（GNU 9.5 语义，版本差） |
| M17 | 符号链接作为源 | 移动链接本身，不跟随 | 一致 | 一致 |
| M18 | `-f` 覆盖只读目标 | 成功 | 一致 | 一致 |
| M19 | `--update=none` | 跳过 | 一致 | 一致 |
| M20 | 缺目标操作数 | rc=1 + `Try 'mv --help'` | 逐字节一致 | 一致 |
| M21 | `--strip-trailing-slashes` | 去尾斜杠后移动 | 一致 | 一致 |
| M22 | 非法选项 | `unrecognized option`，rc=1 | 一致 | 一致 |

## 三、差异复现

fixture：`dir/{f.txt,sub/g.txt}`、`a.txt`（内容 `A\n`）。

| # | 复现 | GNU expected | WinuxCmd actual |
|---|---|---|---|
| 1 | `mv dir a.txt` | `mv: cannot overwrite non-directory 'a.txt' with directory 'dir'`，rc=1，`a.txt` 仍是 2 字节普通文件 | rc=0，`a.txt` 变成目录（`Attributes: Directory`），原子文件内容不可恢复 |
| 2 | `mv src src/inner` | `mv: cannot move 'src' to a subdirectory of itself, 'src/inner'`，rc=1 | `mv: cannot copy a directory, 'src', into itself, 'src/inner'`，rc=1（动词 copy 与 GNU 不符） |

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1326 | `-u` 恒压 `--update=` | 未直接覆盖（仅测 `-u` 单用）——列入后续 |
| #1325 | 原始 argv 扫描误判 `-n/-i` | 通过（`-t` 值未误判；`-i`/`-n` 行为正确） |
| #1365 | 跨卷移动把链接变副本 | 未覆盖（需跨卷） |
| #1068 | `--exchange` 恒失败 | 现已实现（GNU 9.5 语义） |
| #1140 | Windows 源 + POSIX 目标混合失败 | 未覆盖 |
| #951 | 链接+斜杠源 | 未覆盖 |

## 五、计数

- 契约行：**22**
- 一致：**20**　compat-gap：**1**　wording：**1**
- 立案：1 条 —— #1649（M11，数据丢失级）
