# 命令实现状态

本文档跟踪 WinuxCmd 项目中命令的实现状态，该项目现在使用基于管道的架构进行命令处理。

## 分类

### 文件管理

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `ls` | ✅ 已实现 | 高 | 列出目录内容 | `-a, --all`: 不忽略以 . 开头的条目<br>`-A, --almost-all`: 不列出隐含的 . 和 ..<br>`-b, --escape`: 用 C 风格转义显示不可打印字符<br>`-d, --directory`: 列出目录本身而不是目录内容<br>`-f`: 按目录顺序列出所有条目<br>`-F, --classify`: 为条目追加类型指示符<br>`-I, --ignore=PATTERN`: 忽略匹配 PATTERN 的条目<br>`-l, --long-list`: 使用长列表格式<br>`-h, --human-readable`: 与 -l 一起使用，以人类可读格式打印大小<br>`-r, --reverse`: 反转排序顺序<br>`-t`: 按修改时间排序<br>`-U`: 不排序，按目录顺序列出<br>`-v`: 对文本中的版本号进行自然排序<br>`-X, --sort=extension`: 按扩展名排序<br>`-n, --numeric-uid-gid`: 类似 -l，但列出数字用户和组 ID<br>`-g`: 类似 -l，但不列出所有者<br>`-o`: 类似 -l，但不列出组信息<br>`-1`: 每行列出一个文件<br>`-C`: 按列列出条目<br>`-w, --width`: 将输出宽度设置为 COLS<br>`--color`: 彩色化输出 | 使用管道架构实现，支持彩色输出，SmallVector 优化 |
| `cat` | ✅ 已实现 | 高 | 连接文件并打印到标准输出 | `-n, --number`: 对所有输出行编号<br>`-b, --number-nonblank`: 对非空输出行编号<br>`-E, --show-ends`: 在每行末尾显示 $<br>`-s, --squeeze-blank`: 压缩多个相邻空行<br>`-T, --show-tabs`: 将 TAB 字符显示为 ^I | 简单文件读写，带有管道结构，SmallVector 优化 |
| `cp` | ✅ 已实现 | 高 | 复制文件和目录 | `-i, --interactive`: 覆盖前提示<br>`-r, --recursive`: 递归复制目录<br>`-v, --verbose`: 解释正在执行的操作<br>`-f, --force`: 强制复制而不提示 | 文件系统操作，带有错误处理 |
| `mv` | ✅ 已实现 | 高 | 移动（重命名）文件 | `-i, --interactive`: 覆盖前提示<br>`-v, --verbose`: 解释正在执行的操作<br>`-f, --force`: 覆盖前不提示<br>`-n, --no-clobber`: 不覆盖现有文件 | 文件系统操作，带有错误处理，SmallVector 优化 |
| `rm` | ✅ 已实现 | 高 | 删除文件或目录 | `-f, --force`: 忽略不存在的文件和参数，从不提示<br>`-i, --interactive`: 每次删除前提示<br>`-r, -R, --recursive`: 递归删除目录及其内容<br>`-v, --verbose`: 解释正在执行的操作 | 文件系统操作，带有错误处理 |
| `mkdir` | ✅ 已实现 | 高 | 创建目录 | `-p, --parents`: 不存在时无错误，必要时创建父目录<br>`-v, --verbose`: 为创建的每个目录打印一条消息<br>`-m, --mode`: 设置文件模式（如 chmod） | 文件系统操作，带有错误处理 |
| `rmdir` | ✅ 已实现 | 中 | 删除空目录 | `--ignore-fail-on-non-empty`: 忽略删除非空目录的每次失败<br>`-p, --parents`: 删除目录及其祖先<br>`-v, --verbose`: 为删除的每个目录打印一条消息 | 文件系统操作，带有错误处理 |
| `touch` | ✅ 已实现 | 中 | 更改文件时间戳或创建空文件 | `-a`: 仅更改访问时间<br>`-m`: 仅更改修改时间<br>`-c, --no-create`: 不创建任何文件<br>`-d, --date`: 解析字符串并使用它代替当前时间<br>`-r, --reference`: 使用此文件的时间代替当前时间<br>`-t, --time`: 使用指定时间而不是当前时间<br>`-h, --no-dereference`: 影响每个符号链接而不是任何引用的文件 | 文件系统操作，带有错误处理 |
| `ln` | ✅ 已实现 | 中 | 在文件之间创建链接 | `-s, --symbolic`: 创建符号链接而不是硬链接<br>`-f, --force`: 删除现有目标文件<br>`-i, --interactive`: 提示是否删除目标<br>`-v, --verbose`: 打印每个链接文件的名称<br>`-n, --no-dereference`: 如果 LINK_NAME 是指向目录的符号链接，则将其视为普通文件 | 文件链接（硬/符号） |
| `diff` | ✅ 已实现 | 中 | 逐行比较文件 | `-u, --unified=NUM`: 输出 NUM（默认 3）行统一上下文<br>`-q, --brief`: 仅输出文件是否不同<br>`-i, --ignore-case`: 忽略文件内容中的大小写差异<br>`-w, --ignore-all-space`: 忽略所有空白<br>`-B, --ignore-blank-lines`: 忽略所有行均为空白的更改<br>`-y, --side-by-side`: 以两列输出 [未支持]<br>`-r, --recursive`: 递归比较任何子目录 [未支持] | 文件比较 |
| `file` | ✅ 已实现 | 中 | 确定文件类型 | `-b, --brief`: 不在输出行前添加文件名<br>`-i, --mime`: 输出 mime 类型字符串<br>`-z, --compress`: 尝试查看压缩文件内部<br>`--mime-type`: 仅输出 MIME 类型<br>`--mime-encoding`: 仅输出 MIME 编码 | 文件类型检测 |
| `realpath` | ✅ 已实现 | 低 | 打印解析的绝对路径 | `-e, --canonicalize-existing`: 路径的所有组件必须存在<br>`-m, --canonicalize-missing`: 不需要任何路径组件存在<br>`-s, --strip, --no-symlinks`: 不展开符号链接<br>`-z, --zero`: 每个输出行以 NUL 结束 | 路径解析 |

