# GNU Coreutils 兼容性审计

这里记录第一批高频、适合 AI 使用的 GNU 参数兼容工作。它是待办表，
不是“全部已经完成”的声明。

## 依赖策略

这批工作先不引入第三方库。优先使用 C++ 标准库和 Win32 API 写小型
内部工具。只有在能显著降低维护成本、且依赖足够小、足够稳定时，才
重新评估第三方库。

| 命令 | GNU 参考 | 当前状态 | 下一步缺口 |
|---|---|---|---|
| `cp` | [cp invocation](https://www.gnu.org/software/coreutils/manual/html_node/cp-invocation.html) | 第一批里 `-i`、`-r/-R`、`-t`、`-v`、`-f`、`-n`、`-u`、`-T` 已对齐 | `-a`、`-d`、`-H`、`-L`、`-P`、`-p`、`-s`、`-l`、`-x`、`-Z` |
| `mv` | [mv invocation](https://www.gnu.org/software/coreutils/manual/html_node/mv-invocation.html) | 第一批里 `-t`、`-n`、`-u`、`-b`、`-S`、`--strip-trailing-slashes` 已对齐 | `--exchange`、`--no-copy`、`--context`、更完整的 `--interactive` 语义 |
| `rm` | [rm invocation](https://www.gnu.org/software/coreutils/manual/html_node/rm-invocation.html) | `-I`、`-i`、递归删除、根目录保护、以及基于 Windows 卷的 `--one-file-system` 已纳入 | 精确的 `--interactive=once|always|never` 解析 |
| `install` | [install invocation](https://www.gnu.org/software/coreutils/manual/html_node/install-invocation.html) | `-d`、`-b`、`-g`、`-m`、`-o`、`-p`、`-s`、`-t`、`-T`、`-D`、`--strip-program`、`--preserve-context`、`-Z` 都已接入解析；target-directory 行为已接上 | Windows 上的 owner/mode/strip/SELinux 实际效果 |
| `chmod` | [chmod invocation](https://www.gnu.org/software/coreutils/manual/html_node/chmod-invocation.html) | `-c`、`-f`、`-v`、`-R` 已有 | `--reference`、`-H`、`-L`、`-P`、根目录保护参数 |
| `mkdir` | [mkdir invocation](https://www.gnu.org/software/coreutils/manual/html_node/mkdir-invocation.html) | `-p`、`-v` 已有 | `-m`、`-Z` |
| `touch` | [touch invocation](https://www.gnu.org/software/coreutils/manual/html_node/touch-invocation.html) | `-a`、`-c`、`-d`、`-m`、`-r`、`-t`、`--time` 现状不一 | `-h`、`--time=access|modify`、更严格的 GNU 日期解析 |
| `ls` | [ls invocation](https://www.gnu.org/software/coreutils/manual/html_node/ls-invocation.html) | 常用显示参数外加 `-B`、`-b`、`-d`、`-f`、`-F`、`-I`、`-p`、`-Q`、`-q`、`-R`、`-U`、`-v`、`-X` 已更完整 | 余下缺口：`-L`、`--time-style`、inode/block/context 列以及 GNU 引号边界语义 |
| `head` | [head invocation](https://www.gnu.org/software/coreutils/manual/html_node/head-invocation.html) | `-c`、`-n`、`-q`、`-v`、`-z` 已有，另外兼容传统 `-NUM` 写法 | 负数计数和多文件模式的 GNU 精确行为 |
| `tail` | [tail invocation](https://www.gnu.org/software/coreutils/manual/html_node/tail-invocation.html) | `-c`、`-n`、`-q`、`-v`、`-z` 已有，另外兼容传统 `-NUM` / `+NUM` 写法 | `-f`、`-F`、`--pid`、`--retry`、`--sleep-interval` |
| `sort` | [sort invocation](https://www.gnu.org/software/coreutils/manual/html_node/sort-invocation.html) | `-b`、`-f`、`-g`、`-h`、`-k`、`-n`、`-o`、`-t`、`-u`、`-V`、`-z` 已覆盖常见 AI 使用场景 | `-d`、`-i`、`-M`、`-m`、`-R`、`-S`、`-s`、以及 locale 相关排序边界 |
| `uniq` | [uniq invocation](https://www.gnu.org/software/coreutils/manual/html_node/uniq-invocation.html) | `-c`、`-d`、`-D`、`-f`、`-i`、`-s`、`-u`、`-w`、`-z` 已纳入第一批 | `--group` 以及 GNU 分组分隔符的精确行为 |
| `cut` | [cut invocation](https://www.gnu.org/software/coreutils/manual/html_node/cut-invocation.html) | 分隔符/字段模式已有 | byte/char/line 模式和 `--complement` |
| `tr` | [tr invocation](https://www.gnu.org/software/coreutils/manual/html_node/tr.html) | 基础替换/删除已有 | `-c`、`-d`、`-s`、`-t`、字符类边界情况 |
| `du` | [du invocation](https://www.gnu.org/software/coreutils/manual/html_node/du-invocation.html) | 有基础支持 | `-a`、`-c`、`-h`、`-L`、`-P`、`-x`、`--inodes`、`--time` |
| `readlink` | [readlink invocation](https://www.gnu.org/software/coreutils/manual/html_node/readlink-invocation.html) | 有基础支持 | `-f`、`-e`、`-m`、`-n`、`-z` |
| `realpath` | [realpath invocation](https://www.gnu.org/software/coreutils/manual/html_node/realpath-invocation.html) | 有基础支持 | `-e`、`-m`、`-s`、`-z` |
| `truncate` | [truncate invocation](https://www.gnu.org/software/coreutils/manual/html_node/truncate-invocation.html) | 有基础支持 | `-c`、`-s`、`-r`、`-o` |
| `basename` | [basename invocation](https://www.gnu.org/software/coreutils/manual/html_node/basename-invocation.html) | 有基础支持 | `-a`、`-s`、`-z` |
| `dirname` | [dirname invocation](https://www.gnu.org/software/coreutils/manual/html_node/dirname-invocation.html) | 有基础支持 | `-z` |
| `stat` | [stat invocation](https://www.gnu.org/software/coreutils/manual/html_node/stat-invocation.html) | 有基础支持 | `-c`、`-f`、`-L`、`-t` 和 Windows 语义差异 |
