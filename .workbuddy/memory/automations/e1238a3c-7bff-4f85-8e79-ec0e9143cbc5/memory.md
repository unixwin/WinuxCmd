# Automation execution history: e1238a3c (fetch issues + fix winuxcmd)

## 2026-09-07 run
- Refreshed K:\coreutils-issues: uutils open (1000 issues, open_compact.json), uutils recently-updated (300), GNU Savannah HTML + NEWS. New helper: fetch_incremental.py.
- Baseline: full test suite takes ~3-6 min (NOT hung; earlier "hangs" were timeout truncations). Baseline was 2360/2366.
- Fixed 3 GNU-parity bugs (commits f9a237e style 7f47fe9 on wpm-alias-expansion, pushed):
  1. echo -e: do not expand \u/\U (GNU prints literally; uutils #14414). Removed expansion + unused append_utf8; updated 2 unit tests that asserted old behavior.
  2. printf: malformed \u/\U in %b and format string -> "printf: missing hexadecimal number in escape" to stderr, drop rest, exit 1 (uutils #14404/#14406). EscapeResult gained bad_unicode flag; propagation through interpret_escapes/render_directive/render_once.
  3. find -nogroup: Windows "None" primary group now counts as "no POSIX group" -> fixed the last find platform-limitation test. Remaining failures: 5 ldd tests (Windows platform limitation).
- Test status after fixes: 2361/2366.
- i18n: new message key legacy.4741b0a7dde8ac5a (FNV-1a 64-bit, NOT sha1) added to unixwin/winuxcmd-i18n zh-CN catalog ("printf：转义序列中缺少十六进制数字"), commit d1b5312, pushed. i18n_batch.py extract/validate used; ~150 pre-existing key drift remains from earlier sessions.
- Gotchas: LNK1236 COFF link error is transient — retry build once before clean rebuild. Formatting via scripts/format.py is slow (~7 min, run in background) and fixes drift in files you didn't touch — commit style-only churn separately from functional fixes.

## 2026-09-07 run 2 (~21:35)
- Issue fetch fully rate-limited (GitHub 403, Savannah TLS ban persists) — no new data; existing K:\coreutils-issues datasets retained and reused.
- Fixed MSYS-path gap (commit cc1bc4f on wpm-alias-expansion, pushed): commands opening files via raw std::ifstream could not resolve Git-Bash style /d/... operands. Fixed narrow normalize_api_operand in utils/native_path.cppm to delegate to the wide impl, then wrapped open sites in wc, md5sum/sha1sum/sha224sum/sha256sum/sha384sum/sha512sum/b2sum/cksum/sum (incl. portable_digest::hash_file_hex, needs `import :native_path;`), rev, nl, fold, fmt, cut, paste, od, hexdump, grep (4 sites), file (wstring -> use _w variant), comm, cmp, dos2unix, d2u. ~30 sites / 24 files.
- Fixed base64/base32/basenc -w 0 emitting a trailing newline (GNU emits none); updated 5 unit test assertions (base32:65, base64:138, basenc:42/237/257).
- Tests back to baseline 2361/2366 (only the 5 known ldd platform-limitation failures). No new user-visible message keys -> no i18n repo sync needed.
- New gotchas: don't put helper python scripts in /tmp (Windows python resolves to d:\tmp); bash heredoc escapes \n inside single-quoted 'EOF' via the Bash tool — use the Edit tool for string-literal test edits. file.cpp read_file_header takes std::wstring — use normalize_api_operand_w there.

## 2026-09-07 run 3 (~22:40)
- Issue fetch: uutils refreshed (open_compact.json 1000 issues, newest #14433; 10 new since run 1). GNU Savannah still TLS/timed out — existing gnu data retained. New issues scanned for parity gaps.
- Fixed tr [c*n] handling (commit 66edee2 on wpm-alias-expansion, pushed; i18n repo commit 13d9904 pushed):
  1. `[*]`/`[c*0]` parsed as literal chars before (tr 'l' '[x*]' -> he[[o). Now indefinite repeat filling set2 up to set1 effective length, inserted at the repeat's own position (GNU tr.c semantics verified against real GNU: expansion is per-char with last-occurrence-wins, NOT element pairing).
  2. GNU validation added: [c*] in string1 rejected; >1 [c*] in string2 rejected; [c*] without translation mode rejected — exact GNU messages.
  3. Explicit counts: octal for leading zero (GNU xstrtoumax); malformed/overflow -> "invalid repeat count 'X' in [c*n] construct"; materialization clamped to 65536 (semantically identical for 256-entry table; GNU itself stalls on 1e12 counts, we now return instantly).
- chmod '4755,2' rejection verified CORRECT (GNU rejects comma-separated numeric modes too) — no change needed.
- i18n: 4 new keys command.tr.error.* added to i18n_batch.py MANUAL_MESSAGES + translate_error (dedicated prefix/suffix block for the dynamic one) + zh-CN catalog. zh runtime verified (build-vs/usr/bin/.wpm/i18n/zh-CN/catalog.json is the runtime catalog location — exe dir, not repo root!).
- Test suite gotcha: serial ctest HANGS at test 995 (logger) unless stdin is </dev/null — the test inherits an open pipe. PowerShell tool kills background tasks at 120s unless timeout param set; use Bash tool with run_in_background. ctest -j8 causes flaky tty/stty/sleep failures (terminal contention). Final: 2377/2382, only 5 known ldd failures.
- ~150 pre-existing i18n key drift between extract output and zh-CN catalog still open (untouched).

## 2026-09-08 run 4 (~00:48)
- uutils issues refreshed (1000 open, newest #14437); Savannah still blocked — gnu data reused.
- Verified already-GNU-correct: date --file dir (exit 1), chmod octal-in-list/empty-clause rejection.
- Fixed fold obsolete -WIDTH (uutils #14248, dispatcher rewrite hook) + GNU width error messages; du/df bare-suffix --block-size now prints unit letter (uutils #13605). Commit 8f1c09b pushed; i18n repo a055ad3 pushed (2 new keys + zh translations).
- Tests: 2381/2386 (5 known ldd platform failures), 8 new unit tests pass.

## 2026-09-08 run 5 (~02:45)
- uutils refreshed (1000 issues, newest #14438; open_compact.json rebuilt with 68 no-PR open issues). GNU NEWS + github_forks refreshed; Savannah HTML reused from previous day.
- Fixed shuf + split numeric diagnostics (commit 4044a0c on wpm-alias-expansion, pushed; i18n repo 4e9ae27 pushed):
  1. shuf -i errors now quote the WHOLE range arg (uutils #14402): fixed `1-` printing `''` and overflow printing only the lo bound.
  2. split messages aligned to GNU strtoint_die / parse_chunk (uutils #14398): `invalid number of lines|bytes|chunks: 'arg'` (quoted arg, includes zero), `invalid chunk number: 'k'` for k/N; -C reuses the "lines" message (GNU does too). -n parsing rewritten: non-numeric k part rejects the whole arg, N part errors quote N text.
- i18n: new keys common.error.invalid_num_chunks / invalid_chunk_number; GOTCHA: the quoted/fallbacks constexpr arrays in i18n.cppm have explicit sizes (was 32/26) — adding entries requires bumping them or MSVC C2078. Runtime catalog at build-vs/usr/bin/.wpm/i18n/zh-CN/ must be manually overwritten from the repo copy.
- Tests: 2386/2391 (only 5 known ldd platform-limitation failures); +16 new assertions all pass.
- Reconfirmed no-change-needed: base64/basenc "error: " prefix (#14396) and unexpand over-large tabstop (#14409) already GNU-correct.
- Renewed gotchas: Bash-tool heredoc turns \n into real newlines inside python strings — use Edit tool for test-literal edits; Windows python needs K:/ style paths, not /k/.


## 2026-09-08 run 6 (~04:05)
- Issue fetch fully rate-limited (GitHub 403); newest uutils still #14438 — K:\coreutils-issues datasets reused.
- Fixed date operand/option parity with GNU (commit f5e716a on wpm-alias-expansion, pushed; i18n repo c55b430 pushed):
  1. MAJOR: non-'+' positional operand was silently ignored (printed current time). Now parsed as POSIX set-clock MMDDhhmm[[CC]YY][.ss] via new parse_posix_clock; invalid -> invalid date exit 1.
  2. Added GNU checks: extra operand, "lacks a leading '+'" (operand + date option/--set), multiple output formats (-R/-I/--rfc-3339 + +FORMAT), mutually exclusive date sources (-d/-f/-r/--resolution), --set vs print options.
  3. --set and POSIX set-clock now print the new date (GNU), "date: cannot set date" on failure; --resolution now prints 0.000000100 (was "100ns").
- CMake fix: added src/utils/pager.cppm to the 3 hand-maintained module lists (winuxcmd-core / winuxcmd-standalone-* / another); standalone targets failed with C7621 missing partition "pager" after utils.cppm gained export import :pager.
- Tests: 2394/2399 final (only 5 known ldd platform failures); +9 new date unit tests all pass.
- New gotchas:
  - i18n_batch.py extract does NOT merge adjacent C string literals — multi-line concatenated messages get split into two fragment keys and the runtime full-string hash finds no translation. Keep messages as single-line literals.
  - Building a standalone target can leave usr/bin command hardlinks pointing at a broken partial-registry dispatcher (symptom: tests exit 127 + "未找到命令：X"; usr/bin/winuxcmd.exe 4.4MB instead of 17MB). Recovery: rebuild winuxcmd target, then `./winuxcmd.exe wpm links rebuild` inside build-vs/usr/bin.

## 2026-09-08 run 7 (~13:15)
- Issue fetch: uutils refreshed (1000 open, newest #14444, new since run 5: #14442 realpath quiet exit OK, #14443/#14444 lint-only); Savannah still blocked (timed out) — gnu data reused. Verified already-GNU-correct: realpath -q missing (exit 1, silent), chmod comma-forms rejection, date -r dir (prints dir mtime, exit 0).
- Fixed csplit parity (uutils #14407, commit 20516a0 on wpm-alias-expansion, pushed; i18n repo 81a4f73 pushed):
  1. GNU messages: "'<pat>': line number out of range" (both LineNumber kind and regex-offset crossing) and "'<pat>': match not found" (was "pattern 'X' not found").
  2. Incremental write refactor (OutputWriter): segments written as finalized; on fatal error sizes of closed files are printed, an in-progress file is materialized (empty for regex-offset errors, all-remaining-lines for line-number/not-found), and created files are deleted unless -k. Verified against Git Bash GNU 8.x differentially.
- Fixed date parity (uutils #14434/#14435):
  1. MAJOR: -u/--utc now parses zone-less date strings as UTC (was local-then-convert = double -8h shift). parse_fixed_date_time/parse_date_argument gained use_utc param; -d/--set/--file honor it.
  2. --file on a dir -> "date: <f>: read error: Is a directory"; missing -> "date: <f>: No such file or directory"; batch continues past invalid lines (exit 1 at end); "-f -" reads stdin.
  3. SetSystemTime API bug: SetSystemTime takes UTC, not local — old --set path converted to local then SetSystemTime (clock would land 8h off). Now --set sets UTC directly; POSIX operand uses SetLocalTime (or SetSystemTime when -u). NOT live-tested (mutating system clock is dangerous).
- NEW TOOL: scripts/build.py — Python wrapper for vcvars64+cmake+Ninja (parity with build-with-vs.ps1); flags: --skip-configure --target X --run-tests. Must pass the command STRING to cmd.exe (list args re-quote and break). All builds this run used it.
- i18n: new keys command.csplit.error.line_out_of_range / match_not_found + command.date.error.cannot_open / cannot_open_generic / read_error registered in i18n_batch.py + zh-CN catalog; dead legacy keys (911e0d8c68719ecc, 2763b225151998d4) removed after verifying source no longer emits them. GNU quoting rule learned: quotef = shell-escape "only when necessary" (unquoted simple names, used by date --file); quote()/quoteaf always quote (used by csplit).
- Tests: 2405/2410 full suite (only 5 known ldd platform failures); 41/41 date+csplit unit tests.
- CRITICAL gotchas (new):
  1. Do NOT issue parallel Edit tool calls against the SAME file — each reads from disk at call time and the last write clobbers earlier ones (cost 2 build cycles with C2065 before diagnosing).
  2. Bash tool cwd does NOT reliably persist between calls in this environment — a `cd /d/repo/winuxcmd-i18n` in one call was reset, so a follow-up `git pull --rebase origin main` ran in the WINUXCMD repo and started rebasing wpm-alias-expansion. Always use `git -C <path>` for the second repo (Windows-style "D:/repo/winuxcmd-i18n" works; "/d/repo/..." was rejected once). The rebase was aborted cleanly; wpm-alias-expansion push had already succeeded.
- Note: winuxcmd origin/main has advanced (32ff5f2 shim aliases, #192) — wpm-alias-expansion not rebased on it this run.

## 2026-09-08 run 8 (~15:00)
- Issue fetch: uutils refreshed (1000 open, newest #14444 — no new issues); Savannah still TLS-blocked, gnu data reused.
- Fixed date case-flag parity (uutils #14351, commit 789c798 on wpm-alias-expansion, pushed):
  1. GNU %^/%# case flags implemented per gnulib strftime semantics in date.cpp format_time: %^ forces upper (propagates into %c/%r expansions); %# = upper for %a/%A/%b/%h/%B, lower for %p/%P/%Z, no-op on %c/numerics; %P keeps forced-lower even under %^. Rule verified empirically against Git Bash GNU 8.32 before coding.
  2. Added %q (quarter of year, GNU 8.32+). Dangling '%' with flags and unknown specs emitted verbatim (matches GNU).
- Fixed seq scientific-notation precision (uutils #14153 family): precision = mantissa decimals - exponent (8.0e-1 -> "0.80"); was %g fallback. Strip leading '+' of exponent before std::from_chars (it rejects 'e+5').
- Fixed base64/base32/basenc -w diagnostics (uutils #14084): -w/--wrap changed INT_TYPE->STRING_TYPE so raw token reaches command; non-numeric/negative -> "invalid wrap size: '<raw>'"; print site now wraps errors with winux::i18n::translate_error (was previously untranslated — the exact-table entry existed but was never used).
- i18n: new key common.error.invalid_wrap_value registered in i18n_batch.py MANUAL_MESSAGES + i18n.cppm quoted[34->35]/fallbacks[28->29] (explicit array sizes must be bumped!) + zh-CN catalog "换行大小无效：'{}'" (i18n repo 88a041b pushed). GNU curly quotes '‘''’' rendered as ASCII ' per codebase convention.
- New runtime gotcha: build-vs/usr/bin/.wpm/i18n/ has NO en/ directory — English is built-in fallback; default runtime language is zh-CN even without WINUX_LANG (catalog auto-selected). Copy updated zh-CN catalog into build-vs/usr/bin/.wpm/i18n/zh-CN/ for runtime verification.
- Workflow gotcha: appending to test files while a background build is running = build misses them; do all edits before launching the build.
- Verified already-GNU-correct: dd error operand quoting, base64 -w 0 (no trailing newline), seq -w basic sci forms.
- Tests: full suite twice at 2408/2408 (-E ldd, ~4 min each); +10 new unit tests (date 3, seq 4, base64 3). Commits: winuxcmd 789c798 + 2ad2228 (pushed), i18n repo 88a041b (pushed).

## 2026-09-08 merge-to-main (~16:15, user-requested)
- wpm-alias-expansion (43 commits incl. all parity fixes from runs 1-8) merged into main via PR #193 (merge commit 5366f45). main is protected — direct push rejected (GH013); workaround: push local merge commit to temp branch merge/wpm-alias-expansion, gh pr create + gh pr merge, then delete all branches.
- Local main was diverged from origin (duplicate shim-aliases commit; origin's tree was a strict superset, verified +4 lines) -> reset to origin/main before merging.
- Conflict: only src/commands/wpm.cpp, 3 hunks; kept branch's create_package_aliases() refactor (HEAD's inline shim-layout alias block dropped; branch calls create_package_aliases unconditionally in install flow).
- Post-merge: full suite 2411/2411 (-E ldd). Deleted: local + remote wpm-alias-expansion, remote merge/wpm-alias-expansion. Local main now at 5366f45 == origin/main.

## 2026-09-08 run 9 (~14:55, 汇总统计请求)
- 未做源码修改。对 open_compact.json（640 open issues + 421 PR）+ Savannah 21 条做了全量分层统计并与 run 1-8 修复/验证清单对账。
- 结果：涉及 winuxcmd 命令的 open issues ~435（89/176 命令）；已处理 23 个编号；剩可行动 ~335（parity 248/panic 38/Windows 15/功能 34），不适用 ~100。加自有 ~150 条 i18n 漂移，在账 ~485，实口径约 250-300 真实要修。
- 产出（已 present）：K:/coreutils-issues/summary/ISSUE_TALLY_2026-09-08.md + final_tally.py + final_todo_list.json + final_tally_buckets.json。
- 下轮建议：先验 od #13608、printf #13866/#13867、numfmt #13937 四个已定位候选；i18n 漂移可脚本化批量处理。
