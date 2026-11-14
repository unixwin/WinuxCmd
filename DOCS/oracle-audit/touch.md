# touch — Oracle 行为契约表（WP-K 下一波）

- 上游权威：coreutils 9.4 `src/touch.c`（`touch`:121-207、`date_relative`:109-117、`main`:259-438）＋ gnulib `parse-datetime.y` / `posixtime.c`。
- Oracle：WSL2 GNU coreutils 9.4，`LC_ALL=C`、`TZ=UTC`，脚本文件通道。
- 被测：`origin/main = f0cf6b17` Release 构建 `WinuxCmd 1.2.1`（**rust2 已接线**：`touch.rs`，`WINUXCMD_RUST2_COMMANDS` 含 touch）。
- 时间观测：两侧都用同一份 `stat -c '%Y %X'` 探针读取（产品 stat 与 GNU stat 各自读本侧树）；`TZ=UTC` 经 `win-run.ps1` 显式注入产品进程。
- 原始输出：`work/out/touch-final.txt`（30 例，含 probe）。

## 一、契约行

| # | 契约（GNU 依据） | WinuxCmd 现状 | 裁定 | 最小复现 / 字节证据 |
|---:|---|---|---|---|
| 1 | 无文件操作数：`missing file operand` + Try，rc 1（touch.c:422-427） | 同字节 | 一致 | `touch` |
| 2 | 不存在则创建空文件，rc 0 | 同字节 | 一致 | `touch new.txt`；probe `%Y` 与 GNU 同秒 |
| 3 | 已存在则更新时间 | 同字节 | 一致 | `touch a.txt` |
| 4 | `-c` 不创建，目标缺失仍 rc 0（touch.c:196-198） | 同字节（probe stat 两侧均 ENOENT） | 一致 | `touch -c new2.txt` |
| 5 | 裸日期按 `TZ` 解释（parse-datetime 读 TZ；`TZ=UTC` 下 2020-01-02 03:04:05 = epoch 1577934245） | 固定按 Windows 本地时区（UTC+8）解释，得 1577905445 | compat-gap | `TZ=UTC touch -a -d '2020-01-02 03:04:05' a.txt`；`stat -c %Y` GNU 1577934245 / Win 1577905445 |
| 6 | `-a` 只改 atime，mtime 保持 | 改 atime（但时区错，同 #5），mtime 保持 | compat-gap | `touch -a -d … a.txt`：GNU `%Y %X`=1577934245 1577934245；Win=1577934245 1577905445 |
| 7 | `-m` 只改 mtime，atime 保持 | 改 mtime（时区错），atime 保持 | compat-gap | `touch -m -d … a.txt`：Win `%Y`=1577905445 |
| 8 | `-t` 时间戳同样按 TZ 解释（posixtime） | 同样按本地时区，差 28800s | compat-gap | `TZ=UTC touch -t 202001020304.05 a.txt`：GNU 1577934245 / Win 1577905445 |
| 9 | ISO 裸日期 `2020-01-02T03:04:05` 按 TZ | 同 #5 | compat-gap | `touch -d 2020-01-02T03:04:05 a.txt` |
| 10 | 显式 `UTC` 后缀覆盖 TZ | 同字节（1577934245） | 一致 | `touch -d '2020-01-02 03:04:05 UTC' a.txt` |
| 11 | `-d @0` 纪元秒，纳秒可带小数 | 同字节 | 一致 | `touch -d @0 a.txt`；probe 0 |
| 12 | `-t 2020` 非法：`invalid date format '2020'`，rc 1（posixtime 失败 → error） | 同字节 | 一致 | `touch -t 2020 a.txt` |
| 13 | `-r REF` 复制 atime/mtime | 同字节 | 一致 | `touch -r ref.txt a.txt`；probe 1577934245 |
| 14 | `-r` 缺失：`failed to get attributes of 'nope': No such file or directory`，rc 1 | 同字节 | 一致 | `touch -r nope a.txt` |
| 15 | 多文件逐个处理；任一失败 rc 1 | 同字节 | 一致 | `touch a.txt b.txt new3.txt` |
| 16 | `-h` 作用于链接自身（悬空链接也不 deref），rc 0 | 同字节 | 一致 | `touch -h -d @0 dangling`、`touch -h dangling` |
| 17 | `--time=atime` 等价 `-a` | 同字节（`%Y %X` 均 1577934245 0） | 一致 | `touch --time=atime -d @0 a.txt` |
| 18 | `-r` 与 `-d` 可组合：日期相对 REF 时间（touch.c:330-345） | 同字节 | 一致 | `touch -r ref.txt -d @0 a.txt` |
| 19 | `-a -m` 同时给出时两者都改（change_times 位掩码） | 只改 mtime；atime 保持旧值 | compat-gap | `touch -a -m -d @0 a.txt`：GNU `%Y %X`=0 0；Win=0 1577934245 |
| 20 | `-d ''` 视为今天本地零点，rc 0（touch.c:352-360） | 同字节 rc 0 | 一致 | `touch -d '' a.txt` |
| 21 | 非法日期：`invalid date format 'not-a-date'`，rc 1 | 同字节 | 一致 | `touch -d not-a-date a.txt` |
| 22 | 空操作数 `''` 是文件参数：`cannot touch '': No such file or directory`，rc 1 | 被当成“缺操作数”：`missing file operand` + Try | compat-gap | `touch ''`（GNU 报 cannot touch；Win 报 missing file operand） |
| 23 | 操作数 `-` 触碰 stdout 对应时间（touch.c:127-129） | 同 rc 0、无输出 | 一致 | `touch -` |
| 24 | `--help` 输出 | 产品自有帮助（Windows 说明） | wording | `touch --help` |
| 25 | `--version` 横幅 / 未知选项诊断 | 版本为产品横幅；`--badopt` 字节一致 | 一致 | `touch --version`、`touch --badopt a.txt` |
| 26 | 旧式 `MMDDhhmm` 仅在 POSIX2 < 2001 时启用；现代默认当文件名（touch.c:397-419） | 同样把 `01020304` 当文件并触碰成功（两侧均创建该名） | noise-platform | `touch 01020304 a.txt`：两侧 rc0，epoch 差 1s |
| 27 | 相对/完整日期语法：`next Monday`、`2 days ago`、`fortnight`、`a fortnight`、`… EST`、`201612312359.60`、`000001010000` 均接受 | `next Monday`/`2 days ago` 通过；`fortnight`（裸）、`a fortnight`、`EST`、闰秒 `.60`、年 0000 全部拒绝 | compat-gap | `touch -d fortnight a.txt`（GNU rc0 / Win invalid）；其余见 §二.3 |
| 28 | 非 ASCII 参数在错误里按 quotearg 转义：`'20\303\25101020304'` | 原样 UTF-8 `'20é01020304'` | wording | `touch -t 20é01020304 a.txt` |
| 29 | 多字节 `-t` 不 panic（#823 回归） | 同 rc 1、无 panic | 一致 | `touch -t 2016010123é.05 a.txt` |
| 30 | `-d now` 按“当前时刻”处理（并保留 -a/-m 语义） | rc0，probe 同秒内 | 一致 | `touch -d now a.txt` |

