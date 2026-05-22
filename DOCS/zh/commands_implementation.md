# 命令实现状态

本文档跟踪 WinuxCmd 项目中命令的实现状态，该项目现在使用基于管道的架构进行命令处理。

## 分类

### 文件管理

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `ls` | ✅ 已实现 | 高 | 列出目录内容 | `-a, --all`: 不忽略以 . 开头的条目<br>`-A, --almost-all`: 不列出隐含的 . 和 ..<br>`-b, --escape`: 用 C 风格转义显示不可打印字符<br>`-d, --directory`: 列出目录本身而不是目录内容<br>`-f`: 按目录顺序列出所有条目<br>`-F, --classify`: 为条目追加类型指示符<br>`--file-type`: 追加类型指示符，但不为可执行文件加 `*`<br>`-I, --ignore=PATTERN`: 忽略匹配 PATTERN 的条目<br>`-l, --long-list`: 使用长列表格式<br>`-h, --human-readable`: 与 -l 一起使用，以人类可读格式打印大小<br>`-r, --reverse`: 反转排序顺序<br>`-t`: 按修改时间排序<br>`-U`: 不排序，按目录顺序列出<br>`-v`: 对文本中的版本号进行自然排序<br>`-X, --sort=WORD`: 按指定键排序，例如 `extension`<br>`-n, --numeric-uid-gid`: 类似 -l，但列出数字用户和组 ID<br>`-g`: 类似 -l，但不列出所有者<br>`-o`: 类似 -l，但不列出组信息<br>`-1`: 每行列出一个文件<br>`-C`: 按列列出条目<br>`-w, --width`: 将输出宽度设置为 COLS<br>`--indicator-style=WORD`: 选择 slash/file-type/classify/none 后缀样式<br>`--color`: 彩色化输出 | 使用管道架构实现，支持彩色输出，SmallVector 优化 |
| `cat` | ✅ 已实现 | 高 | 连接文件并打印到标准输出 | `-A, --show-all`: 等同于 `-vET`<br>`-b, --number-nonblank`: 仅对非空输出行编号<br>`-e`: 等同于 `-vE`<br>`-E, --show-ends`: 在每行末尾显示 $<br>`-n, --number`: 对所有输出行编号<br>`-s, --squeeze-blank`: 压缩多个相邻空行<br>`-t`: 等同于 `-vT`<br>`-T, --show-tabs`: 将 TAB 字符显示为 ^I<br>`-u`: GNU 兼容占位，当前忽略<br>`-v, --show-nonprinting`: 显示不可打印字符 | 简单文件读写，带有管道结构，SmallVector 优化 |
| `cp` | ✅ 已实现 | 高 | 复制文件和目录 | `-a, --archive`: 递归复制并保留 Windows 时间戳/属性<br>`-p`: 保留 Windows 时间戳/属性<br>`-i, --interactive`: 覆盖前提示<br>`-r, -R, --recursive`: 递归复制目录<br>`-t, --target-directory`: 将所有 SOURCE 复制到 DIRECTORY<br>`-T, --no-target-directory`: 将 DEST 当作普通文件<br>`-n, --no-clobber`: 不覆盖已有文件<br>`-u, --update`: 仅当 SOURCE 更新时复制<br>`-v, --verbose`: 解释正在执行的操作<br>`-f, --force`: 强制复制而不提示 | 文件系统操作，带有错误处理；owner/mode 保留是 Windows 近似 |
| `mv` | ✅ 已实现 | 高 | 移动（重命名）文件 | `-i, --interactive`: 覆盖前提示<br>`-v, --verbose`: 解释正在执行的操作<br>`-f, --force`: 覆盖前不提示<br>`-n, --no-clobber`: 不覆盖现有文件 | 文件系统操作，带有错误处理，SmallVector 优化 |
| `rm` | ✅ 已实现 | 高 | 删除文件或目录 | `-f, --force`: 忽略不存在的文件和参数，从不提示<br>`-i, --interactive`: 每次删除前提示<br>`-r, -R, --recursive`: 递归删除目录及其内容<br>`-v, --verbose`: 解释正在执行的操作 | 文件系统操作，带有错误处理 |
| `mkdir` | ✅ 已实现 | 高 | 创建目录 | `-p, --parents`: 不存在时无错误，必要时创建父目录<br>`-v, --verbose`: 为创建的每个目录打印一条消息<br>`-m, --mode`: 设置文件模式（如 chmod） | 文件系统操作，带有错误处理 |
| `rmdir` | ✅ 已实现 | 中 | 删除空目录 | `--ignore-fail-on-non-empty`: 忽略删除非空目录的每次失败<br>`-p, --parents`: 删除目录及其祖先<br>`-v, --verbose`: 为删除的每个目录打印一条消息 | 文件系统操作，带有错误处理 |
| `touch` | ✅ 已实现 | 中 | 更改文件时间戳或创建空文件 | `-a`: 仅更改访问时间<br>`-m`: 仅更改修改时间<br>`-c, --no-create`: 不创建任何文件<br>`-d, --date`: 解析字符串并使用它代替当前时间<br>`-r, --reference`: 使用此文件的时间代替当前时间<br>`-t, --time`: 使用指定时间而不是当前时间<br>`-h, --no-dereference`: 影响每个符号链接而不是任何引用的文件<br>`--time=WORD`: 选择要更改的时间类型 | 文件系统操作，带有错误处理 |
| `ln` | ✅ 已实现 | 中 | 在文件之间创建链接 | `-s, --symbolic`: 创建符号链接而不是硬链接<br>`-f, --force`: 删除现有目标文件<br>`-i, --interactive`: 提示是否删除目标<br>`-v, --verbose`: 打印每个链接文件的名称<br>`-n, --no-dereference`: 如果 LINK_NAME 是指向目录的符号链接，则将其视为普通文件 | 文件链接（硬/符号） |
| `diff` | ✅ 已实现 | 中 | 逐行比较文件 | `-u, --unified=NUM`: 输出 NUM（默认 3）行统一上下文<br>`-q, --brief`: 仅输出文件是否不同<br>`-i, --ignore-case`: 忽略文件内容中的大小写差异<br>`-w, --ignore-all-space`: 忽略所有空白<br>`-B, --ignore-blank-lines`: 忽略所有行均为空白的更改<br>`-y, --side-by-side`: 以两列输出<br>`-r, --recursive`: 递归比较任何子目录 [未支持] | 文件比较；文件参数走统一通配符策略，并且必须最终解析成恰好两个文件 |
| `file` | ✅ 已实现 | 中 | 确定文件类型 | `-b, --brief`: 不在输出行前添加文件名<br>`-i, --mime`: 输出 mime 类型字符串<br>`-z, --compress`: 尝试查看压缩文件内部<br>`--mime-type`: 仅输出 MIME 类型<br>`--mime-encoding`: 仅输出 MIME 编码 | 文件类型检测 |
| `cksum` | ✅ 已实现 | 低 | CRC 校验和和字节计数 | 当前 WinuxCmd 还没有选项；GNU 兼容还需要 `-a/--algorithm`、`-c/--check`、`--tag`、`--untagged`、`--zero`、`--raw` | 校验和计算 |
| `base64` | ✅ 已实现 | 低 | Base64 编码/解码数据 | `-d, --decode`: 解码数据<br>`-i, --ignore-garbage`: 解码时忽略非字母表字符<br>`-w, --wrap=COLS`: 在 COLS 字符后折行 | Base64 编码/解码 |
| `base32` | ✅ 已实现 | 低 | Base32 编码/解码数据 | `-d, --decode`: 解码数据<br>`-w, --wrap=COLS`: 在 COLS 字符后折行 | Base32 编码/解码 |
| `basenc` | ✅ 已实现 | 低 | 使用多种编码算法进行编解码 | `-d, --decode`: 解码数据<br>`-i, --ignore-garbage`: 解码时忽略非字母表字符<br>`-w, --wrap=COLS`: 在 COLS 字符后折行<br>`--base64`、`--base32`、`--base16`、`--base64url`、`--base32hex`、`--base2msbf`、`--base2lsbf`、`--z85`: 选择编码 | 多种编码/解码 |
| `dd` | ✅ 已实现 | 中 | 转换并复制文件 | `if=FILE`: 从 FILE 读取而不是 stdin<br>`of=FILE`: 写入 FILE 而不是 stdout<br>`bs=BYTES`: 同时设置输入和输出块大小<br>`ibs=BYTES`: 设置输入块大小<br>`obs=BYTES`: 设置输出块大小<br>`cbs=BYTES`: 转换块大小 [已解析]<br>`count=N`: 只复制 N 个输入块<br>`skip=N`: 跳过 N 个 ibs 大小的输入块<br>`seek=N`: 跳过 N 个 obs 大小的输出块<br>`conv=CONVS`: 支持 `notrunc` 和 `sync`；`noerror` 仍是占位<br>`status=none|noxfer|progress`: 控制诊断输出 | 文件转换与复制 |
| `sum` | ✅ 已实现 | 低 | 打印校验和和块计数 | `-r`: 使用 BSD 格式校验和<br>`-s`: 使用 System V 格式校验和 | 旧式校验和兼容 |
| `expr` | ✅ 已实现 | 中 | 计算表达式 | `EXPRESSION`: 要求值的表达式 | 表达式求值 |
| `factor` | ✅ 已实现 | 低 | 打印质因数 | 无选项 | 质因数分解 |
| `realpath` | ✅ 已实现 | 低 | 打印解析的绝对路径 | `-e, --canonicalize-existing`: 路径的所有组件必须存在<br>`-m, --canonicalize-missing`: 不需要任何路径组件存在<br>`-L, --logical`: 已接收，使用 Windows 路径规范化<br>`-P, --physical`: 已接收，使用 Windows 路径规范化<br>`-q, --quiet`: 抑制错误消息<br>`-s, --strip, --no-symlinks`: 不展开符号链接<br>`-z, --zero`: 每个输出行以 NUL 结束 | 路径解析；GNU `-L` 与 `-P` 的完整 symlink 顺序仍是 partial |
| `find` | ✅ 已实现 | 高 | 搜索目录层次结构中的文件 | `-name PATTERN`: 基名匹配 shell 模式 PATTERN<br>`-iname PATTERN`: 基名匹配 shell 模式 PATTERN，忽略大小写<br>`-path PATTERN`: 完整路径匹配 shell 模式 PATTERN<br>`-ipath PATTERN`: 完整路径匹配 shell 模式 PATTERN，忽略大小写<br>`-type X`: 文件类型为 X（当前支持 f/d/l）<br>`-size N`: 文件大小匹配 N 个单位；支持 `+N/-N/N` 与 `c/k/M/G` 单位<br>`-empty`: 文件或目录为空<br>`-mtime N`: 文件内容最后修改距今 N*24 小时<br>`-mmin N`: 文件内容最后修改距今 N 分钟<br>`-mindepth N`: 至少从 N 层目录之后开始测试<br>`-maxdepth N`: 最多搜索到 N 层深度<br>`-print`: 输出完整路径<br>`-print0`: 输出完整路径并以 NUL 结尾<br>`-printf FORMAT`: 输出 GNU 风格文件字段（`%p`、`%f`、`%h`、`%y`、`%s`、`%m`、`%T@`、`%%`）和常用转义<br>`-prune`: 跳过当前遍历中的目录下探<br>`-quit`: 立即停止搜索<br>`-L`: 遍历时跟随符号链接<br>`-delete`: 使用深度优先遍历删除匹配文件/目录<br>`-exec COMMAND {} ;`: 每个匹配项执行一次并替换 `{}`<br>`-exec COMMAND {} +`: 批量把匹配路径追加到 COMMAND 末尾<br>`-ok COMMAND {} ;`: 每个匹配项确认后执行<br>`-H`: 在实现中标记为 [NOT SUPPORT] | 文件搜索；`-exec/-ok` 是 action 级支持，还不是完整 GNU 表达式解析器 |