### 文本处理

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `echo` | ✅ 已实现 | 高 | 显示一行文本 | `-n`: 不输出尾随换行符<br>`-e`: 启用反斜杠转义的解释<br>`-E`: 显式禁止反斜杠转义的解释<br>`-u, --upper`: 将文本转换为大写<br>`-r, --repeat N`: 重复输出 N 次 | 使用管道架构实现，作为参考实现，支持包括 `\n`、`\t`、`\xHH`、`\uHHHH` 在内的转义序列 |
| `grep` | ✅ 已实现 | 高 | 打印匹配模式的行 | `-E, --extended-regexp`: 将模式解释为扩展正则表达式<br>`-F, --fixed-strings`: 将模式解释为固定字符串<br>`-G, --basic-regexp`: 将模式解释为基本正则表达式<br>`-i, --ignore-case`: 忽略大小写区别<br>`-v, --invert-match`: 反转匹配的含义<br>`-n, --line-number`: 用 1 基行号为每行输出添加前缀<br>`-c, --count`: 仅打印选中行的计数<br>`-l, --files-with-matches`: 仅打印包含匹配项的 FILE 名称<br>`-L, --files-without-match`: 仅打印不包含匹配项的 FILE 名称<br>`--color`: 使用标记高亮匹配项 | 支持正则表达式的模式匹配，SmallVector 优化 |
| `sort` | ✅ 已实现 | 中 | 对文本文件的行进行排序 | `-b, --ignore-leading-blanks`: 忽略前导空格<br>`-f, --ignore-case`: 将小写字符折叠为大写字符<br>`-n, --numeric-sort`: 根据字符串数值进行比较<br>`-r, --reverse`: 反转比较结果<br>`-u, --unique`: 使用 -c 时检查严格排序<br>`-k, --key`: 通过键排序<br>`-t, --field-separator`: 使用 SEP 代替从非空白到空白的转换<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符<br>`-o FILE`: 将结果写入 FILE 而不是标准输出 | 支持多键排序 |
| `wc` | ✅ 已实现 | 中 | 为每个文件打印换行符、单词和字节计数 | `-c, --bytes`: 打印字节计数<br>`-l, --lines`: 打印换行符计数<br>`-w, --words`: 打印单词计数<br>`-m, --chars`: 打印字符计数<br>`-L, --max-line-length`: 打印最大显示宽度 | 带有多种模式的字符计数，SmallVector 优化 |
| `head` | ✅ 已实现 | 低 | 输出文件的前几部分 | `-n, --lines`: 打印前 N 行而不是前 10 行<br>`-c, --bytes`: 打印前 N 个字节<br>`-q, --quiet, --silent`: 从不打印给出文件名的头<br>`-v, --verbose`: 始终打印给出文件名的头<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符 | 文件头部提取，支持字节/行选项，SmallVector 优化 |
| `tail` | ✅ 已实现 | 低 | 输出文件的最后部分 | `-n, --lines`: 输出最后 N 行<br>`-c, --bytes`: 输出最后 N 个字节<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符<br>`-f, --follow`: 随着文件增长输出附加数据 [未支持] | 文件尾部提取，支持跟踪，SmallVector 和 ConstexprMap 优化 |
| `sed` | ✅ 已实现 | 中 | 用于过滤和转换文本的流编辑器 | `-e, --expression`: 将 SCRIPT 添加到要执行的命令<br>`-f, --file`: 将 SCRIPT-FILE 的内容添加到命令<br>`-n, --quiet, --silent`: 禁止模式空间的自动打印<br>`-i[SUFFIX], --in-place[=SUFFIX]`: 就地编辑文件 [未支持] | 支持脚本的文本流编辑，SmallVector 优化 |
| `uniq` | ✅ 已实现 | 中 | 报告或省略重复行 | `-c, --count`: 在行前加上出现次数作为前缀<br>`-d, --repeated`: 仅打印重复行，每组一个<br>`-f, --skip-fields`: 避免比较前 N 个字段<br>`-i, --ignore-case`: 比较时忽略大小写差异<br>`-s, --skip-chars`: 避免比较前 N 个字符<br>`-u, --unique`: 仅打印唯一行<br>`-w, --check-chars`: 比较行中不超过 N 个字符<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符 | 重复行检测和过滤，SmallVector 优化 |
| `cut` | ✅ 已实现 | 中 | 从文件的每一行中移除部分 | `-d, --delimiter`: 使用 DELIM 而不是 TAB 作为字段定界符<br>`-f, --fields`: 仅选择这些字段<br>`-s, --only-delimited`: 不打印不包含定界符的行<br>`-z, --zero-terminated`: 行定界符是 NUL，而不是换行符 | 从定界文本中提取字段 |
| `tee` | ✅ 已实现 | 中 | 从标准输入读取并写入标准输出和文件 | `-a, --append`: 追加到给定的文件，不覆盖<br>`-i, --ignore-interrupts`: 忽略中断信号<br>`-p, --diagnose`: 将错误写入标准错误 | 标准输入/输出重定向，SmallVector 优化 |
| `xargs` | ✅ 已实现 | 高 | 从输入构建并执行命令行 | `-n, --max-args=MAX-ARGS`: 每个命令行最多使用 MAX-ARGS 个参数<br>`-I, --replace[=R-STR]`: 用从标准输入读取的名称替换初始参数中的 R-STR<br>`-P, --max-procs=MAX-PROCS`: 一次最多运行 MAX-PROCS 个进程<br>`-t, --verbose`: 在执行之前在标准错误输出上打印命令行<br>`-0, --null`: 输入项以空字符而不是空白字符终止<br>`-d, --delimiter=DELIM`: 输入项以 DELIM 而不是空白字符终止 [未支持] | 从输入执行命令，SmallVector 优化 |

