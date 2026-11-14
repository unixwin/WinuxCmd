# Consuming WinuxCmd

See `DOCS/2.0-CONSUMING.md` for the full guide. This directory holds runnable
examples.

## Bash product

`bash-product.sh` shows the two supported shell integration modes:

- applets on `PATH` (hardlinks created by the installer or wpm)
- direct dispatch through one `winuxcmd.exe` (`winux <applet> ...`)

```bash
WINUXCMD_BIN="/c/Program Files/WinuxCmd/winuxcmd.exe" ./bash-product.sh
./bash-product.sh --shims   # build a private hardlink farm first
```

## C ABI consumer

`abi-consumer.cpp` calls `winuxcmd_run` with explicit argv/envp/cwd/stdin and
prints the returned buffers. It checks `winuxcmd_abi_version()` before use.

```bat
cl /nologo /I src\shared examples\consume\abi-consumer.cpp ^
   build-vs\rust2-target\x86_64-pc-windows-msvc\release\winuxcmd_ffi.lib ^
   advapi32.lib kernel32.lib ntdll.lib userenv.lib ws2_32.lib dbghelp.lib ^
   wtsapi32.lib netapi32.lib
abi-consumer.exe
```

Expected shape:

```
abi=2 has_sort=1 has_nosuch=0
alpha
beta
gamma
```
