# chmod — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/chmod.c`（`describe_change` chmod.c:148、`process_file`:203、`process_files`:333、`main`:417-571）与 `lib/modechange.c`（`mode_compile`:134、`mode_adjust`:339）。
- Oracle：WSL2 GNU coreutils 9.4（`chmod (GNU coreutils) 9.4`），`LC_ALL=C`、`TZ=UTC`，经 `wsl bash <脚本文件>` 调用。
- 被测：`origin/main = f0cf6b17` 本地 Release 构建 `WinuxCmd 1.2.1`（C++ applet；`WINUXCMD_RUST2_COMMANDS` 不含 chmod）。
- fixture：`f.txt/g.txt` 0644、`x.sh` 0755、`dir/` 0755、`dir/sub/b.txt` 0600、`c.txt` 0640；Windows 侧 DrvFs 不保存 POSIX 位，产品按“0644/0755 + 只读位”合成模式（见 §方法注）。
- 原始输出：`work/out/chmod-final.txt`（35 例，含 stderr/rc 逐字节）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 数字模式 644 无变化时 `-v` 打印 `mode of 'f.txt' retained as 0644 (rw-r--r--)`（chmod.c:196-198） | 同字节 | 一致 | `chmod -v 644 f.txt` |
| 2 | 数字模式变化打印 `changed from 0644 (rw-r--r--) to 0600 (rw-------)`（chmod.c:186-195） | 同字节 | 一致 | `chmod -v 600 f.txt` |
| 3 | 符号 `u+x` 仅加 owner 执行位 | 同字节 | 一致 | `chmod -v u+x f.txt` |
| 4 | 单 clause 多操作 `u+rw-x` 按序应用（#1331） | 同字节 | 一致 | `chmod -v u+rw-x f.txt` |
| 5 | copy clause `g=u` 取 u 的三位复制到 g（modechange.c:134-315） | 同字节 | 一致 | `chmod -v g=u f.txt` → 0664 |
| 6 | 空权限 `a=` 清空全部九位 | 同字节 | 一致 | `chmod -v a= f.txt` → 0000 |
| 7 | `X` 对普通文件不置 x（modechange.c:339 `mode_adjust`） | 同字节 | 一致 | `chmod -v a+X f.txt` retained 0644 |
| 8 | `X` 对目录置 x | 同字节 | 一致 | `chmod -v a+X dir` retained 0755 |
| 9 | 逗号列表 `ug=rw,o=` 依次应用 | 同字节 | 一致 | `chmod -v ug=rw,o= g.txt` → 0660 |
| 10 | 省略 who 的 `+x` 受 umask 约束（GNU 022 下 → 0755） | 同字节 | 一致 | `chmod -v +x g.txt` |
| 11 | `u+s` 置 setuid，`-v` 显示 `4755 (rwsr-xr-x)`（chmod.c:120-146 二次 stat） | 解析通过但 s 位丢弃：`retained as 0644`；不置位 | noise-platform | `chmod -v u+s x.sh`：GNU `to 4755 (rwsr-xr-x)`；Win `retained as 0644 (rw-r--r--)` |
| 12 | `+t` 置 sticky，`-v` 显示 `1755 (rwxr-xr-t)` | 丢弃 t 位，retained 0755 | noise-platform | `chmod -v +t dir` |
| 13 | 前导 0 的 `0755` 等价 755 | 同字节 | 一致 | `chmod -v 0755 f.txt` |
| 14 | 四位数字模式 `4755` 保留 setuid 高位的解析与显示 | 解析/显示 `4755 (rwxr-xr-x)`，但旧模式按合成 0644、且不置 s 位 | noise-platform | `chmod -v 4755 x.sh`（GNU old 0755→4755 rwsr-xr-x） |
| 15 | 非法数字 `999`：`invalid mode: '999'` + Try 行，rc 1 | 同字节 | 一致 | `chmod 999 f.txt` |
| 16 | 非法符号 `z+x`：`invalid mode: 'z+x'` + Try 行，rc 1 | 同字节 | 一致 | `chmod z+x f.txt` |
| 17 | 只有文件没有模式：`missing operand after 'f.txt'`（chmod.c:520-535） | 同字节 | 一致 | `chmod f.txt` |
| 18 | 不存在文件：`cannot access 'nope.txt': No such file or directory`，rc 1（chmod.c:203-290） | 同字节 | 一致 | `chmod 644 nope.txt` |
| 19 | `-c` 仅在变化时输出 changed 行 | 同字节 | 一致 | `chmod -c 600 f.txt` |
| 20 | `-c` 无变化时完全静默 | 同字节 | 一致 | `chmod -c 644 f.txt`（stdout/stderr 空） |
| 21 | `-Rv` 按 fts 前序遍历，路径用 `/` 连接（chmod.c:333-365） | 遍历集合一致但顺序为 Windows 枚举序、分隔符为 `\` | wording | `chmod -Rv 600 dir`：GNU `dir/a.txt`…；Win `dir\a.txt`…（见 §二.1） |
| 22 | `--reference=RFILE` 复制 RFILE 模式 | 一致（合成模式下 644→644） | 一致 | `chmod --reference f.txt g.txt`；probe `stat -c %a g.txt` 均 644 |
| 23 | `--reference` 目标不存在：`failed to get attributes of 'nope': No such file or directory` | 同字节 | 一致 | `chmod --reference nope f.txt` |
| 24 | `-f` 抑制诊断但 rc 仍 1 | 同字节（无输出，rc 1） | 一致 | `chmod -f 644 nope.txt` |
| 25 | `--` 后 `644` 作为模式 | 同字节 | 一致 | `chmod -- 644 f.txt` |
| 26 | 非递归 `--preserve-root` 不影响普通文件 | 同字节 | 一致 | `chmod --preserve-root 644 f.txt` |
| 27 | `--no-preserve-root` 接受 | 同字节 | 一致 | `chmod --no-preserve-root 644 f.txt` |
| 28 | `-R` 目标不存在：`cannot access 'nope': No such file or directory` | rc 1 相同但原因错成 `Input/output error` | wording | `chmod -R 644 nope`：GNU `…No such file or directory`；Win `…Input/output error` |
| 29 | 多操作数 `-v` 逐行输出，顺序=argv（chmod.c:556-570） | 同字节 | 一致 | `chmod -v 755 f.txt g.txt` |
| 30 | 空模式 `''`：`invalid mode: ''`，rc 1 | 同字节 | 一致 | `chmod '' f.txt` |
| 31 | 非便携模式即选项 `-w` 被 getopt 当模式（chmod.c:445-475） | 同字节（rc 0） | 一致 | `chmod -w f.txt` |
| 32 | `+x` 作为模式选项 | 同字节（rc 0） | 一致 | `chmod +x f.txt` |
| 33 | `-rwx` 作为模式选项 | 同字节（rc 0） | 一致 | `chmod -rwx f.txt` |
| 34 | 单独 `=` 合法（清空） | 同字节（rc 0） | 一致 | `chmod = f.txt` |
| 35 | 选项可置于操作数之后（getopt 置换） | 同字节 | 一致 | `chmod 644 -v f.txt` |
| 36 | 模式 > 07777 的 `10000` 非法 | 同字节 | 一致 | `chmod 10000 f.txt` |

## 二、差异明细（字节级）

### 1. 递归 `-v` 的路径分隔符与顺序（#21，wording）

```
$ chmod -Rv 600 dir
GNU                                WinuxCmd
mode of 'dir' ...                  mode of 'dir' ...
mode of 'dir/a.txt' ...            mode of 'dir\a.txt' ...
mode of 'dir/sub' ...              mode of 'dir\sub' ...
mode of 'dir/sub/c.txt' ...        mode of 'dir\sub\b.txt' ...
mode of 'dir/sub/b.txt' ...        mode of 'dir\sub\c.txt' ...
```

- 集合一致；差异仅分隔符（`/` vs `\`）与同层枚举顺序（GNU readdir 序 vs Windows 枚举序）。
- 旧模式列还差 0640（`c.txt`）→ 产品按 0644 合成，属 Windows 无 POSIX 位（noise-platform，合并计在 #11/#14 根因）。

### 2. `-R` 访问失败的原因文本（#28，wording）

```
$ chmod -R 644 nope
GNU : chmod: cannot access 'nope': No such file or directory
WIN : chmod: cannot access 'nope': Input/output error   (rc 均为 1)
```

根因：递归入口的 Win32 错误码到 errno 文本映射未覆盖 `ERROR_FILE_NOT_FOUND/PATH_NOT_FOUND`（非递归入口已覆盖，见 chmod.cpp 的 `format_missing_reference_error`）。

## 三、方法与安全边界

- 未执行任何真实根递归：`--preserve-root` 只在普通文件与相对目录上探测；源码核对确认 GNU 9.4 `preserve_root` 默认 false（chmod.c:423、486-489），产品同默认，不存在“默认保护缺失”。
- Windows 上没有 POSIX 权限位：fixture 在 DrvFs 上读回统一 0777，产品按 `FILE_ATTRIBUTE_READONLY`+扩展名合成 0644/0755 用于 `-v/-c` 文本。故 #11/#12/#14 与 0640→0644 一律按平台噪声计，不立案。
- `u+s/+t` 的实际副作用在 Windows 无对应物；本表只裁“解析+报告面”。

## 四、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 36 | 31 | 0 | 2 | 3 | 0 | 0 |

> 本命令无新开 issue；wording 两条（路径分隔符/顺序、EIO 原因）记入本表与 README，作为 2.0 修复批的低优先项。