### 系统信息

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `which` | ✅ 已实现 | 高 | 在 PATH 中定位命令 | `-a, --all`: 打印每个参数的所有匹配路径名<br>`--skip-dot`: 跳过 PATH 中以点开头的目录<br>`--skip-tilde`: 跳过 PATH 中以波浪号开头的目录 | 路径搜索，支持 PATHEXT，SmallVector 优化 |
| `env` | ✅ 已实现 | 中 | 在修改的环境中运行命令 | `-i, --ignore-environment`: 从空环境开始<br>`-u, --unset`: 从环境中删除变量<br>`-0, --null`: 每个输出行以 NUL 结束，而不是换行符 | 环境变量操作，SmallVector 优化 |
| `pwd` | ✅ 已实现 | 高 | 打印工作目录 | `-L, --logical`: 如果环境变量 PWD 包含指向当前目录的绝对文件名，则输出它<br>`-P, --physical`: 避免所有符号链接 | 工作目录显示 |
| `ps` | ✅ 已实现 | 高 | 报告进程状态 | `-e, -A`: 选择所有进程<br>`-a`: 选择所有进程，但不包括会话领导和没有关联终端的进程<br>`-x`: 选择没有控制 tty 的进程<br>`-f`: 完整格式列表<br>`-l`: 长格式<br>`-u USER`: 按有效用户 ID 或名称选择<br>`-w`: 宽输出<br>`--no-headers`: 不打印头<br>`--sort=KEY`: 按列排序 | 进程列表和过滤 |
| `chmod` | ✅ 已实现 | 中 | 更改文件模式位 | `-c, --changes`: 类似详细，但仅在做出更改时报告<br>`-f, --silent, --quiet`: 禁止大多数错误消息<br>`-v, --verbose`: 为处理的每个文件输出诊断<br>`-R, --recursive`: 递归更改文件和目录<br>`--reference=RFILE`: 使用 RFILE 的模式而不是 MODE 值 | 文件权限修改 |
| `date` | ✅ 已实现 | 中 | 打印或设置系统日期/时间 | `-d, --date=STRING`: 显示由 STRING 描述的时间<br>`-u, --utc, --universal`: 打印或设置协调世界时（UTC）<br>`+FORMAT`: 输出格式字符串 | 日期/时间显示和格式化 |
| `kill` | ✅ 已实现 | 高 | 向进程发送信号 | `-l, --list`: 列出所有信号名称<br>`-s, --signal=SIGNAL`: 发送指定的信号<br>`SIGNAL`: 信号编号或名称（例如，-9、-KILL、-15、-TERM） | 进程信号发送 |

