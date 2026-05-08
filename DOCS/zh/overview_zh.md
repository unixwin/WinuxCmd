# WinuxCmd 概览

## WinuxCmd 是什么

WinuxCmd 是一个原生 Windows 命令行工具集，让你在不依赖 WSL 的情况下使用常见 Linux 风格命令。

核心目标：

- 保持 Linux 命令使用习惯
- 保持 Windows 原生运行与轻量部署
- 提升 AI 生成命令在 Windows 上的可执行性

## 核心差异点

1. 原生 Windows 运行
2. Linux 风格命令体验
3. Windows 工具与 Linux 风格命令可直接管道
4. 二进制小、启动快

## Shell 行为（重点）

WinuxCmd REPL 具备“内置命令 + 原生回退”机制。

- 命中内置命令：内部执行
- 未命中内置命令：回退到原生 shell
- 回退 shell 根据进入来源自动选择：
  - 从 PowerShell/pwsh 进入 -> 回退到 PowerShell
  - 从 cmd 进入 -> 回退到 cmd

因此，从 PowerShell 进入时，`Get-Process` / `Where-Object` 可以正常执行。

## 自动补全

用户补全文件：

![自动补全演示](../images/auto.gif)

- 默认：`%USERPROFILE%\.winuxcmd\completions\user-completions.txt`
- 环境变量覆盖：`WINUXCMD_COMPLETION_FILE`

格式：

```text
cmd|<command>|<description>
opt|<command>|<option>|<description>
```

## Shell 集成文档

- [中文 Shell 集成说明](winux_shell_integration_zh.md)
- [English Shell Integration](../en/winux_shell_integration_en.md)

## 命令矩阵

- [命令兼容性矩阵](commands_implementation.md)

## 构建与测试

- 构建模式：[build_modes.md](build_modes.md)
- 测试框架：[testing_framework.md](testing_framework.md)
- 工作区集成：[workspace_integration.md](workspace_integration.md)
- AI 使用指南：[../../skills/winuxcmd/SKILL.md](../../skills/winuxcmd/SKILL.md)
- GNU coreutils 兼容性审计：[gnu_coreutils_parity.md](gnu_coreutils_parity.md)

## 贡献现状

高价值贡献方向：

1. Shell 兼容性修复
2. REPL / fallback 回归测试
3. 文档与示例改进
