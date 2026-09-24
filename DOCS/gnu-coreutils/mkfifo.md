# mkfifo 命令

## 状态

**Windows limitation**。Win32 普通文件系统不提供 POSIX FIFO，因此 WinuxCmd 不会伪造 FIFO 文件。

## 命令格式

`mkfifo [选项]... NAME...`


当前行为会报告 FIFO 不受支持并以非零状态退出。