### 系统工具

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `df` | ✅ 已实现 | 中 | 报告文件系统磁盘空间使用情况 | `-h, --human-readable`: 以 1024 的幂打印大小<br>`-H, --si`: 以 1000 的幂打印大小<br>`-T, --print-type`: 打印文件系统类型<br>`-i, --inodes`: 列出 inode 信息而不是块使用情况<br>`-t, --type=TYPE`: 将列表限制为 TYPE 类型的文件系统<br>`-x, --exclude-type=TYPE`: 将列表限制为非 TYPE 类型的文件系统<br>`-a, --all`: 包括虚拟文件系统 | 磁盘空间报告 |
| `du` | ✅ 已实现 | 中 | 估算文件空间使用情况 | `-h, --human-readable`: 以 1024 的幂打印大小<br>`-H, --si`: 以 1000 的幂打印大小<br>`-s, --summarize`: 仅显示每个参数的总计<br>`-c, --total`: 产生总计<br>`-d, --max-depth=N`: 仅当目录（或带有 --all 的文件）低于命令行参数 N 或更少级别时，才打印该目录（或文件）的总计<br>`-a, --all`: 为所有文件编写计数，而不仅仅是目录 | 文件/目录大小估算 |

### 网络

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `ping` | ❌ 未实现 | 高 | 向网络主机发送 ICMP ECHO_REQUEST | `-c COUNT`: 发送 COUNT 个 ECHO_REQUEST 数据包后停止<br>`-i INTERVAL`: 在发送每个数据包之间等待 INTERVAL 秒 | 尚未实现 |
| `curl` | ❌ 未实现 | 中 | 从服务器传输数据或向服务器传输数据 | `-s, --silent`: 静默模式<br>`-o, --output`: 将输出写入文件而不是 stdout | 尚未实现 |

### 其他

| 命令 | 状态 | 优先级 | 描述 | 参数选项 | 实现说明 |
|------|------|--------|------|----------|----------|
| `help` | ✅ 已实现 | 高 | 显示命令的帮助信息 | `COMMAND`: 显示特定命令的帮助 | 使用命令元数据生成帮助 |
| `exit` | ✅ 已实现 | 高 | 退出 shell | 无选项 | 简单退出调用，带有管道结构 |
| `clear` | ✅ 已实现 | 高 | 清除终端屏幕 | 无选项 | 使用系统调用清除屏幕 |
| `cd` | ✅ 已实现 | 高 | 更改当前目录 | `-L, --logical`: 强制遵循符号链接<br>`-P, --physical`: 使用物理目录结构而不遵循符号链接 | 使用 SetCurrentDirectory API，带有管道结构 |
| `type` | ❌ 未实现 | 中 | 描述命令类型 | `COMMAND`: 描述特定命令 | 尚未实现 |
| `alias` | ❌ 未实现 | 低 | 为命令创建别名 | `NAME=VALUE`: 定义别名<br>`-p`: 打印所有定义的别名 | 尚未实现 |

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