## 二、差异明细（字节级）

### 1. TZ 被忽略（#5-#9）

```
$ TZ=UTC touch -d '2020-01-02 03:04:05' a.txt; stat -c %Y a.txt
GNU : 1577934245     （UTC）
WIN : 1577905445     （UTC+8，差 28800s）
```

- `touch.rs` 明确走 `TzSpecificLocalTimeToSystemTime` 且注释“CRT global timezone is never used”；对不带 zone 的日期一律本地时区。GNU `parse_datetime` 读 `TZ`。
- 显式 `UTC`/`+0000`/`Z` 后缀可得到正确值，说明差距仅在“隐形时区”面。
- 影响：CI 里 `TZ=UTC touch -d ...` 的确定性时间戳全部偏 8 小时（本机 UTC+8）。

### 2. `-a -m` 同时给出（#19）

```
$ touch -a -m -d @0 a.txt; stat -c '%Y %X' a.txt
GNU : 0 0
WIN : 0 1577934245
```

`-a` 位在“两者都要改”的分支里丢失；`-a` 单独或 `--time=atime` 单独正常（#6/#17）。

### 3. 日期语法残差（#27，回归 #1382 的剩余面）

```
$ touch -d 'fortnight'   a.txt   GNU rc=0   WIN invalid date format 'fortnight'
$ touch -d 'a fortnight' a.txt   GNU rc=0   WIN invalid date format 'a fortnight'
$ touch -d '2020-01-02 03:04:05 EST' a.txt   GNU rc=0   WIN invalid
$ touch -t 201612312359.60 a.txt GNU rc=0（闰秒归一） WIN invalid
$ touch -t 000001010000 a.txt    GNU rc=0（公元 0 年） WIN invalid
```

`parse_relative` 只接受 `<amount> <unit>`（`1 fortnight` 可用），裸单位/冠词 `a` 未覆盖；zone 词只认 `UTC/GMT`。

### 4. 空操作数（#22）

```
$ touch ''
GNU : touch: cannot touch '': No such file or directory   rc=1
WIN : touch: missing file operand
      Try 'touch --help' for more information.            rc=1
```

产品在参数收集阶段丢弃空串，后续按“无操作数”处理。

## 三、计数

| 契约行 | 一致 | compat-gap | wording | noise-platform | noise-locale | 未覆盖 |
|---:|---:|---:|---:|---:|---:|---:|
| 30 | 19 | 8 | 2 | 1 | 0 | 0 |

> 8 个 gap 行归并为 **4 张 issue**：TZ（#5-#9，并入 #1655 评论）、`-a -m`（#19 → #1704）、日期语法（#27 → #1705）、空操作数（#22 → #1706）。
