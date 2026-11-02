# mkfifo 命令

## 状态

**Emulated**（#1038，读侧会合 #1493）。Win32 文件系统没有 FIFO 节点类型，
WinuxCmd 用一个 7 字节 `!<fifo>` marker 文件（带 `FILE_ATTRIBUTE_SYSTEM`）
表示 FIFO——同一手法是 Cygwin 早期用于非原生符号链接的约定。WinuxCmd 命令
经共享打开层（`file_io::open_binary_file` / `create_binary_file`）打开
marker 时，会桥接到确定性命名管道
`\\.\pipe\winuxcmd-fifo-<canonical-path-hash>`：

- 读端 `CreateNamedPipeW` + `ConnectNamedPipe` 阻塞等待写者（POSIX
  `open(O_RDONLY)` 语义）；
- 写端 `CreateFileW` 循环等待读者出现（`open(O_WRONLY)` 语义）；
- 非 WinuxCmd 工具看到的是一个小 marker 文件。

`test -p` / `stat %F` / `find -type p` / `ls -l`（`p` 类型位）均识别
marker 为 FIFO。#1493 起，od/paste/comm/hexdump/nl/md5sum/sha1sum/
sha256sum/sha512sum/cksum 的操作数读与 wc 的字节数快路径统一走桥接
（此前会读出 7 个 marker 字节）。

已知边界（按 GNU 语义）：读无写者的 FIFO 会阻塞（可被 Ctrl-C / 外部
终止取消）；`ls -l` 的 size 列显示 marker 文件的 7 而非 GNU 的 0。

## 命令格式

`mkfifo [选项]... NAME...`

支持 `-m/--mode`（八进制与符号模式，`s`/`t` 位按 GNU 报错）与
`-Z/--context`（无 SELinux 内核上警告并忽略）。
