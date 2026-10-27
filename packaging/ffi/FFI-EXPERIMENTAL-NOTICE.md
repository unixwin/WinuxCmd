# FFI acceleration — experimental feature notice

This package ships an FFI acceleration library for the WinuxCmd command
set (`usr/bin/ffi/v<version>/winuxcore.dll`). Please read this before
enabling it.

## It is off by default

Nothing changes unless **you** turn it on. The default way commands run
— the shell finding `winuxcmd.exe` on `PATH` and starting it as its own
process — is unchanged, and stays available as the fallback at all
times.

To enable FFI routing in the niubash shell, set the environment
variable before starting the shell:

```
NIU_WINUXCMD_FFI=1
```

Unset it (or set it to `0`) and the very next command runs the classic
way again. Individual commands can be excluded at any time with
`NIU_WINUXCMD_FFI_EXCLUDE` (comma-separated command names).

## Why "experimental"

- The DLL in this release is **not yet code-signed**. Loading an
  unsigned DLL from a package-managed directory is exactly the shape
  that SmartScreen and Microsoft Defender watch closely, so you may see
  a SmartScreen prompt or a Defender detection when the DLL is first
  loaded or replaced. That is expected for this interim build; it is
  not, by itself, evidence of tampering — verify the DLL against the
  SHA-256 in `SHA256SUMS.txt` at the install root (and in the wpm
  package index) if you want certainty.
- Integrity is pinned even while unsigned: the host shell verifies the
  DLL's SHA-256 against the pinned value before loading it, and refuses
  to load it on mismatch (the shell keeps working via the normal
  process path, with a warning).
- The tier table (`tiers.tsv`, shipped beside the DLL) decides which
  commands may run in-process at all; commands outside the table always
  run as separate processes, with or without the switch.

## The signing gate

A code-signed DLL — same certificate line as the rest of the product —
is a **hard gate** for FFI ever leaving this experimental state (on by
default, or shipped without this notice). Until signing lands, FFI
ships only in this labelled, default-off form.
