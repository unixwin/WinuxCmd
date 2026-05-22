# Command Guidance

Use the repository-local executables directly once the workspace is activated.

## Prefer these patterns

- Use `man.exe <command>` instead of `man` in PowerShell.
- Use explicit `.exe` names when a PowerShell alias could collide.
- `man` resolves to PowerShell help in many shells; `man.exe` forces the
  WinuxCmd executable.
- Prefer the repo's GNU-parity switches when they reduce AI mistakes:
  - `ls`: `-d`, `-b`, `-f`, `-I`, `-U`, `-X`, `-v`, `-i`, `-s`, `-h/--human-readable`, `--block-size=SIZE`, `-Q`, `-b`, `-q`, `-N`, `--quoting-style=WORD`; directory long listings include `total`, hard-link counts come from Windows file metadata, `-h` humanizes long sizes and `-s`/`total`, sort/time/quoting-affecting options use last-occurrence precedence, and wildcard matches that are directories list contents unless `-d` is active
  - `cp`: `-a/--archive`, `-p`, `-t`, `-T`, `-n`, `-u`, `-b/--backup`, `-S/--suffix`; `-t` requires an existing directory, `-t` and `-T` conflict, self-copy is refused except `--force --backup`, and plain file copies do not create missing parents
  - `mv`: `-t`, `-T`, `-n`, `-u`, `-b`, `-S`; `-t` requires an existing directory, `-t` and `-T` conflict, and ordinary multi-source moves require an existing target directory after wildcard expansion
  - `rm`: `-f`, `-d/--dir`, `-r/-R/--recursive`, `-I`, `-i`, `--interactive[=never|once|always]`, `--one-file-system`; `rm -f` with no operands succeeds, `-d` removes only empty directories, and `-f`/`-i`/`-I`/`--interactive=WHEN` use last-option precedence for prompting/force behavior
  - `install`: `-D`, `-d`, `-t`, `-T`; `-t` and `-T` conflict, multi-source installs require an existing target directory after wildcard expansion, `-D -t` creates the target directory, and `-d` creates parent directories
  - `mkdir`: `-p/--parents`, `-v/--verbose`, `-m/--mode`, `-Z/--context`; on Windows, `-m` maps write bits to the read-only attribute and `-p -m` applies only to the final named directory
  - `touch`: `-a`, `-m`, `-c`, `-d/--date`, `-r/--reference`, `-t`, `--time=access|atime|use|modify|mtime`; `-t [[CC]YY]MMDDhhmm[.ss]`, ISO-like dates, UTC/GMT/Z/offset suffixes, `@epoch`, and common relative dates are supported, and `-r` is the base for relative `-d`
  - `date`: `-d/--date`, `-u/--utc`, `-R/--rfc-email`, `--rfc-2822`, `-I/--iso-8601[=TIMESPEC]`, `--rfc-3339=TIMESPEC`, and common `+FORMAT` escapes are supported; use `date -u -d @0 +%F_%T` style commands for deterministic checks
  - `base64` / `base32`: `-d/--decode`, `-i/--ignore-garbage`, `-w/--wrap=COLS`; default wrap is 76, `--wrap=0` disables wrapping, `-` reads stdin, and extra file operands are rejected
  - `basenc`: pass exactly one selector such as `--base64`, `--base64url`, `--base32`, `--base32hex`, `--base16`, `--base2msbf`, or `--base2lsbf`; `--base58` and `--z85` are recognized but intentionally report not implemented
  - `sort`: repeatable `-k/--key` with `F[.C][OPTS][,F[.C][OPTS]]`, `--sort=WORD`, `-M`, `-R`, `--random-source`, `-S/--buffer-size`, `-s`, `-t \0`, `-u`, `-V`; `-t \0` uses NUL as the field separator, global ordering modes use last-occurrence precedence, `-n` does not accept leading `+` or exponent notation, `-h` compares sign then SI suffix then value, `-u` uses sort-key equality and disables last-resort whole-line ordering, `-c/-C -u` rejects adjacent equal keys, `-b` affects `-k F.C` character counts, and end position `.0` means field end
  - `grep`: `-E`, `-F`, `-G`, repeatable `-e/--regexp`, repeatable `-f/--file`, `-i`, `-v`, `-n`, `-c`, `-l`, `-L`, `-H/-h`, `--label`, `--line-buffered`, repeatable `--include/--exclude/--exclude-from/--exclude-dir`, `-A/-B/-C` context output, `-o`, `-D/--devices`, `--binary-files`, `-a`, `-I`, `--color[=WHEN]`; bare `--color` defaults to `auto`, `always|auto|never` are accepted, invalid color modes fail, zero-byte `-f` pattern files match nothing, include/exclude file rules keep command-line order and the last matching rule wins, command-line file operands match include/exclude globs by name suffix, and `--exclude-dir` skips matching command-line directories while ignoring trailing slashes; context ranges merge, match prefixes use `:`, context prefixes use `-`, and `-o` warns while ignoring context options; default binary mode suppresses detailed NUL-containing matches and reports a binary-match diagnostic unless `-q` is active; wildcard operands that expand to directories recurse when `-r/-R` is active
  - `diff`: `-q`, `-u`, `-y`, `-w`
  - `sed`: ordered repeatable `-e/--expression` and `-f/--file`, `-i[SUFFIX]` / `--in-place[=SUFFIX]`, `-n/--quiet/--silent`, `-E`, `-r`, `-s`, `-z/--null-data/--zero-terminated`, `-u/--unbuffered`, `-b/--binary`, `-l/--line-length`; in-place editing creates no backup when suffix is omitted, appends suffix when it has no `*`, and replaces each `*` with the current filename otherwise; `s///` supports `&`, `\1`-`\9`, numeric occurrence flags, `g`, `p`, and `I/i`; address negation with `!`, `first~step`, `0~step`, and `/regex/I` are supported; `=` prints line numbers and `l` lists escaped pattern space
  - `head`: `-c`, `-n`, `-q`, `-v`, `-z`; GNU count suffixes are accepted (`KB`/`MB`/.../`QB` decimal, `K`/`M`/.../`Q` binary, and `KiB`/.../`QiB` IEC) with overflow rejected; first-argument obsolete `-[num][bkm][cqv]` is accepted for compatibility, but prefer `-c NUM` or `-n NUM`
  - `tail`: `-c`, `-n`, `-q`, `-v`, `-z`, `-f`, `-F`, `-s`, `--max-unchanged-stats`, repeatable `--pid`, `--debug`, `--retry`; GNU count suffixes are accepted (`KB`/`MB`/.../`QB` decimal, `K`/`M`/.../`Q` binary, and `KiB`/.../`QiB` IEC) with overflow rejected; obsolete `-[num][bcl][f]` is accepted when it does not conflict with normal parsing, but prefer `-c NUM`, `-n NUM`, and `-f`
  - `tr`: `-c/-C`, `-d`, `-s`, `-t`, escapes, ascending byte ranges, and ASCII `[:CLASS:]` names are supported; delete+squeeze deletes from SET1 and then squeezes repeated bytes from the last specified set
  - `find`: `-L`, `-name`, `-iname`, `-path`, `-ipath`, `-regex`, `-iregex`, `-type`, `-size`, `-empty`, `-mtime`, `-mmin`, `-newer`, `-mindepth`, `-maxdepth`, `-depth`, `-true`, `-false`, `!`, `-not`, implicit AND, `-a/-and`, `-o/-or`, parentheses, `-print`, `-print0`, `-printf`, `-delete`, `-prune`, `-quit`, `-exec ... {} ;`, `-exec ... {} +`, `-ok ... {} ;`; predicate operators and these common actions follow GNU expression precedence and short-circuit behavior; remaining gaps are comma expressions, `-ok ... {} +`, full `-printf`, full GNU regex dialect details, and `-delete` plus `-prune` interaction edge cases
  - `cut`: `-b/--bytes`, `-c/--characters`, `-d/--delimiter`, `-f/--fields`, `-s/--only-delimited`, `--complement`, `-n/--no-partial`, `-O/--output-delimiter`, `-z`; field mode prints undelimited lines unless `-s` is set, and empty `-d ""` means NUL
  - `df`: `-a/--all`, `-B/--block-size=SIZE`, `-h`, `-H`, `-k`, `-T/--print-type`, `-i/--inodes`, `--sync`, `--no-sync`, `--total`
  - `du`: `-a`, `-A/--apparent-size`, `-B/--block-size`, `-b/--bytes`, `-c/--total`, `-d/--max-depth`, `-h`, `--si`, `-H/--dereference-args`, `-k`, `-s`, `-t/--threshold`, `--exclude=PATTERN`; default output uses 1024-byte blocks, later size-display options override earlier ones, `--block-size=human-readable|si` is accepted, `--max-depth=0` prints only the root total, `--exclude` patterns stay literal option patterns, Windows size reporting uses file length as the allocated/apparent-size approximation, and `-H` is not SI output
  - `wc`: `-c`, `-m`, `-l`, `-L`, `-w`, `--debug`, `--files0-from`, `--total`; `--debug` reports the portable counting path on stderr without changing stdout, `-m` counts UTF-8 characters separately from bytes, and `-L` expands tabs to 8-column stops
  - `basename`: `-a/--multiple`, `-s/--suffix`, `-z/--zero`; `-s` implies multi-name mode and `-z` emits NUL delimiters for safe parsing
  - `dirname`: multi-name operation and `-z/--zero`; use `-z` when paths may contain newlines
  - `stat`: `-c/--format`, `--printf`, `-f/--file-system`, `-L`, `-t`; common file and filesystem format fields are covered, and `--printf` interprets common backslash escapes
  - `readlink`: `-f`, `-e`, `-m`, `-n`, `-q/-s`, `-v`, `-z`; canonical modes normalize Windows paths while bare mode reads symlink/junction reparse targets, and default diagnostics are quiet unless `-v` is used
  - `realpath`: `-e`, `-m`, `-E`, `-q`, `-s`, `-z`, `--relative-to=DIR`, `--relative-base=DIR`; default mode requires existing parents and `-m` permits missing components
  - `truncate`: `-c`, `-s`, `-r`, `-o`; GNU size prefixes `+`, `-`, `<`, `>`, `/`, `%` and common decimal/binary/IEC suffixes are accepted; `--size` overrides `--reference`, option operands stay literal, and only file operands expand globs
  - `xargs`: `-n/--max-args`, `-L/--max-lines`, `-l`, `-I`, `-i/--replace`, `-P/--max-procs`, `-p/--interactive`, `-0/--null`, `-d/--delimiter`, `-a/--arg-file`, `-E`, `-e/--eof`, `--show-limits`, `-t/--verbose`, `-o/--open-tty`, `-r/--no-run-if-empty`, `-s/--max-chars`, `-x/--exit`, `--process-slot-var`; `-p` prompts before each command and runs only for `y`/`Y`, default echo output has no trailing space; mutually exclusive `-I/-i`, `-L/-l`, and `-n` families use the last conflicting family and warn, except `-I ... -n1`; child failures map GNU-style for common cases (`1..125 -> 123`, `255 -> 124`, not found -> `127`)
  - `printf`: format reuse across extra arguments, GNU missing-argument defaults, `%b`, width/precision, C-style numeric constants, `\c`, and common backslash escapes
  - `seq`: `-f/--format`, `-s/--separator`, `-w/--equal-width`; zero increments fail, decreasing/negative ranges work, and `-f` requires one floating conversion
  - `sum`: `-r/--bsd`, `-s/--sysv`; BSD is the default with 1024-byte block counts, System V uses byte-sum checksums and 512-byte block counts, and the last algorithm option wins
  - `tac`: `-s/--separator`, `-b/--before`, `-r/--regex`; each file is reversed independently, empty `-s ""` uses NUL, and `-b` attaches separators before records
  - `shuf`: `-e/--echo`, `-i/--input-range`, `-n/--head-count`, `-o/--output`, `--random-source`, `-r/--repeat`, `-z/--zero-terminated`; `-r` samples with replacement and `-o` reads input before writing output
  - `split`: `-l`, `-b`, `-C`, `-a`, `-d/--numeric-suffixes[=FROM]`, `-x/--hex-suffixes[=FROM]`, `--additional-suffix`; omitted input or `-` reads stdin, and ambiguous wildcard input is rejected
  - `csplit`: numeric line patterns, regex patterns with offsets, `%regex%` skip patterns, `{n}` / `{*}` repeats, `--suppress-matched`, `-q/--silent`, `-z`, `-f`, `-b`, and `-n` are supported; ambiguous wildcard input is rejected
  - `expand`: `-t/--tabs`, `-i/--initial`; GNU tab lists accept comma/blank separators, `/N`, and `+N`; tab-list option values stay literal and are not glob-expanded
  - `pathchk`: `-p`, `-P`, `--portability`; default checks reject empty or dash-leading components unless `POSIXLY_CORRECT` is set, `--portability` implies `-p -P`, and `-p` uses POSIX portable filename limits
  - `unexpand`: `-t/--tabs`, `-a/--all`, `--first-only`; `-t` implies all blanks, `--first-only` overrides it, and tab-list option values stay literal
  - `fold`: `-b/--bytes`, `-c/--characters`, `-s/--spaces`, `-w/--width`; widths are strictly parsed and tab/backspace/carriage-return columns are handled
  - `fmt`: `-c/--crown-margin`, `-t/--tagged-paragraph`, `-s/--split-only`, `-u/--uniform-spacing`, `-w/--width`, `-g/--goal`, `-p/--prefix`; preserves blank lines and indentation, and prefix mode formats only matching lines
  - `paste`: `-s/--serial`, `-d/--delimiters`, `-z`; delimiter lists cycle and accept `\0`, `\t`, `\n`, `\b`, `\f`, `\r`, `\v`, and `\\`; an empty delimiter list inserts no separator
  - `comm`: `-1`, `-2`, `-3`, `--check-order`, `--nocheck-order`, `--output-delimiter`, `--total`, `-z`; empty output delimiter means NUL
  - `join`: `-a`, `-v`, `-i`, `-1`, `-2`, `-j`, `-o FIELD-LIST`, `-o auto`, `-e`, `-t`, `--header`, `--check-order`, `--nocheck-order`, `-z`; header inference and NUL records are supported
  - `nl`: `-b`, `-d`, `-f`, `-h`, `-i`, `-l`, `-n`, `-p`, `-s`, `-v`, `-w`; logical pages, section-specific numbering including `pBRE`, empty separators, and negative start/increment values are supported
  - `env`: `-i/--ignore-environment`, repeatable `-u/--unset=NAME`, `-0/--null`, `-C/--chdir=DIR`, `NAME=VALUE COMMAND [ARG]...`
  - `dd`: `if=`, `of=`, `bs=`, `ibs=`, `obs=`, `count=`, `skip=`, `seek=`, `conv=sync`, `conv=notrunc`, `status=`
- Treat wildcard characters as syntax unless the operand is a file input:
  `grep` patterns, `sed` scripts, `jq` filters, `xargs` input items, and
  destination operands such as `tee OUT` or `install SRC DEST` stay literal.
  File input operands such as `grep PATTERN *.txt` and `sed SCRIPT *.txt`
  expand inside WinuxCmd.
- For fixed-arity file commands such as `diff`, `diff3`, `split`, and
  `csplit`, wildcard expansion must resolve to the exact operand count the
  command expects. Do not guess a first match.
- Keep the implementation matrix and GNU parity ledger synchronized whenever a
  command option changes.

## When inspecting the repo

- Prefer `rg` or `rg --files` for search and file discovery.
- Prefer `ls -d` or `ls -a` when you want directory names, not recursive noise.
- Avoid relying on shell aliases for command semantics.
