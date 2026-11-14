# join — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/join.c`（`extract_field`:259、`xfields`:272、`keycmp`:327、`check_order`:395、`prfields`:566、`prjoin`:587、`join`:658、`add_field_list`:891、`add_file_name`:944、`main`:990-1186）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（**rust2 已接线**：`join.rs`，`WINUXCMD_RUST2_COMMANDS` 含 join）。
- 原始输出：`work/out/join-final.txt`（30 例）＋ §二 的手工 NUL/反斜杠探针。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 默认按字段 1 内连接，空白分隔 | 同字节 | 一致 | `join j1.txt j2.txt` |
| 2 | `-a1` 保留文件 1 未配对行 | 同字节 | 一致 | `join -a1 j1.txt j2.txt` |
| 3 | `-a2` 保留文件 2 未配对行 | 同字节 | 一致 | `join -a2 j1.txt j2.txt` |
| 4 | `-a1 -a2` 两侧都保留 | 同字节 | 一致 | `join -a1 -a2 j1.txt j2.txt` |
| 5 | `-v1` 只输出文件 1 未配对 | 同字节 | 一致 | `join -v1 j1.txt j2.txt` |
| 6 | `-v2` 只输出文件 2 未配对 | 同字节 | 一致 | `join -v2 j1.txt j2.txt` |
| 7 | `-1 2` 用文件 1 第 2 字段 | 同字节（空结果） | 一致 | `join -1 2 m1.txt m2.txt` |
| 8 | `-2 2` 用文件 2 第 2 字段 | 同字节（空） | 一致 | `join -2 2 m1.txt m2.txt` |
| 9 | `-j 2` 两侧都用第 2 字段 | 同字节（空） | 一致 | `join -j 2 m1.txt m2.txt` |
| 10 | `-o 1.2,2.2` 字段投影 | 同字节 | 一致 | `join -o '1.2,2.2' j1.txt j2.txt` |
| 11 | `-o 0,...` 0=连接键 | 同字节 | 一致 | `join -o '0,1.2,2.2' j1.txt j2.txt` |
| 12 | `-t,` 自定义分隔符 | 同字节 | 一致 | `join -t, c1.csv c2.csv` |
| 13 | `-i` 忽略大小写 | 同字节 | 一致 | `join -i -1 1 j1.txt j2.txt` |
| 14 | `-e X` 填充缺失字段 | 同字节 | 一致 | `join -a1 -e X -o '1.2,2.2' j1.txt j2.txt` |
| 15 | `-z` NUL 输入/输出（NUL 分隔文件） | 逐字节一致（`1 alice A red\0 3 carol C green\0`） | 一致 | `printf '1 alice A\0…' > n1; join -z n1 n2 \| od -An -c` |
| 16 | `-z` 下换行 **不是** 分隔符：整份换行文件是单条记录 | 仍按换行切记录，只把输出终止符变 NUL | compat-gap | `join -z j1.txt j2.txt \| od -An -c`：GNU 单条 `1 alice A 2 bob B 3 carol C  red\0`；Win `1 alice A red\0 2 bob B\0…` |
| 17 | `--header` 首行不参与配对、先原样输出 | 同字节 | 一致 | `join --header h1.txt h2.txt` |
| 18 | `--check-order` 乱序：`join: u1.txt:2: is not sorted: 1 a`，rc 1（join.c:395-430） | 同字节 | 一致 | `join --check-order u1.txt u2.txt` |
| 19 | `--nocheck-order` 不检查 | 同字节 | 一致 | `join --nocheck-order u1.txt u2.txt` |
| 20 | 无操作数：`missing operand` + Try | 同字节 | 一致 | `join` |
| 21 | 一个操作数：`missing operand after 'j1.txt'` + Try（join.c:1147-1150） | 同字节 | 一致 | `join j1.txt` |
| 22 | 缺失文件：`join: nope.txt: No such file or directory`，rc 1 | 同字节 | 一致 | `join j1.txt nope.txt` |
| 23 | FILE1 `-` 读 stdin | 同字节 | 一致 | `join - j2.txt`（stdin j1 内容） |
| 24 | 两空文件 → 空输出 rc0；一空一非空 → 空输出 rc0 | 同字节 | 一致 | `join e1.txt e2.txt`、`join e1.txt j2.txt` |
| 25 | `-o 1.9` 越界字段 → 该字段为空（保留前置分隔空格） | 同字节（` red` / ` green`） | 一致 | `join -o '1.9,2.2' j1.txt j2.txt` |
| 26 | `-o auto` 按首行字段数 | 同字节 | 一致 | `join -o auto j1.txt j2.txt` |
| 27 | `-k`/`--empty` 非 GNU 选项被拒绝（#1070/#1411 回归） | 同字节（`invalid option -- 'k'` / `unrecognized option '--empty'`） | 一致 | `join -k 1 j1.txt j2.txt` |
| 28 | 行尾反斜杠是普通数据（#1432 面） | 同字节（`1 a\ x`） | 一致 | 输入 `1 a\` + `1 x`；`join e1.txt e2.txt \| od -An -c` |
| 29 | 文本模式 0x1A 不截断（#1212 回归） | 同字节 | 一致 | `join cz1.txt cz2.txt \| od -An -c` |
| 30 | `--help` | 产品自有帮助 | wording | `join --help` |
| 31 | `--version` / 未知选项 | 产品横幅（预期）；badopt 字节一致 | 一致 | `join --version`、`join --badopt j1.txt j2.txt` |
| 32 | GNU 只接受 `--version`，`-V` 非法 | dispatcher 全局把 `-V` 当 version，rc 0 | compat-gap | `join -V`（全局根因） |

## 二、差异明细

### 1. `-z` 的记录切分（#16，残差 #1008）

```
# 输入仍是换行文件
$ join -z j1.txt j2.txt | od -An -c
GNU : 1 alice A 2 bob B 3 carol C   r e d \0        # 整份文件 = 一条记录
WIN : 1 alice A r e d \0 2 bob B \0 3 …             # 仍按 \n 切
```

- GNU 在 `-z` 下把记录分隔符切到 `\0`，换行成为普通字节；产品同时接受 `\n` 与 `\0`，因此换行文件仍按行 join。
- 正确 NUL 输入（两侧都一致）已通过；差异只在“换行文件 + -z”这一错误用法，低危。

### 2. `-V`（#32）

全局 dispatcher 行为，per-command 不开单。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 29 | 2 | 1 | 0 | 0 | 0 |

> 2 个 gap：`-z` 换行切分（#1713）、`-V`（全局 #1714）。
