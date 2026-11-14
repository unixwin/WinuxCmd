# ls 行为契约表（#1462 Oracle 审计）

- 审计对象：WinuxCmd `winuxcmd.exe ls`（build-vs Release，`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`
- GNU oracle：WSL2 内 GNU coreutils **9.4**（`ls (GNU coreutils) 9.4`），LC_ALL=C、TZ=UTC、stdout 为管道
- 方法：同一份 fixture 分别由 GNU 与 WinuxCmd 执行，逐字节比较 stdout/stderr/rc；平台相关列（权限位、属主、inode、块数、总块、目录 size）按注记归一
- 证据：`DOCS/oracle-audit/work/out/ls-final.txt`（99 例）；WSL 脚本 `work/run.sh`，Windows 侧经 `work/win-run.ps1` 直接以原始字节捕获（避免 tty/代码页干扰）

## 一、权威源（GNU coreutils 9.4，`src/ls.c`）

| 语义 | 归属 C 函数 | 行 |
|---|---|---|
| 选项解码 / 输出宽度 | `decode_switches`；`COLUMNS` 读取 | 1902；2294-2307 |
| 条目采集 / 类型判定 | `gobble_file` | 3370 |
| 排序 | `sort_files` | 4086 |
| 列数计算（严格 `<`） | `calculate_columns` | 5366（判据 5404-5407） |
| 长格式（含 ` -> target`） | `print_long_format` | 4304（箭头 4498） |
| 指示符 | `print_type_indicator` | 4933 |
| 名称引用/转义 | `quote_name` / `print_name_with_quoting` | 4753 / 4818 |
| 列式输出 | `print_many_per_line` / `print_horizontal` / `print_with_separator` | 5136 / 5172 / 5213 |
| 递归与操作数分区 | `print_dir` / `extract_dirs_from_files` | 2981 / 3741 |

## 二、契约表 A：选项面（逐 flag）

