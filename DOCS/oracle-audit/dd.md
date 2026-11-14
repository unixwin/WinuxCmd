# dd — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/dd.c`（`scan_args`:1300-1600、`copy`:1900-2410、`print_stats`:816-847、`print_xfer_stats`:739-815、`diagnose`:519-535）。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（C++ applet）。
- 时间/速率归一：GNU 的 `x.xx s, yy kB/s` 段用 `<RATE>` 掩码；records/bytes 形状不掩码。
- 原始输出：`work/out/dd-final.txt`（27 例）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 默认 stdin→stdout 原样复制 | 数据字节一致 | 一致 | `printf 'hello dd' \| dd` |
| 2 | `count=2 bs=1` 只读 2 字节 | 同字节 | 一致 | `printf abcdef \| dd count=2 bs=1` |
| 3 | `if=/of=` 文件复制 | 内容/大小一致（mtime 1s 噪声另计） | 一致 | `dd if=small.txt of=out.txt` |
| 4 | `status=none` 抑制全部统计 | 同字节 | 一致 | `printf abc \| dd status=none` |
| 5 | 统计行形状：`FULL+PARTIAL records in/out`（dd.c:816-836） | 只打印单一计数 `1 records in` / `1 records out` | compat-gap | `printf hello dd \| dd`：GNU `0+1 records in`；Win `1 records in` |
| 6 | 统计尾行：`N bytes copied, <secs> s, <rate> kB/s`（dd.c:780-790） | 只打印 `N bytes copied`，无时间/速率 | compat-gap | 同上：GNU `8 bytes copied, 9.06e-05 s, 33.1 kB/s`；Win `8 bytes copied` |
| 7 | `status=noxfer` 只去掉尾行，保留 `+` 形状（dd.c:836） | 保留计数但仍无 `+` 形状 | compat-gap | `printf abc \| dd status=noxfer` |
| 8 | 非法 `bs=abc`：`invalid number: 'abc'`，rc 1 | 同字节 | 一致 | `dd bs=abc` |
| 9 | 非法 `count=x`：`invalid number: 'x'`，rc 1 | 同字节 | 一致 | `dd count=x` |
| 10 | 未知操作数：`unrecognized operand 'foo=bar'` + Try，rc 1（dd.c:1501） | 同字节 | 一致 | `dd foo=bar` |
| 11 | `conv=ucase/lcase/swab` 纯字节变换（dd.c:1210-1270） | 全部拒绝：`unsupported conv flag 'ucase' [DIFFERS: not supported on Windows]`，rc 1 | compat-gap | `printf abc \| dd conv=ucase`：GNU `ABC`；Win 空 + 错误行 |
| 12 | `conv=ascii/ebcdic/ibm` 字符集转换 | 同样拒绝 | compat-gap | `dd conv=ascii` |
| 13 | `conv=block/unblock` 按 `cbs` 记录填充/去尾空格 | 同样拒绝 | compat-gap | `dd conv=block cbs=4` |
| 14 | `conv=sync bs=4 count=2` 短块补 NUL | 输出 8 字节一致 | 一致 | `printf abcdef \| dd conv=sync bs=4 count=2` |
| 15 | `conv=notrunc` 不清空输出文件 | 内容一致（2 字节） | 一致 | `dd if=small.txt of=zero.bin conv=notrunc bs=1 count=2` |
| 16 | `skip=N bs=1` 跳过输入 | 数据一致 | 一致 | `dd skip=2 bs=1 if=in.bin count=4` |
| 17 | `seek=N conv=notrunc` 定位输出 | 文件 0 字节一致 | 一致 | `dd seek=2 bs=1 of=out.bin conv=notrunc` |
| 18 | `if=nope`：`failed to open 'nope': No such file or directory`，rc 1 | 同字节 | 一致 | `dd if=nope` |
| 19 | `of=nodir/x` 同上 | 同字节 | 一致 | `dd if=small.txt of=nodir/x` |
| 20 | `oflag=append` 追加 | 内容一致 | 一致 | `dd if=small.txt of=out.txt oflag=append` |
| 21 | `iflag=skip_bytes` 以字节计 skip | 数据一致（1536 字节） | 一致 | `dd if=in.bin skip=2 iflag=skip_bytes count=3` |
| 22 | `bs=1k` 后缀 ×1024 | 读取 1024 字节 | 一致 | `dd bs=1k count=1 if=in.bin` |
| 23 | `count=0` 不复制 | 同字节 | 一致 | `printf abc \| dd count=0` |
| 24 | `bs=0`：`invalid number: '0'`，rc 1 | 同字节 | 一致 | `dd bs=0` |
| 25 | `conv=bogus`：`invalid conversion: 'bogus'` | 同字节（rc 1） | 一致 | `dd conv=bogus` |
| 26 | `if=-`/`of=-` 是普通文件名，不存在即失败 | 同字节 rc 1 | 一致 | `dd if=- of=-` |
| 27 | `--help` | 产品帮助；conv 列表只列已实现子集 | wording | `dd --help` |
| 28 | `--version` | 产品横幅（预期） | 一致 | `dd --version` |
| 29 | `--badopt` 走 dd 操作数解析（不是 getopt） | 同字节 | 一致 | `dd --badopt` |
| 30 | 输出文件 mtime 为写入时刻 | 与 GNU 差 1s（快照比较噪声） | noise-platform | `dd if=small.txt of=out.txt` 的 tree snapshot |
| 31 | 非法 conv 的错误不应带内部标注 | 错误行夹带 `[DIFFERS: not supported on Windows]` | wording | `dd conv=ucase` |

## 二、差异明细

### 1. 统计行形状与速率（#5/#6/#7）

```
$ printf 'hello dd' | dd >/dev/null
GNU : 0+1 records in
      0+1 records out
      8 bytes copied, 9.06e-05 s, 33.1 kB/s
WIN : 1 records in
      1 records out
      8 bytes copied

$ printf abc | dd status=noxfer >/dev/null
GNU : 0+1 records in
      0+1 records out
WIN : 1 records in
      1 records out
```

- `dd.cpp:718-721` 只累加 `in_records`/`bytes_copied`，没有 partial-block 维度，也没有 `siginfo`/计时打印。
- 脚本常用 `grep -o '[0-9]*+[0-9]* records'` 提取块数；当前形状无法兼容。

### 2. `conv` 纯字节变换未实现（#11/#12/#13）

```
$ printf abc | dd conv=ucase
GNU : ABC   + 统计
WIN : (stdout 空)
      dd: unsupported conv flag 'ucase' [DIFFERS: not supported on Windows]   rc=1
```

受影响的 8 个符号：`ascii ebcdic ibm block unblock ucase lcase swab`。这些都不依赖 Windows 特权或文件系统语义，属实现缺口而非平台噪声；`cbs=` 因此也无实际用途。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 31 | 22 | 6 | 2 | 1 | 0 | 0 |

> 6 个 gap 行归并为 **2 张 issue**：统计行形状/速率（#1707）、conv 变换族（#1708）。
