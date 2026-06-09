# WinuxCmd Shell 集成说明（PowerShell / CMD / Windows Terminal）

## 推荐启动方式

### PowerShell 命令名激活

PowerShell 自带 alias 会遮住部分常见 GNU 命令名，比如 `ls`、`cat`、`rm`、
`man`。在当前会话运行：

```powershell
winux activate
```

之后这些裸命令会指向 WinuxCmd，Windows PowerShell 5.1 也适用。需要恢复
PowerShell 原始 alias 时运行：

```powershell
winux deactivate
```

如果希望每次打开交互式 PowerShell 都自动激活，先安装 `winux` profile
wrapper，然后把下面这段放在 `$PROFILE` 里 wrapper 之后：

```powershell
if (Get-Command winux -ErrorAction SilentlyContinue) {
    winux activate 6>$null
}

如果 `winux activate` 提示 `winux.ps1 not found`，通常不是当前安装包漏文件，
而是 `$PROFILE` 里还残留着旧安装路径生成的 wrapper，例如早期 Scoop 路径。
请从当前 WinuxCmd 安装目录重新运行 `winux-activate.ps1`，让 wrapper 改为动态
查找最新的 `%LOCALAPPDATA%\WinuxCmd` 运行时目录。
```

### CMD / Windows Terminal

把 WinuxCmd 的 `bin` 目录加入 PATH，或在 release 的 `bin` 目录运行
`create_links.ps1`。命令解析、管道、重定向和未知命令仍由 cmd 或
PowerShell 自己处理。

## 常见问题

1. `rm`、`cat`、`man` 仍然走 PowerShell 自带命令：
   - 运行 `winux activate`，或把上面的自动激活片段加入 `$PROFILE`。
2. Profile 没生效：
   - 检查执行策略与 profile 路径（`echo $PROFILE`）。
3. 明明 `Get-Command ls` 还是 PowerShell alias，但输入 `ls` 却跑到了别的实现：
   - 说明可能有别的工具在 `$PROFILE` 里注入了 `PSConsoleHostReadLine` 改写逻辑；微软 `coreutils` 就会这样做。