### 文本处理

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `echo` | ✅ 已实现 | 高 | 显示一行文本 | `-n`: 不输出尾随换行符<br>`-e`: 启用反斜杠转义的解释<br>`-E`: 显式禁止反斜杠转义的解释<br>`-u, --upper`: 将文本转换为大写<br>`-r, --repeat N`: 重复输出 N 次 | 使用管道架构实现，作为参考实现，支持包括 `\n`、`\t`、`\xHH`、`\uHHHH` 在内的转义序列 |
| `grep` | ✅ 已实现 | 高 | 打印匹配模式的行 | `-E, --extended-regexp`: 将模式解释为扩展正则表达式<br>`-F, --fixed-strings`: 将模式解释为固定字符串<br>`-G, --basic-regexp`: 将模式解释为基本正则表达式<br>`-e, --regexp=PATTERNS`: 指定匹配模式，可重复<br>`-f, --file=FILE`: 从文件读取模式，可重复<br>`-i, --ignore-case`: 忽略大小写区别<br>`-w, --word-regexp`: 只匹配完整单词<br>`-x, --line-regexp`: 只匹配整行<br>`-z, --null-data`: 使用 NUL 分隔记录<br>`-v, --invert-match`: 反转匹配的含义<br>`-m, --max-count=NUM`: 选中 NUM 行后停止<br>`-b, --byte-offset`: 输出字节偏移<br>`-n, --line-number`: 用 1 基行号为每行输出添加前缀<br>`-H, --with-filename`: 输出文件名前缀<br>`-h, --no-filename`: 不输出文件名前缀<br>`-o, --only-matching`: 只输出匹配片段<br>`-q, --quiet`: 不输出常规结果<br>`-r, --recursive`: 递归搜索目录<br>`-R, --dereference-recursive`: 递归并跟随目录符号链接<br>`--include=GLOB`、`--exclude=GLOB`、`--exclude-from=FILE`、`--exclude-dir=GLOB`: 过滤搜索文件<br>`-A/-B/-C`: 输出上下文<br>`--group-separator=SEP`、`--no-group-separator`: 控制上下文组分隔符<br>`--binary-files=TYPE`、`-a`、`-I`: 二进制文件策略<br>`-D, --devices=ACTION`: 对设备/FIFO/socket 使用 `read` 或 `skip` 策略<br>`--color`: 使用标记高亮匹配项 | 支持正则表达式的模式匹配，文件参数支持通配符展开，支持可重复参数存储，SmallVector 优化 |
| `sort` | ✅ 已实现 | 中 | 对文本文件的行进行排序 | `-b, --ignore-leading-blanks`: 忽略前导空格<br>`-d, --dictionary-order`: 仅按空白和字母数字比较<br>`-f, --ignore-case`: 将小写字符折叠为大写字符<br>`-g, --general-numeric-sort`: 按通用数值比较<br>`-h, --human-numeric-sort`: 按人类可读数值比较<br>`-i, --ignore-nonprinting`: 忽略不可打印字符<br>`-k, --key`: 可重复键排序，支持 `F[.C][OPTS][,F[.C][OPTS]]` 范围<br>`-M, --month-sort`: 按月份名称排序<br>`-n, --numeric-sort`: 根据字符串数值进行比较<br>`-r, --reverse`: 反转比较结果<br>`-R, --random-sort`: 随机排序<br>`--random-source=FILE`: 从 FILE 读取确定性随机源<br>`-S, --buffer-size=SIZE`: 接受 GNU SIZE 形式作为内存提示<br>`-s, --stable`: 稳定排序<br>`-u, --unique`: 使用 -c 时检查严格排序<br>`-t, --field-separator`: 使用 SEP 代替从非空白到空白的转换<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符<br>`-o FILE`: 将结果写入 FILE 而不是标准输出 | 支持有序多键、键范围、字符位置和常见每键修饰符 |
| `tsort` | ✅ 已实现 | 低 | 执行拓扑排序 | 无选项 | 成对输入的拓扑排序 |
| `wc` | ✅ 已实现 | 中 | 为每个文件打印换行符、单词和字节计数 | `-c, --bytes`: 打印字节计数<br>`-l, --lines`: 打印换行符计数<br>`-w, --words`: 打印单词计数<br>`-m, --chars`: 打印字符计数<br>`-L, --max-line-length`: 打印最大显示宽度<br>`--files0-from=FILE`: 从 NUL 分隔列表读取文件名<br>`--total=WHEN`: 控制总计输出方式 | 带有多种模式的字符计数，SmallVector 优化 |
| `head` | ✅ 已实现 | 低 | 输出文件的前几部分 | `-c, --bytes=[-]NUM`: 打印前 N 个字节，可用负值表示从末尾回退<br>`-n, --lines=[-]NUM`: 打印前 N 行，可用负值表示从末尾回退<br>`-q, --quiet, --silent`: 从不打印给出文件名的头<br>`-v, --verbose`: 始终打印给出文件名的头<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符 | 文件头部提取，支持字节/行选项，SmallVector 优化 |
| `tail` | ✅ 已实现 | 低 | 输出文件的最后部分 | `-c, --bytes=[+]NUM`: 输出最后 N 个字节，可用正值表示从头开始偏移<br>`-n, --lines=[+]NUM`: 输出最后 N 行，可用正值表示从头开始偏移<br>`-q, --quiet, --silent`: 从不打印给出文件名的头<br>`-v, --verbose`: 始终打印给出文件名的头<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符<br>`-f, --follow`: 随着文件增长输出附加数据<br>`--follow=name`: 按文件名而不是描述符跟踪<br>`-F`: 等同于 `--follow=name --retry`<br>`-s, --sleep-interval=SECONDS`: 跟踪时两轮之间暂停<br>`--pid=PID`: 在 PID 退出后停止跟踪<br>`--retry`: 持续尝试打开不可访问的文件<br>`--max-unchanged-stats=N`: 名称跟踪时 N 轮未变化后重新打开检查 | 文件尾部提取，支持按名称跟踪，SmallVector 和 ConstexprMap 优化 |
| `sed` | ✅ 已实现 | 中 | 用于过滤和转换文本的流编辑器 | `-e, --expression`: 将 SCRIPT 添加到要执行的命令；可重复，并与 `-f` 保持顺序<br>`-f, --file`: 将 SCRIPT-FILE 的内容添加到命令；可重复，并与 `-e` 保持顺序<br>`-n, --quiet, --silent`: 禁止模式空间的自动打印<br>`-i, --in-place`: 就地编辑文件 | 支持按顺序执行的可重复脚本参数，SmallVector 优化 |
| `uniq` | ✅ 已实现 | 中 | 报告或省略重复行 | `-c, --count`: 在行前加上出现次数作为前缀<br>`-d, --repeated`: 仅打印重复行，每组一个<br>`-D, --all-repeated[=METHOD]`: 打印每组的所有重复行<br>`-f, --skip-fields`: 避免比较前 N 个字段<br>`-i, --ignore-case`: 比较时忽略大小写差异<br>`-s, --skip-chars`: 避免比较前 N 个字符<br>`--group[=METHOD]`: 以组为单位输出重复项<br>`-u, --unique`: 仅打印唯一行<br>`-w, --check-chars`: 比较行中不超过 N 个字符<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符 | 重复行检测和过滤，SmallVector 优化；分组模式已接入，GNU 分隔符细节仍需打磨 |
| `cut` | ✅ 已实现 | 中 | 从文件的每一行中移除部分 | `-b, --bytes`: 按字节切割<br>`-c, --characters`: 按字符切割<br>`-d, --delimiter`: 使用 DELIM 而不是 TAB 作为字段定界符<br>`-f, --fields`: 仅选择这些字段<br>`--complement`: 选择指定字节、字符或字段集合的补集<br>`--output-delimiter=STRING`: 在选中字段或字节/字符范围之间使用 STRING<br>`-s, --only-delimited`: 不打印不包含定界符的行<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符 | 字段、字节和字符提取 |
| `tee` | ✅ 已实现 | 中 | 从标准输入读取并写入标准输出和文件 | `-a, --append`: 追加到给定的文件，不覆盖<br>`-i, --ignore-interrupts`: 忽略中断信号<br>`-p, --diagnose`: 将错误写入标准错误 | 标准输入/输出重定向，SmallVector 优化 |
| `xargs` | ✅ 已实现 | 高 | 从输入构建并执行命令行 | `-n, --max-args=MAX-ARGS`: 每个命令行最多使用 MAX-ARGS 个参数<br>`-L, --max-lines=MAX-LINES`: 每个命令行最多使用 MAX-LINES 个非空输入行<br>`-l[LINES]`: `-L` 的旧式别名，裸形式默认 1 行<br>`-I R-STR`: 用从标准输入读取的名称替换初始参数中的 R-STR<br>`-i, --replace[=R-STR]`: `-I` 的旧式别名，裸形式默认 `{}`<br>`-P, --max-procs=MAX-PROCS`: 一次最多运行 MAX-PROCS 个进程<br>`-a, --arg-file=FILE`: 从文件而不是标准输入读取项<br>`-E EOF`: 设置逻辑 EOF 字符串<br>`-e[EOF], --eof[=EOF]`: `-E` 的旧式别名；裸形式会保持 EOF 关闭<br>`--show-limits`: 显示命令行长度限制<br>`-t, --verbose`: 在执行之前在标准错误输出上打印命令行<br>`-0, --null`: 输入项以空字符而不是空白字符终止<br>`-d, --delimiter=DELIM`: 输入项以 DELIM 而不是空白字符终止<br>`-o, --open-tty`: 在子进程中重新打开控制台输入<br>`-r, --no-run-if-empty`: 输入为空时不执行命令<br>`-x, --exit`: 命令行过长时直接退出<br>`--process-slot-var=VAR`: 为子进程导出槽位变量 | 从输入执行命令，SmallVector 优化 |

