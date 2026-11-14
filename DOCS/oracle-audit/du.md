# du 行为契约表（#1462 Oracle 审计）

- 审计对象：`winuxcmd.exe du`（`WinuxCmd 1.2.1`）
- 基线：`origin/main = b781ff35`；GNU oracle：WSL2 **coreutils 9.4**
- 方法：固定树（f1000/f2000/sub/f3000/sub/deep/f4000/硬链接 hard1000/符号链接 linkf、linkdir/空目录）逐例比较；数值类先遮蔽数字再比结构，关键用例另跑原始数字；证据 `work/out/du-final.txt`（36 例）

## 一、权威源（GNU coreutils 9.4）

| 语义 | 归属 C 函数 | 位置 |
|---|---|---|
| 命令行/遍历 | `main` → `du_files` | `src/du.c:721` / `src/du.c:677` |
| 单条目统计、硬链接去重、-L/-P | `process_file` | `src/du.c:487` |
| 数值/单位输出 | `print_size` / `print_only_size` | `src/du.c:408` / `src/du.c:395` |
| 硬链接集合 | `hash_ins` | `src/du.c:365` |

## 二、契约表

| ID | 契约项 | GNU 契约/实测 | WinuxCmd 现状 | 裁定 |
|---|---|---|---|---|
| D01 | 默认（仅目录，含 `.`） | 自底向上，块数 | 结构与顺序一致 | 一致 |
| D02 | `-a` | 每个文件一行，硬链接只计一次 | 一致（去重成功；保留哪个名字取决于遍历顺序） | 一致 |
| D03 | `-s` | 仅合计 | 一致 | 一致 |
| D04 | `-h` | 1024 进制 `4.0K/16K` | 格式一致（数值随分配块差异，见 D28） | 一致 |
| D05 | `--apparent-size` | 逻辑字节数 | 一致（`-s` 均为 10） | 一致 |
| D06 | `-b` | `--apparent-size --block-size=1` | 一致 | 一致 |
| D07 | `-k` / `-m` | 1K / 1M 块 | 一致 | 一致 |
| D08 | `-B N` / `--block-size` | 指定块大小 | 一致 | 一致 |
| D09 | `-c` | 末尾 `total` 行 | 一致 | 一致 |
| D10 | `--max-depth=N` / `-d 0` | 限制深度 | 一致 | 一致 |
| D11 | `--exclude=PAT` | 跳过匹配项 | 一致 | 一致 |
| D12 | `-L`（跟随符号链接） | 对目录符号链接参数按目标遍历，输出 `dirlink/...` | `du: cannot access 'dirlink'`（无 `: <原因>`），rc=1；对 `sub` 正常 | compat-gap |
| D13 | `-l` | 硬链接按链接数重复计 | 一致 | 一致 |
| D14 | `--inodes` | 目录=1+子项，文件=1 | 一致（`1/2/4/10`，#1372 回归通过） | 一致 |
| D15 | `-x` | 不跨文件系统 | 一致（单卷） | 一致 |
| D16 | `-0` | NUL 结尾 | 逐字节一致 | 一致 |
| D17 | `--time` | 附加 mtime | 结构与格式一致（时区见 D30） | 一致 |
| D18 | `--time --time-style=+%Y` | 自定义格式 | 一致 | 一致 |
| D19 | `-S` | 目录独占大小单列 | 一致 | 一致 |
| D20 | `-P` | 不跟随符号链接 | 一致 | 一致 |
| D21 | `--files0-from=-` | 从 stdin 读 NUL 分隔路径 | 一致 | 一致 |
| D22 | `--threshold=2K` | 仅输出 ≥ 阈值 | 一致（目录块值为平台差异） | 一致 |
| D23 | `--si` | 1000 进制、后缀小写 `k`（`21k`） | 仍按 1024、后缀 `K`（`21K`） | compat-gap |
| D24 | `-h --apparent-size` | 人类可读逻辑大小 | 一致 | 一致 |
| D25 | 符号链接作为参数 | 默认不跟随，只报链接自身 | 一致 | 一致 |
| D26 | 不存在路径 | `cannot access ...: No such file or directory`，rc=1 | 逐字节一致 | 一致 |
| D27 | 非法选项 | `unrecognized option`，rc=1 | 一致 | 一致 |
| D28 | 目录占块 | ext4 目录 4K；`.` 合计 32K | NTFS 目录 0；合计 16K | noise-platform |
| D29 | 硬链接保留名 | 遍历先遇到的名字（ext4 哈希序） | NTFS 有序 → 保留 `f1000` | noise-platform |
| D30 | `--time` 时区 | 服从 `TZ` | 固定本地 `+0800`（与 stat 同源） | compat-gap |

## 三、差异复现

fixture：`dirlink -> dir`，`dir` 至少一个文件；`huge.bin` 20000 字节。

| # | 复现 | GNU expected | WinuxCmd actual |
|---|---|---|---|
| 1 | `du -L -s dirlink` | `16<TAB>dirlink`，rc=0 | `du: cannot access 'dirlink'`，rc=1（且错误行缺 strerror 原因） |
| 2 | `du --si -s huge.bin` | `21k<TAB>huge.bin` | `21K<TAB>huge.bin` |
| 3 | `TZ=UTC du --time -s huge.bin` | 时间带 `+0000` | 时间带 `+0800`（见 stat 表 T09） |

## 四、已关闭 issue 回归核对

| issue | 主题 | 结论 |
|---|---|---|
| #1372 | `--inodes` 目录计 0 / 混入字节数 | 通过（1/2/4/10） |
| #1351 | `-l` 把文件用量乘链接数 | 通过（集合与 GNU 一致） |
| #1159 | 尾斜杠输出双斜杠 | 通过（`./sub` 形式） |
| #1059 | 默认解引用目录链接、`-L` 不解引用文件链接 | **部分回归**：`-L` 目录链接失败（D12） |
| #1039 | 报 apparent size 而非分配块 | 通过（默认块计，目录 0 属平台） |
| #964 | 零块大小环境变量应忽略 | 未覆盖 |
| #1083 | GNU 隐藏调试选项 | 未覆盖 |

## 五、计数

- 契约行：**30**
- 一致：**25**　compat-gap：**3**　noise-platform：**2**
- 立案：D12 → #1656；D23（`--si`）并入 #1644；D30（TZ）并入 #1655