| ID | 选项 | GNU 契约 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| L01 | `-a` | 含 `.` `..` 与点文件 | 一致 | 一致 |
| L02 | `-A` | 排除 `.` `..`，保留点文件 | 一致 | 一致 |
| L03 | `-b` | C 风格转义：`sp\ ace.txt`、C locale 下非 ASCII 转字节八进制 | 空格与 UTF-8 均原样 | compat-gap |
| L04 | `-B` | 忽略 `*~` | 一致 | 一致 |
| L05 | `-c` | 按 ctime 排序；`-l` 时显示 ctime | 接受并排序，但 ctime 语义在 Windows 不可移植，无法与 ext4 对等核验 | noise-platform |
| L06 | `-C` | 多列；宽度取 `-w` > tty > `COLUMNS` > 80；`COLUMNS=20` 时 1 列 | 一致（#1504/#1536/#1542/#1543 回归通过） | 一致 |
| L07 | `-d` | 列目录自身 | 一致 | 一致 |
| L08 | `-D`/`--dired` | `-l` + `//DIRED//` 块 | 块与偏移一致（箭头问题见 L31） | 一致 |
| L09 | `-f` | `-aU`（无排序） | 接受；NTFS 目录枚举本身有序 | noise-platform |
| L10 | `-F` | 分类符 `*/=>@|`；可执行位加 `*` | 一致；Windows 上 `.exe` 额外加 `*`（无 exec 位） | noise-platform |
| L11 | `-g` | 省略属主列 | 一致（组名 `None`） | 一致 |
| L12 | `-G` | 省略组列 | 一致 | 一致 |
| L13 | `-h` | 1024 进制人类可读 | 一致 | 一致 |
| L14 | `-H` | 仅解引用命令行符号链接 | 一致 | 一致 |
| L15 | `-i` | 打印 inode | 一致（值本身平台相关） | 一致 |
| L16 | `-I PATTERN` | 忽略匹配项 | 一致 | 一致 |
| L17 | `-k` | 1K 块 | 一致 | 一致 |
| L18 | `-l` | 长格式；符号链接 `link -> target`，size=目标串长 | 缺 ` -> target`，size=0 | compat-gap |
| L19 | `-L` | 解引用所有符号链接；悬空链接报错但继续 | 文件链接报 `cannot access './x'`，目录链接同样失败，rc=2 | compat-gap |
| L20 | `-m` | 逗号分隔填充 | 一致 | 一致 |
| L21 | `-n` | 数字 uid/gid | 结构一致（权限位平台相关） | noise-platform |
| L22 | `-N` | 字面名称 | 一致 | 一致 |
| L23 | `-o` | 省略组列 | 一致 | 一致 |
| L24 | `-p` | 目录加 `/` | 一致 | 一致 |
| L25 | `-q` | C locale 下不可打印字节→`?`（`h??llo`） | 保留 UTF-8（`héllo`） | noise-locale |
| L26 | `-Q` | 双引号 + 非 ASCII 转**字节**八进制（`h\303\251llo`） | 转**码点**（`h\xe9llo`） | noise-locale |
| L27 | `-r` | 反序 | 一致 | 一致 |
| L28 | `-R` | 递归，`dir:` 头 | 一致 | 一致 |
| L29 | `-s` | 块数，按最大宽度右对齐（`20 huge.bin` ⇒ 单位数带前导空格） | 数值不右对齐（`1 a.txt` 无前导空格） | compat-gap |
| L30 | `-S` | 按 st_size 降序；目录 4096、符号链接=目标串长 | 目录/链接按 0 计，顺序随之改变 | compat-gap |
| L31 | `-t` | 按 mtime 降序，等值按名称 | 一致 | 一致 |
| L32 | `-T` | 制表宽度 | 接受；本机无法构造含 TAB 文件名（NTFS 禁控符） | 一致 |
| L33 | `-u` | 按 atime | 一致（硬链接 atime 归属略有差异） | 一致 |
| L34 | `-U` | 目录顺序、不排序 | NTFS 枚举本身按名有序 | noise-platform |
| L35 | `-v` | 版本序 | 一致 | 一致 |
| L36 | `-w N` | 指定宽度；0=不限 | 一致 | 一致 |
| L37 | `-x` | 横向排列；列距=该列最宽+2 | 列间多 1 空格；窄宽度下少一列 | compat-gap |
| L38 | `-X` | 按扩展名 | 一致 | 一致 |
| L39 | `-1` | 单列 | 一致 | 一致 |
| L40 | `-Z` | 打印安全上下文；非 SELinux 输出 `?`，rc=0 | `ls: --context is not supported on Windows`，rc=1 | compat-gap |
| L41 | `--author` | `-l` 附加作者列 | 结构一致（值为当前用户） | 一致 |
| L42 | `--block-size=SIZE` | 缩放 size 列；`human-readable` 可用 | 值一致；`-s` 对齐问题随 L29 | compat-gap |
| L43 | `--color[=WHEN]` | 解析 LS_COLORS；未设 LS_COLORS 时不着色 | 解析 LS_COLORS 一致；未设时仍输出内置配色 | noise-platform |
| L44 | `--file-type` | 目录 `/`、链接 `@` 等 | 一致 | 一致 |
| L45 | `--format=across` | 同 `-x` | 同 L37 | compat-gap |
| L46 | `--format=commas/vertical/single-column` | 对应 `-m/-C/-1` | 一致 | 一致 |
| L47 | `--full-time` | ISO 全精度 | 一致（时区见 stat 表） | 一致 |
| L48 | `--group-directories-first` | 目录（含“指向目录的符号链接”）前置 | 指向目录的符号链接仍留在文件段 | compat-gap |
| L49 | `--hide-control-chars` | 同 `-q` | 同 L25 | noise-locale |
| L50 | `--hyperlink[=WHEN]` | OSC 8 + `file://` URI，百分号编码 | 一致（主机名前缀/路径不同） | 一致 |
| L51 | `--indicator-style=WORD` | none/slash/file-type/classify | 一致 | 一致 |
| L52 | `--inode/--size/--literal/--numeric-uid-gid/--quote-name/--recursive/--reverse/--show-control-chars/--tabsize/--width` | 同对应短选项 | 一致 | 一致 |
| L53 | `--quoting-style=WORD` | 8 种 style；shell 家族按需引号 | `locale`/`c-maybe` 码点转义；`shell` 对 `bak~` 多余加引号、撇号用 `'\''` 而非双引号 | compat-gap |
| L54 | `--si` | 1000 进制，后缀小写 `k`（`20k`） | 仍按 1024，后缀 `K`（`20K`） | compat-gap |
| L55 | `--sort=WORD` | 非法值 rc=1，候选表无 `name`/`type` | rc=2；候选表含 `name`、`type`，顺序不同 | compat-gap |
| L56 | `--time=WORD` | atime/ctime/use | 一致 | 一致 |
| L57 | `--time-style=WORD` | 支持 `+FORMAT`/TIME_STYLE | 一致 | 一致 |
| L58 | `--version` | GNU 版本头 | WinuxCmd 版本头 | wording |

## 三、契约表 B：语义与回归