### 追加文本处理

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `ptx` | ✅ 已实现 | 低 | 生成文件内容的排列索引 | `-f, --ignore-case`: 忽略大小写差异<br>`-r, --references`: 只输出单词引用<br>`-w, --width`: 设置输出宽度 | 排列索引生成 |
| `expand` | ✅ 已实现 | 低 | 将制表符转换为空格 | `-t, --tabs=tab1[,tab2]...`: 设置制表位<br>`-i, --initial`: 不转换非空白后的制表符 | 制表符转空格 |
| `unexpand` | ✅ 已实现 | 低 | 将空格转换为制表符 | `-t, --tabs=tab1[,tab2]...`: 设置制表位<br>`-a, --all`: 转换所有空白，而不仅仅是开头<br>`--first-only`: 只转换第一段空格 | 空格转制表符 |
| `fold` | ✅ 已实现 | 低 | 将每一行折叠到指定宽度 | `-w, --width=WIDTH`: 使用 WIDTH 列而不是 80 列<br>`-b, --bytes`: 按字节而不是按列计数<br>`-c, --characters`: 按字符而不是按列计数<br>`-s, --spaces`: 在空格处断行 | 行折叠 |
| `fmt` | ✅ 已实现 | 低 | 简单的最佳文本排版器 | `-w, --width=WIDTH`: 最大行宽（默认 75）<br>`-p, --prefix=STRING`: 只格式化以 STRING 开头的行<br>`-c, --crown-margin`: 保留 crown margin<br>`-t, --tagged-paragraph`: 保留带标签的段落<br>`-s, --split-only`: 只拆分长行<br>`-u, --uniform-spacing`: 统一空格 | 文本格式化 |
| `nl` | ✅ 已实现 | 低 | 为文件的行编号 | `-b, --body-numbering=STYLE`: 为正文行编号<br>`-n, --number-format=FORMAT`: 按 FORMAT 插入行号<br>`-s, --number-separator=STRING`: 在行号后添加 STRING<br>`-f, --footer-numbering=STYLE`: 页脚编号样式<br>`-h, --header-numbering=STYLE`: 页眉编号样式<br>`-i, --line-increment=NUMBER`: 行号递增量<br>`-l, --join-blank-lines=NUMBER`: 合并空行数量<br>`-v, --starting-line-number=NUMBER`: 起始行号<br>`-w, --number-width=NUMBER`: 行号宽度 | 行编号 |
| `paste` | ✅ 已实现 | 低 | 合并文件的行 | `-d, --delimiters=LIST`: 使用 LIST 中的字符代替 TAB<br>`-s, --serial`: 一次处理一个文件而不是并行<br>`-z, --zero-terminated`: 用 NUL 结束行 | 行合并 |
| `pr` | ✅ 已实现 | 低 | 为打印格式转换文本文件 | `+FIRST_PAGE[:LAST_PAGE]`: 从第 FIRST_PAGE 页开始（并可停止于 LAST_PAGE）<br>`-C, --columns=NUM`: 输出 NUM 列并按列向下打印<br>`-a, --across`: 横向排列列<br>`-d, --double-space`: 双倍行距<br>`-h, --header=HEADER`: 使用居中的 HEADER 代替文件名<br>`-l, --length=PAGE_LENGTH`: 设置页长度<br>`-o, --indent=MARGIN`: 为每行加上 MARGIN 个空格<br>`-T, --omit-pagination`: 省略分页控制 | 打印格式文本 |
| `tr` | ✅ 已实现 | 中 | 转换或删除字符 | `-c, -C, --complement`: 使用 SET1 的补集<br>`-d, --delete`: 删除 SET1 中的字符<br>`-s, --squeeze-repeats`: 压缩重复字符序列<br>`-t, --truncate-set1`: 将 SET1 截断到 SET2 长度 | 字符转换与删除 |
| `rev` | ✅ 已实现 | 低 | 反转文件中的每一行 | 无选项 | 行反转 |
| `tac` | ✅ 已实现 | 低 | 反向连接并打印文件 | `-b, --before`: 将分隔符放在前面而不是后面<br>`-r, --regex`: 将分隔符解释为正则表达式<br>`-s, --separator=STRING`: 使用 STRING 作为分隔符 | 反向拼接 |
| `shuf` | ✅ 已实现 | 低 | 打乱输入行 | `-e, --echo`: 将每个参数视为输入行<br>`-i, --input-range=LO-HI`: 将 LO 到 HI 的数字视为输入行<br>`-n, --head-count=COUNT`: 最多输出 COUNT 行<br>`-o, --output=FILE`: 将结果写入 FILE<br>`--random-source=FILE`: 从 FILE 读取随机字节<br>`-r, --repeat`: 允许重复输出<br>`-z, --zero-terminated`: 使用 NUL 作为行终止符 | 行打乱 |
| `split` | ✅ 已实现 | 低 | 将文件拆分为多个片段 | `-l, --lines=NUMBER`: 每个输出文件放 NUMBER 行<br>`-b, --bytes=SIZE`: 每个输出文件放 SIZE 字节<br>`-C, --line-bytes=SIZE`: 每个输出文件放 SIZE 字节但不拆分行<br>`--filter=COMMAND`: 对每个输出文件运行 COMMAND<br>`-n, --number=CHUNKS`: 生成 CHUNKS 份<br>`-a, --suffix-length=N`: 生成长度为 N 的后缀<br>`-d, --numeric-suffixes[=FROM]`: 使用数字后缀<br>`-x, --hex-suffixes[=FROM]`: 使用十六进制后缀<br>`--additional-suffix=SUFFIX`: 追加 SUFFIX<br>`-e, --elide-empty-files`: 不生成空输出文件<br>`-t, --separator=SEP`: 使用 SEP 作为记录分隔符<br>`-u, --unbuffered`: 每行后刷新输出<br>`--verbose`: 打印输出文件名 | 文件拆分；输入参数走统一通配符策略，歧义匹配会被拒绝，必须最终解析成恰好一个文件 |
| `csplit` | ✅ 已实现 | 低 | 按上下文将文件拆分为多个片段 | `-f, --prefix=PREFIX`: 使用 PREFIX 作为输出文件名前缀<br>`-b, --suffix-format=FORMAT`: 使用 FORMAT 作为后缀格式<br>`-n, --digits=DIGITS`: 使用指定位数<br>`-k, --keep-files`: 出错时不删除输出文件<br>`--suppress-matched`: 在输出中省略匹配行<br>`-z, --elide-empty-files`: 省略空输出文件<br>`-s, -q, --quiet, --silent`: 不打印输出文件大小 | 按上下文拆分；输入参数走统一通配符策略，歧义匹配会被拒绝，必须最终解析成恰好一个文件 |
| `join` | ✅ 已实现 | 低 | 按共同字段连接两个文件的行 | `-1 FIELD`: 使用文件 1 的 FIELD 作为连接键<br>`-2 FIELD`: 使用文件 2 的 FIELD 作为连接键<br>`-a FILE-NUMBER`: 打印无法配对的文件 NUMBER 行<br>`--check-order`: 检查输入是否正确排序<br>`--nocheck-order`: 不检查排序顺序<br>`-e STRING`: 用 STRING 填充空输出字段<br>`--header`: 将每个文件的首行视为表头<br>`-i, --ignore-case`: 比较键时忽略大小写<br>`-j FIELD`: 两个文件都使用 FIELD 作为连接键<br>`-o FIELD-LIST`: 按 FIELD-LIST 输出字段<br>`-t, --separator=CHAR`: 使用 CHAR 作为输入和输出字段分隔符<br>`-v FILE-NUMBER`: 仅打印无法配对的文件 NUMBER 行<br>`-z, --zero-terminated`: 使用 NUL 作为行终止符 | 行连接 |
| `comm` | ✅ 已实现 | 低 | 逐行比较两个已排序文件 | `-1`: 屏蔽仅在 FILE1 中出现的行<br>`-2`: 屏蔽仅在 FILE2 中出现的行<br>`-3`: 屏蔽两个文件都出现的行<br>`--check-order`: 检查输入是否正确排序<br>`--nocheck-order`: 不检查排序顺序<br>`--output-delimiter=STRING`: 在列之间使用 STRING<br>`--total`: 打印总计<br>`-z, --zero-terminated`: 使用 NUL 作为行终止符 | 已排序文件比较 |

