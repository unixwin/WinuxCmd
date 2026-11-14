# split — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/split.c`（`set_suffix_length`:164、`next_file_name`:362、`create`:472、`bytes_split`:649、`lines_split`:726、`line_bytes_split`:773、`lines_chunk_split`:882、`bytes_chunk_extract`:1008、`lines_rr`:1156、`strtoint_die`:1301、`parse_n_units`:1317、`main`:1347-1699）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（C++ applet）。
- 副作用比较：每例对 **输出文件集合/大小/SHA-256/链接组** 做快照；mtime 1s 级差异按噪声剔除。
- 原始输出：`work/out/split-final.txt`（30 例）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 默认每 1000 行、后缀 xaa/xab…（split.c:1347-1400） | 文件名/内容/大小一致 | 一致 | `split many.txt` → xaa 5000 / xab 5000 / xac 2500 |
| 2 | `-l 3` 每片 3 行 | 同字节 | 一致 | `split -l 3 lines.txt` → xaa..xad（21/21/21/7） |
| 3 | `-l 0`：`invalid number of lines: '0'`，rc 1（split.c:1578） | 同字节 | 一致 | `split -l 0 lines.txt` |
| 4 | `-b 30` 每片 30 字节，尾片余量 | 同字节 | 一致 | `split -b 30 bin.dat` → 30/30/30/10 |
| 5 | `-b 1K` 二进制后缀 | 同字节 | 一致 | `split -b 1K many.txt` → 12×1024+212 |
| 6 | `-C 20` 行内字节上限（line_bytes_split） | 同字节 | 一致 | `split -C 20 lines.txt` → 5×14 |
| 7 | `-n 3` 固定块数，余数前倾（lines_chunk_split） | 同字节 | 一致 | `split -n 3 lines.txt` → 24/23/23 |
| 8 | `-n l/3` 按行均分 | 同字节 | 一致 | `split -n l/3 lines.txt` → 28/21/21 |
| 9 | `-n r/2` 轮转分发（lines_rr） | 同字节 | 一致 | `split -n r/2 lines.txt` → 35/35 |
| 10 | 非法 chunks `x/2`：`invalid number of chunks: 'x/2'` | 同字节 | 一致 | `split -n x/2 lines.txt` |
| 11 | `-a 1` 单字符后缀 | 同字节 | 一致 | `split -l 3 -a 1 lines.txt` → xa..xd |
| 12 | `-d` 数字后缀从 0 起 | 同字节 | 一致 | `split -l 3 -d lines.txt` → x00..x03 |
| 13 | `--numeric-suffixes=5` 起始值 | 同字节 | 一致 | `split -l 3 --numeric-suffixes=5 lines.txt` → x05..x08 |
| 14 | `--additional-suffix=.txt` 追加后缀 | 同字节 | 一致 | `split -l 3 --additional-suffix=.txt lines.txt` |
| 15 | `-e` 省略 `-n` 产生的空片 | 同字节 | 一致 | `split -e -l 4 lines.txt` → 3 片 |
| 16 | `-t ,` 记录分隔符（本 fixture 无逗号 → 单文件） | 同字节 | 一致 | `split -t , -l 2 lines.txt` → xaa 70 |
| 17 | `--verbose` 每创建一片打印 `creating file 'xaa'` | 同字节 | 一致 | `split --verbose -l 4 lines.txt` |
| 18 | 后缀耗尽：`output file suffixes exhausted`，rc 1（split.c:465） | 同字节 | 一致 | `split -l 1 -a 1 many.txt` |
| 19 | 过时 `-NUMBER` 行数（split.c:1420-1440） | 同字节 | 一致 | `split -3 lines.txt` |
| 20 | 过时 `-NUMBER` 与 `-a` 组合 | 同字节 | 一致 | `split -3 -a 1 lines.txt` |
| 21 | 无 FILE 读 stdin，默认 1000 行（stdin 落盘） | 同字节（xaa 内容一致） | 一致 | `printf 'a\nb\n' \| split` |
| 22 | 缺失文件：`cannot open 'nope.txt' for reading: No such file or directory`（split.c:1613） | 同字节 | 一致 | `split nope.txt` |
| 23 | `--filter=COMMAND` 用 `$FILE` 命名并执行（split.c:561-618） | 同字节（生成 out-xaa/out-xab） | 一致 | `split -l 2 '--filter=cat > out-$FILE' f` |
| 24 | `--help` | 产品自有帮助 | wording | `split --help` |
| 25 | `--version` / 未知选项 | 产品横幅；`--badopt` 字节一致 | 一致 | `split --version`、`split --badopt` |
| 26 | `-b 0`：`invalid number of bytes: '0'` | 同字节 | 一致 | `split -b 0 lines.txt` |
| 27 | `PREFIX` 第二操作数 | 同字节 | 一致 | `split -l 4 part- lines.txt` → part-aa… |
| 28 | `-` 作为 FILE 读 stdin | 同字节 | 一致 | `split -l 2 -`（stdin 3 行） |
| 29 | 空输入 | 同字节（无输出片） | 一致 | `split empty.txt` |
| 30 | `-x` / `-dl` 打包短选项（#1306 回归） | `x00/x01/x02` 与 GNU 同名同内容；`-dl 1` rc0 | 一致 | `split -x -l 1 f`、`split -dl 1 ovf.txt` |
| 31 | `-a` 溢出：`invalid suffix length: '<arg>': Value too large for defined data type`（strtoint_die 追加 errno，split.c:1301-1316；#1524） | 只打印 `invalid suffix length`，缺参数与 errno | compat-gap | `split -a 18446744073709551616 f`：GNU 全串+ERANGE；Win 短句 |
| 32 | 隐藏 `---io-blksize=SIZE` 接受 | 同 rc 0（两侧均不产生副作用差异） | 一致 | `split ---io-blksize=1024 ovf.txt` |
| 33 | 输出片 mtime = 写入时刻 | 与 GNU 差 1s | noise-platform | 各 EFFECT-DIFF 均为 mtime 列 |

## 二、差异明细

### 1. 后缀长度溢出的诊断形状（#31，回归 #1524）

```
$ split -a 18446744073709551616 ovf.txt
GNU : split: invalid suffix length: '18446744073709551616': Value too large for defined data type   rc=1
WIN : split: invalid suffix length                                                                   rc=1
```

- `split.cpp` 的 `quoted_number_error` 对 `-a` 路径只保留 msgid，既不带实参也不带 ERANGE 文本。
- 同一族 `-l 99999999999999999999` / `-b ...` / `-C ...` 两侧都 rc0（GNU 会把大值钳到可表示范围），所以该 issue 只针对 `-a` 的死亡消息。

### 2. 安全插曲（负结果）

`split -n 18446744073709551616` 会让 GNU 与 WinuxCmd **都**进入长循环创建空片（5 秒内分别 364k/1M+ 文件）；这是两侧共同的病态输入，不计 gap。本次误触后已用 `rd /s /q` 清理并重建 fixture；后续不再对 `-n` 传入超大值。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 33 | 30 | 1 | 1 | 1 | 0 | 0 |

> 1 个 gap 行 → **1 张 issue**（-a 溢出诊断形状，#1524 回归 → #1711）。
