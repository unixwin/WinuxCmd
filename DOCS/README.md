# WinuxCmd Docs

This folder is the working README for WinuxCmd documentation.

## What to read first

1. [GNU Coreutils parity ledger](en/gnu_coreutils_parity.md)
2. [Implementation status](en/commands_implementation_en.md)
3. [Workspace integration](en/workspace_integration.md)
4. [TODO](en/TODO.md)

## Quick Start

1. Download the Windows binary archive from the project release page.
2. Unpack it into the repository, then run
   `.\scripts\setup-workspace-bin.ps1 -Source <unpacked-binary-dir>`.
3. Enable this workspace without touching global PATH:
   `.\scripts\activate-workspace.ps1`.
4. Optional persistent setup for AI shells:
   `.\scripts\install-workspace-profile-hook.ps1 -Quiet`.
5. On Windows, call WinuxCmd manuals as `man.exe <command>`; bare `man` can hit
   a PowerShell alias instead.

Release packages should include both the Windows binaries and
`WinuxCmd-skill-v<version>.zip`, so agents can install the repo-local skill
next to the workspace integration scripts.

## Rules

- Keep GNU references pointed at the official manuals.
- Track compatibility as a matrix, not as copied manual text.
- Keep file-operand wildcard expansion limited to commands that explicitly opt in.
- For fixed-arity file commands such as `diff`, `diff3`, `split`, and
  `csplit`, wildcard expansion must resolve to the exact number of operands
  the command expects.
- Do not touch global PATH for workspace activation.
- Use `man.exe` on Windows when asking WinuxCmd for help.

## Current focus

- Close the remaining GNU parity gaps for `ls`, `grep`, `head`, `tail`,
  `sed`, `find`, `xargs`, `install`, `timeout`, `stdbuf`, `chown`, `link`,
  `nice`, `nohup`, `printenv`, `tty`, and `unlink`.
- Keep the GNU-parity ledger current as batches land; recent batches covered
  `sum`, `stat`, `readlink`, `realpath`, `truncate`, `comm`, `join`, `paste`,
  `nl`, `expand`, `unexpand`, `fold`, and `fmt`.
- Keep the AI-facing command guidance small, explicit, and executable.
- Prefer repo-local skill instructions over ad hoc shell setup.

## References

- [English overview](en/overview.md)
- [Chinese overview](zh/overview_zh.md)
- [English TODO](en/TODO.md)
- [Chinese TODO](zh/TODO_zh.md)
- GNU `find` action references: [`-exec ... ;`](https://www.gnu.org/software/findutils/manual/html_node/find_html/Single-File.html),
  [`-exec ... {} +`](https://www.gnu.org/software/findutils/manual/html_node/find_html/Multiple-Files.html)
- GNU `xargs` reference: [Invoking xargs](https://www.gnu.org/software/findutils/manual/html_node/find_html/Invoking-xargs.html)