### 文件工具补充

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `basename` | ✅ 已实现 | 低 | 去掉目录和后缀的文件名 | `-a, --multiple`: 支持多个参数并分别处理<br>`-s, --suffix=SUFFIX`: 移除尾随 SUFFIX<br>`-z, --zero`: 每个输出行以 NUL 结束 | 文件名处理 |
| `dirname` | ✅ 已实现 | 低 | 去掉最后一个路径组件 | `-z, --zero`: 每个输出行以 NUL 结束 | 文件名处理 |
| `readlink` | ✅ 已实现 | 低 | 打印解析后的符号链接或规范化文件名 | `-f, --canonicalize`: 跟随每个符号链接进行规范化<br>`-e, --canonicalize-existing`: 跟随每个符号链接并要求存在<br>`-m, --canonicalize-missing`: 跟随每个符号链接并允许缺失<br>`-n, --no-newline`: 不输出尾随分隔符<br>`-z, --zero`: 每个输出行以 NUL 结束 | 符号链接解析 |
| `cmp` | ✅ 已实现 | 低 | 逐字节比较两个文件 | `-b, --print-bytes`: 打印不同的字节<br>`-i, --ignore-initial=SKIP`: 跳过两个输入的前 SKIP 个字节<br>`-l, --verbose`: 输出字节编号和差异字节值<br>`-n, --bytes=LIMIT`: 最多比较 LIMIT 个字节 | 字节比较 |
| `cpio` | ✅ 已实现 | 低 | 复制归档中的文件 | `-o, --create`: 创建归档<br>`-i, --extract`: 从归档中提取文件<br>`-t, --list`: 列出归档内容<br>`-d, --make-directories`: 必要时创建前导目录<br>`-m, --preserve-modification-time`: 创建文件时保留修改时间 | 归档复制 |
| `pathchk` | ✅ 已实现 | 低 | 检查文件名是否有效或可移植 | `-p, --portability`: 按 POSIX 检查<br>`-P, --posix`: 按 POSIX 检查<br>`--help`: 显示帮助<br>`--version`: 输出版本信息 | 路径有效性检查 |
| `sync` | ✅ 已实现 | 低 | 将缓存写入持久存储 | 无选项 | 文件系统同步 |
| `shred` | ✅ 已实现 | 中 | 覆盖文件以隐藏其内容 | `-f, --force`: 更改权限以允许写入<br>`-n, --iterations=N`: 覆盖 N 次而不是默认的 3 次<br>`-s, --size=N`: 仅处理 N 字节<br>`-u, --remove`: 覆盖后删除并截断文件<br>`-v, --verbose`: 显示进度<br>`-z, --zero`: 最后再用零覆盖一次 | 安全删除 |
| `md5sum` | ✅ 已实现 | 低 | 计算并检查 MD5 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | MD5 校验和 |
| `sha1sum` | ✅ 已实现 | 低 | 计算并检查 SHA1 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | SHA1 校验和 |
| `sha224sum` | ✅ 已实现 | 低 | 计算并检查 SHA224 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | SHA224 校验和 |
| `sha256sum` | ✅ 已实现 | 低 | 计算并检查 SHA256 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | SHA256 校验和 |
| `sha384sum` | ✅ 已实现 | 低 | 计算并检查 SHA384 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | SHA384 校验和 |
| `sha512sum` | ✅ 已实现 | 低 | 计算并检查 SHA512 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | SHA512 校验和 |
| `b2sum` | ✅ 已实现 | 低 | 计算并检查 BLAKE2 摘要 | `-b, --binary`: 二进制模式读取<br>`-c, --check`: 读取并检查校验和<br>`--tag`: 创建 BSD 风格校验和<br>`-t, --text`: 文本模式读取<br>`-z, --zero`: 每个输出行以 NUL 结束 | BLAKE2 校验和 |
| `od` | ✅ 已实现 | 低 | 以八进制和其他格式转储文件 | `-A, --address-radix=RADIX`: 选择偏移量输出的基数<br>`-j, --skip-bytes=BYTES`: 先跳过 BYTES 字节<br>`-N, --read-bytes=BYTES`: 限制读取字节数<br>`-t, --format=TYPE`: 选择输出格式<br>`-v, --output-duplicates`: 不用 * 标记重复行<br>`-w, --width BYTES`: 每行输出 BYTES 字节 | 八进制转储 |
| `xxd` | ✅ 已实现 | 低 | 制作十六进制转储或反向转换 | `-b, --bits`: 以位模式输出<br>`-c, --cols COLS`: 每行输出 COLS 个字节<br>`-g, --groupsize BYTES`: 每组字节数<br>`-l, --len LENGTH`: 处理到 LENGTH 字节为止<br>`-p, --ps`: 以连续 hexdump 风格输出<br>`-r, --reverse`: 将 hexdump 逆向为二进制<br>`-s, --seek OFFSET`: 从 OFFSET 开始 | 十六进制转储与反向转换 |
| `patch` | ✅ 已实现 | 中 | 将 diff 文件应用到原始文件 | `-p NUM`: 去掉文件名前 NUM 层前缀<br>`-i, --input=PATCHFILE`: 从 PATCHFILE 读取补丁<br>`-d, --directory=DIR`: 切换到 DIR<br>`-R, --reverse`: 假设补丁是反向顺序<br>`-N, --forward`: 假设补丁是旧新文件交换顺序 | 补丁应用 |
| `diff3` | ✅ 已实现 | 低 | 比较三个文件 | `-e`: 输出 ed 脚本<br>`-3`: 像 -e 一样，但包含第三个文件的修改<br>`-m, --merge`: 输出合并后的文件<br>`-i`: 像 -e 一样，但括起修改<br>`-A`: 把旧版本到你的所有修改都合并进去<br>`-E`: 忽略旧版本到你的修改<br>`-X`: 覆盖重叠修改 | 三方文件比较；文件参数走统一通配符策略，并且必须最终解析成恰好三个文件 |
| `sdiff` | ✅ 已实现 | 低 | 并排合并文件差异 | `-o FILE`: 将输出写入 FILE<br>`-w, --width=NUM`: 设置输出宽度<br>`-l, --left-only`: 对共同行只打印左列<br>`-s, --suppress-common-lines`: 不打印共同行 | 并排差异 |

