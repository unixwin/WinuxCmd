# uniq — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/uniq.c`（`find_field`:255、`different`:281、`writeline`:301、`check_file`:321-478、`main`:479-665）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（C++ applet）。
- 原始输出：`work/out/uniq-final.txt`（30 例，NUL/CRLF 用 `od` 或原样字节比较）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | stdin 相邻去重 | 同字节 | 一致 | `printf 'a\na\nb\n' \| uniq` |
| 2 | FILE 操作数 | 同字节 | 一致 | `uniq u.txt` → a/b/c |
| 3 | INPUT OUTPUT 双操作数 | 内容一致（6 字节） | 一致 | `uniq u.txt out2.txt` |
| 4 | `-c` 计数列宽 7 右对齐 | 同字节 | 一致 | `uniq -c u.txt` → `      2 a` |
| 5 | `-d` 只输出重复组首行 | 同字节 | 一致 | `uniq -d u.txt` |
| 6 | `-u` 只输出唯一行 | 同字节 | 一致 | `uniq -u u.txt` |
| 7 | `-D` 输出全部重复行 | 同字节 | 一致 | `uniq -D u.txt` |
| 8 | `--all-repeated=separate` 组间空行 | 同字节 | 一致 | `uniq --all-repeated=separate u.txt` |
| 9 | `--all-repeated=prepend` 首组前也空行 | 同字节 | 一致 | `uniq --all-repeated=prepend u.txt` |
| 10 | `-f 1` 跳过 1 个字段（find_field） | 同字节 | 一致 | `uniq -f 1 w.txt` → 仅 xaa1 |
| 11 | `-s 1` 跳过 1 字符 | 同字节 | 一致 | `uniq -s 1 w.txt` → 三行全出 |
| 12 | `-w 3` 只比较前 3 字节 | 同字节 | 一致 | `uniq -w 3 w.txt` → xaa1/ybb3 |
| 13 | `-w 0` 比较 0 字节 → 全部相等 | 同字节 | 一致 | `uniq -w 0 w.txt` → xaa1 |
| 14 | `-w -1`：`-1: invalid number of bytes to compare`（size_opt:233） | 同字节（#1341 回归） | 一致 | `uniq -w -1 w.txt` |
| 15 | `-i` 大小写不敏感 | 同字节（Aa/bb） | 一致 | `uniq -i case.txt` |
| 16 | `-z` NUL 记录 | 同字节（NUL 分隔原样） | 一致 | `uniq -z nul.txt \| od -An -c` |
| 17 | `--group`（默认 separate） | 同字节 | 一致 | `uniq --group u.txt` |
| 18 | `--group=prepend` | 同字节 | 一致 | `uniq --group=prepend u.txt` |
| 19 | `--group=append` | 同字节 | 一致 | `uniq --group=append u.txt` |
| 20 | `--group=both` 只 1 个空行（#1010 回归） | 同字节 | 一致 | `uniq --group=both u.txt` |
| 21 | `--group=separate` | 同字节 | 一致 | `uniq --group=separate u.txt` |
| 22 | 同文件 IN/OUT：GNU 9.4 截断为 0 字节、rc 0 | 同行为（0 字节、rc 0） | 一致 | `uniq u.txt u.txt` |
| 23 | 缺失文件：`uniq: nope.txt: No such file or directory`，rc 1 | 同字节 | 一致 | `uniq nope.txt` |
| 24 | `-c -d` 只计数重复组 | 同字节 | 一致 | `uniq -c -d u.txt` |
| 25 | `-f -1`：`-1: invalid number of fields to skip` | 同字节 | 一致 | `uniq -f -1 w.txt` |
| 26 | CRLF 输入按整行（含 `\r`）比较与输出（#1354 回归） | 同字节 | 一致 | `uniq crlf.txt \| od -An -c` |
| 27 | OUTPUT 为目录：`uniq: dir: Is a directory`，rc 1 | `uniq: cannot open output file`（缺文件名与原因） | wording | `uniq u.txt dir` |
| 28 | `-Dc` 冲突：`printing all duplicated lines and repeat counts is meaningless` **+ Try 行**，rc 1 | 首行相同，缺 Try 行 | wording | `uniq -Dc u.txt`、`uniq -c -D u.txt` |
| 29 | `--help` | 产品自有帮助 | wording | `uniq --help` |
| 30 | `--version` / 未知选项 | 产品横幅（预期）；badopt 字节一致 | 一致 | `uniq --version`、`uniq --badopt u.txt` |
| 31 | GNU 只接受 `--version`，`-V` 非法 | dispatcher 全局把 `-V` 当 version，rc 0 | compat-gap | `uniq -V`（全局根因） |
| 32 | 计数超 UINT64_MAX 的钳位（#1523） | 未构造出可复现输入（需要 2^64 行） | 未覆盖 | — |

## 二、差异明细

### 1. 输出目录诊断（#27，wording）

```
$ uniq u.txt dir
GNU : uniq: dir: Is a directory
WIN : uniq: cannot open output file
```

产品在打开输出失败时丢弃了 `dir` 与 errno；GNU 用 `error(0, errno, "%s", quotef(outfile))`。

### 2. `-Dc` 冲突缺 Try（#28，wording）

```
$ uniq -Dc u.txt
GNU : uniq: printing all duplicated lines and repeat counts is meaningless
      Try 'uniq --help' for more information.       rc=1
WIN : uniq: printing all duplicated lines and repeat counts is meaningless
                                                     rc=1
```

### 3. `-V`（#31）

同 readlink/cksum，全局 dispatcher 行为；per-command 不开单。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 27 | 1 | 3 | 0 | 0 | 1 |

> 无 per-command 新 issue；`-V` 归全局 issue，#1523 保持未覆盖（需要不可能构造的输入）。
