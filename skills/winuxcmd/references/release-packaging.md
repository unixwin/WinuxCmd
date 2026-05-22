# Release Packaging

Package the skill as a standalone archive rooted at `winuxcmd/` with these
files:

- `SKILL.md`
- `agents/openai.yaml`
- `references/`

## Recommended archive name

- `WinuxCmd-skill-v<version>.zip`

## Recommended packaging flow

1. Ensure the skill tree contains `SKILL.md`, `agents/openai.yaml`, and
   `references/`.
2. Package the directory with `scripts/package-skill.ps1`.
3. Upload the archive to the GitHub release together with the Windows binaries.

## Notes

- Keep the skill bundle small and focused.
- Do not add README-style docs inside the skill package.
- Keep helper content in `references/` so `SKILL.md` stays short.
- Keep the release bundle self-contained and release-ready: the skill archive
  should match the shipped binaries and should not depend on global PATH.