### 系统信息

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `which` | ✅ 已实现 | 高 | 在 PATH 中定位命令 | `-a, --all`: 打印每个参数的所有匹配路径名<br>`--read-alias`, `--skip-alias`: 读取/跳过别名展开<br>`--read-functions`, `--skip-functions`: 读取/跳过 shell 函数展开<br>`--skip-dot`: 跳过 PATH 中以点开头的目录<br>`--show-dot`: 允许显示以点开头的目录<br>`--skip-tilde`: 跳过 PATH 中以波浪号开头的目录<br>`--show-tilde`: 允许显示以波浪号开头的目录<br>`--tty-only`: 仅在 tty 上显示匹配结果 | 路径搜索，支持 PATHEXT，SmallVector 优化 |
| `env` | ✅ 已实现 | 中 | 在修改的环境中运行命令 | `-i, --ignore-environment`: 从空环境开始<br>`-u, --unset=NAME`: 从环境中删除变量，可重复<br>`-0, --null`: 打印环境时每项以 NUL 结束<br>`-C, --chdir=DIR`: 打印或运行 COMMAND 前切换工作目录<br>`NAME=VALUE`: 添加或覆盖环境变量<br>`COMMAND [ARG]...`: 在修改后的环境中运行命令 | 使用 Windows environment block 打印或执行命令；`-S/--split-string`、`-a/--argv0`、debug 和信号控制仍是缺口 |
| `pwd` | ✅ 已实现 | 高 | 打印工作目录 | `-L, --logical`: 如果环境变量 PWD 包含指向当前目录的绝对文件名，则输出它<br>`-P, --physical`: 避免所有符号链接 | 工作目录显示；GNU 默认等同于 `-P`，`POSIXLY_CORRECT` 会改变默认行为 |
| `ps` | ✅ 已实现 | 高 | 报告进程状态 | `-e, -A`: 选择所有进程<br>`-a`: 选择所有进程，但不包括会话领导和没有关联终端的进程<br>`-x`: 选择没有控制 tty 的进程<br>`-f`: 完整格式列表<br>`-l`: 长格式<br>`-u USER`: 按有效用户 ID 或名称选择<br>`-w`: 宽输出<br>`--no-headers`: 不打印头<br>`--sort=KEY`: 按列排序 | 进程列表和过滤；这条是仓库本地实现，不对应 GNU Coreutils 手册页 |
| `chmod` | ✅ 已实现 | 中 | 更改文件模式位 | `-c, --changes`: 类似详细，但仅在做出更改时报告<br>`-f, --silent, --quiet`: 禁止大多数错误消息<br>`-v, --verbose`: 为处理的每个文件输出诊断<br>`-R, --recursive`: 递归更改文件和目录<br>`--reference=RFILE`: 使用 RFILE 的模式而不是 MODE 值 | 文件权限修改；文件参数走统一通配符策略 |
| `date` | ✅ 已实现 | 中 | 打印或设置系统日期/时间 | `-d, --date=STRING`: 显示由 STRING 描述的时间<br>`--debug`: 打印日期解析调试信息<br>`-f, --file=DATEFILE`: 从文件读取日期字符串<br>`-I, --iso-8601[=TIMESPEC]`: ISO 8601 输出<br>`-r, --reference=FILE`: 参考文件时间<br>`--resolution`: 输出时间分辨率<br>`-R, --rfc-email`: RFC 5322 风格日期<br>`--rfc-3339=TIMESPEC`: RFC 3339 风格输出<br>`-s, --set=STRING`: 设置系统时间<br>`-u, --utc, --universal`: 打印或设置协调世界时（UTC）<br>`+FORMAT`: 输出格式字符串 | 日期/时间显示和格式化 |
| `kill` | ✅ 已实现 | 高 | 向进程发送信号 | `-l, --list`: 列出所有信号名称<br>`-s, --signal=SIGNAL`: 发送指定的信号<br>`-t, --table`: 以表格列出信号<br>`SIGNAL`: 信号编号或名称（例如，-9、-KILL、-15、-TERM） | 进程信号发送 |

### 系统工具

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `df` | ✅ 已实现 | 中 | 报告文件系统磁盘空间使用情况 | `-h, --human-readable`: 以 1024 的幂打印大小<br>`-H, --si`: 以 1000 的幂打印大小<br>`-k`: 使用 1K 块<br>`-T, --print-type`: 打印文件系统类型<br>`-i, --inodes`: 输出 inode 形状的列；Windows 使用占位列<br>文件参数走统一通配符策略<br>`-t, --type=TYPE`、`-x, --exclude-type=TYPE`、`-a, --all`: 在 GNU 兼容表中继续跟踪 | 磁盘空间报告 |
| `du` | ✅ 已实现 | 中 | 估算文件空间使用情况 | `-a, --all`: 为所有文件输出计数，而不仅仅是目录<br>`-B, --block-size=SIZE`: 按 SIZE 缩放后输出<br>`-b, --bytes`: 等价于 apparent-size 且块大小为 1<br>`-c, --total`: 产生总计<br>`-d, --max-depth=N`: 仅输出低于命令行参数 N 或更少级别的条目<br>`-h, --human-readable`: 以 1024 的幂打印大小<br>`-H, --si`: 以 1000 的幂打印大小<br>`-k`: 使用 1K 块<br>`-s, --summarize`: 仅显示每个参数的总计 | 文件/目录大小估算；文件参数走统一通配符策略，块计数向上取整 |

