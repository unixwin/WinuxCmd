# chown — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/chown.c`（`main`:155-330）＋ `lib/userspec.c`（`parse_user_spec`）＋ `lib/chown.c`。
- Oracle：WSL2 GNU coreutils 9.4（root 身份），`LC_ALL=C`、`TZ=UTC`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（C++ applet，Windows ACL/SID 模型）。
- 身份对照：GNU 侧当前用户/组为 `root:root`；Windows 侧为 `cmx:None`。涉及身份的行用 `gnu_args` 把 oracle 换成 `root`，产品侧用 `cmx`（见 `specs/chown.json` 的 `gnu_args/win_args`；机器相关，复现时换 `$USERNAME`）。
- **安全边界**：未做任何真实根递归（`-R /`）；只对临时树 `tpl/` 递归。
- 原始输出：`work/out/chown-final.txt`（38 例）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 无操作数：`missing operand` + Try，rc 1（chown.c:1140-1160） | 同字节 | 一致 | `chown` |
| 2 | 有 spec 无文件：`missing operand after 'cmx'` + Try（chown.c:1147-1150） | 同字节 | 一致 | `chown cmx`（GNU `root`） |
| 3 | 非法用户先于操作数检查报 `invalid user` | 产品同样先报，但 Windows 无 `root` 账户 → 与 oracle 身份不可比 | noise-platform | `chown root f.txt`：GNU rc0；Win `invalid user: 'root'` |
| 4 | 非法用户：仅一行 `invalid user: 'nosuchuser__'`（userspec.c） | 多打印 `Try 'chown --help' …` | wording | `chown nosuchuser__ f.txt` |
| 5 | `--from=nosuch` 失配：`invalid user: 'nosuch'`，无 Try 行 | 多 Try 行 | wording | `chown --from=nosuch root f.txt` |
| 6 | 合法 owner + 非法 group：`invalid group: 'root:nosuchgroup__'` | 同措辞 + 多 Try 行 | wording | `chown cmx:nosuchgroup__ f.txt` |
| 7 | 非法 owner 时不报 group 名 | 产品先报 `invalid user: 'root:nosuchgroup__'`（root 在 Windows 不存在） | noise-platform | `chown root:nosuchgroup__ f.txt` |
| 8 | `owner:`（空 group）表示 owner 不变、group 设为登录组，rc 0 | rc 1 `cannot access 'f.txt': Operation not permitted` | compat-gap | `chown cmx: f.txt`（GNU `chown root: f` rc0） |
| 9 | 仅 `:` 表示 owner/group 均不变，rc 0 | rc 1 `cannot access 'f.txt': Operation not permitted` | compat-gap | `chown : f.txt` |
| 10 | `-v` 的 `ownership of 'f' retained as root` 写 **stdout** | 写 **stderr**，且格式为 `cmx:None` | compat-gap | `chown -v cmx f.txt 1>o 2>e`：GNU o 非空/e 空；Win o 空/e 非空 |
| 11 | `-c` 无变化时完全静默 | 仍打印 `ownership of 'f.txt' retained as cmx:None`（stderr） | compat-gap | `chown -c cmx f.txt`：GNU 两流均空；Win stderr 有行 |
| 12 | `-R` 逐项属性不变时 rc 0 | 每一项都 `cannot access …: Operation not permitted`，rc 1 | compat-gap | `chown -R cmx dir` |
| 13 | 非递归 `--preserve-root` 对普通文件无影响 | 同字节 | 一致 | `chown --preserve-root cmx f.txt`（GNU 侧 rc0） |
| 14 | `--from=<当前 owner>` 匹配后仍是无变化 → rc 0 | rc 1 Operation not permitted | compat-gap | `chown --from=cmx cmx f.txt` |
| 15 | `--from` 失配只报 invalid user，rc 1 | 多 Try 行 | wording | `chown --from=nosuch cmx f.txt` |
| 16 | `--reference` 同 owner/group → rc 0 | rc 1 `cannot access 'g.txt': Operation not permitted` | compat-gap | `chown --reference f.txt g.txt` |
| 17 | `--reference nope`：`failed to get attributes of 'nope': No such file or directory` | 同措辞 + 多 Try 行 | wording | `chown --reference nope f.txt` |
| 18 | `-h` 作用于链接自身；同 owner 时 rc 0 | rc 1 Operation not permitted | compat-gap | `chown -h cmx lnk` |
| 19 | 悬空链接（默认解引用）：`cannot dereference 'dangling': No such file or directory`，rc 1 | `cannot access 'dangling': 系统无法访问此文件。`（系统语言泄漏） | wording + noise-locale | `chown cmx dangling` |
| 20 | 数字 uid `0` 解析为 root | 解析成功但无权限设置 → rc 1 | noise-platform | `chown 0 f.txt` |
| 21 | `0:0` 同上 | 同上 | noise-platform | `chown 0:0 f.txt` |
| 22 | 旧式 `.` 分隔：warning 带完整 spec `'.' should be ':': 'root.root'` | 警告不带 spec，且随后把整串当用户名 | wording | `chown root.root f.txt`（GNU 警告后 rc0） |
| 23 | 多个冒号 `a:b:c`：`invalid user: 'a:b:c'`，无 Try 行 | 多 Try 行 | wording | `chown a:b:c f.txt` |
| 24 | 只有 `:` 无文件：`missing operand after ':'` | 同字节 | 一致 | `chown :` |
| 25 | 未知选项：`unrecognized option '--badopt'` + Try | 同字节 | 一致 | `chown --badopt root f.txt` |
| 26 | `-R` 目标不存在：`cannot access 'nope': No such file or directory` | 同字节（rc 1） | 一致 | `chown -R cmx nope` |
| 27 | `-f` 抑制诊断但 rc 1 | 同字节 | 一致 | `chown -f cmx nope` |
| 28 | 空 user spec `''` 视为“不改”，rc 0 | rc 1 `invalid spec for 'f.txt'` | compat-gap | `chown '' f.txt` |
| 29 | `-v` 无变化只打印 owner（`retained as root`） | 打印 `cmx:None`（owner:group 且 group 名平台化） | wording | 同 #10 |
| 30 | 设置到其他 owner 需特权 | Windows 非管理员一律 EPERM/ACCESS_DENIED，映射为 `Operation not permitted` | noise-platform | `chown Administrator f.txt` |
| 31 | 系统错误文本英文（`strerror`） | 悬空链接路径回退到 `FormatMessageW(LANG_NEUTRAL)`，输出中文本地化串 | noise-locale | 同 #19 |
| 32 | 递归遍历 `-P` 不进入符号链接 | 产品同样跳过 `lnk`（输出仅 dir/a.txt/sub/b.txt） | 一致 | `chown -Rv cmx dir`（GNU stdout，产品 stderr，见 #10） |

