# WinuxCmd 文档入口

这个目录是 WinuxCmd 的文档入口页。

## 先读这些

1. [GNU Coreutils 兼容性审计](zh/gnu_coreutils_parity.md)
2. [命令实现状态](zh/commands_implementation.md)
3. [工作区集成](zh/workspace_integration.md)
4. [TODO](zh/TODO_zh.md)

## 快速开始

1. 从项目 release 页面下载 Windows binary 压缩包。
2. 解压到仓库内，然后运行
   `.\scripts\setup-workspace-bin.ps1 -Source <解压后的二进制目录>`。
3. 在不污染全局 PATH 的情况下启用当前工作区：
   `.\scripts\activate-workspace.ps1`。
4. 可选的 AI shell 持久化设置：
   `.\scripts\install-workspace-profile-hook.ps1 -Quiet`。
5. Windows 上查 WinuxCmd 帮助时使用 `man.exe <command>`；裸 `man` 可能会
   命中 PowerShell alias。

release 包应同时附带 Windows binaries 和 `WinuxCmd-skill-v<version>.zip`，
这样 agent 可以把仓库内 skill 和工作区集成脚本一起安装使用。

## 规则

- GNU 参考只链接官方手册。
- 兼容性用矩阵维护，不直接复制手册正文。
- 只有显式接入 `contains_wildcard` / `glob_expand` 的命令才展开通配符。
- 对于 `diff`、`diff3`、`split`、`csplit` 这类固定参数个数的命令，
  通配符展开必须精确匹配命令期望的参数数量。
- 工作区激活不要污染全局 PATH。
- 在 Windows 上询问帮助时使用 `man.exe`。

## 当前重点

- 继续补 `ls`、`grep`、`head`、`tail`、`sed`、`find`、`xargs`、
  `install`、`timeout`、`stdbuf`、`chown`、`link`、`nice`、`nohup`、
  `printenv`、`tty`、`unlink` 的 GNU 兼容差异。
- GNU 兼容性审计要跟随批次更新；最近批次已覆盖 `sum`、`stat`、
  `readlink`、`realpath`、`truncate`、`comm`、`join`、`paste`、`nl`、
  `expand`、`unexpand`、`fold`、`fmt`。
- 命令指导要短、明确、可直接执行。
- 优先使用仓库内 skills，而不是临时环境改动。

## 入口索引

- [中文概览](zh/overview_zh.md)
- [英文概览](en/overview.md)
- [中文 TODO](zh/TODO_zh.md)
- [英文 TODO](en/TODO.md)
- GNU `find` action 参考：[`-exec ... ;`](https://www.gnu.org/software/findutils/manual/html_node/find_html/Single-File.html)、
  [`-exec ... {} +`](https://www.gnu.org/software/findutils/manual/html_node/find_html/Multiple-Files.html)
- GNU `xargs` 参考：[Invoking xargs](https://www.gnu.org/software/findutils/manual/html_node/find_html/Invoking-xargs.html)