### 网络

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `ping` | ❌ 未实现 | 高 | 向网络主机发送 ICMP ECHO_REQUEST | `-c COUNT`: 发送 COUNT 个 ECHO_REQUEST 数据包后停止<br>`-i INTERVAL`: 在发送每个数据包之间等待 INTERVAL 秒 | 尚未实现 |
| `curl` | ❌ 未实现 | 中 | 从服务器传输数据或向服务器传输数据 | `-s, --silent`: 静默模式<br>`-o, --output`: 将输出写入文件而不是 stdout | 尚未实现 |

### 其他

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `help` | ✅ 已实现 | 高 | 显示命令的帮助信息 | `COMMAND`: 显示特定命令的帮助 | 使用命令元数据生成帮助 |
| `man` | ✅ 已实现 | 低 | 显示手册页 | `COMMAND`: 显示某个命令的手册页 | 仓库本地手册查找，处理 `.exe` |
| `jobs` | ✅ 已实现 | 中 | 显示当前 shell 中作业的状态 | `-l, --list`: 除常规信息外还列出进程 ID<br>`-n, --names`: 只列出自上次通知后状态发生变化的作业<br>`-p, --pid`: 只列出作业进程组首进程的 ID<br>`-r, --running`: 只列出正在运行的作业<br>`-s, --stopped`: 只列出已停止的作业 | 作业状态显示 |
| `bg` | ✅ 已实现 | 中 | 在后台恢复一个作业 | `JOB_SPEC`: 作业标识，或 `-` 表示当前作业 | 后台恢复 |
| `fg` | ✅ 已实现 | 中 | 在前台恢复一个作业 | `JOB_SPEC`: 作业标识，或 `-` 表示当前作业 | 前台恢复 |
| `wait` | ✅ 已实现 | 中 | 等待进程结束 | `PID`: 要等待的进程 ID | 进程等待 |
| `export` | ✅ 已实现 | 中 | 设置环境变量 | 无选项 | 环境变量导出 |
| `unset` | ✅ 已实现 | 中 | 取消设置 shell 变量的值和属性 | 无选项 | 变量取消设置 |
| `stty` | ✅ 已实现 | 中 | 打印和修改终端行设置 | `-a, --all`: 打印所有当前设置<br>`-g, --save`: 以 stty 可读格式打印所有当前设置<br>`-F, --file=DEVICE`: 打开并使用指定设备而不是 stdin | 终端设置 |
| `iconv` | ✅ 已实现 | 中 | 在两种编码之间转换文本 | `-f, --from-code=NAME`: 原始文本的编码<br>`-t, --to-code=NAME`: 输出编码<br>`-l, --list`: 列出所有已知编码字符集<br>`-c`: 忽略无效字符<br>`-s`: 抑制警告 | 编码转换 |
| `localedef` | ✅ 已实现 | 中 | 编译 locale 定义文件 | `-f, --charmap=FILE`: FILE 中定义的字符名<br>`-i, --inputfile=FILE`: locale 定义文件<br>`-u, --alias-file=FILE`: locale 名称别名文件 | locale 定义编译 |
| `renice` | ✅ 已实现 | 中 | 调整正在运行进程的优先级 | `-n, --priority NUM`: 指定调度优先级<br>`-p, --pid PID`: 将参数解释为进程 ID<br>`-u, --user USER`: 将参数解释为用户名<br>`-g, --pgrp PGID`: 将参数解释为进程组 ID | 调整进程优先级 |
| `test` | ✅ 已实现 | 高 | 检查文件类型并比较值 | `-e FILE`: 文件存在<br>`-f FILE`: 文件是普通文件<br>`-d FILE`: 文件是目录<br>`-z STRING`: 字符串长度为零<br>`-n STRING`: 字符串长度非零<br>`STRING1 = STRING2`: 字符串相等<br>`INTEGER1 -eq INTEGER2`: 整数相等 | 文件类型检查和值比较 |
| `[` | ✅ 已实现 | 高 | 检查文件类型并比较值 | 同 `test` | `test` 的别名 |
| `true` | ✅ 已实现 | 低 | 返回成功结果 | 无选项 | 始终返回 0 |
| `false` | ✅ 已实现 | 低 | 返回失败结果 | 无选项 | 始终返回 1 |
| `exit` | ✅ 已实现 | 高 | 退出 shell | 无选项 | 简单退出调用，带有管道结构 |
| `clear` | ✅ 已实现 | 高 | 清除终端屏幕 | 无选项 | 使用系统调用清除屏幕 |
| `reset` | ✅ 已实现 | 低 | 重新初始化终端 | 无选项 | 终端重置 |
| `cd` | ✅ 已实现 | 高 | 更改当前目录 | `-L, --logical`: 强制遵循符号链接<br>`-P, --physical`: 使用物理目录结构而不遵循符号链接 | 使用 SetCurrentDirectory API，带有管道结构 |
| `type` | ❌ 未实现 | 中 | 描述命令类型 | `COMMAND`: 描述特定命令 | 尚未实现 |
| `alias` | ❌ 未实现 | 低 | 为命令创建别名 | `NAME=VALUE`: 定义别名<br>`-p`: 打印所有定义的别名 | 尚未实现 |

