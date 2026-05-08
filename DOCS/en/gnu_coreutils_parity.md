# GNU Coreutils Parity Audit

This page tracks the first parity batch for AI-friendly, high-frequency GNU
options. It is a work queue, not a claim that every command is complete.

## Dependency stance

Do not add a third-party dependency for this batch. Prefer small internal
helpers built from the C++ standard library and Win32 APIs. Revisit dependencies
only when a tiny, well-maintained library clearly reduces parser/date/path
complexity without increasing release size materially.

| Command | GNU reference | Current state | Next gap |
|---|---|---|---|
| `cp` | [cp invocation](https://www.gnu.org/software/coreutils/manual/html_node/cp-invocation.html) | `-i`, `-r/-R`, `-t`, `-v`, `-f`, `-n`, `-u`, `-T` are aligned for the first batch | `-a`, `-d`, `-H`, `-L`, `-P`, `-p`, `-s`, `-l`, `-x`, `-Z` |
| `mv` | [mv invocation](https://www.gnu.org/software/coreutils/manual/html_node/mv-invocation.html) | `-t`, `-n`, `-u`, `-b`, `-S`, `--strip-trailing-slashes` are aligned for the first batch | `--exchange`, `--no-copy`, `--context`, stronger `--interactive` parity |
| `rm` | [rm invocation](https://www.gnu.org/software/coreutils/manual/html_node/rm-invocation.html) | `-I`, `-i`, recursive removal, root preservation, and Windows volume-based `--one-file-system` are tracked | exact `--interactive=once|always|never` parsing |
| `install` | [install invocation](https://www.gnu.org/software/coreutils/manual/html_node/install-invocation.html) | `-d`, `-b`, `-g`, `-m`, `-o`, `-p`, `-s`, `-t`, `-T`, `-D`, `--strip-program`, `--preserve-context`, `-Z` are parsed; target-directory behavior is implemented | real ownership/mode/strip/SELinux effects on Windows |
| `chmod` | [chmod invocation](https://www.gnu.org/software/coreutils/manual/html_node/chmod-invocation.html) | `-c`, `-f`, `-v`, `-R` exist | `--reference`, `-H`, `-L`, `-P`, root-preservation flags |
| `mkdir` | [mkdir invocation](https://www.gnu.org/software/coreutils/manual/html_node/mkdir-invocation.html) | `-p`, `-v` exist | `-m`, `-Z` |
| `touch` | [touch invocation](https://www.gnu.org/software/coreutils/manual/html_node/touch-invocation.html) | `-a`, `-c`, `-d`, `-m`, `-r`, `-t`, `--time` exist in varying form | `-h`, `--time=access|modify`, tighter GNU date parsing |
| `ls` | [ls invocation](https://www.gnu.org/software/coreutils/manual/html_node/ls-invocation.html) | common display flags plus `-B`, `-b`, `-d`, `-f`, `-F`, `-I`, `-p`, `-Q`, `-q`, `-R`, `-U`, `-v`, and `-X` now align better | remaining gaps: `-L`, `--time-style`, inode/block/context columns, and exact GNU quoting edge cases |
| `head` | [head invocation](https://www.gnu.org/software/coreutils/manual/html_node/head-invocation.html) | `-c`, `-n`, `-q`, `-v`, `-z` exist, plus legacy `-NUM` shorthand | exact GNU parsing for negative counts and mixed file modes |
| `tail` | [tail invocation](https://www.gnu.org/software/coreutils/manual/html_node/tail-invocation.html) | `-c`, `-n`, `-q`, `-v`, `-z` exist, plus legacy `-NUM` / `+NUM` shorthand | `-f`, `-F`, `--pid`, `--retry`, `--sleep-interval` |
| `sort` | [sort invocation](https://www.gnu.org/software/coreutils/manual/html_node/sort-invocation.html) | `-b`, `-f`, `-g`, `-h`, `-k`, `-n`, `-o`, `-t`, `-u`, `-V`, and `-z` are aligned for common AI workloads | `-d`, `-i`, `-M`, `-m`, `-R`, `-S`, `-s`, locale-sensitive collation edge cases |
| `uniq` | [uniq invocation](https://www.gnu.org/software/coreutils/manual/html_node/uniq-invocation.html) | `-c`, `-d`, `-D`, `-f`, `-i`, `-s`, `-u`, `-w`, and `-z` are aligned for the first batch | `--group` and exact GNU grouping separators |
| `cut` | [cut invocation](https://www.gnu.org/software/coreutils/manual/html_node/cut-invocation.html) | delimiter/fields exist | byte/char/line modes and `--complement` |
| `tr` | [tr invocation](https://www.gnu.org/software/coreutils/manual/html_node/tr.html) | basic translate/delete exists | `-c`, `-d`, `-s`, `-t`, class/range edge cases |
| `du` | [du invocation](https://www.gnu.org/software/coreutils/manual/html_node/du-invocation.html) | current build has baseline support | `-a`, `-c`, `-h`, `-L`, `-P`, `-x`, `--inodes`, `--time` |
| `readlink` | [readlink invocation](https://www.gnu.org/software/coreutils/manual/html_node/readlink-invocation.html) | baseline exists | `-f`, `-e`, `-m`, `-n`, `-z` |
| `realpath` | [realpath invocation](https://www.gnu.org/software/coreutils/manual/html_node/realpath-invocation.html) | baseline exists | `-e`, `-m`, `-s`, `-z` |
| `truncate` | [truncate invocation](https://www.gnu.org/software/coreutils/manual/html_node/truncate-invocation.html) | baseline exists | `-c`, `-s`, `-r`, `-o` |
| `basename` | [basename invocation](https://www.gnu.org/software/coreutils/manual/html_node/basename-invocation.html) | baseline exists | `-a`, `-s`, `-z` |
| `dirname` | [dirname invocation](https://www.gnu.org/software/coreutils/manual/html_node/dirname-invocation.html) | baseline exists | `-z` |
| `stat` | [stat invocation](https://www.gnu.org/software/coreutils/manual/html_node/stat-invocation.html) | baseline exists | `-c`, `-f`, `-L`, `-t` and Windows parity details |