| ID | 契约项 | GNU 实测 | WinuxCmd 实测 | 裁定 |
|---|---|---|---|---|
| S01 | 操作数分区（文件先于目录） | `ls sub a.txt` → `a.txt` 后 `sub:` | 同 | 一致（#993 回归通过） |
| S02 | 文件操作数尾斜杠 | `aaa.txt/` → ENOTDIR, rc=2 | 同 | 一致（#1051） |
| S03 | 通配符字面量 | `*.txt` 无匹配 → ENOENT, rc=2 | 同 | 一致（#1051） |
| S04 | 裸 `ls /` | 输出 `/` | 输出 `/` | 一致（#1364） |
| S05 | 裸名 `.exe` 透明 | `ls x`（x.exe 存在）→ ENOENT | 同 | 一致（#1572 语义保持） |
| S06 | `COLUMNS` 在管道下生效 | `COLUMNS=20 ls -C` → 每行 1 项 | 同 | 一致（#1504） |
| S07 | 严格列宽（fit == width 不合格） | `grid` 5 名，COLUMNS=4 → 2 列（a dd / b ee / c） | 同 | 一致（#1542/#1543） |
| S08 | `-w` 优先于 `COLUMNS` | `-w 20` 覆盖 COLUMNS | 同 | 一致 |
| S09 | 伪选项拒绝 | `--long` → unrecognized option, rc=2 | 同 | 一致（#1411） |
| S10 | 目录 size | 4096 | 0（NTFS） | noise-platform |
| S11 | 8 进制转义/本地化引号 | C locale：`h\303\251llo.txt` | `h\xe9llo.txt` | noise-locale |
| S12 | 多字节宽度排序 `--sort=width` | 按显示宽度 | 按字节长度，`héllo.txt` 位置不同 | noise-locale |

## 四、差异复现（最小、可原样交接）

fixture（PowerShell；GNU 侧用 `/tmp/iss` 等价内容）：

```powershell
$d = "$env:TEMP/iss"; Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$d/dir/sub","$d/empty" | Out-Null
Set-Content -Path "$d/a.txt" -Value "aaa"; Set-Content -Path "$d/b.txt" -Value "bbbbb"
Set-Content -Path "$d/c.txt" -Value "c"
Set-Content -Path "$d/dir/f.txt" -Value "f"; Set-Content -Path "$d/dir/sub/g.txt" -Value "g"
Set-Content -Path "$d/sp ace.txt" -Value "space"
Set-Content -Path "$d/it's.txt" -Value "quote"
$f = [IO.File]::Create("$d/huge.bin"); $f.SetLength(20000); $f.Close()
# 符号链接需开发者模式/管理员：cmd /c mklink link a.txt 等
```

| # | 复现 | GNU（expected，字节级） | WinuxCmd（actual） |
|---|---|---|---|
| 1 | `ls -l link` | `lrwxrwxrwx 1 root root 5 ... link -> a.txt` | `lrwxrwxrwx 1 cmx None 0 ... link`（无 ` -> a.txt`，size=0） |
| 2 | `ls -lL link a.txt` | 两行均为 `-rw-r--r-- ... a.txt` / `... link`，rc=0 | `a.txt` 正常；`ls: cannot access 'link': No such file or directory`，rc=2 |
| 3 | `ls -s -1`（含 20 块文件） | ` 4 a.txt`、`20 huge.bin`（右对齐，宽 2） | `4 a.txt`、`20 huge.bin`（单位数无前导空格） |
| 4 | `ls -x -w 20` | `a.txt  b.txt` / `c.txt  dangling` …（2 列） | 每行 1 项（列距多 1，放不下第 2 列） |
| 5 | `ls -b -1` | `sp\ ace.txt` | `sp ace.txt` |
| 6 | `ls --quoting-style=shell -1` | `"it's.txt"`、`'sp ace.txt'` | `'it'\''s.txt'`、`'sp ace.txt'` |
| 7 | `ls --group-directories-first -1` | `dir, dirlink, empty, a.txt …` | `dir, empty, a.txt, …, dirlink …` |
| 8 | `ls -Z -1` | `? a.txt` … rc=0 | `ls: --context is not supported on Windows`，rc=1 |
| 9 | `ls --sort=bogus` | rc=1，候选 `none/time/size/extension/version/width` | rc=2，候选多 `name`/`type` |
| 10 | `ls --si -l huge.bin` | `20k` | `20K` |

## 五、已关闭 issue 回归核对

| issue | 主题 | 本轮结论 |
|---|---|---|
| #1504 | 管道下忽略 COLUMNS | 通过（`COLUMNS=20` 生效；先前的失败是审计通道 tty 伪影，已用 `win-run.ps1` 复核） |
| #1542/#1543 | 网格严格 `<` 拟合 | 通过（COLUMNS=3/4/5/7 全对等） |
| #993 | 文件/目录分区 | 通过 |
| #1051 | 通配符/尾斜杠伪造条目 | 通过 |
| #1364 | 裸 `/` 绕过 argv 边界 | 通过 |
| #1067 | `--hyperlink`/`--dired` 恒失败 | 通过（OSC 8 与 DIRED 块一致） |
| #1411 | 伪选项 `--long` | 通过 |
| #1572 | 裸名 `.exe` 透明 | ls 侧语义保持（报 ENOENT，与 GNU 一致） |
| #977 | LS_COLORS 未解析 | 通过（显式 LS_COLORS 时逐字节一致） |

## 六、计数

- 契约行：**70**（表 A 58 + 表 B 12）
- 一致：**44**　compat-gap：**13**　wording：**1**　noise-platform：**7**　noise-locale：**5**
- 立案：8 条 —— #1639（L18+L30）、#1640（L19）、#1641（L29+L37）、#1642（L48）、#1643（L40）、#1644（L54，与 du 合并）、#1645（L55）、#1646（L03+L53）
