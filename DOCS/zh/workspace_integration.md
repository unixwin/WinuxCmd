# 工作区集成

这个仓库支持仅在当前工作区内启用 WinuxCmd，不修改用户 PATH、
系统 PATH 或 PowerShell profile。

## 目标

- 让 WinuxCmd 只在当前仓库可用。
- 避免管理员安装步骤。
- 避免污染全局环境变量。
- 保持仓库内的命令目录独立、可随时删除重建。

## 工作方式

1. `scripts/setup-workspace-bin.ps1` 会在仓库根目录创建 `.winuxcmd/bin`。
2. 目录内放置 `winuxcmd.exe` 和 `src/commands` 下所有命令的 `.exe`
   hardlink。
3. `scripts/activate-workspace.ps1` 只修改当前 PowerShell 进程的 PATH，
   同时设置 `WINUXCMD_HOME`，并清理当前会话里常见的别名冲突。
4. `scripts/activate-workspace.cmd` 对 CMD 也做同样处理。

## 仓库本地 bin

`.winuxcmd/bin` 是这个仓库本地唯一需要的激活路径。它按需生成，
始终留在仓库树内，不会改全局 shell 状态，也可以随时删除后重建。

## 推荐用法

PowerShell:

```powershell
.\scripts\activate-workspace.ps1
man.exe ls
winuxcmd.exe help
```

在 PowerShell 里想明确调用仓库附带的手册程序时，请直接用
`man.exe`，不要依赖裸 `man`。激活脚本会清理当前会话里的常见别名，
但显式写 `.exe` 仍然最稳妥。

CMD:

```cmd
scripts\activate-workspace.cmd
man.exe ls
winuxcmd.exe help
```

## 使用 Visual Studio 编译

如果需要 MSVC 构建环境，但不想改全局 shell 配置，可以直接用这个脚本：

```powershell
.\scripts\build-with-vs.ps1 -Target winuxcmd-tests
```

默认会使用新的 `build-vs` 目录，并且使用 `vcvars64.bat`，除非你显式覆盖环境脚本。

## 可选的持久激活

如果你希望在这个仓库里新开的 PowerShell 会话也自动激活 WinuxCmd，
可以安装用户 profile hook。它会把同一段 hook 写入当前用户的
PowerShell 7 profile 和 Windows PowerShell 5.1 profile，修改前会给每个
文件留备份，并且只在当前目录位于这个仓库根目录下时自动激活：

```powershell
.\scripts\install-workspace-profile-hook.ps1
```

移除它：

```powershell
.\scripts\install-workspace-profile-hook.ps1 -Remove
```

## 说明

- PowerShell 激活后，`ls`、`cp`、`mv`、`rm` 这类常见命令会直接落到 WinuxCmd。
- 如果你想保持绝对明确，仍然可以显式写 `.exe`，尤其是 `man.exe`。
- 删除本地 bin：`scripts\setup-workspace-bin.ps1 -Remove`
- hardlink 依赖 NTFS 风格文件系统。
- 仓库内面向 AI 的使用说明在 `skills/winuxcmd/SKILL.md`。