### 补充命令

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `disown` | ✅ 已实现 | 中 | 从当前 shell 中移除作业 | `-a, --all`: 未给出 JOB_SPEC 时移除所有作业<br>`-h, --help`: 显示帮助<br>`-r, --running`: 仅移除正在运行的作业 | 作业移除 |
| `wait` | ✅ 已实现 | 中 | 等待进程结束 | `PID`: 要等待的进程 ID | 进程等待 |
| `export` | ✅ 已实现 | 中 | 设置环境变量 | 无选项 | 环境变量导出 |
| `unset` | ✅ 已实现 | 中 | 取消设置 shell 变量的值和属性 | 无选项 | 变量取消设置 |
| `logname` | ✅ 已实现 | 低 | 打印用户的登录名 | 无选项 | 登录名显示 |
| `whoami` | ✅ 已实现 | 低 | 打印有效用户 ID | 无选项 | 用户 ID 显示 |
| `id` | ✅ 已实现 | 中 | 打印用户和组信息 | `-a, --all`: 忽略，仅用于兼容其他版本<br>`-g, --group`: 仅打印有效组 ID<br>`-G, --groups`: 打印所有组 ID<br>`-n, --name`: 打印名称而不是数字<br>`-r, --real`: 打印真实 ID 而不是有效 ID<br>`-u, --user`: 仅打印有效用户 ID | 用户和组信息 |
| `users` | ✅ 已实现 | 低 | 打印当前登录用户的用户名 | 无选项 | 已登录用户显示 |
| `who` | ✅ 已实现 | 低 | 显示当前登录信息 | `-a, --all`: 等同于 -b -d --login -p -r -t -T -u<br>`-b, --boot`: 上次系统启动时间<br>`-d, --dead`: 打印死进程<br>`-H, --heading`: 打印列标题<br>`-l, --login`: 打印系统登录进程<br>`-m`: 仅显示 stdin 关联的主机名和用户<br>`-p, --process`: 打印 init 派生的活动进程<br>`-q, --count`: 打印登录名和在线人数<br>`-r, --runlevel`: 打印当前 runlevel<br>`-s, --short`: 仅打印名称、行和时间<br>`-t, --time`: 打印上次系统时钟变更时间<br>`-u, --users`: 列出已登录用户 | 用户信息 |
| `groups` | ✅ 已实现 | 低 | 打印用户所属的组 | `--help`: 显示帮助<br>`--version`: 输出版本信息 | 组信息 |
| `hostname` | ✅ 已实现 | 低 | 显示或设置系统主机名 | `-a, --alias`: 别名<br>`-A, --all-fqdns`: 所有长主机名<br>`-d, --domain`: DNS 域名<br>`-f, --fqdn`: FQDN<br>`-i, --ip-address`: 主机名地址<br>`-I, --all-ip-addresses`: 主机所有地址<br>`-s, --short`: 短主机名<br>`-y, --yp, --nis`: NIS/YP 域名 | 主机名显示 |
| `hostid` | ✅ 已实现 | 低 | 打印当前主机的数值标识 | 无选项 | 主机 ID 显示 |
| `uname` | ✅ 已实现 | 中 | 打印系统信息 | `-a, --all`: 打印全部信息<br>`-s, --kernel-name`: 打印内核名<br>`-n, --nodename`: 打印网络节点主机名<br>`-r, --kernel-release`: 打印内核版本<br>`-v, --kernel-version`: 打印内核版本字符串<br>`-m, --machine`: 打印机器硬件名<br>`-p, --processor`: 打印处理器类型<br>`-i, --hardware-platform`: 打印硬件平台<br>`-o, --operating-system`: 打印操作系统 | 系统信息 |
| `arch` | ✅ 已实现 | 低 | 打印机器架构 | 无选项 | 架构显示 |
| `uptime` | ✅ 已实现 | 中 | 显示系统运行了多久 | `-p, --pretty`: 以简洁格式显示运行时间<br>`-s, --since`: 显示系统启动时间<br>`-h, --help`: 显示帮助<br>`-V, --version`: 输出版本信息 | 系统运行时间 |
| `free` | ✅ 已实现 | 中 | 显示系统空闲和已用内存 | `-b, --bytes`: 以字节显示<br>`-k, --kilo`: 以 KB 显示<br>`-m, --mega`: 以 MB 显示<br>`-g, --giga`: 以 GB 显示<br>`--tera`: 以 TB 显示<br>`-h, --human`: 以人类可读格式显示<br>`-l, --lohi`: 显示 low/high memory 统计 | 内存使用显示 |
| `lsof` | ✅ 已实现 | 低 | 列出打开的文件 | 无选项 | 打开文件列表 |
| `nproc` | ✅ 已实现 | 低 | 打印可用处理单元数量 | `--all`: 打印已安装的处理单元数量<br>`--ignore=N`: 尽量排除 N 个处理单元 | 处理器数量 |
| `numfmt` | ✅ 已实现 | 低 | 在人类可读字符串和数值之间转换 | `-d, --delimiter=X`: 使用 X 代替空白作为分隔符<br>`-f, --format=FORMAT`: 使用 printf 风格浮点格式<br>`--from=UNIT`: 将输入自动缩放到 UNIT<br>`--to=UNIT`: 将输出自动缩放到 UNIT<br>`--round=METHOD`: 使用指定舍入方式<br>`--padding=N`: 输出填充到 N 个字符 | 数字格式化 |
| `column` | ✅ 已实现 | 低 | 列化列表 | `-c, --output-width=WIDTH`: 设置输出宽度<br>`-t, --table`: 识别表格列数<br>`-s, --separator SEPARATOR`: 指定输入分隔符<br>`-o, --output-separator STRING`: 指定表格输出列分隔符<br>`-x, --fillrows`: 先填充行再填充列 | 列格式化 |
| `jq` | ✅ 已实现 | 中 | 命令行 JSON 处理器 | `-c, --compact-output`: 紧凑输出<br>`-r, --raw-output`: 输出原始字符串<br>`-s, --slurp`: 将所有输入读入数组<br>`-R, --raw-input`: 读取原始字符串<br>`-M, --monochrome-output`: 不给 JSON 上色 | JSON 处理 |
| `mktemp` | ✅ 已实现 | 中 | 创建临时文件或目录 | `-d, --directory`: 创建目录而不是文件<br>`-u, --dry-run`: 不实际创建任何内容<br>`-q, --quiet`: 出错时静默失败<br>`-p, --tmpdir[=DIR]`: 将模板视为相对于 DIR<br>`-t`: 使用临时目录路径生成模板 | 临时文件/目录创建 |
| `mpicalc` | ✅ 已实现 | 低 | 简单任意精度计算器 | 无选项 | 任意精度计算 |
| `hmac256` | ✅ 已实现 | 低 | 计算 HMAC-SHA256 | `--help`: 显示帮助<br>`--version`: 输出版本信息 | HMAC 计算 |
| `pinky` | ✅ 已实现 | 低 | 轻量 finger | `-l`: 长格式输出<br>`-b`: 省略用户主目录和 shell<br>`-h`: 省略用户项目文件<br>`-p`: 省略用户计划文件<br>`-s`: 短格式输出 | 用户信息 |
| `seq` | ✅ 已实现 | 低 | 打印数字序列 | `-f, --format=FORMAT`: 使用 printf 风格浮点格式<br>`-s, --separator=STRING`: 用 STRING 分隔数字<br>`-w, --equal-width`: 通过补零对齐宽度 | 序列生成 |
| `sleep` | ✅ 已实现 | 低 | 延迟指定时间 | `NUMBER[SUFFIX]`: 暂停 NUMBER 秒 | 睡眠延迟 |
| `yes` | ✅ 已实现 | 低 | 重复输出字符串 | `STRING`: 要输出的字符串 | 重复输出 |
| `printf` | ✅ 已实现 | 高 | 格式化并打印数据 | `FORMAT`: 格式字符串<br>`ARGUMENTS`: 要格式化的参数 | 格式化输出 |

## 实现指南

### 一般结构

每个命令应遵循以下一般结构：

1. **模块声明**：以 `export module cmd.<command>;` 开头
2. **导入**：导入必要的模块（`import core;`、`import utils;` 等）
3. **常量命名空间**：定义命令特定的常量
4. **选项定义**：使用 `OPTION` 宏定义命令选项
5. **管道组件**：实现用于命令处理的管道组件
6. **命令实现**：实现主命令逻辑
7. **注册**：使用 `REGISTER_COMMAND` 宏注册命令

### 示例结构

```cpp
export module cmd.echo;

import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

namespace echo_constants {
    // 命令特定的常量可以在这里定义
}

// 定义选项
export auto constexpr ECHO_OPTIONS =
    std::array{
        OPTION("-n", "--no-newline", "不输出尾随换行符"),
        OPTION("-e", "--escape", "启用反斜杠转义的解释"),
    };

namespace echo_pipeline {
    namespace cp = core::pipeline;

    // 验证参数
    auto validate_arguments(std::span<const std::string_view> args) -> cp::Result<std::vector<std::string>> {
        std::vector<std::string> validated_args;
        for (auto arg : args) {
            validated_args.push_back(std::string(arg));
        }
        return validated_args;
    }

    // 从参数构建文本
    auto build_text(const std::vector<std::string>& args) -> std::string {
        std::string text;
        for (size_t i = 0; i < args.size(); ++i) {
            text += args[i];
            if (i < args.size() - 1) {
                text += " ";
            }
        }
        return text;
    }

    // 处理命令
    template<size_t N>
    auto process_command(const CommandContext<N>& ctx)
        -> cp::Result<std::string>
    {
        auto args_result = validate_arguments(ctx.positionals);
        if (!args_result) {
            return args_result.error();
        }
        auto args = *args_result;
        return build_text(args);
    }

}

REGISTER_COMMAND(echo,
                 /* name */
                 "echo",

                 /* synopsis */
                 "echo [SHORT-OPTION]... [STRING]...",

                 /* description */
                 "显示一行文本。",

                 /* examples */
                 "  echo Hello World      显示 'Hello World'\n"
                 "  echo -n Hello         显示 'Hello' 但不带尾随换行符\n"
                 "  echo -e Hello\\nWorld  在单独的行上显示 'Hello' 和 'World'",

                 /* see_also */
                 "cat, printf",

                 /* author */
                 "WinuxCmd Team",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 ECHO_OPTIONS
) {
    using namespace echo_pipeline;

    auto result = process_command(ctx);
    if (!result) {
        cp::report_error(result, L"echo");
        return 1;
    }

    auto text = *result;

    // 使用 CommandContext 获取选项
    bool no_newline = ctx.get<bool>("--no-newline", false);
    bool escape = ctx.get<bool>("--escape", false);

    // 如果启用，处理转义序列
    if (escape) {
        // 实现转义序列处理
    }

    // 输出结果
    std::cout << text;
    if (!no_newline) {
        std::cout << std::endl;
    }

    return 0;
}
```

### 最佳实践

1. **管道架构**：使用管道组件进行模块化处理
2. **类型安全**：使用 `CommandContext` 进行类型安全的选项访问
3. **错误处理**：使用 `core::pipeline::Result` 进行一致的错误处理
4. **文档**：为每个命令提供清晰的文档
5. **测试**：使用各种输入和选项测试命令
6. **性能**：在适当的地方优化性能
7. **兼容性**：尽可能遵循 POSIX 标准

