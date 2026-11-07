# WinuxCmd Performance Baseline (perf/baseline-sweep, 2026-11-07)
Build: `scripts/build-with-vs.ps1 -BuildDir build-vs-rel -Configuration Release -Target winuxcmd` (Release; VS18 bundled cmake/ninja PATH-prefixed — Niubash-installed cmake on PATH has corrupted Modules). Do NOT run `wpm links rebuild` in build-vs-rel/usr/bin (treats cwd as install root). Fixtures: 64MiB text (1.66M lines), 16MiB + C-sorted pair, binary, 10k-file tree, 1000-file rename dir; md5-identical on NTFS (/tmp/wcperf) and WSL ext4. awk/tar/gzip are NOT built-ins (wpm opt/ payloads) — excluded. grep skipped (wc-grep #1531).
## Median seconds (5 runs, 3 for tree ops; stdout>NUL; LC_ALL=C)
| cmd | WinuxCmd NTFS | MSYS NTFS | GNU ext4 | vs GNU | vs MSYS | hotspot >5x GNU |
|---|---|---|---|---|---|---|
| cat 64M | 0.301 | 0.213 | 0.010 | 30x | 1.41x | YES (engine) |
| wc 64M | 0.358 | 0.249 | 1.018 | 0.35x | 1.44x | no (beats GNU) |
| head -c1M | 0.021 | 0.030 | 0.001 | 21x | 0.70x | YES (spawn floor) |
| tail -c1M | 0.026 | 0.029 | 0.001 | 26x | 0.90x | YES (spawn floor) |
| sort 64M | 13.917 | 12.063 | 0.192 | 72x | 1.15x | YES (platform) |
| uniq -c 16M | 2.436 | 2.473 | 0.065 | 37x | 0.99x | YES (platform) |
| tr 16M | 0.046 | 0.037 | 0.014 | 3.3x | 1.24x | no |
| cut 16M | 0.168 | 0.157 | 0.058 | 2.9x | 1.07x | no |
| sed 16M | 0.181 | 0.154 | 0.082 | 2.2x | 1.18x | no |
| od 1.5M | 0.220 | 0.237 | 0.226 | 0.97x | 0.93x | no |
| comm 16M | 1.106 | 1.018 | 0.088 | 12.6x | 1.09x | YES (platform) |
| xargs cat 10k | 2.319 | 2.646 | 0.143 | 16x | 0.88x | YES (platform) |
| find -type f 10k | 0.454 | 0.455 | 0.005 | 91x | 1.00x | YES (platform) |
| ls -R 10k | 0.107 | 0.118 | 0.006 | 18x | 0.91x | YES (platform) |
| split -l20k 16M | 0.481 | 0.460 | 0.013 | 37x | 1.05x | YES (platform) |
| cp 64M | 0.114 | 0.119 | 0.025 | 4.6x | 0.96x | no |
| cp -r tree10k | 12.742 | 12.930 | 0.216 | 59x | 0.99x | YES (platform) |
| mv 1000 renames | 0.049 | 0.054 | 0.023 | 2.1x | 0.91x | no |
**Headline**: vs MSYS GNU on the same NTFS the engine is 0.7x-1.45x everywhere; the >5x flags vs WSL GNU are platform-owned (NTFS+Defender metadata, GNU parallel sort + ext4 temps, small-buffer line reads — MSYS pays the same on NTFS).
## Top-5 hotspot attribution (diagnosis only, no code changed)
1. **cat 1.41x vs MSYS — the one real engine hit.** 64KiB chunks (commands/cat.cpp:316) go through safePrint(string_view) (utils/console.cppm:579) -> i18n::translate_legacy -> legacy_key FNV-1a byte-loop over all 64KiB + std::string key/fallback allocs (utils/i18n.cppm:65-71,185-189): 2 extra full-chunk passes + 2 heap allocs per chunk. Fix direction: length-gate translate_legacy (catalog keys are short literals; >256B can never match) — class invariant, not a symptom guard.
2. **wc 1.44x vs MSYS.** Full wc decodes UTF-8 per char (commands/wc.cpp:275,459-506); -l/-c fast paths exist (wc.cpp:384). Direction: 7-bit ASCII fast path in the word scanner.
3. **sort 72x vs GNU — platform.** --parallel/-T/--compress are ignored hints (sort.cpp:59,99,102); single-thread + NTFS temps; real GNU sort also 12.1s on NTFS (MSYS). Direction: chunked parallel sort + k-way merge; honor -T.
4. **find/ls/cp-tree/xargs — platform.** 10k-file metadata ops identical in MSYS (1.00x/0.91x/0.99x/0.88x): per-file CreateFile/stat + Defender dominate. uniq/comm same class (small-buffer line reads, engine parity).
5. **head/tail — spawn floor.** ~20ms named-exe invocation init (main.cpp:592-607 dispatch path); MSYS cat on an empty file also costs 21ms.
## Startup overhead (10-run median)
`winuxcmd --version` 1ms (early exit before registry init, main.cpp:524); named-exe real invocation floor ~20ms — parity with MSYS cat spawn (21ms); MSYS true.exe 9ms; WSL GNU env --version 1ms; wsl.exe true from Windows 184ms (VM relay).
## Method notes
bash TIMEFORMAT=%R, median of 5 (3 tree ops), warmup, Release build (Debug inflates 5-20x). Harness awk/sort pinned to /usr/bin — perfbin-first PATH captured the harness's own awk/sort mid-run (fixed). GNU oracle: WSL 5.3.20 on ext4, md5-identical inputs; NTFS-vs-ext4 ratios are platform-inclusive; MSYS column is the like-for-like Windows control. GNU wc full-mode slowness re-verified (1.018/1.194 vs -l 0.011); GNU xargs re-measured on ext4 paths (0.143s) after an initial 9P-path mistake.
Raw: target-perf-baseline/{bench-win.sh,bench-gnu.sh,results/{win,msys,gnu}.tsv}; build logs target-perf-build*.log. Fixtures removed after the run.

## Hotspot fix sweep (perf/baseline-sweep, 2026-11-07, after-fix)

Fixes for hotspots 1-2 above (no other code touched):

1. **cat / i18n**: `translate_legacy` (src/utils/i18n.cppm) now length-gates on
   `kMaxLegacyKeyTextLength = 256`. Class invariant: legacy.* keys are FNV-1a
   digests of literals extracted from source by scripts/i18n_batch.py; the
   longest safePrint/std::unexpected literal in the tree is 92 bytes (extraction
   regex re-run over src/), and the shipped zh-CN catalog's longest legacy value
   is 59 bytes, so a >256B buffer can never match a catalog entry. Over the
   gate it returns the fallback verbatim — no FNV-1a chunk pass, no key allocs,
   no JSON lookup. Console layer (safePrint) is unchanged.
2. **wc**: 7-bit ASCII fast loop in `count_stream`'s process_buffer
   (src/commands/wc.cpp). While bytes stay <0x80 the ASCII arms of
   consume_codepoint are spelled out inline (printable runs bulk-accounted as
   before; \n \r \f \t \v space and remaining C0/DEL per the per-codepoint
   path); the loop stops at the first byte >=0x80 and defers to the existing
   UTF-8 decoder, then resumes. -l/-c fast paths untouched; -m still counts
   every valid codepoint including C0/DEL and skips invalid UTF-8 bytes.

Re-measured with target-perf-baseline/bench-scope.sh (5-run medians, same
session/machine/fixtures: t64m.txt regenerated as 64 MiB / 1,636,801 lines,
med in target-perf-baseline/results/scope-{before,after}-{win,msys}.tsv;
Release build target-perf-rebuild-rel.log). Scope: cat/wc + head/tail/sort
spot checks.

| cmd | before | after | MSYS before | MSYS after | vs MSYS before | vs MSYS after |
|---|---|---|---|---|---|---|
| cat 64M | 0.318 | 0.122 | 0.045 | 0.042 | 7.1x | 2.9x |
| wc 64M | 0.409 | 0.243 | 0.382 | 0.379 | 1.07x | 0.64x |
| head -c1M | 0.030 | 0.021 | 0.029 | 0.029 | 1.03x | 0.72x |
| tail -c1M | 0.036 | 0.019 | 0.034 | 0.031 | 1.06x | 0.61x |
| sort 64M | 4.923 | 5.537 | 0.706 | 0.848 | 7.0x | 6.5x |

- **wc: 0.64x vs MSYS** — now beats GNU on the same NTFS; the ASCII loop cut
  full-mode scanning ~40% (0.409 -> 0.243).
- **cat: 0.318 -> 0.122 (-62%)**; the removed cost is exactly the diagnosed
  i18n tax (FNV-1a over each 64 KiB chunk + 3 heap allocs + JSON find +
  fallback copy, ~0.19s over 1,024 chunks). After the fix cat sits at the
  engine's raw-copy floor: `cp` of the same file probes 0.127-0.140 (cp writes
  to a real disk file; cat writes to NUL). The residual gap vs this session's
  GNU cat (0.042-0.065) is winuxcmd's ifstream read path plus the ~20ms named-
  exe spawn floor — a different owner than the diagnosed hotspot, untouched
  here. The ledger's MSYS cat control (0.213) was NOT reproducible this
  session (GNU cat 0.042-0.065); against that ledger control the after number
  is 0.57x, i.e. the 1.41x headline is closed with margin under ledger
  conditions, and the honest in-session ratio is 2.9x.
