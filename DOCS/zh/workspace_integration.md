# 工作区集成

这个仓库支持仅在当前工作区内启用 WinuxCmd，不修改用户 PATH、
系统 PATH 或 PowerShell profile。

## 目标

- 让 WinuxCmd 只在当前仓库可用。
- 避免管理员安装步骤。
- 避免污染全局环境变量。

## 工作方式

1. `scripts/setup-workspace-bin.ps1` 会创建 `.winuxcmd/bin`。
2. 目录内放置所有命令的 `.exe` hardlink。
3. `scripts/activate-workspace.ps1` 只修改当前 PowerShell 进程的 PATH。
4. `scripts/activate-workspace.cmd` 对 CMD 也做同样处理。
5. PowerShell 激活时会清理当前会话里常见的别名冲突。

## 推荐用法

PowerShell:

```powershell
.\scripts\activate-workspace.ps1
man.exe ls
winuxcmd.exe help
```

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

如果你希望在这个仓库里新开的 PowerShell 会话也自动加载 WinuxCmd，
可以安装用户 profile hook。它会同时更新当前用户的 PowerShell 7
profile 和 Windows PowerShell 5.1 profile，并在修改前为每个文件保留备份：

```powershell
.\scripts\install-workspace-profile-hook.ps1
```

移除它：

```powershell
.\scripts\install-workspace-profile-hook.ps1 -Remove
```

## 说明

- PowerShell 激活后，`ls`、`cp`、`mv`、`rm` 这类常见命令会直接落到 WinuxCmd。
- 如果你想保持绝对明确，仍然可以显式写 `.exe`。
- 删除本地 bin：`scripts\setup-workspace-bin.ps1 -Remove`
- hardlink 依赖 NTFS 风格文件系统。
- 仓库内面向 AI 的使用说明在 `skills/winuxcmd/SKILL.md`。
