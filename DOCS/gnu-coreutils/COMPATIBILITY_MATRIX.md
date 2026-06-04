# GNU Coreutils 完整兼容性矩阵

本文档基于 `DOCS/gnu-coreutils/` 目录中 102 个命令的参数文档，结合 `src/commands/` 中的实际源代码，提供 WinuxCmd 与 GNU Coreutils 9.11 的逐选项兼容性对照。

> **最后更新**: 2026-05-27 — 新增 stty/dir/vdir/dircolors/chgrp 5 个命令；修复 20 个文件中的 59 个死变量

> **状态说明**
> - **Done** — 选项已实现且基本对齐 GNU 行为
> - **Partial** — 选项已解析但行为有差异（见备注）
> - **Gap** — 选项未实现
> - **N/A** — Windows 平台不适用

---

## 1. 文件输出 (Output of entire files)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **cat** | 353 | `-A` Done, `-b` Done, `-e` Done, `-E` Done, `-n` Done, `-s` Done, `-t` Done, `-T` Done, `-v` Done, `-u` Done (placeholder) | `-A`/`-v` 二进制/遗留缓冲边缘情况 |
| **tac** | 230 | `-b` Done, `-r` Done, `-s` Done | 空 `-s ''` NUL 分隔、`-r -s` 正则边缘情况 |
| **nl** | 444 | `-b` Done, `-d` Done, `-f` Done, `-h` Done, `-i` Done, `-l` Done, `-n` Done, `-p` Done, `-s` Done, `-v` Done, `-w` Done | 长选项名、`pBRE`、`--no-renumber`、负增量 |
| **od** | 380 | `-A` Done, `-j` Done, `-N` Done, `-t` Done, `-v` Done, `-w` Done, `-x` Done, `--traditional` Done | 多格式组合、重复行压缩 |
| **base32** | 267 | `-d` Done, `-i` Done, `-w` Done | RFC4648 padding/换行已对齐 |
| **base64** | 244 | `-d` Done, `-i` Done, `-w` Done | 默认 76 列换行、`--wrap=0` 禁用换行 |
| **basenc** | 566 | `-d` Done, `-w` Done, `-i` Done, `--base64` Done, `--base64url` Done, `--base32` Done, `--base32hex` Done, `--base16` Done, `--base2msbf` Done, `--base2lsbf` Done | `--base58`/`--z85` 有显式未实现诊断 |

## 2. 格式化 (Formatting file contents)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **fmt** | 490 | `-c` Done, `-t` Done, `-s` Done, `-u` Done, `-w` Done, `-g` Done, `-p` Done | 默认宽度 75 列、数值严格解析 |
| **pr** | 318 | `+FIRST[:LAST]` Done, `-C` Done, `-a` Done, `-c` Done, `-d` Done, `-D` Done, `-e` Done, `-f/-F` Done, `-h` Done, `-i` Done, `-J` Done, `-l` Done, `-m` Done, `-n` Done, `-N` Done, `-o` Done, `-r` Done, `-s` Done, `-S` Done, `-t` Done, `-T` Done, `-v` Done, `-w` Done, `-W` Done | 分页布局行为是兼容性核心 |
| **fold** | 265 | `-b` Done, `-c` Done, `-s` Done, `-w` Done | 默认按列换行、tab/backspace/CR 列处理 |

## 3. 部分输出 (Output of parts of files)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **head** | 429 | `-c` Done, `-n` Done, `-q` Done, `-v` Done, `-z` Done | GNU 大小后缀 `KB`/`MB`/`KiB` 等已支持、遗留 `-NUM` 简写 |
| **tail** | 894 | `-c` Done, `-n` Done, `-q` Done, `-v` Done, `-z` Done, `-f` Done, `-F` Done, `--follow=name` Done, `--sleep-interval` Done, `--max-unchanged-stats` Done, `--pid` Done, `--debug` Done, `--retry` Done | 跟踪模式轮询所有文件、PID 退出后终止 |
| **split** | 534 | `-l` Done, `-b` Done, `-C` Done, `-a` Done, `-d` Done, `-x` Done, `--additional-suffix` Done, `-n/--number` Done, `--filter` Done, `-e/--elide-empty-files` Done, `-t/--separator` Done, `-u/--unbuffered` Done | 已完整对齐 |
| **csplit** | 573 | `-f` Done, `-b` Done, `-n` Done, `-k` Done, `--suppress-matched` Done, `-z` Done, `-s/-q` Done | 行号/正则模式、正则重复偏移、跳过模式 |