- **head/tail**: improved (their 1 MiB outputs also cross the gate now); 
  **sort**: before/after within its own run noise (probes 4.95-5.97; its
  per-line output is <256B so the gate does not change its path). No other
  command regressed.
- Harness gotchas fixed for this run (see scripts in target-perf-baseline/):
  this session's PATH resolves /usr/bin to the Niubash winuxcmd install, so
  bench-scope.sh and the differential pin every tool to D:/Git/usr/bin; the
  Release/Debug builds must PATH-prefix the VS18-bundled cmake+ninja
  (target-perf-baseline/rebuild-rel.ps1, rebuild-tests.ps1) or configure falls
  to the Niubash cmake 4.4.2 with corrupted Modules and ninja fails (RC1107 /
  silent pcre2 failure); differential tags on NTFS must not differ only in
  letter case (L vs l collided case-insensitively — tags now encode capL).

### After-fix correctness evidence (target-perf-baseline/correctness/)

- **wc**: 150-case matrix (12 fixtures x 12 option sets + 6 stdin cases:
  ASCII, zh-CN UTF-8, CJK+emoji+combining mix, CRLF, no-newline EOF, controls
  VT/FF/CR/DEL/C0, invalid UTF-8 0xFF + truncated sequence, 64 KiB chunk-edge
  multibyte, 200 KB single line, NUL binary, empty; -l -w -c -m -L -lw -wmc
  -lwc -mL -lwcmL -cl + default) run through WSL GNU 5.3.20 (LC_ALL=C.UTF-8),
  pre-fix build and post-fix build; stdout/stderr/exit compared three ways —
  **0 differing**. Pre-fix == post-fix == GNU everywhere, so the fast path
  introduced no behavior change.
