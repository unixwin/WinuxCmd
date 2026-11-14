# GNU SIGPIPE / exit-status probes (WSL, 2026-11-14)

Command shape: `seq 1 1000000 | <cmd> | head -1 >/dev/null`, then
`${PIPESTATUS[1]}` (or `[0]` for the direct seq case).

```
tee_default=141
tee_p=0
tee_warn_nopipe=0
grep=141
sed=141
seq=141
```

xargs exit-status probes:

```
xargs_child3=123     # printf "a\nb\n" | xargs -n1 sh -c "exit 3"
xargs_killed=125     # printf "a\n"   | xargs sh -c "kill -TERM \$\$"
xargs_missing=127    # printf "a\n"   | xargs /no/such/cmd
```

GNU source anchors used in `ABI-V3-ATTRIBUTION-MEMO.md`:

| Behavior | Anchor |
|---|---|
| tee default: no SIG_IGN → dies 141 | coreutils 9.11 `src/tee.c:172-173` |
| tee `-p` = `--output-error=warn-nopipe` | `src/tee.c:154-157`, `:210-219` |
| sort: ignore, then re-raise if originally `SIG_DFL` | `src/sort.c:364-365, 406-408, 4800-4803` |
| tail: explicit `raise(SIGPIPE)` | `src/tail.c:377-383` |
| split: tracks startup SIGPIPE disposition | `src/split.c:66-67, 559-560, 1709` |
| grep: no handler; write error → `die(2, ...)` | grep 3.11 `src/grep.c:441-479, 672-675, 1345-1346` |
| sed: `panic()` → `EXIT_PANIC=4` | sed `utils.c:222-313`, `utils.h:23-25` |
| xargs: 0/1/123/124/125/126/127 | findutils 4.10 `xargs(1)` EXIT STATUS |
| uutils tee: `disable_pipe_errors` + per-writer BrokenPipe ignore | uucore 0.12 `signals.rs:600-615`; uutils `tee.rs:23-51, 280-305` |
| uutils seq: BrokenPipe special case, SIGPIPE-ignored → rc 1 | uutils `seq.rs:282-296` |
| uutils xargs: 123-127 mapping | `D:\repo\findutils\src\xargs\mod.rs:1277-1285` |

Note: the GNU `seq` probe returned 141, while the uutils `seq` source
comment claims GNU exits 0 on BrokenPipe.  This is recorded as a
divergence-risk data point, not as a GNU behavior claim.
