# WinuxCmd

[English](README.md) | 中文

> 让 GNU 风格命令在真正的 Windows shell 里顺手工作。

![GitHub release (latest by date)](https://img.shields.io/github/v/release/caomengxuan666/WinuxCmd)
![GitHub all releases](https://img.shields.io/github/downloads/caomengxuan666/WinuxCmd/total)
![GitHub stars](https://img.shields.io/github/stars/caomengxuan666/WinuxCmd)
![GitHub license](https://img.shields.io/github/license/caomengxuan666/WinuxCmd)
![Windows Support](https://img.shields.io/badge/platform-Windows-blue)

WinuxCmd 把 `grep`、`sed`、`find`、`xargs`、`ls`、`cat`、`rm` 这一类 GNU 风格命令带回 Windows，而且是以适合 PowerShell、`cmd`、Windows Terminal 的方式落地，而不是要求你先离开它们。

它面向的是一个很真实的场景：AI、文档、CI 日志、issue 评论、旧 shell 习惯，都会不断给你 Linux 命令；但你眼前真正要操作的机器，仍然是 Windows。

![Windows Terminal](DOCS/images/WindowsTerminal.png)

## 安装

WinuxCmd 已经进入 winget：

```powershell
winget install caomengxuan666.WinuxCmd
```

如果你更喜欢 zip 包，也可以直接从 [Releases](https://github.com/caomengxuan666/WinuxCmd/releases) 下载最新版本。

它同样适合那些依旧只有 Windows PowerShell 5.1 的保守环境，所以对很多 AI sandbox 和自动化宿主也比较友好。

## 为什么是 WinuxCmd

- 原生 Windows 优先，不是 WSL，不是虚拟机，也不是换 shell。
- 重点不是“模拟 Linux”，而是让 Windows 原生命令和 GNU 风格过滤链自然共存。
- 项目从 2026 年 1 月 23 日开始，早于微软当前那条 Windows coreutils 仓库在 2026 年 5 月 15 日出现。
- 对 Windows PowerShell 5.1 这类现实环境更友好，包括很多 AI sandbox 和 Codex 这类只给你 PWSH5 的场景。

这也是和微软当前 [Coreutils for Windows](https://github.com/microsoft/coreutils) 路线的根本差异之一：它们的 README 明确要求 PowerShell 7.4 或更新版本，而 WinuxCmd 从一开始就考虑了 PowerShell 5.1 宿主里的可用性。

## 它的实际体验

```bash
netstat -ano | grep 8080
tasklist | grep -i chrome
ipconfig | grep -i ipv4
dir /b | grep -E "\.cpp$" | sort | uniq
find . -name "*.cpp" -print0 | xargs -0 grep -n TODO
```

WinuxCmd 不是把 Windows 伪装成 Linux，而是把你熟悉的 GNU 文本工作流真正接到 Windows 终端里。

## 怎么用最顺手

在当前 PowerShell 会话里优先使用 WinuxCmd 命令名：

```powershell
winux activate
cat README.md
rm old.txt
man ls
```

恢复 PowerShell 原始行为：

```powershell
winux deactivate
```

如果你希望每次打开交互式 PowerShell 都自动激活，把这段放到 `$PROFILE` 里的 `winux` wrapper 后面：

```powershell
if (Get-Command winux -ErrorAction SilentlyContinue) {
    winux activate 6>$null
}
```

## 它适合哪些场景

WinuxCmd 很适合下面这些情况：

- AI 给了 GNU 风格 one-liner，但你的宿主系统是 Windows。
- 你想用 `grep | sort | xargs | find` 这类链路，但不想专门打开 WSL。
- 你希望 `tasklist`、`netstat`、`sc`、`ipconfig` 这些原生命令也能进入熟悉的文本过滤流程。
- 你的环境比较旧或受限，真正拿到手的仍然是 Windows PowerShell 5.1。

## 命令覆盖

WinuxCmd 目前已经实现 148 个命令，覆盖了这些高频能力：

- 文件工具：`ls`、`cp`、`mv`、`rm`、`mkdir`、`ln`、`stat`、`readlink`、`realpath`
- 文本工具：`cat`、`grep`、`sed`、`sort`、`uniq`、`cut`、`head`、`tail`、`wc`
- 搜索与组合：`find`、`xargs`
- Windows 常用补位：`ps`、`lsof`、`which`、`tree`、`hexdump`、`strings`

如果你关心具体实现了哪些参数、哪些 GNU 语义已经对齐，详细清单在这里：

- [命令兼容性矩阵 (ZH)](DOCS/zh/commands_implementation.md)
- [GNU 兼容清单 (ZH)](DOCS/zh/gnu_coreutils_parity.md)
- [Command Compatibility Matrix (EN)](DOCS/en/commands_implementation_en.md)
- [GNU Parity Ledger (EN)](DOCS/en/gnu_coreutils_parity.md)

## Windows 侧说明

- 未识别命令会回退给父 shell，所以 WinuxCmd 是补足 PowerShell 和 `cmd`，不是替换它们。
- 执行 `winux activate` 之后，`man` 可以在当前 PowerShell 会话里直接覆盖 PowerShell 自带映射。
- 如果你主要在交互式 `cmd` 下用它，优先考虑让 Windows Terminal 以 `%SystemRoot%\System32\cmd.exe /k winuxcmd` 启动，而不是配置全局 `AutoRun`。

## 更多

- [贡献指南（英文）](CONTRIBUTING.md)
- [贡献指南（中文）](CONTRIBUTING_ZH.MD)
- [构建模式文档](DOCS/zh/build_modes.md)
- [自定义容器与基准说明](DOCS/en/custom_containers.md)
- [仓库内 AI 工作说明](skills/winuxcmd/SKILL.md)
