# cp 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe cp`（`WinuxCmd 1.2.1`，build-vs Release）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**，LC_ALL=C、TZ=UTC
- 方法：每个用例独立 fixture（`tpl` → `cNN` 拷贝，mtime 固定为 2020-01-02），比较 stdout/stderr/rc，并对执行后的整棵树做快照（路径/类型/size/mtime/hash/硬链接组）比较
- 证据：`work/out/cp-final.txt`（30 例，含树快照）；GNU 侧 `/tmp/wca/fx/gnu/cp`，Windows 侧 `work/fx/win/cp`

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| 命令行 / 参数收集 | `main` → `do_copy` | `src/cp.c:968` / `src/cp.c:608` |
| 递归复制 | `copy_dir` | `src/copy.c` |
| 单文件复制、链接分支 | `copy_internal` / `copy_reg` | `src/copy.c`（`copy_reg` 打开源：`open(..., O_NOFOLLOW if DEREF_NEVER)`，失败即 `cannot open %s for reading`） |
| 选项默认值 | `cp_option_init` | `src/cp.c:823` |
| 备份/更新/交互 | `--backup`、`--update` 表 | `src/cp.c:92-112` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| C01 | `cp file dest` | 内容/大小一致，dest mtime=now | 一致 | 一致 |
| C02 | 覆盖已存在文件 | 截断重写，内容一致 | 一致 | 一致 |
| C03 | `-i` 回答 n | 打印 `cp: overwrite 'x'? `，不复制，**rc=1** | 同样提示、不复制，**rc=0** | compat-gap |
| C04 | `-i` 回答 y | 复制，rc=0 | 一致 | 一致 |
| C05 | `-i` 遇 EOF | 视同拒绝，rc=1 | rc=0（同上） | compat-gap |
| C06 | `-n` / `--no-clobber` | 跳过并输出 9.4 的 non-portable 警告 | 逐字节一致 | 一致 |
| C07 | `-v` | `'src' -> 'dst'`（stdout） | 一致 | 一致 |
| C08 | `-r` 目录树 | 递归复制（符号链接按链接复制） | **含符号链接即失败**：`cp: cannot open 'src\lnk' for reading: Invalid argument`，rc=1 | compat-gap |
| C09 | `-a` 目录树 | `-dR --preserve=all`，目录 mtime 保留 | 同样因符号链接失败；无链接目录时 mtime 正确保留 | compat-gap |
| C10 | `cp -a src/. dst/` | 复制目录内容 | 因符号链接失败 | compat-gap |
| C11 | 符号链接源，默认解引用 | 复制目标内容（普通文件） | `cannot open ... Invalid argument`，rc=1 | compat-gap |
| C12 | `-d` / `-P` 保留链接 | 新建同目标符号链接 | 同样失败 | compat-gap |
| C13 | `-L` 强制解引用 | 复制目标内容 | 同样失败 | compat-gap |
| C14 | `-p` | mode/mtime/owner 保留（mtime 实测一致） | 一致 | 一致 |
| C15 | `--parents src/one.txt dir` | 目标须存在，否则 rc=1 并给 `Try 'cp --help'` | 错误同，**缺 help 提示行** | wording |
| C16 | `-t DIR` / `-T` | `-t` 目标目录必须存在；`-T` 视 dest 为普通名 | 一致 | 一致 |
| C17 | `-l` 硬链接 | 同 inode、nlink+1 | 一致 | 一致 |
| C18 | `-s` 符号链接 | 创建链接 | `A required privilege is not held by the client`（本机未开开发者模式） | noise-platform |
| C19 | `-b` / `--backup=numbered` | `existing.txt~` / `existing.txt.~1~` | 一致 | 一致 |
| C20 | `-f` 覆盖只读目标 | 先 unlink 再建 | 一致 | 一致 |
| C21 | `-u` 等 mtime | 不复制 | 一致 | 一致 |
| C22 | `--update=none` | 跳过 | 一致 | 一致 |
| C23 | `--reflink=never` | 普通复制 | 一致 | 一致 |
| C24 | 目录复制到自身 | rc=1；GNU 先复制部分内容后报错 | rc=1；WinuxCmd 预检查，未落盘，消息为 `cannot copy directory 'src' into itself 'src/inner'` + 第二条 `error copying directory` | wording（行为更安全） |
| C25 | 无 `-r` 复制目录 | `cp: -r not specified; omitting directory 'emptydir'`，rc=1 | 一致 | 一致 |
| C26 | `-g` 伪选项 | `invalid option -- 'g'`，rc=1 | 一致（#1411 回归通过） | 一致 |
| C27 | 缺目标/缺源 | rc=1，消息逐字节一致 | 一致 | 一致 |
| C28 | `-x` / `--one-file-system` | 不跨文件系统 | 未覆盖（单卷 fixture 无法构成） | 未覆盖 |
| C29 | `--sparse=*` / `--reflink=always` | 稀疏/克隆语义 | 未覆盖（需 NTFS 稀疏/块克隆对照） | 未覆盖 |
| C30 | `--preserve=mode,timestamps` | 等价 `-p` 子集 | 未单独覆盖（`-p` 已覆盖） | 未覆盖 |

## 三、差异复现

fixture：`a.txt`(A)、`b.txt`(B)、`dir/{f.txt,sub/g.txt}`、`link -> a.txt`。

| # | 复现 | GNU expected | WinuxCmd actual |
|---|---|---|---|
| 1 | `cp link out` | rc=0，`out` 为 2 字节普通文件 | `cp: cannot open 'link' for reading: Invalid argument`，rc=1，无 `out` |
| 2 | `cp -r dir dst`（`dir` 内含链接，或 `cp -r . dst`） | 递归复制全部，链接按链接复制，rc=0 | 其余文件已复制，但遇链接报上述错误，rc=1 |
| 3 | `printf 'n\n' \| cp -i a.txt b.txt` | 提示后不复制，**rc=1** | 提示后不复制，**rc=0** |
| 4 | `cp --parents a.txt missing-dir` | 两行：错误 + `Try 'cp --help' for more information.` | 仅第一行 |

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1435 | `cp -r` 自复制前缀检查可绕过 | 通过（rc=1，且未在自身内落盘） |
| #1324 | `-i` 按字符读答案导致下一提示被自动回答 | 通过（单个答案只消费一次，第 29 例 EOF 未误答） |
| #1411 | `cp -g` 伪选项 | 通过 |
| #1145 | `cp` 对 `/d/...` 盘符路径 | 未覆盖（本 lane 未构造 POSIX 盘符操作数） |
| #1052 | 尾斜杠被静默剥离 | 未覆盖（本轮未设该用例） |
| #995 | `-Z` 拒绝 / 本地化诊断 | 未覆盖（SELinux 选项） |

## 五、计数

- 契约行：**30**
- 一致：**16**　compat-gap：**8**　wording：**2**　noise-platform：**1**　未覆盖：**3**
- 立案：2 条 —— #1647（C08-C13，符号链接源不可复制）、#1648（C03+C05，`-i` 拒绝 rc 应为 1）