- **cat**: 15 payloads (all 12 fixtures + 2-file concat + 2 stdin forms)
  md5-compared pre-fix vs post-fix vs WSL GNU cat — **all identical**.
- **i18n catalog state**: with the real zh-CN catalog (3,559 keys, 1,061
  legacy keys) installed at a fake WPM root and WINUX_LANG=zh-CN: `wc -l`
  count line still translates through the <=256B gate (injected legacy key
  prints "yi-TRANSLATED"); `cat` of 300B and 144KB payloads stays
  byte-identical (md5) with catalog active; `ls --help` renders translated.
- **Unit tests** (tests/unit/wc/wc_unit_test.cpp,
  tests/unit/winuxcmd/winuxcmd_unit_test.cpp): added
  wc_ascii_fast_path_controls_word_and_width_exact,
  wc_ascii_fast_path_multibyte_inside_and_across_chunks,
  wc_ascii_fast_path_max_line_width_tabs_and_wide_chars,
  i18n_legacy_length_gate_bulk_output_stays_verbatim. Suites wc.* (32) and
  winuxcmd.* (42) fully green; cat.* 44/45 — the one failure,
  cat.nul_txt_reads_file_issue1333, is PRE-EXISTING on this machine and
  unrelated to the fix: with the fixes stashed the pre-fix build fails it
  identically (~0.9s, exit 1, empty stdout — looks like a fresh-create race
  on a file literally named NUL.txt under %TEMP%), while the capability
  itself passes on all three builds when the file is created first
  (prefix/postfix Release and current Debug cat all read a real NUL.txt
  correctly).

