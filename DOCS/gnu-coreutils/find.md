# find 命令

## 状态

**Partial**。WinuxCmd 支持常用的 `-type f`、`-type d` 和 `-type l`，但 Windows 上没有 GNU block device、character device、FIFO 或 Unix socket 的直接文件类型语义。

## 已支持的类型

- `-type f`：普通文件
- `-type d`：目录
- `-type l`：符号链接

## 平台限制

`-type b`、`-type c`、`-type p` 和 `-type s` 不应被视为已实现；当前会报告不支持的类型并失败。