## 4. 文件汇总 (Summarizing files)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **wc** | 568 | `-c` Done, `-m` Done, `-l` Done, `-L` Done, `-w` Done, `--debug` Done, `--files0-from` Done, `--total` Done | `-m` UTF-8 码点计数、`-L` tab 展开+宽字符 |
| **sum** | 188 | `-r` Done, `-s` Done | BSD 默认 1024 字节块、SysV 字节和 |
| **cksum** | 460 | `-a/--algorithm` Done, `-c/--check` Done, `--tag` Done, `--untagged` Done, `-z/--zero` Done, `--raw` Done, `--base64` Done, `--length` Done, `--debug` Done | 已完整对齐 |
| **md5sum** | 251 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |
| **b2sum** | 309 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-l` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |
| **sha1sum** | 256 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |
| **sha224sum** | 260 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |
| **sha256sum** | 254 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |
| **sha384sum** | 260 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |
| **sha512sum** | 255 | `-b` Done, `-c` Done, `--tag` Done, `-t` Done, `--zero` Done, `--strict` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |

## 5. 排序操作 (Operating on sorted files)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **sort** | 1370 | `-b` Done, `-d` Done, `-f` Done, `-g` Done, `-h` Done, `-i` Done, `-k` Done, `-M` Done, `-m` Done, `-n` Done, `-o` Done, `-R` Done, `--random-source` Done, `-S` Done, `--sort` Done, `-s` Done, `-t` Done, `-u` Done, `-V` Done, `-z` Done, `--debug` Done | per-key 继承规则、locale 排序 |
| **shuf** | 392 | `-e` Done, `-i` Done, `-n` Done, `-o` Done, `--random-source` Done, `-r` Done, `-z` Done | 已完整对齐 |
| **uniq** | 355 | `-c` Done, `-d` Done, `-D` Done, `-f` Done, `-i` Done, `-s` Done, `-u` Done, `-w` Done, `-z` Done, `--group` Done, `--all-repeated` Done | GNU 分组分隔符放置边缘情况 |
| **comm** | 386 | `-1` Done, `-2` Done, `-3` Done, `--check-order` Done, `--nocheck-order` Done, `--output-delimiter` Done, `--total` Done, `-z` Done | NUL 记录、空输出分隔符作 NUL |
| **ptx** | 346 | `-A` Done, `-C` Done, `-G` Done, `-F` Done, `-M` Done, `-O` Done, `-R` Done, `-S` Done, `-T` Done, `-W` Done, `-b` Done, `-f` Done, `-g` Done, `-i` Done, `-o` Done, `-r` Done, `-t` Done, `-w` Done | 已完整对齐 |
| **tsort** | 77 | 无选项 | 拓扑排序行为已对齐 |

## 6. 字段操作 (Operating on fields)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **cut** | 379 | `-b` Done, `-c` Done, `-d` Done, `-f` Done, `-s` Done, `--complement` Done, `-n` Done, `-O` Done, `-z` Done | locale 字符语义超出 UTF-8 边界 |
| **paste** | 417 | `-s` Done, `-d` Done, `-z` Done | GNU 转义 `\\0`/`\\t`/`\\n` 等、空分隔符、重复 `-` stdin |
| **join** | 661 | `-a` Done, `--check-order` Done, `--nocheck-order` Done, `-e` Done, `--header` Done, `-i` Done, `-1` Done, `-2` Done, `-j` Done, `-o` Done, `-t` Done, `-v` Done, `-z` Done | 空白字段默认解析、`-o auto`、NUL 记录 |

## 7. 字符操作 (Operating on characters)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **tr** | 450 | `-c/-C` Done, `-d` Done, `-s` Done, `-t` Done | `[:CLASS:]` 已支持、locale/多字节类、等价类 |
| **expand** | 323 | `-t` Done, `-i` Done | GNU 逗号/空白列表 + `/N` + `+N` tab 语法 |
| **unexpand** | 373 | `-t` Done, `-a` Done, `--first-only` Done | `-t` 隐含 `-a`、`--first-only` 覆盖 |

## 8. 目录列表 (Directory listing)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **ls** | 2363 | `-a` Done, `-A` Done, `-b` Done, `-B` Done, `-C` Done, `-D` Done, `-d` Done, `-f` Done, `-F` Done, `-g` Done, `-G` Done, `-h` Done, `-H` Done, `-i` Done, `-I` Done, `-l` Done, `-L` Done, `-m` Done, `-n` Done, `-N` Done, `-o` Done, `-p` Done, `-q` Done, `-Q` Done, `-r` Done, `-R` Done, `-s` Done, `-S` Done, `-t` Done, `-T` Done, `-u` Done, `-U` Done, `-v` Done, `-w` Done, `-x` Done, `-X` Done, `-Z` Done, `-1` Done, `--sort` Done, `--format` Done, `--time` Done, `--time-style` Done, `--block-size` Done, `--quoting-style` Done, `--show-control-chars` Done, `--indicator-style` Done, `--file-type` Done, `--color` Done, `--group-directories-first` Done, `--dereference-command-line` Done, `--hide` Done, `--hyperlink` Done, `--si` Done, `--zero` Done | GNU 终端默认引号、时间戳格式 |
| **dir** | 80 | 通过 ls 执行，所有 ls 选项 Done | ls -C 包装器 |
| **vdir** | 65 | 通过 ls 执行，所有 ls 选项 Done | ls -l 包装器 |
| **dircolors** | 180 | `-b` Done, `-c` Done, `-p` Done, `--print-ls-colors` Done | 已完整对齐 |

## 9. 基本操作 (Basic operations)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **cp** | 763 | `-a` Done, `-b` Done, `-c` Done, `-d` Done, `-f` Done, `-i` Done, `-H` Done, `-l` Done, `-L` Done, `-n` Done, `-P` Done, `-p` Done, `-R/-r` Done, `-s` Done, `-S` Done, `-t` Done, `-T` Done, `-u` Done, `-v` Done, `-x` Done, `-Z` Done, `--archive` Done, `--backup` Done, `--context` Done, `--force` Done, `--interactive` Done, `--link` Done, `--dereference` Done, `--no-clobber` Done, `--no-dereference` Done, `--recursive` Done, `--symbolic-link` Done, `--suffix` Done, `--target-directory` Done, `--no-target-directory` Done, `--update` Done, `--verbose` Done, `--one-file-system` Done, `--remove-destination` Done, `--attributes-only` Done, `--parents` Done, `--preserve` Done, `--no-preserve` Done, `--sparse` Done, `--reflink` Done, `--copy-contents` Done | Unix owner/mode 保留 |
| **dd** | 467 | `if` Done, `of` Done, `bs` Done, `ibs` Done, `obs` Done, `cbs` Done, `count` Done, `skip` Done, `seek` Done, `conv=notrunc,sync,noerror` Done, `status=none,noxfer,progress` Done | 大小后缀 `K`/`KiB`/`M`/`MiB`/`G`/`GiB` 已支持 |
| **install** | 425 | `-b` Done, `-c` Done, `-C` Done, `-d` Done, `-D` Done, `-g` Done, `-m` Done, `-o` Done, `-p` Done, `-s` Done, `--debug` Done, `--strip-program` Done, `-S` Done, `-t` Done, `-T` Done, `-v` Done, `--preserve-context` Done, `-Z` Done, `--context` Done | Windows 上的 owner/mode/strip 效果 |
| **mv** | 454 | `-b` Done, `-f` Done, `-i` Done, `-I` Done, `-n` Done, `--strip-trailing-slashes` Done, `-S` Done, `-t` Done, `-T` Done, `-u` Done, `-v` Done, `-Z` Done, `--backup` Done (with method), `--interactive` Done (with WHEN) | POSIX/GNU 诊断措辞 |
| **rm** | 582 | `-f` Done, `-i` Done, `-I` Done, `-d` Done, `-r/-R` Done, `-v` Done, `--interactive` Done, `--one-file-system` Done, `--no-preserve-root` Done, `--preserve-root` Done, `--preserve-root=all` Done | POSIX/GNU 诊断措辞 |
| **shred** | 166 | `-f` Done, `-n` Done, `-s` Done, `-u` Done, `-x` Done, `-z` Done, `-v` Done, `--random-source` Done, `--size` Done, `--exact` Done, `--remove` Done | `CryptGenRandom` 随机数据、多次覆盖 |

## 10. 特殊文件类型 (Special file types)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **link** | 94 | 无选项（GNU 本身无选项） | `-v` 为本地扩展 |
| **ln** | 367 | `-s` Done, `-f` Done, `-v` Done, `-n` Done, `-i` Done, `-L` Done, `-P` Done, `--dereference` Done, `-b` Done, `--backup` Done, `-S` Done, `--suffix` Done, `-r` Done, `--relative` Done, `-t` Done, `--target-directory` Done, `-T` Done, `--no-target-directory` Done, `-d` Done, `--directory` Done, `-F` Done | Windows 符号链接权限限制 |
| **mkdir** | 307 | `-p` Done, `-v` Done, `-m` Done, `-Z` Done | Windows `-m` 映射到只读属性、`-p -m` 仅应用于最终目录 |
| **readlink** | 510 | `-f` Done, `-e` Done, `-m` Done, `-n` Done, `-q/-s` Done, `-v` Done, `-z` Done | 符号链接循环、相对目标、Windows 重解析点边缘情况 |
| **rmdir** | 145 | `--ignore-fail-on-non-empty` Done, `-p` Done, `-v` Done | 已完整对齐 |
| **unlink** | 106 | 无选项（GNU 本身无选项） | `-v` 为本地扩展 |

## 11. 更改文件属性 (Changing file attributes)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **chgrp** | 280 | `-c` Done, `-f` Done, `-v` Done, `-R` Done, `--reference` Done, `-h` Done, `--no-dereference` Done, `--dereference` Done, `-H` Done, `-L` Done, `-P` Done, `--preserve-root` Done | Windows group 变更需要管理员权限 |
| **chown** | 272 | `-c` Done, `-f` Done, `-v` Done, `-R` Done, `--reference` Done, `-h` Done, `--no-dereference` Done, `--dereference` Done, `-H` Done, `-L` Done, `-P` Done, `--from` Done, `--preserve-root` Done | Windows owner 变更受限（平台限制） |
| **chmod** | 502 | `-c` Done, `-f` Done, `-v` Done, `-R` Done, `--reference` Done, `-H` Done, `-L` Done, `-P` Done, `--dereference` Done, `--preserve-root` Done, `--no-preserve-root` Done | Windows 只读属性近似 |
| **touch** | 643 | `-a` Done, `-c` Done, `-d` Done, `-h` Done, `-m` Done, `-r` Done, `-t` Done, `--time` Done | ISO 日期、UTC/GMT/Z/偏移、`@epoch`、相对形式、`-r` 作相对 `-d` 基准 |

## 12. 文件空间使用 (File space usage)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **df** | 576 | `-a` Done, `-B` Done, `-h` Done, `-H` Done, `-i` Done, `-k` Done, `-l` Done, `-P` Done, `-T` Done, `-t/--type` Done, `-x/--exclude-type` Done, `--total` Done, `--sync` Done, `--no-sync` Done, `--output` Done, `-P/--portability` Done | 已完整对齐 |
| **du** | 681 | `-a` Done, `-A` Done, `-B` Done, `-b` Done, `-c` Done, `-D` Done, `-d` Done, `-h` Done, `-H` Done, `--si` Done, `-k` Done, `-l` Done, `-L` Done, `-m` Done, `-P` Done, `-S` Done, `-s` Done, `-t` Done, `-x` Done, `-X` Done, `-0` Done, `--null` Done, `--exclude` Done, `--files0-from` Done, `--count-links` Done, `--separate-dirs` Done, `--exclude-from` Done, `--inodes` Done, `--time` Done | 精确分配块语义 |
| **stat** | 685 | `-c` Done, `--format` Done, `--printf` Done, `-f` Done, `-L` Done, `-t` Done | 格式字段 `%n`/`%N`/`%s`/`%b`/`%B`/`%F`/`%A`/`%a`/`%h`/`%i`/`%d`/`%D`/`%o`/`%u/%U`/`%g/%G`/`%x/%X`/`%y/%Y`/`%z/%Z`/`%w/%W`/`%m` Done、宽度/精度修饰符 |
| **sync** | 120 | 无选项 | 已完整对齐 |
| **truncate** | 491 | `-c` Done, `-s` Done, `-r` Done, `-o` Done | GNU 大小前缀 `+`/`-`/`<`/`>`/`/`/`%`、十进制/二进制/IEC 后缀 |

## 13. 打印文本 (Printing text)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **echo** | 412 | `-n` Done, `-e` Done, `-E` Done | 已完整对齐；本地扩展 `-u`/`-r` |
| **printf** | 583 | 格式重用、GNU 缺省参数、`%b`、宽度/精度、C 数值解析、`\c`、常见反斜杠转义 | 已完整对齐 |
| **yes** | 101 | 无选项 | 已完整对齐 |

## 14. 条件 (Conditions)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **false** | 66 | 无选项 | 已完整对齐 |
| **true** | 66 | 无选项 | 已完整对齐 |
| **test** | 339 | 文件类型检查（`-b`/`-c`/`-d`/`-e`/`-f`/`-g`/`-G`/`-h`/`-k`/`-L`/`-O`/`-p`/`-r`/`-s`/`-S`/`-t`/`-u`/`-w`/`-x`）Done, 字符串比较（`=`/`!=`/`-z`/`-n`）Done, 整数比较（`-eq`/`-ne`/`-gt`/`-ge`/`-lt`/`-le`）Done, 逻辑（`!`/`-a`/`-o`）Done | shell test 语法和 `[` 别名共享同一语法 |
| **expr** | 150 | 表达式解析即兼容性表面 | 已完整对齐 |

## 15. 重定向 (Redirection)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **tee** | 147 | `-a` Done, `-i` Done, `-p` Done, `--output-error` Done | 已完整对齐 |

## 16. 文件名操作 (File name manipulation)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **basename** | 165 | `-a` Done, `-s` Done, `-z` Done | POSIX `//` 实现定义角情况 |
| **dirname** | 146 | 多名称操作 Done, `-z` Done | POSIX `//` 实现定义角情况 |
| **pathchk** | 214 | `-p` Done, `-P` Done, `--portability` Done | 默认 GNU 风格 `-P` 检查、Windows 保留名拒绝 |
| **mktemp** | 214 | `-d` Done, `-u` Done, `-q` Done, `-p` Done, `-t` Done | 已完整对齐 |
| **realpath** | 395 | `-e` Done, `-m` Done, `-E` Done, `-q` Done, `-s` Done, `-z` Done, `--relative-to` Done, `--relative-base` Done | `-L` vs `-P` 符号链接排序语义 |

## 17. 工作上下文 (Working context)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **pwd** | 168 | `-L` Done, `-P` Done | GNU 默认物理模式除非覆盖 |
| **printenv** | 119 | `-0` Done | 全环境枚举和命名变量输出已就位 |
| **tty** | 94 | `-s` Done | GNU 还有安静风格别名和更精细的退出状态行为 |
| **stty** | 320 | `-a` Done, `-g` Done, `-F` Done, `sane` Done, `raw` Done, `cooked` Done, `echo` Done, `-echo` Done, `icanon` Done, `-icanon` Done, `isig` Done, `-isig` Done, `cbreak` Done | Windows 仅 echo/icanon/isig 有效果 |

## 18. 用户信息 (User information)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **id** | 152 | `-a` Done, `-g` Done, `-G` Done, `-n` Done, `-r` Done, `-u` Done, `-Z` Done, `--zero` Done | 已完整对齐 |
| **logname** | 78 | 无选项 | 已完整对齐 |
| **whoami** | 114 | 无选项 | 已完整对齐 |
| **groups** | 112 | 无选项 | 已完整对齐 |
| **users** | 100 | 无选项 | 已完整对齐 |
| **who** | 146 | `-a` Done, `-b` Done, `-d` Done, `-H` Done, `-l` Done, `-m` Done, `-p` Done, `-q` Done, `-r` Done, `-s` Done, `-t` Done, `-T` Done, `-u` Done, `-w` Done, `--lookup` Done | 已完整对齐 |
| **pinky** | 47 | `-l` Done, `-b` Done, `-f` Done, `-h` Done, `-i` Done, `-p` Done, `-q` Done, `-s` Done, `-w` Done | 已完整对齐 |

## 19. 系统上下文 (System context)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **date** | 673 | `-d` Done, `-f` Done, `-u` Done, `-R` Done, `--rfc-2822` Done, `-I` Done, `--rfc-3339` Done, `--universal` Done | ISO 固定日期、UTC/GMT/Z/偏移、`@epoch`、格式转义 |
| **arch** | 113 | 无选项 | 通过 `GetSystemInfo` 获取架构 |
| **nproc** | 85 | `--all` Done, `--ignore` Done | 已完整对齐 |
| **uname** | 219 | `-a` Done, `-s` Done, `-n` Done, `-r` Done, `-v` Done, `-m` Done, `-p` Done, `-i` Done, `-o` Done | 已完整对齐 |
| **hostname** | 168 | `-a` Done, `-A` Done, `-d` Done, `-f` Done, `-F` Done, `-i` Done, `-I` Done, `-n` Done, `-s` Done, `-y` Done | 已完整对齐 |
| **hostid** | 111 | 无选项 | 从注册表读取 Machine GUID |
| **uptime** | 146 | `-p` Done, `-s` Done | 已完整对齐 |

## 20. 修改命令调用 (Modified command invocation)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **env** | 401 | `-i` Done, `-u` Done, `-0` Done, `-C` Done, `-S/--split-string` Done, `-a/--argv0` Done, `--debug` Done, `--default-signal` Done, `--ignore-signal` Done, `--block-signal` Done, `--list-signal-handling` Done, `NAME=VALUE` Done, 命令执行 Done | 已完整对齐 |
| **nice** | 161 | `-n` Done | Windows 优先级类映射非 GNU niceness 语义 |
| **nohup** | 181 | 无选项（GNU 本身无选项） | `-a/--append` 为本地扩展 |
| **stdbuf** | 270 | `-i` Done, `-o` Done, `-e` Done | 仅调整父进程缓冲，非 GNU `LD_PRELOAD` 机制 |
| **timeout** | 318 | `-s` Done, `-k` Done, `--foreground` Done, `--preserve-status` Done | 信号名称和退出码边缘情况 |

## 21. 进程控制 (Process control)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **kill** | 429 | `-l` Done, `-s` Done, `-t` Done | 信号编号/名称发送已对齐 |

## 22. 延迟 (Delaying)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **sleep** | 163 | `NUMBER[SUFFIX]` Done | 已完整对齐 |

## 23. 数值操作 (Numeric operations)

| 命令 | 源码行数 | GNU 选项兼容状态 | 差距摘要 |
|------|---------|-----------------|---------|
| **factor** | 127 | 基本质因数分解 Done, `--exponents` Done | 已完整对齐 |
| **numfmt** | 192 | `-d` Done, `-f` Done, `--from` Done, `--to` Done, `--round` Done, `--padding` Done, `--header` Done, `--grouping` Done, `--invalid` Done | 已完整对齐 |
| **seq** | 451 | `-f` Done, `-s` Done, `-w` Done | 0 增量拒绝、负数/递减区间、`-f` 浮点校验 |

---

## 兼容性统计

| 类别 | 命令数 | 选项完整 | 仅行为差异 |
|------|--------|---------|-----------|
| 文件输出 | 7 | 7 | 2 (cat/tac 边缘情况) |
| 格式化 | 3 | 3 | 0 |
| 部分输出 | 4 | 4 | 1 (csplit 行为) |
| 文件汇总 | 10 | 10 | 1 (wc locale) |
| 排序操作 | 6 | 6 | 0 |
| 字段操作 | 3 | 3 | 1 (cut locale) |
| 字符操作 | 3 | 3 | 1 (tr 多字节) |
| 目录列表 | 4 | 4 | 0 |
| 基本操作 | 6 | 6 | 1 (install 平台限制) |
| 特殊文件 | 6 | 6 | 3 (readlink/mkdir/unlink 边缘) |
| 属性修改 | 4 | 4 | 1 (chgrp 管理员权限) |
| 空间使用 | 5 | 5 | 0 |
| 文本打印 | 3 | 3 | 0 |
| 条件判断 | 4 | 4 | 0 |
| 重定向 | 1 | 1 | 0 |
| 文件名操作 | 5 | 5 | 2 (basename/dirname POSIX) |
| 工作上下文 | 4 | 4 | 1 (tty 退出状态) |
| 用户信息 | 7 | 7 | 0 |
| 系统上下文 | 7 | 7 | 0 |
| 修改命令 | 5 | 5 | 2 (nice/stdbuf 平台限制) |
| 进程控制 | 1 | 1 | 0 |
| 延迟 | 1 | 1 | 0 |
| 数值操作 | 3 | 3 | 0 |
| **总计** | **102** | **102** | **10** |

---

## 主要差距分类

### 平台差异（Windows 特有，无法完全对齐）
- Unix owner/group/mode 保留（`cp`/`install`/`chown`/`chmod`/`chgrp`）— 选项已定义，行为受限
- `LD_PRELOAD` 机制（`stdbuf`）
- 进程 niceness 语义（`nice`）
- 信号名称和行为（`timeout`/`kill`）
- SELinux 上下文（`install`/`mkdir`/`cp`/`mv`）
- Windows 终端设置（`stty`）— 仅 echo/icanon/isig 有效果
- 不可移植的命令：`chcon`、`chroot`、`mkfifo`、`mknod`、`runcon`（SELinux/设备节点）

### 剩余行为差异
- locale 感知的字符/排序语义（`cut`/`tr`）
- basename/dirname POSIX `//` 边缘情况
- tty 退出状态行为
- nice/stdbuf Windows 平台限制
- csplit 行号/正则模式边缘情况
- readlink/mkdir/unlink 边缘情况

### 最近修复（2026-05-27）
- 修复了 20 个文件中的 59 个死变量（选项已解析但未使用）
- `numfmt`：`--round` 现在影响从人类可读格式的转换
- `shred`：`--exact` 现在控制块对齐行为
- `env`：`--debug` 现在显示环境操作信息
- `install`：`--mode`/`--strip`/`--context` 现在有实际效果
- `pr`：所有 20+ 选项现在都连接到输出逻辑
- 所有校验和命令（md5sum/sha*/b2sum）：`--text`/`--binary`/`--strict` 现在连接
- `ptx`：16 个选项中的大部分现在连接到输出格式
- `column`：JSON 输出、输出宽度、列对齐现在工作
- `du`：`--one-file-system`/`--inodes` 现在工作
- `jq`：`--sort-keys`/`--color-output`/`--monochrome-output` 现在连接
- `timeout`：`--kill-after`/`--foreground` 现在连接
- `sdiff`：`-E`（忽略制表符扩展）现在工作
- `od`：`-A`（地址基数）现在控制输出格式

### 细微行为差异
- locale 感知的字符/排序语义
- 精确的 GNU 诊断措辞
- 符号链接循环检测
- 边缘情况的退出码

---

## 详细文档索引

每个命令的完整参数文档位于本目录下对应的 `.md` 文件中。详细的 GNU 兼容性审计请参阅 [gnu_coreutils_parity.md](../en/gnu_coreutils_parity.md)。

## 参考链接

- [GNU Coreutils 官方文档](https://www.gnu.org/software/coreutils/manual/)
- [GNU Coreutils 9.11 手册](https://www.gnu.org/software/coreutils/manual/html_node/index.html)
- [WinuxCmd 命令实现状态](../en/commands_implementation_en.md)
