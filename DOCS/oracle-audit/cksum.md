# cksum — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/cksum.c`（`crc_sum_stream`:220、`output_crc`:252）＋ `src/digest.c`（算法表、`-c` 校验器、`-l` 解析）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（C++ applet，`portable_digest` 共享层）。
- 原始输出：`work/out/cksum-final.txt`（32 例；二进制用 `\xNN` 转义展示）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 默认 CRC：`3015617425 6 a.txt` | 同字节 | 一致 | `cksum a.txt` |
| 2 | 多文件逐行，顺序=argv | 同字节 | 一致 | `cksum a.txt b.txt` |
| 3 | 空文件 `4294967295 0 empty.txt` | 同字节 | 一致 | `cksum empty.txt` |
| 4 | stdin 无文件名列 | 同字节 | 一致 | `printf 'hello\n' \| cksum` |
| 5 | `-a crc` 与默认一致 | 同字节 | 一致 | `cksum -a crc a.txt` |
| 6 | `-a md5` 默认 BSD tag：`MD5 (a.txt) = …` | 同字节 | 一致 | `cksum -a md5 a.txt` |
| 7 | `-a sha256` tag 输出 | 同字节 | 一致 | `cksum -a sha256 a.txt` |
| 8 | `-a sha512` 128 hex | 同字节 | 一致 | `cksum -a sha512 a.txt` |
| 9 | `--raw` 输出原始 16 字节 | 逐字节一致（含 0x0A 不转 CRLF） | 一致 | `cksum --raw -a md5 a.txt \| od -An -tx1` |
| 10 | `--base64` | 同字节 | 一致 | `cksum --base64 -a sha256 a.txt` |
| 11 | `--tag` | 同字节 | 一致 | `cksum --tag -a sha256 a.txt` |
| 12 | `-z` 用 NUL 终止 | 同字节 | 一致 | `cksum -z a.txt \| od -An -c` |
| 13 | `-b` 在管道/普通文件上与默认相同 | 同字节 | 一致 | `cksum -b a.txt` |
| 14 | `-t` 默认 tag 下拒绝：`--text mode is only supported with --untagged` + Try | 同字节 | 一致 | `cksum -t a.txt` |
| 15 | `-b -t` 后者生效 → 同 #14 | 同字节 | 一致 | `cksum -b -t a.txt`（#1362 回归） |
| 16 | `-t -b` 后者生效 → 正常 tag 输出 | 同字节 | 一致 | `cksum -t -b a.txt`（#1362 回归） |
| 17 | `-c` 对 BSD/tag 校验文件逐行 `OK/FAILED` | 同字节 | 一致 | `cksum -c -a md5 md5.txt` |
| 18 | `-c` 对 tagged md5 | 同字节 | 一致 | `cksum -c md5tag.txt` |
| 19 | `-c` 文件不存在：`cksum: nosums.txt: No such file or directory`，rc 1 | 同字节 | 一致 | `cksum -c nosums.txt` |
| 20 | `-c` 对 **CRC/untagged-CRC** 行：GNU 9.4 报 `no properly formatted checksum lines found`，rc 1（`digest.c` 校验器不接受 crc 格式） | 接受并输出 `a.txt: OK`，rc 0 | compat-gap | `cksum a.txt > sums.txt; cksum -c sums.txt`（GNU rc1；Win rc0） |
| 21 | `-c --untagged` 对 CRC 行同样拒绝 | 同样接受 OK | compat-gap | `cksum -c --untagged sums.txt` |
| 22 | `--untagged -a md5`（反向格式） | 同字节 | 一致 | `cksum --untagged -a md5 a.txt` |
| 23 | 缺失文件：`cksum: nope.txt: No such file or directory`，rc 1 | 同字节 | 一致 | `cksum nope.txt` |
| 24 | `-a bogus` 的合法参数表（9.4：无 `crc32b`） | 表里多 `crc32b`（见 #25） | wording | `cksum -a bogus a.txt` |
| 25 | 9.4 不识别 `-a crc32b`（`invalid argument 'crc32b'`） | 接受并输出 `CRC32B (a.txt) = 363a3020`，rc 0 | compat-gap | `cksum -a crc32b a.txt` |
| 26 | `--raw --base64` 互斥 + Try | 同字节 | 一致 | `cksum --raw --base64 -a md5 a.txt` |
| 27 | `--length` 非 blake2b：`--length is only supported with --algorithm=blake2b` | 同字节 | 一致 | `cksum -a sha256 --length=8 a.txt` |
| 28 | 非法长度：`invalid length: 'x'` | 只打印 `invalid length`（缺 `: 'x'`） | wording | `cksum --length=x -a blake2b a.txt` |
| 29 | `-a bsd` / `-a sysv` 与 `sum` 等价 | 同字节（`36979 1` / `542 1`） | 一致 | `cksum -a bsd a.txt`、`cksum -a sysv a.txt` |
| 30 | `-c -a crc`：`--check is not supported with --algorithm={bsd,sysv,crc}` | 同字节 | 一致 | `cksum -c -a crc sums.txt` |
| 31 | `--help` | 产品帮助；列出 `-q/-s`（实际拒绝）、`-V`（GNU 无） | wording | `cksum --help` |
| 32 | `--version` / `--badopt` | 版本为产品横幅（预期）；badopt 字节一致 | 一致 | `cksum --version`、`cksum --badopt a.txt` |
| 33 | Ctrl-Z (0x1A) 不截断输入（#1195 回归） | 同 hash（`2049426561 9`） | 一致 | `cksum bin.dat`（含 `\x1a`） |
| 34 | 操作数 `-` 读 stdin | 同字节 | 一致 | `printf 'hello\n' \| cksum -` |
| 35 | GNU 只接受 `--version`，`-V` 非法 | dispatcher 全局把 `-V` 当 version，rc 0 | compat-gap | `cksum -V`（全局根因） |

