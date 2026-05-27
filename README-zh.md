# WinuxCmd：为 Windows 带来 Linux 命令 × 跨平台管道

[English](README.md) | 中文

> 原生 Windows 实现的 Linux 命令集 | 仅 ~900KB | 无需 WSL/WSL2 | Windows 命令 × Linux 过滤器无缝互通

![GitHub release (latest by date)](https://img.shields.io/github/v/release/caomengxuan666/WinuxCmd)
![GitHub all releases](https://img.shields.io/github/downloads/caomengxuan666/WinuxCmd/total)
![GitHub stars](https://img.shields.io/github/stars/caomengxuan666/WinuxCmd)
![GitHub license](https://img.shields.io/github/license/caomengxuan666/WinuxCmd)
![Windows Support](https://img.shields.io/badge/platform-Windows-blue)

## ⭐ Star 趋势

[![Star History Chart](https://api.star-history.com/svg?repos=caomengxuan666/WinuxCmd&type=date&legend=top-left)](https://www.star-history.com/#caomengxuan666/WinuxCmd&type=date&legend=top-left)

## ✨ 核心亮点

| | 特性 | 说明 |
|---|---|---|
| 🔗 | **Windows × Linux 管道直连** | `netstat -ano \| grep 8080` 开箱即用，Windows 工具输出直接喂给 Linux 过滤器 |
| ⚡ | **极速启动** | 完整命令执行通常在毫秒级，日常链式命令体感明显更快 |
| 🪶 | **超轻量** | 单个可执行文件约 900KB，按需链接命令，部署简单 |
| 🤖 | **AI 友好** | AI 生成的 Linux 命令在 Windows 上可直接落地，减少手工改写 |
| 🔍 | **智能补全** | 内置命令 + Windows 系统命令 + 用户自定义补全，Tab 键即达 |
| 🚫 | **无需管理员** | 安装与运行均不强制提权 |
| 📦 | **无外部依赖** | 纯 Win32 API，默认无额外运行时负担 |

---

## 🚀 快速开始

### 一键安装（推荐）

```powershell
# 在 PowerShell 中运行（无需管理员权限）
irm https://dl.caomengxuan666.com/install.ps1 | iex
```

### 手动安装

1. 从 [Releases](https://github.com/caomengxuan666/WinuxCmd/releases) 下载
2. 解压到任意目录，进入 `bin` 目录
3. 运行 `create_links.ps1` 生成命令链接
4. 将 `bin` 目录加入 PATH

```powershell
.\create_links.ps1                    # NTFS 文件系统（推荐）
.\create_links.ps1 -UseSymbolicLinks  # 非 NTFS 文件系统
.\create_links.ps1 -Remove            # 移除全部链接
```

### Shell 集成

**PowerShell**：把以下内容加入 `$PROFILE`，仅在交互式终端自动进入 WinuxCmd。

```powershell
# 自动进入 WinuxCmd 交互环境
$cliArgs = [Environment]::GetCommandLineArgs() | ForEach-Object { $_.ToLowerInvariant() }
$isNonInteractiveLaunch = ($cliArgs -contains '-command') -or ($cliArgs -contains '-c') -or ($cliArgs -contains '-file') -or ($cliArgs -contains '-f')
$isRealTerminal = $env:WT_SESSION -or $env:TERM_PROGRAM
if ($Host.Name -eq 'ConsoleHost' -and -not $isNonInteractiveLaunch `
    -and $env:WINUXCMD_BOOTSTRAPPED -ne '1' -and $isRealTerminal) {
    $env:WINUXCMD_BOOTSTRAPPED = '1'
    $winuxExe = (Get-Command winuxcmd -ErrorAction SilentlyContinue).Source
    if (-not $winuxExe) {
        $devExe = 'your\winuxcmd.exe\path'  # 替换为你的实际路径
        if (Test-Path $devExe) { $winuxExe = $devExe }
    }
    if ($winuxExe -and (Test-Path $winuxExe)) { & $winuxExe }
}
```

请将 `$devExe` 替换为你的本地 `winuxcmd.exe` 实际路径。

**CMD / Windows Terminal**：
如果你是从普通命令行进入，请手动输入 `winuxcmd` 进入补全环境。
不建议使用 cmd 的 `AutoRun`（注册表自动运行钩子）：它会影响 `cmd /c` 等后台任务，导致非交互场景也触发 WinuxCmd。
因此更推荐使用 `Windows Terminal`，并将 cmd 启动命令设置为：

```bat
%SystemRoot%\System32\cmd.exe /k winuxcmd
```

![Windows Terminal](DOCS/images/WindowsTerminal.png)

### 自动补全

![自动补全演示](DOCS/images/auto.gif)

内置三层补全，Tab 键直达：

- WinuxCmd 命令及其参数
- Windows 系统命令白名单（含说明）
- 用户自定义扩展：`%USERPROFILE%\.winuxcmd\completions\user-completions.txt`
- 通过环境变量覆盖补全文件：`WINUXCMD_COMPLETION_FILE`

```text
# 用户补全文件格式
cmd|git|Distributed version control
opt|git|pull|Fetch from and integrate with another repository
```

> 详见 [补全与行为概览](DOCS/zh/overview_zh.md)

---

## 🔌 FFI API

FFI 代码暂时保留在仓库里，作为后续实验入口；但当前默认构建和 release
都不包含 FFI。

- 默认构建保持 `BUILD_FFI=OFF`
- release 相关构建模式会强制关闭 FFI
- 当前 release 流程不发布 FFI 二进制产物

`src/ffi` 和示例代码可以继续作为参考，但除非明确恢复这条路线，否则先视为未启用。

## 工作区集成

如果希望 WinuxCmd 只在当前仓库内可用，不污染全局 `PATH`，使用仓库本地激活脚本：

```powershell
.\scripts\activate-workspace.ps1
man.exe ls
winuxcmd.exe help
```

如果希望在这个仓库中新开的交互式 PowerShell 也自动生效，可以安装可选 profile hook：

```powershell
.\scripts\install-workspace-profile-hook.ps1
```

面向 AI 的本地使用说明在 `skills/winuxcmd/SKILL.md`。
GitHub release 还会附带独立的 `WinuxCmd-skill-v<version>.zip` 包，和
Windows 二进制一起发布。

---

## 📦 已实现命令（148 个）

> 完整参数列表见 [命令实现文档](DOCS/zh/commands_implementation.md)

支持 48 个命令的通配符（glob）功能：

| 命令 | 描述 | 命令 | 描述 |

| 命令 | 描述 | 命令 | 描述 |
|------|------|------|------|
| `ls` | 列出目录内容 | `ps` | 进程列表 |
| `cat` | 显示文件内容 | `kill` | 发送信号 |
| `grep` | 文本搜索 | `lsof` | 打开文件与网络连接 |
| `find` | 查找文件 | `sed` | 流式编辑器 |
| `cp` / `mv` / `rm` | 文件操作 | `sort` / `uniq` / `cut` | 文本处理 |
| `mkdir` / `rmdir` | 目录操作 | `head` / `tail` / `wc` | 文本工具 |
| `chmod` / `ln` / `touch` | 文件系统 | `df` / `du` / `diff` | 磁盘/比较 |
| `echo` / `env` / `which` | 工具类 | `pwd` / `realpath` / `tree` | 路径工具 |
| `date` / `tee` / `xargs` | 实用命令 | `file` | 文件类型识别 |
| `more` / `hexdump` / `strings` | 查看/分析 | `col` / `stty` | 终端工具 |
| `dir` / `vdir` / `dircolors` | 目录/颜色 | `chgrp` | 文件权限 |

---

## 🎯 为什么选择 WinuxCmd？

### 痛点

- AI 工具生成的 Linux 命令在 Windows 上经常不能直接运行
- Windows 原生命令与 Linux 过滤器之间缺少统一体验
- 多 Shell 混用时，命令语义和参数差异带来迁移成本

### 解决方式

```bash
# 安装 WinuxCmd 后可直接使用：
netstat -ano | grep 8080                      # 查占用端口的进程
tasklist | grep -i chrome                     # 过滤 Chrome 进程
ipconfig | grep -i "ipv4"                     # 只看 IP 地址
lsof -i :8080                                 # 查端口归属（支持 :PORT / tcp:PORT）
ls -la | grep ".cpp" | xargs wc -l            # 统计 C++ 代码行数
set | grep -i proxy                           # 检查代理环境变量
```

---

## 💡 技术亮点

### 1. Windows × Linux 管道互通

WinuxCmd 让 Windows 工具 stdout 直接进入 Linux 风格过滤器 stdin，无需额外桥接。

```bash
# 网络诊断
netstat -an | grep LISTENING | sort -u
lsof -i :8080
lsof -i tcp:443

# 进程分析
tasklist | grep -i chrome

# 文件统计
dir /b /a-d | grep -oE "\.[^.]+$" | sort | uniq -c | sort -r
```

### 2. 性能与体积

完整命令执行耗时（启动 + 执行 + 退出），1000 文件目录，20 次迭代：

| 命令 | WinuxCmd (ms) | uutils Rust (ms) | 胜负 |
|:----:|:---:|:---:|:---:|
| ls   | 6.30 | 7.27 | ✅ WinuxCmd |
| cat  | 6.19 | 7.01 | ✅ WinuxCmd |
| head | 6.27 | 6.79 | ✅ WinuxCmd |
| tail | 6.34 | 6.84 | ✅ WinuxCmd |
| grep | 6.42 | 5.99 | uutils |
| sort | 6.31 | 7.27 | ✅ WinuxCmd |
| uniq | 6.23 | 6.84 | ✅ WinuxCmd |
| wc   | 6.21 | 6.81 | ✅ WinuxCmd |

结论：8 个命令中胜出 7 个，整体约快 1.09x。

体积对比：

```text
WinuxCmd（静态）：         ~900 KB
BusyBox Windows：         ~1.24 MB
GNU coreutils（MSYS2）：   ~5 MB
单个 ls.exe（C/CMake）：   ~1.5 MB
```

---

## 🔧 构建

> 需要 MSVC 与 CMake 3.30+，详见 [构建模式文档](DOCS/zh/build_modes.md)

```powershell
cmake --preset dev
cmake --build build-dev
```

---

## 📈 Roadmap

### 阶段 1：核心与兼容性

- 稳定 Linux 风格核心命令在 Windows 上的行为
- 强化 REPL 在 cmd/PowerShell 下的回退正确性
- 继续优化体积与启动性能

### 阶段 2：Shell 与工具链

- 提升补全相关性与排序体验
- 增强 Windows/Linux 混合管道兼容
- 完善 shell 边界场景测试覆盖

### 阶段 3：生态建设

- 提供更完整的集成文档与模板
- 提升贡献流程和自动化工具
- 规划长期跨平台抽象能力

---

## 🤝 参与贡献

欢迎提交 PR，详见 [CONTRIBUTING_ZH.MD](CONTRIBUTING_ZH.MD)。

新手友好任务：

- 为命令补全 `-h/--help` 支持
- 改进错误信息与示例

---

## ❓ 常见问题（Q&A）

**Q：和 WSL 有什么区别？**
A：WSL 是完整 Linux 子系统；WinuxCmd 是原生 Windows 可执行文件，直接支持 Linux 风格命令语法。

**Q：会影响原生 Windows 命令吗？**
A：不会。WinuxCmd 无法识别的命令会按当前父 Shell 环境回退执行。

**Q：补全可以自定义吗？**
A：可以。使用 `%USERPROFILE%\.winuxcmd\completions\user-completions.txt` 或 `WINUXCMD_COMPLETION_FILE`。

**Q：为什么 lsof 可能出现 access denied / partial warning？**
A：Windows 对句柄可见性受权限限制。WinuxCmd 会降级处理并输出警告，而不是直接崩溃。

**Q: 具有sudo提权吗?**
A: 使用Windows 11自带的sudo命令,我们会转换逻辑,即sudo ls为被解析为sudo winuxcmd ls确保没有链接任然可以正确运行
---

## 📚 文档

| 文档 | 说明 |
|------|------|
| [概览与架构](DOCS/zh/overview_zh.md) | 核心行为与补全配置 |
| [命令实现细节](DOCS/zh/commands_implementation.md) | 参数兼容性矩阵 |
| [构建模式](DOCS/zh/build_modes.md) | Dev / Release / ASan 预设 |
| [选项处理指南](DOCS/zh/option-handling_zh.md) | 如何添加新命令 |
| [测试框架](DOCS/zh/testing_framework.md) | wctest 使用说明 |
| [Shell 集成](DOCS/zh/winux_shell_integration_zh.md) | PowerShell / CMD 配置 |

---

## 关于作者

联系：<2507560089@qq.com>
GitHub：[@caomengxuan666](https://github.com/caomengxuan666)
产品：https://dl.caomengxuan666.com

MIT License © 2026 caomengxuan666。详见 [LICENSE](LICENSE)。