### 性能优化

WinuxCmd 使用自定义容器优化性能：

#### SmallVector
栈分配的向量，具备小缓冲优化 (SBO)：
- 小规模 (< 64 元素) 时比 std::vector 快 5-10 倍
- 典型命令场景下减少 80%+ 的堆分配
- 超过容量时自动回退到堆分配

#### ConstexprMap
编译时哈希映射表，用于固定大小的键值对：
- 零初始化开销
- 运行时 O(1) 查找
- 完美适用于配置表和映射

**已优化的命令**：
- find, cat, env, mv, xargs, grep, sed, head, tail, tee, wc, uniq, which (使用 SmallVector)
- tail (使用 ConstexprMap 实现后缀乘数：K, M, G, T, P, E)

详细信息请参阅 [custom_containers.md](custom_containers.md)。

## 测试

每个命令应使用以下内容进行测试：

1. **基本功能**：测试没有选项的命令
2. **所有选项**：单独测试每个选项
3. **组合选项**：一起测试多个选项
4. **边缘情况**：测试空输入、大输入等
5. **错误情况**：测试无效输入和选项

## 迁移指南

### 从旧实现迁移

要将命令从旧实现迁移到新的基于管道的结构：

1. **更新模块声明**：使用 `export module cmd.<command>;`
2. **添加导入**：添加必要的导入（`import core;`、`import utils;`）
3. **定义选项**：使用 `OPTION` 宏定义选项
4. **创建管道命名空间**：实现管道组件
5. **更新命令实现**：使用 `CommandContext` 进行选项访问
6. **注册命令**：使用新的 `REGISTER_COMMAND` 宏

## 参考实现

`echo.cppm` 文件作为新的基于管道的架构的参考实现。它演示了：

1. **管道组件**：单独的验证和处理逻辑
2. **选项处理**：使用 `CommandContext` 进行类型安全的选项访问
3. **错误处理**：使用 `core::pipeline::Result` 进行错误报告
4. **命令注册**：正确使用 `REGISTER_COMMAND` 宏

所有新命令都应遵循此模式，以确保一致性和可维护性。

### GNU 兼容补充

下面这些命令在源码里已经存在，但主表里还没有完整收录；它们大多有 GNU Coreutils 对照页，适合作为当前一轮的补充清单。像 `getconf`、`cal`、`less`、`top`、`watch`、`cygpath`、`dos2unix`、`unix2dos`、`u2d`、`d2u`、`tzset`、`tree` 这类本地或第三方命令，仍按仓库本地工具另行维护。

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `chown` | ✅ 已实现 | 中 | 更改文件所有者和组 | `-R, --recursive`: 递归处理<br>`-v, --verbose`: 为每个处理的文件输出诊断 | Windows 上仅做状态报告和路径遍历，实际所有权变更仍受权限限制；GNU 语义还缺 `-c`、`-f`、`-h`、`-H`、`-L`、`-P`、`--dereference`、`--from`、`--preserve-root`、`--reference` |
| `install` | ✅ 已实现 | 中 | 复制文件并设置属性 | `-b, --backup`: 为已存在的目标文件创建备份<br>`-c`: 兼容旧 Unix 的占位参数<br>`-C`: 兼容旧 Unix 的占位参数<br>`-d, --directory`: 把所有参数当作目录名<br>`-D`: 创建 DEST 除最后一段外的所有前缀<br>`-g, --group`: 设置组所有权<br>`-m, --mode`: 设置权限模式<br>`-o, --owner`: 设置所有权<br>`-p, --preserve-timestamps`: 保留源文件时间戳<br>`-s, --strip`: 去掉符号表<br>`--strip-program`: 指定 strip 程序<br>`-S, --suffix`: 覆盖默认备份后缀<br>`-t, --target-directory`: 指定目标目录<br>`-T, --no-target-directory`: 不把最后一个参数特殊视为目录<br>`-v, --verbose`: 打印创建目录时的名称<br>`--preserve-context`: 保留 SELinux 安全上下文<br>`-Z`: 将目标文件的 SELinux 上下文设为默认值 | 目前更像文件复制+属性应用的简化实现，Windows 上的 owner/mode/SELinux 效果仍不完整；GNU 语义还要继续对齐 `-C`、`--compare`、`--debug`、`--backup=CONTROL` 等细节 |
| `link` | ✅ 已实现 | 中 | 创建硬链接 | `-v, --verbose`: 为每次链接输出信息 | GNU `link` 本身是无选项命令；当前实现把 `-v` 作为仓库本地扩展，并直接调用 Win32 硬链接 API |
| `nice` | ✅ 已实现 | 中 | 以修改后的调度优先级运行程序 | `-n, --adjustment=N`: 调整优先级增量 | 当前实现把 niceness 映射为 Windows 进程优先级类；GNU `nice` 还支持无命令时打印当前 niceness，且数值语义与调度优先级不同 |
| `nohup` | ✅ 已实现 | 中 | 让命令忽略挂断信号 | `-a, --append`: 追加输出到文件 | GNU `nohup` 本身没有常规选项；当前实现保留了一个本地 `-a` 扩展，并通过分离进程模拟挂断隔离 |
| `printenv` | ✅ 已实现 | 中 | 打印全部或部分环境变量 | `-0, --null`: 每行以 NUL 结束 | 目前支持全量枚举和指定变量输出；GNU 侧还要继续核对退出码和空值变量细节 |
| `stat` | ✅ 已实现 | 中 | 显示文件或文件系统状态 | `-L, --dereference`: 跟随符号链接<br>`-f, --file-system`: 显示文件系统状态而不是文件状态<br>`-c, --format`: 使用指定格式并自动追加换行<br>`-t, --terse`: 以简洁格式输出<br>`--printf`: 类似 `--format`，但解释反斜杠转义且不自动追加换行<br>常用格式字段：`%n`、`%N`、`%s`、`%b`、`%B`、`%F`、`%A`、`%a`、`%h`、`%i`、`%d`、`%D`、`%o`、`%u`、`%g`、`%U`、`%G`、`%x/%X`、`%y/%Y`、`%z/%Z`、`%w/%W` | 基于 Windows 文件状态输出；owner/group 和 symlink 细节是 Windows 近似语义 |
| `stdbuf` | ✅ 已实现 | 中 | 以修改后的缓冲方式运行命令 | `-i, --input`: 调整标准输入缓冲<br>`-o, --output`: 调整标准输出缓冲<br>`-e, --error`: 调整标准错误缓冲 | 现在只调整父进程侧缓冲并启动子进程；GNU `stdbuf` 还要把缓冲策略更准确地作用到被执行程序本身 |
| `timeout` | ✅ 已实现 | 中 | 以时间限制运行命令 | `-s, --signal`: 超时后发送的信号<br>`-k, --kill-after`: 超时后再等多久发送 KILL<br>`--foreground`: 允许受管命令使用前台 TTY<br>`--preserve-status`: 让退出码跟随被执行命令 | 现有实现基于 Windows 进程等待和终止；GNU 版本的信号名称、退出码约定和前台/后台细节还需要继续收敛 |
| `tty` | ✅ 已实现 | 低 | 打印标准输入连接的终端名 | `-s, --silent`: 只返回退出状态，不输出内容 | 当前实现检查控制台存在性并返回状态；GNU 还提供 `--quiet` 别名和更精确的退出状态划分 |
| `truncate` | ✅ 已实现 | 低 | 缩短或扩展文件大小 | `-c, --no-create`: 不创建文件<br>`-s, --size`: 设置或调整文件大小<br>`-r, --reference`: 以 RFILE 的大小为准 | `-o` 还未实现；GNU 的 size 解析、相对长度和 I/O 块语义还需要继续补 |
| `unlink` | ✅ 已实现 | 低 | 删除单个文件 | `-v, --verbose`: 为每个动作输出信息 | GNU `unlink` 基本是无选项命令；当前实现把 `-v` 作为本地扩展，并直接调用 Windows 删除 API |
| `test_bracket` | ✅ 已实现 | 高 | `[` 的内部实现入口 | 同 `test` | 这是 `test`/`[` 共享语法的内部命令壳，主要用于注册和分派，不应单独引入新的 GNU 语义 |