## 二、差异明细（字节级）

### A. 无变化的 chown 被当成失败（#8/#9/#12/#14/#16/#18 同根因）

```
$ chown cmx f.txt                 # f.txt 已属 cmx
WIN  rc=1  stderr: chown: cannot access 'f.txt': Operation not permitted
GNU  rc=0  （对 root 自己的文件同样 rc=0；Linux 非特权用户 chown 到自身也 rc=0，
           已用 `su nobody -c 'chown nobody f'` 复核）
```

- 同一根因的其余面：`chown cmx: f`、`chown : f`、`chown --from=cmx cmx f`、`chown --reference f g`、`chown -h cmx lnk`、`chown -R cmx dir`。
- 产品在 `-v` 路径会先比较 SID 并打印 `retained`（rc0），但普通路径直接调用 `SetNamedSecurityInfoW`，把“无需修改”也送进特权失败分支。
- 影响：最常见的 `chown $(whoami) FILE` 在 Windows 上必失败；脚本无法把“已正确”当成功。

### B. `-v`/`-c` 报告通道与条件（#10/#11）

```
$ chown -v cmx f.txt 1>o 2>e
GNU  o: ownership of 'f.txt' retained as root      e: (empty)
WIN  o: (empty)                                     e: chown: ownership of 'f.txt' retained as cmx:None

$ chown -c cmx f.txt
GNU  (stdout/stderr 均空)          # 无变化，-c 不报告
WIN  stderr: ... retained as cmx:None
```

GNU `chown.c:98-130` 的 `describe_change` 走 `printf`（stdout），且 `-c` 仅在 `CH_SUCCEEDED` 时打印；产品把“retained”也纳入 `-c`，并整体走 `safeErrorPrint`。

### C. 空 spec（#28）

```
$ chown '' f.txt
GNU  rc=0（空 owner/group 均表示不改；userspec.c 空串返回 -1/-1）
WIN  rc=1  chown: invalid spec for 'f.txt'
```

### D. wording 族（#4/#5/#6/#15/#17/#19/#22/#23/#29）

- 多打印 `Try 'chown --help' for more information.`：GNU 的 invalid user/group、`--from` 解析、`--reference` 失败都是 `error(EXIT_FAILURE)` 路径，不带 Try；产品一律带。
- `.` 分隔警告缺 spec 原文：GNU `'.' should be ':': 'root.root'`。
- 悬空链接：GNU `cannot dereference` vs 产品 `cannot access`；系统原因句为中文 `系统无法访问此文件。`（`FormatMessageW` 中性语言回退），违反 I18N“稳定 key + 英文 fallback”纪律。
- `-v` 无变化格式：GNU 只列 owner；产品 `owner:group`。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 8 | 9 | 9 | 5 | 1 | 0 |

> 9 个 gap 行归并为 **3 张 issue**（A 无变化失败 #1701、B 报告通道/-c #1702、C 空 spec #1703）；wording/noise 不立案，记入看板。
