# chroot 命令

## 状态

**Windows limitation**。WinuxCmd 注册该命令并验证根目录，但 Windows 没有 GNU chroot 的直接等价行为。

## 命令格式

`chroot NEWROOT [COMMAND [ARG]...]`


当前行为会报告平台不支持并返回 GNU 风格的失败状态；它不会改变当前进程的根目录。
