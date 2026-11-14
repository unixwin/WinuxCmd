# comm — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/comm.c`（`writeline`:172、`check_order`:216、`compare_files`:256、`main`:425-512）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（C++ applet）。
- 原始输出：`work/out/comm-final.txt`（24 例）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 三列输出：列 1 无前缀、列 2 `\t`、列 3 `\t\t` | 同字节 | 一致 | `comm a.txt b.txt` |
| 2 | `-1` 抑制列 1 | 同字节 | 一致 | `comm -1 a.txt b.txt` |
| 3 | `-2` 抑制列 2 | 同字节 | 一致 | `comm -2 a.txt b.txt` |
| 4 | `-3` 只留共同行 | 同字节 | 一致 | `comm -3 a.txt b.txt` |
| 5 | `-12` 组合短选项 | 同字节 | 一致 | `comm -12 a.txt b.txt` |
| 6 | `-123` 全抑制 → 空输出 rc0 | 同字节 | 一致 | `comm -123 a.txt b.txt` |
| 7 | `--output-delimiter=STR` 替换列分隔（连续两列→重复） | 同字节 | 一致 | `comm --output-delimiter=\| a.txt b.txt` |
| 8 | `--total` 末行 `N1\tN2\tN3\ttotal` | 同字节（`2\t2\t1\ttotal`） | 一致 | `comm --total a.txt b.txt` |
| 9 | `--check-order` 乱序：先输出到断点，再 `file N is not in sorted order`，rc 1（comm.c:216-253） | 同字节（#1009 回归） | 一致 | `comm --check-order u1.txt u2.txt` |
| 10 | `--nocheck-order` 不做顺序检查 | 同字节 | 一致 | `comm --nocheck-order u1.txt u2.txt` |
| 11 | FILE1 为 `-` 读 stdin（仅一个可为 `-`） | 同字节 | 一致 | `comm - b.txt`（stdin a/c/e） |
| 12 | 两个操作数都是 `-`：GNU 9.4 读第二遍 stdin 失败，`comm: -: Bad file descriptor`，rc 1 | 按同一输入流处理，rc 0、无诊断 | compat-gap | `printf 'a\nb\n' \| comm - -`：GNU 输出 a/`\tb` + stderr + rc1；Win 输出 a/`\tb` rc0 |
| 13 | 无操作数：`missing operand` + Try | 同字节 | 一致 | `comm` |
| 14 | 一个操作数：`missing operand after 'a.txt'` + Try（comm.c:499-503） | 同字节 | 一致 | `comm a.txt` |
| 15 | 缺失文件：`comm: nope.txt: No such file or directory`，rc 1 | 同字节 | 一致 | `comm a.txt nope.txt` |
| 16 | CRLF 输入按整行比较（#1354 回归） | 同字节（共同行 `a\r`、`b\r`、`c\r` 列位一致） | 一致 | `comm crlf1.txt crlf2.txt \| od -An -c` |
| 17 | 两个空文件 → 空输出 rc0 | 同字节 | 一致 | `comm e1.txt e2.txt` |
| 18 | 同文件两次：全部落共同列 | 同字节 | 一致 | `comm a.txt a.txt` |
| 19 | 未知选项 + Try | 同字节 | 一致 | `comm --badopt a.txt b.txt` |
| 20 | `--help` | 产品自有帮助 | wording | `comm --help` |
| 21 | `--version` | 产品横幅（预期） | 一致 | `comm --version` |
| 22 | `--output-delimiter=`（空串） | 同字节（列分隔消失） | 一致 | `comm --output-delimiter= a.txt b.txt \| od -An -c` |
| 23 | `--total` 与 `-3` 同用，total 仍统计全部三列 | 同字节 | 一致 | `comm --total -3 a.txt b.txt` |
| 24 | `--` 结束选项 | 同字节 | 一致 | `comm -- a.txt b.txt` |
| 25 | `-z` NUL 记录 | 同字节（`a\0\tb\0\t\tc\0…`） | 一致 | `printf 'a\0c\0e\0' > z1; …; comm -z z1 z2 \| od -An -c` |
| 26 | 文本模式 0x1A 不截断（#1212 回归） | 同字节 | 一致 | `comm t1.txt t2.txt \| od -An -c`（含 `\032`） |
| 27 | GNU 只接受 `--version`，`-V` 非法 | dispatcher 全局把 `-V` 当 version，rc 0 | compat-gap | `comm -V`（全局根因） |

## 二、差异明细

### 1. `comm - -`（#12，回归 #429 的剩余面）

```
$ printf 'a\nb\n' | comm - -
GNU : stdout: a\n\tb\n
      stderr: comm: -: Bad file descriptor
      rc=1
WIN : stdout: a\n\tb\n
      rc=0
```

- GNU 对两个 `-` 逐一 `fdopen`/读取，第二次 stdin 已在 EOF/关闭态，报 EBADF。
- 产品把两个 `-` 都绑定到同一 stdin 读取器，静默成功。
- #429 已验证“不再死锁”，但退出码/诊断仍与 GNU 不同；按 2.0 兼容口径立案（低危）。

### 2. `-V`（#27）

同 readlink/cksum/uniq：`src/core/dispatcher.cppm:1620-1635` 的全局拦截。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 27 | 24 | 2 | 1 | 0 | 0 | 0 |

> 2 个 gap 行：`comm - -`（#1712）、`-V`（全局 #1714）。