## 二、差异明细

### 1. `-c` 接受 GNU 9.4 拒绝的 CRC 校验格式（#20/#21）

```
$ printf 'hello\n' > a.txt
$ cksum a.txt > sums.txt          # "3015617425 6 a.txt"
$ cksum -c sums.txt
GNU : cksum: sums.txt: no properly formatted checksum lines found   rc=1
WIN : a.txt: OK                                                     rc=0
```

- 9.4 的 `digest.c` 校验器把 crc/bsd/sysv 排除在 `-c` 外（`-a crc -c` 直接报 `--check is not supported …`），即使 cksum 自己会输出该格式。
- 产品把 `3015617425 6 a.txt` 解析成合法的 CRC 行并校验。
- 这是“过度接受”方向：GNU 脚本中作为错误检测的 `cksum -c` 会被静默当作成功。

### 2. `crc32b` 额外算法（#25）

```
$ cksum -a crc32b a.txt
GNU : cksum: invalid argument 'crc32b' for '--algorithm' …   rc=1
WIN : CRC32B (a.txt) = 363a3020                              rc=0
```

9.4 算法表只有 bsd/sysv/crc/md5/sha1/sha224/sha256/sha384/sha512/blake2b/sm3；`crc32b` 是产品扩展（推测为 9.5 名称预留），需裁定“文档化扩展”或拒绝。

### 3. wording（#24/#28/#31）

- `--length=x` 丢实参：GNU `invalid length: 'x'`。
- 非法 `-a` 的参数表多列 `crc32b`。
- `--help` 列 `-q/-s`，但实际 `cksum -q/-s` 均报 `invalid option`；同时列 `-V`（GNU 无）。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 35 | 28 | 4 | 3 | 0 | 0 | 0 |

> 4 个 gap 行归并为 **2 张 per-command issue**（CRC `-c` 接受面 #1709、crc32b #1710）＋ 1 张全局 `-V` issue（#1714）。`--length`/help 差异记 wording。
