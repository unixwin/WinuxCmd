# readlink — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/readlink.c`（`main`:92-177；canonicalize 模式经 `lib/canonicalize.c`）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`、`TZ=UTC`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（**rust2 已接线**：`readlink.rs`，`WINUXCMD_RUST2_COMMANDS` 含 readlink）。
- 通道说明：fixture 由 WSL `ln -s` 在 DrvFs 上创建；Windows 10 19045 只把它识别为 `ReparsePoint` 而无 `LinkType`（WSL 私有 tag），因此 **默认模式读不到目标**。这不代表 NTFS 符号链接面；另用 `mklink /J` 建 junction 复核（见 §二.2），默认/`-f`/`-e`/`-z`/多操作数全部正确。
- 原始输出：`work/out/readlink-final.txt`（28 例）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 无操作数：`missing operand` + Try，rc 1（readlink.c:137-141） | 同字节 | 一致 | `readlink` |
| 2 | 默认模式打印链接值 + `\n`；`-n` 去掉终止符（readlink.c:158-166） | LX 私有 reparse 链接 rc 1/无输出；junction 正确 | noise-platform | `readlink l_rel`：GNU `target.txt` rc0；Win 空 rc1（见 §二.1） |
| 3 | `-f/-e/-m` 逐组件解析链接（canonicalize.c） | LX 链接不解析，返回链接自身路径；junction 正确 | noise-platform | `readlink -f l_rel`：GNU `…/target.txt`；Win `…/l_rel` |
| 4 | `-f` 悬空链接：仅最后一级可不存在（CAN_ALL_BUT_LAST） | 返回链接路径，不展开 | noise-platform | `readlink -f l_dangling`：GNU `…/nope`；Win `…/l_dangling` |
| 5 | `-e` 悬空链接：rc 1、静默（CAN_EXISTING） | 同 rc 1、无输出 | 一致 | `readlink -e l_dangling` |
| 6 | `-m` 悬空链接：返回规范化目标（CAN_MISSING） | 返回链接路径 | noise-platform | `readlink -m l_dangling` |
| 7 | `-f/-e` 目录链接解析到目录 | LX 链接不解析（rc 0/1 均见过）；junction 正确 | noise-platform | `readlink -f l_dir`、`readlink -e l_dir` |
| 8 | `-f` 普通目录：绝对规范化路径 | 同语义，仅路径前缀/分隔符不同 | 一致 | `readlink -f dir` |
| 9 | `-f` 普通文件：绝对规范化路径 | 同语义（前缀 `/tmp` vs `D:/`） | 一致 | `readlink -f target.txt` |
| 10 | `-f` 不存在路径：绝对化但不要求存在 | 同语义 | 一致 | `readlink -f nope` |
| 11 | `-e` 不存在：rc 1 静默 | 同字节 | 一致 | `readlink -e nope` |
| 12 | `-m` 不存在：绝对化 | 同语义 | 一致 | `readlink -m nope` |
| 13 | 多操作数逐个输出（readlink.c:146-156） | LX 链接全部失败（rc 1）；junction 上正确 | noise-platform | `readlink l_rel l_dir` |
| 14 | 多操作数含失败：先输出的成功项仍打印，最终 rc 1 | LX 场景无成功项；逻辑分支存在 | noise-platform | `readlink l_rel nope`：GNU `target.txt` + rc1；Win 空 + rc1 |
| 15 | `-q`/`-s` 静默（默认）、`-v` 报告；最后出现者生效（readlink.c:124-132） | 同语义；`-v nope` 字节一致 | 一致 | `readlink -q nope`、`readlink -v nope` |
| 16 | 多操作数 + `-n`：warning + 恢复普通终止符（readlink.c:148-153） | 同 warning/行为（junction 复核） | 一致 | `readlink -n junc junc`；stderr 均 `readlink: ignoring --no-newline with multiple arguments` |
| 17 | `-z`：NUL 终止（readlink.c:162-165） | junction 上同为 `target \0` | 一致 | `readlink -z junc \| od -An -c` |
| 18 | 操作数 `-` 是普通文件名（无 stdin 特判） | 同语义（`-f -` 输出 `…/-`） | 一致 | `readlink -f -` |
| 19 | 绝对目标链接：打印原值 | LX 链接不可读 | noise-platform | `readlink l_abs`：GNU `/tmp`；Win 空 rc1 |
| 20 | 相对上级目标：打印 `../target.txt` | LX 链接不可读 | noise-platform | `readlink dir/l_up` |
| 21 | `--help` 输出 | 产品自有帮助（含 Windows 说明、`-V`） | wording | `readlink --help` |
| 22 | `--version` 输出版本横幅 | 产品品牌横幅（预期差异） | 一致 | `readlink --version` |
| 23 | 未知选项：`unrecognized option '--badopt'` + Try | 同字节 | 一致 | `readlink --badopt l_rel` |
| 24 | GNU 只接受 `--version`，**不接受 `-V`**：`invalid option -- 'V'`，rc 1 | dispatcher 全局把 `-V` 拦截为 version，rc 0 | compat-gap | `readlink -V`：GNU rc1；Win 版本横幅 rc0（全局根因，见 README） |
| 25 | Windows 方言：`/d/`、`/cygdrive/d/`、`/mnt/d/`、反斜杠路径折叠到 `D:/`（#1491/#1231/#1246 回归） | 四种拼写均折叠为 `D:/repo/...`，rc 0 | 一致 | `readlink -f /d/repo/wc-auditnext/DOCS` 等 4 组 |
| 26 | junction/目录链接的默认模式与 `-e`（Windows 平台面） | `mklink /J` junction：默认/`-f`/`-e` 均 rc0 且解析到目标 | 一致 | `readlink junc` → `D:\…\dir`；`readlink -e junc` rc0 |

## 二、差异明细

### 1. 审计通道伪影：WSL 私有 reparse 链接

```
# WSL 创建（fixture）               # Windows 观察
ln -sf target.txt l_rel             Get-Item l_rel → Attributes=Archive,ReparsePoint;
                                    LinkType 为空；cmd dir /AL 不列出
WIN : readlink l_rel        -> (空) rc=1
GNU : readlink l_rel        -> target.txt rc=0
```

Windows 自身（`cmd`/`dir`/`Get-Item`）也不把这个对象当作可解析链接，属 fixture 通道噪声。审计结论以 NTFS junction 为准。

### 2. junction 复核（真实 Windows reparse）

```
mklink /J symtest\junc symtest\dir
WIN : readlink junc        -> D:\…\symtest\dir      rc 0
WIN : readlink -f junc     -> D:/…/symtest/dir      rc 0
WIN : readlink -e junc     -> D:/…/symtest/dir      rc 0
WIN : readlink -z junc     -> D:\…\dir\0
GNU : readlink junc        -> /mnt/d/…/symtest/dir  rc 0
```

值的前缀/分隔符随平台（`D:\` vs `/mnt/d/`）不同，语义一致；不计 gap。

### 3. `-V`（#24）

```
$ readlink -V
GNU : readlink: invalid option -- 'V'
      Try 'readlink --help' for more information.   rc=1
WIN : readlink (WinuxCmd) 1.2.1 …                    rc=0
```

根因在 dispatcher：`src/core/dispatcher.cppm:1620-1635` 对未声明 `-V` 的所有 applet 统一按 version 事件拦截；一张跨命令 issue 处理（README §立案）。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 26 | 15 | 1 | 1 | 9 | 0 | 0 |

> 9 条 noise-platform 中 8 条同根（WSL 私有 reparse 链接不可读）；junction 面全部一致。无新增 per-command issue；`-V` 计入全局 issue #1714。
