# GNU Coreutils 参数参考手册

本目录包含 WinuxCmd 已实现的 GNU Coreutils 命令的参数参考文档。

## 文档结构

每个命令一个 Markdown 文件，包含：
- 命令概述
- 完整参数列表
- 参数详细说明
- 使用示例
- WinuxCmd 实现状态

## 命令分类

### 文件输出 (Output of entire files)
- [cat](cat.md) - 连接文件并输出
- [tac](tac.md) - 反向连接文件并输出
- [nl](nl.md) - 为文件添加行号
- [od](od.md) - 八进制或其他格式输出文件
- [base32](base32.md) - Base32 编码/解码
- [base64](base64.md) - Base64 编码/解码
- [basenc](basenc.md) - 多种编码/解码

### 格式化 (Formatting file contents)
- [fmt](fmt.md) - 重新格式化段落文本
- [pr](pr.md) - 为打印分页或分栏
- [fold](fold.md) - 将输入行包装为指定宽度

### 部分输出 (Output of parts of files)
- [head](head.md) - 输出文件开头部分
- [tail](tail.md) - 输出文件末尾部分
- [split](split.md) - 将文件分割为多个部分
- [csplit](csplit.md) - 根据上下文分割文件

### 文件汇总 (Summarizing files)
- [wc](wc.md) - 统计行数、单词数和字节数
- [sum](sum.md) - 打印校验和和块计数
- [cksum](cksum.md) - 打印和验证文件校验和
- [md5sum](md5sum.md) - 打印或检查 MD5 摘要
- [b2sum](b2sum.md) - 打印或检查 BLAKE2 摘要
- [sha1sum](sha1sum.md) - 打印或检查 SHA-1 摘要
- [sha224sum](sha224sum.md) - 打印或检查 SHA-224 摘要
- [sha256sum](sha256sum.md) - 打印或检查 SHA-256 摘要
- [sha384sum](sha384sum.md) - 打印或检查 SHA-384 摘要
- [sha512sum](sha512sum.md) - 打印或检查 SHA-512 摘要

### 排序操作 (Operating on sorted files)
- [sort](sort.md) - 排序文本文件
- [shuf](shuf.md) - 随机打乱文本
- [uniq](uniq.md) - 报告或省略重复行
- [comm](comm.md) - 逐行比较两个排序文件
- [ptx](ptx.md) - 生成置换索引
- [tsort](tsort.md) - 拓扑排序

### 字段操作 (Operating on fields)
- [cut](cut.md) - 打印选定部分的行
- [paste](paste.md) - 合并文件行
- [join](join.md) - 在公共字段上连接行

### 字符操作 (Operating on characters)
- [tr](tr.md) - 转换、压缩和/或删除字符
- [expand](expand.md) - 将制表符转换为空格
- [unexpand](unexpand.md) - 将空格转换为制表符

### 目录列表 (Directory listing)
- [ls](ls.md) - 列出目录内容

### 基本操作 (Basic operations)
- [cp](cp.md) - 复制文件和目录
- [dd](dd.md) - 转换和复制文件
- [install](install.md) - 复制文件并设置属性
- [mv](mv.md) - 移动（重命名）文件
- [rm](rm.md) - 删除文件或目录
- [shred](shred.md) - 安全删除文件

### 特殊文件类型 (Special file types)
- [link](link.md) - 通过 link 系统调用创建硬链接
- [ln](ln.md) - 创建文件链接
- [mkdir](mkdir.md) - 创建目录
- [readlink](readlink.md) - 打印符号链接值或规范文件名
- [rmdir](rmdir.md) - 删除空目录
- [unlink](unlink.md) - 通过 unlink 系统调用删除文件

### 更改文件属性 (Changing file attributes)
- [chown](chown.md) - 更改文件所有者和组
- [chmod](chmod.md) - 更改访问权限
- [touch](touch.md) - 更改文件时间戳

### 文件空间使用 (File space usage)
- [df](df.md) - 报告文件系统空间使用情况
- [du](du.md) - 估算文件空间使用
- [stat](stat.md) - 报告文件或文件系统状态
- [sync](sync.md) - 将缓存写入同步到持久存储
- [truncate](truncate.md) - 缩小或扩展文件大小

