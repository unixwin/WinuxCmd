# WinuxCmd Shell 集成说明（PowerShell / CMD / Windows Terminal）

## 推荐启动方式

### PowerShell 配置文件自动进入

把以下内容加入 `$PROFILE`：

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
        if (Test-Path $devExe) {
            $winuxExe = $devExe
        }
    }
    if ($winuxExe -and (Test-Path $winuxExe)) {
        & $winuxExe
    }
}
```

请将 `$devExe` 替换为你本机 `winuxcmd.exe` 的实际路径。

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
```

### CMD / Windows Terminal 启动命令

```bat
%SystemRoot%\System32\cmd.exe /k winuxcmd
```

## 回退行为说明

- 命中 WinuxCmd 内置命令：内部执行
- 未命中内置命令：交给原生 shell 执行
- 原生 shell 会按进入来源自动选择：
  - 从 PowerShell/pwsh 进入 -> 回退到 PowerShell
  - 从 cmd 进入 -> 回退到 cmd

这样从 PowerShell 进入时可以直接执行 `Get-Process` / `Where-Object`。

## 自动补全

- 默认补全文件：`%USERPROFILE%\.winuxcmd\completions\user-completions.txt`
- 环境变量覆盖：`WINUXCMD_COMPLETION_FILE`

## 常见问题

1. `Get-Process` 提示找不到：
   - 通常是从 cmd 进入，若需要 PowerShell cmdlet，请从 PowerShell 启动。
2. 管道里 `lsof` 提示找不到：
   - 请更新到最新构建，内置命令管道重写已处理此场景。
3. `rm`、`cat`、`man` 仍然走 PowerShell 自带命令：
   - 运行 `winux activate`，或把上面的自动激活片段加入 `$PROFILE`。
4. Profile 没生效：
   - 检查执行策略与 profile 路径（`echo $PROFILE`）。
