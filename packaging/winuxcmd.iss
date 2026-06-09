#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef ArchSlug
  #define ArchSlug "x64"
#endif

#if ArchSlug == "x64"
  #define InnoArchitecture "x64compatible"
#elif ArchSlug == "arm64"
  #define InnoArchitecture "arm64"
#else
  #define InnoArchitecture ArchSlug
#endif

#ifndef StageDir
  #error StageDir must point at a staged WinuxCmd install tree.
#endif

#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{A39D4B7F-01E6-4E40-8183-FD317A1C4C12}
AppName=WinuxCmd
AppVersion={#AppVersion}
AppPublisher=WinuxCmd Project
AppPublisherURL=https://github.com/caomengxuan666/WinuxCmd
AppSupportURL=https://github.com/caomengxuan666/WinuxCmd
AppUpdatesURL=https://github.com/caomengxuan666/WinuxCmd/releases
DefaultDirName={localappdata}\Programs\WinuxCmd
DefaultGroupName=WinuxCmd
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\winuxcmd.exe
PrivilegesRequired=lowest
ChangesEnvironment=yes
SolidCompression=yes
WizardStyle=modern
OutputDir={#OutputDir}
OutputBaseFilename=WinuxCmd-{#AppVersion}-{#ArchSlug}-setup
ArchitecturesAllowed={#InnoArchitecture}
ArchitecturesInstallIn64BitMode={#InnoArchitecture}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add WinuxCmd command executables to your user PATH"
Name: "pwshprofile"; Description: "Install the Winux PowerShell wrapper for PowerShell 5.1 and 7"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: filesandordirs; Name: "{app}\*"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
procedure RunPowerShell(const ScriptPath: String; const Params: String; const WorkingDir: String; const ErrorPrefix: String);
var
    ResultCode: Integer;
    CommandLine: String;
begin
    CommandLine :=
        '-NoProfile -ExecutionPolicy Bypass -File "' + ScriptPath + '" ' + Params;

    if not Exec(
        ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
        CommandLine,
        WorkingDir,
        SW_HIDE,
        ewWaitUntilTerminated,
        ResultCode) then
    begin
        RaiseException(ErrorPrefix + ': failed to launch PowerShell');
    end;

    if ResultCode <> 0 then
        RaiseException(ErrorPrefix + ': exit code ' + IntToStr(ResultCode));
end;

procedure RunTemporaryPowerShellScript(const ScriptBody: String; const WorkingDir: String; const ErrorPrefix: String);
var
    ScriptPath: String;
begin
    ScriptPath := ExpandConstant('{tmp}\winuxcmd-installer-helper.ps1');
    if not SaveStringToFile(ScriptPath, ScriptBody, False) then
        RaiseException(ErrorPrefix + ': failed to write helper script');

    RunPowerShell(ScriptPath, '', WorkingDir, ErrorPrefix);
end;

procedure CreateCommandLinks;
begin
    RunPowerShell(
        ExpandConstant('{app}\create_links.ps1'),
        '-Force',
        ExpandConstant('{app}'),
        'Failed to create WinuxCmd command links');
end;

procedure ConfigurePowerShellProfiles(Install: Boolean);
var
    Params: String;
begin
    if Install then
        Params := '-Install -ProfilesOnly -Quiet'
    else
        Params := '-Uninstall -ProfilesOnly -Quiet';

    RunPowerShell(
        ExpandConstant('{app}\winux-activate.ps1'),
        Params,
        ExpandConstant('{app}'),
        'Failed to update PowerShell profiles');
end;

procedure ModifyUserPath(Install: Boolean);
var
    PathValue: String;
    Parts, Updated: TArrayOfString;
    I, Count: Integer;
    AppPath: String;
begin
    AppPath := ExpandConstant('{app}');
    if not RegQueryStringValue(HKCU, 'Environment', 'Path', PathValue) then
        PathValue := '';

    Parts := StringSplit(PathValue, [';'], stExcludeEmpty);
    SetArrayLength(Updated, GetArrayLength(Parts) + 1);
    Count := 0;
    for I := 0 to GetArrayLength(Parts) - 1 do
    begin
        if CompareText(Parts[I], AppPath) <> 0 then
        begin
            Updated[Count] := Parts[I];
            Count := Count + 1;
        end;
    end;

    if Install then
    begin
        for I := Count downto 1 do
            Updated[I] := Updated[I - 1];
        Updated[0] := AppPath;
        Count := Count + 1;
    end;

    SetArrayLength(Updated, Count);
    if not RegWriteExpandStringValue(HKCU, 'Environment', 'Path', StringJoin(';', Updated)) then
        RaiseException('Failed to update user PATH');
end;

procedure RemovePowerShellProfiles;
var
    ScriptBody: String;
begin
    ScriptBody :=
        '$ErrorActionPreference = ''Stop''' + #13#10 +
        'function Get-ProfileInstallPaths {' + #13#10 +
        '    $docs = [Environment]::GetFolderPath(''MyDocuments'')' + #13#10 +
        '    return @(' + #13#10 +
        '        (Join-Path $docs ''WindowsPowerShell\profile.ps1''),' + #13#10 +
        '        (Join-Path $docs ''WindowsPowerShell\Microsoft.PowerShell_profile.ps1''),' + #13#10 +
        '        (Join-Path $docs ''PowerShell\profile.ps1''),' + #13#10 +
        '        (Join-Path $docs ''PowerShell\Microsoft.PowerShell_profile.ps1'')' + #13#10 +
        '    ) | Sort-Object -Unique' + #13#10 +
        '}' + #13#10 +
        'function Remove-LegacyProfileBlocks {' + #13#10 +
        '    param([string]$Content)' + #13#10 +
        '    $options = [System.Text.RegularExpressions.RegexOptions]::Singleline -bor [System.Text.RegularExpressions.RegexOptions]::Multiline' + #13#10 +
        '    $patterns = @(' + #13#10 +
        '        ''^# >>> WinuxCmd integration >>>\r?\n.*?^# <<< WinuxCmd integration <<<\r?\n?'',' + #13#10 +
        '        ''^# Enumerate supported install roots\. WINUXCMD_INSTALL_ROOTS is mainly for\r?\n.*?^Register-EngineEvent -SourceIdentifier PowerShell\.Exiting -Action \{.*?\} \| Out-Null\r?\n?'',' + #13#10 +
        '        ''^# Enumerate supported install roots\. WINUXCMD_INSTALL_ROOTS is mainly for\r?\n.*?^# Find winuxcmd\.exe and set alias for it\.\r?\n?'',' + #13#10 +
        '        ''^# Find winuxcmd\.exe and set alias for it\.\r?\n?'',' + #13#10 +
        '        ''# WinuxCmd wrapper.*?(?=^# Find winuxcmd\.exe|\Z)'',' + #13#10 +
        '        ''^# set alias\s*\r?\nUpdate-WinuxCmdAlias\s*\r?\n\r?\n'',' + #13#10 +
        '        ''^function Update-WinuxCmdAlias\s*\{.*?^\}'',' + #13#10 +
        '        ''^function global:winux\s*\{.*?^\}'',' + #13#10 +
        '        ''^Set-Alias -Name winuxcmd -Value [^\r\n]+\r?\n?'',' + #13#10 +
        '        ''^# =+[\r\n]+# WinuxCmd Integration.*?# =+[\r\n]+# End WinuxCmd Integration[\r\n]*'',' + #13#10 +
        '        ''^Register-EngineEvent -SourceIdentifier PowerShell\.Exiting -Action \{.*?\} \| Out-Null\r?\n?''' + #13#10 +
        '    )' + #13#10 +
        '    foreach ($pattern in $patterns) {' + #13#10 +
        '        $Content = [regex]::Replace($Content, $pattern, '''', $options)' + #13#10 +
        '    }' + #13#10 +
        '    return $Content' + #13#10 +
        '}' + #13#10 +
        'foreach ($profilePath in Get-ProfileInstallPaths) {' + #13#10 +
        '    if (-not (Test-Path $profilePath)) { continue }' + #13#10 +
        '    $content = Get-Content $profilePath -Raw' + #13#10 +
        '    $content = Remove-LegacyProfileBlocks -Content $content' + #13#10 +
        '    $lines = $content -split "`r?`n" | Where-Object { $_ -notmatch ''# WinuxCmd Tab Completion'' -and $_ -notmatch ''winuxcmd-completions\.ps1'' }' + #13#10 +
        '    $newContent = ($lines -join "`r`n").Trim()' + #13#10 +
        '    Set-Content -LiteralPath $profilePath -Value $newContent -Encoding UTF8' + #13#10 +
        '}';

    RunTemporaryPowerShellScript(
        ScriptBody,
        ExpandConstant('{tmp}'),
        'Failed to remove PowerShell profile integration');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
    if CurStep = ssPostInstall then
    begin
        CreateCommandLinks;

        if WizardIsTaskSelected('addtopath') then
            ModifyUserPath(True);

        if WizardIsTaskSelected('pwshprofile') then
            ConfigurePowerShellProfiles(True);
    end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
    if CurUninstallStep = usUninstall then
    begin
        RemovePowerShellProfiles;
        ModifyUserPath(False);
    end;
end;