### 打印文本 (Printing text)
- [echo](echo.md) - 打印一行文本
- [printf](printf.md) - 格式化和打印数据
- [yes](yes.md) - 重复打印字符串直到中断

### 条件 (Conditions)
- [false](false.md) - 不做任何操作，失败退出
- [true](true.md) - 不做任何操作，成功退出
- [test](test.md) - 检查文件类型和比较值
- [expr](expr.md) - 计算表达式

### 重定向 (Redirection)
- [tee](tee.md) - 将输出重定向到多个文件或进程

### 文件名操作 (File name manipulation)
- [basename](basename.md) - 从文件名中去除目录和后缀
- [dirname](dirname.md) - 去除最后一个文件名组件
- [pathchk](pathchk.md) - 检查文件名有效性和可移植性
- [mktemp](mktemp.md) - 创建临时文件或目录
- [realpath](realpath.md) - 打印解析后的文件名

### 工作上下文 (Working context)
- [pwd](pwd.md) - 打印工作目录
- [printenv](printenv.md) - 打印所有或部分环境变量
- [tty](tty.md) - 打印连接到标准输入的终端文件名

### 用户信息 (User information)
- [id](id.md) - 打印用户身份
- [logname](logname.md) - 打印当前登录名
- [whoami](whoami.md) - 打印有效用户名称
- [groups](groups.md) - 打印用户所属的组名
- [users](users.md) - 打印当前登录用户的登录名
- [who](who.md) - 打印当前登录的用户
- [pinky](pinky.md) - 打印用户信息

### 系统上下文 (System context)
- [date](date.md) - 打印或设置系统日期和时间 ✓
- [arch](arch.md) - 打印机器硬件名称 ✓
- [nproc](nproc.md) - 打印可用处理器数量 ✓
- [uname](uname.md) - 打印系统信息 ✓
- [hostname](hostname.md) - 打印或设置系统名称 ✓
- [hostid](hostid.md) - 打印数字主机标识符 ✓
- [uptime](uptime.md) - 打印系统运行时间和负载 ✓

### 修改命令调用 (Modified command invocation)
- [env](env.md) - 在修改的环境中运行命令
- [nice](nice.md) - 以修改的优先级运行命令
- [nohup](nohup.md) - 运行不受挂断影响的命令
- [stdbuf](stdbuf.md) - 以修改的 I/O 流缓冲运行命令
- [timeout](timeout.md) - 以时间限制运行命令

### 进程控制 (Process control)
- [kill](kill.md) - 向进程发送信号

### 延迟 (Delaying)
- [sleep](sleep.md) - 延迟指定时间

### 数值操作 (Numeric operations)
- [factor](factor.md) - 打印质因数
- [numfmt](numfmt.md) - 重新格式化数字
- [seq](seq.md) - 打印数字序列

## 通用选项

所有 GNU Coreutils 命令都支持以下通用选项：

- `--help` - 显示帮助信息并退出
- `--version` - 输出版本信息并退出

### 备份选项
某些命令支持备份相关选项：
- `-b` / `--backup[=METHOD]` - 备份每个将被覆盖或删除的文件
- `-S SUFFIX` / `--suffix=SUFFIX` - 备份文件后缀

### 块大小
- `--block-size=SIZE` - 在打印前按 SIZE 缩放大小

### 目标目录
- `-t DIRECTORY` / `--target-directory=DIRECTORY` - 指定目标目录
- `-T` / `--no-target-directory` - 不将最后一个操作数特殊对待

### 符号链接遍历
- `-H` - 如果命令行参数是符号链接，则跟随它
- `-L` - 遍历每个遇到的符号链接
- `-P` - 不遍历任何符号链接

## 环境变量

- `LANG` - 语言环境
- `LC_ALL` - 覆盖所有语言环境设置
- `LC_COLLATE` - 排序顺序
- `LC_CTYPE` - 字符类型
- `LC_MESSAGES` - 消息语言
- `TMPDIR` - 临时文件目录
- `POSIXLY_CORRECT` - 启用 POSIX 严格模式

## 退出状态

- `0` - 成功
- `1` - 小问题
- `2` - 严重错误

## 参考链接

- [GNU Coreutils 官方文档](https://www.gnu.org/software/coreutils/manual/)
- [GNU Coreutils 9.11 手册](https://www.gnu.org/software/coreutils/manual/html_node/index.html)
