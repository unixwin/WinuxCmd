# Command Guidance

Use the repository-local executables directly once the workspace is activated.

## Prefer these patterns

- Use `man.exe <command>` instead of `man` in PowerShell.
- Use explicit `.exe` names when a PowerShell alias could collide.
- Prefer the repo's GNU-parity switches when they reduce AI mistakes:
  - `ls`: `-d`, `-b`, `-f`, `-I`, `-U`, `-X`, `-v`
  - `cp`: `-t`, `-T`, `-n`, `-u`
  - `mv`: `-t`, `-T`, `-n`, `-u`, `-b`, `-S`
  - `install`: `-D`, `-t`, `-T`
  - `sort`: `-V`

## When inspecting the repo

- Prefer `rg` or `rg --files` for search and file discovery.
- Prefer `ls -d` or `ls -a` when you want directory names, not recursive noise.
- Avoid relying on shell aliases for command semantics.
