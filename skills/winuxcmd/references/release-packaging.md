# Release Packaging

Package the skill as a standalone archive rooted at `winuxcmd/` with these
files:

- `SKILL.md`
- `agents/openai.yaml`
- `references/`

## Recommended archive name

- `WinuxCmd-skill-v<version>.zip`

## Recommended packaging flow

1. Validate the skill with `scripts/quick_validate.py skills/winuxcmd`.
2. Package the directory with the repo-local skill packager.
3. Upload the archive to the GitHub release together with the Windows binaries.

## Notes

- Keep the skill bundle small and focused.
- Do not add README-style docs inside the skill package.
- Keep helper content in `references/` so `SKILL.md` stays short.
