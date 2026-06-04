# Command Implementation Status

This document tracks the implementation status of commands in the WinuxCmd project, which now uses a pipeline-based architecture for command processing.

## Categories

### File Management

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `ls` | ✅ Done | High | List directory contents | `-a, --all`: Do not ignore entries starting with `.`<br>`-A, --almost-all`: Do not list implied `.` and `..`<br>`-b, --escape`: Print C-style escapes for nongraphic characters<br>`-d, --directory`: List directories themselves, not their contents<br>`-f`: List all entries in directory order<br>`-F, --classify`: Append indicator characters to entries<br>`--file-type`: Append type indicators but not `*` for executables<br>`-I, --ignore=PATTERN`: Ignore entries matching PATTERN<br>`-l, --long-list`: Use a long listing format<br>`-h, --human-readable`: With `-l`, print sizes in human readable format<br>`-r, --reverse`: Reverse order while sorting<br>`-t`: Sort by modification time<br>`-U`: Do not sort; list entries in directory order<br>`-v`: Natural sort of version-like numbers<br>`-X, --sort=WORD`: Sort by a selected key such as `extension`<br>`-n, --numeric-uid-gid`: Like `-l`, but list numeric user and group IDs<br>`-g`: Like `-l`, but do not list owner<br>`-o`: Like `-l`, but do not list group information<br>`-1`: List one file per line<br>`-C`: List entries by columns<br>`-w, --width`: Set output width to COLS<br>`--indicator-style=WORD`: Select slash/file-type/classify/none suffixes<br>`--color`: Colorize output | Implemented with pipeline architecture, supports color output, SmallVector optimization |
| `cat` | ✅ Done | High | Concatenate files and print on the standard output | `-A, --show-all`: Equivalent to `-vET`<br>`-b, --number-nonblank`: Number only non-empty output lines<br>`-e`: Equivalent to `-vE`<br>`-E, --show-ends`: Display $ at end of each line<br>`-n, --number`: Number all output lines<br>`-s, --squeeze-blank`: Squeeze multiple adjacent empty lines<br>`-t`: Equivalent to `-vT`<br>`-T, --show-tabs`: Display TAB characters as `^I`<br>`-u`: GNU compatibility placeholder, currently ignored<br>`-v, --show-nonprinting`: Show nonprinting characters | Simple file reading and writing with pipeline structure, SmallVector optimization |
| `cp` | ✅ Done | High | Copy files and directories | `-a, --archive`: Recursive copy and preserve Windows timestamps/attributes<br>`-p`: Preserve Windows timestamps/attributes<br>`-i, --interactive`: Prompt before overwrite<br>`-r, -R, --recursive`: Copy directories recursively<br>`-t, --target-directory`: Copy all SOURCE arguments into DIRECTORY<br>`-T, --no-target-directory`: Treat DEST as a normal file<br>`-n, --no-clobber`: Do not overwrite an existing file<br>`-u, --update`: Copy only when SOURCE is newer than DEST<br>`-v, --verbose`: Explain what is being done<br>`-f, --force`: Force copy without prompt | File system operations with error handling; owner/mode preservation is a Windows approximation |
| `mv` | ✅ Done | High | Move (rename) files | `-i, --interactive`: Prompt before overwrite<br>`-v, --verbose`: Explain what is being done<br>`-f, --force`: Do not prompt before overwriting<br>`-n, --no-clobber`: Do not overwrite an existing file | File system operations with error handling, SmallVector optimization |
| `rm` | ✅ Done | High | Remove files or directories | `-f, --force`: Ignore nonexistent files and arguments, never prompt<br>`-i, --interactive`: Prompt before every removal<br>`-r, -R, --recursive`: Remove directories and their contents recursively<br>`-v, --verbose`: Explain what is being done | File system operations with error handling |
| `mkdir` | ✅ Done | High | Make directories | `-p, --parents`: No error if existing, make parent directories as needed<br>`-v, --verbose`: Print a message for each created directory<br>`-m, --mode`: Set file mode (as in chmod) | File system operations with error handling |
| `rmdir` | ✅ Done | Medium | Remove empty directories | `--ignore-fail-on-non-empty`: Ignore each failure to remove a directory that is non-empty<br>`-p, --parents`: Remove DIRECTORY and its ancestors<br>`-v, --verbose`: Print a message for each removed directory | File system operations with error handling |
| `touch` | ✅ Done | Medium | Change file timestamps or create empty files | `-a`: Change only the access time<br>`-m`: Change only the modification time<br>`-c, --no-create`: Do not create any files<br>`-d, --date`: Parse STRING and use it instead of current time<br>`-r, --reference`: Use this file's times instead of current time<br>`-t, --time`: Use the specified timestamp instead of the current time<br>`-h, --no-dereference`: Change each symbolic link rather than any referenced file<br>`--time=WORD`: Change the specified time type | File system operations with error handling |
| `basename` | ✅ Done | Low | Strip directory and suffix from file names | `-a, --multiple`: Support multiple arguments and treat each as a NAME<br>`-s, --suffix=SUFFIX`: Remove a trailing SUFFIX<br>`-z, --zero`: End each output line with NUL | File name processing |
| `dirname` | ✅ Done | Low | Strip last component from file name | `-z, --zero`: End each output line with NUL | File name processing |
| `readlink` | ✅ Done | Low | Print resolved symbolic links or canonical file names | `-f, --canonicalize`: Canonicalize by following every symlink<br>`-e, --canonicalize-existing`: Canonicalize by following every symlink and verify existence<br>`-m, --canonicalize-missing`: Canonicalize by following every symlink and verify existence without requirement<br>`-n, --no-newline`: Do not output the trailing delimiter<br>`-z, --zero`: End each output line with NUL | Symbolic link resolution |
| `realpath` | ✅ Done | Low | Print the resolved absolute path | `-e, --canonicalize-existing`: All components of the path must exist<br>`-m, --canonicalize-missing`: No path components need to exist<br>`-L, --logical`: Accepted; Windows path normalization is used<br>`-P, --physical`: Accepted; Windows path normalization is used<br>`-q, --quiet`: Suppress error messages<br>`-s, --strip, --no-symlinks`: Don't expand symlinks<br>`-z, --zero`: End each output line with NUL | Path resolution; full GNU `-L` versus `-P` symlink ordering is still partial |
| `stat` | ✅ Done | Medium | Display file or file system status | `-c, --format=FORMAT`: Use the specified FORMAT instead of the default and append a newline<br>`--printf=FORMAT`: Like `--format`, but interpret backslash escapes and do not append a newline<br>`-f, --file-system`: Display file system status instead of file status<br>`-L, --dereference`: Follow symbolic links<br>`-t, --terse`: Display the information in terse form<br>Common format fields: `%n`, `%N`, `%s`, `%b`, `%B`, `%F`, `%A`, `%a`, `%h`, `%i`, `%d`, `%D`, `%o`, `%u`, `%g`, `%U`, `%G`, `%x/%X`, `%y/%Y`, `%z/%Z`, `%w/%W` | Windows-backed file status display; owner/group and symlink details are Windows approximations |
| `link` | ✅ Done | Medium | Create a link between two files | `-v, --verbose`: Print name of each file before linking<br>`-s, --symbolic`: Make symbolic links instead of hard links<br>`-f, --force`: Remove existing destination files<br>`-i, --interactive`: Prompt whether to remove destinations<br>`-n, --no-dereference`: Treat LINK_NAME as a normal file if it is a symbolic link to a directory | File linking |
| `unlink` | ✅ Done | Low | Remove a single file | No options | File removal |
| `truncate` | ✅ Done | Low | Shrink or extend the size of a file | `-c, --no-create`: Do not create files<br>`-s, --size=SIZE`: Set or adjust the file size by SIZE bytes<br>`-r, --reference=RFILE`: Base the size of FILE on the size of RFILE | File size modification |
| `sync` | ✅ Done | Low | Synchronize cached writes to persistent storage | No options | File system synchronization |
| `install` | ✅ Done | Medium | Copy files and set attributes | `-b, --backup[=CONTROL]`: Make a backup of each existing destination file<br>`-C, --compare`: Compare each pair of source and destination files<br>`-d, --directory`: Treat all arguments as directory names<br>`-D, --create-leading-dirs`: Create all leading components of DEST except the last<br>`-g, --group=GROUP`: Set group ownership<br>`-m, --mode=MODE`: Set permission mode<br>`-o, --owner=OWNER`: Set ownership<br>`-p, --preserve-timestamps`: Apply access/modification times of SOURCE files<br>`-s, --strip`: Strip symbol tables<br>`-t, --target-directory=DIRECTORY`: Install all SOURCE arguments into DIRECTORY<br>`-T, --no-target-directory`: Treat DEST as a normal file<br>`-v, --verbose`: Print the name of each directory as it is created | File installation with attributes |
| `find` | ✅ Done | High | Search for files in a directory hierarchy | `-name PATTERN`: Base file name matches shell pattern PATTERN<br>`-iname PATTERN`: Base file name matches shell pattern PATTERN, case-insensitive<br>`-path PATTERN`: Full path matches shell pattern PATTERN<br>`-ipath PATTERN`: Full path matches shell pattern PATTERN, case-insensitive<br>`-type X`: File is of type X (currently `f`, `d`, `l`)<br>`-size N`: File uses N units of space; supports `+N/-N/N` and `c/k/M/G` units<br>`-empty`: File or directory is empty<br>`-mtime N`: File data was last modified N*24 hours ago<br>`-mmin N`: File data was last modified N minutes ago<br>`-mindepth N`: Begin tests only after depth N<br>`-maxdepth N`: Descend at most N levels<br>`-print`: Print the full file name on standard output<br>`-print0`: Print the full file name followed by NUL<br>`-printf FORMAT`: Print selected GNU-style file fields (`%p`, `%f`, `%h`, `%y`, `%s`, `%m`, `%T@`, `%%`) and common escapes<br>`-prune`: Skip descending into directories in the current traversal<br>`-quit`: Stop the search immediately<br>`-L`: Follow symbolic links during traversal<br>`-delete`: Delete matched files/directories using depth-first traversal<br>`-exec COMMAND {} ;`: Execute once per match with `{}` replacement<br>`-exec COMMAND {} +`: Batch matched paths at the end of COMMAND<br>`-ok COMMAND {} ;`: Prompt before executing once per match<br>`-H`: tracked as [NOT SUPPORT] in the implementation | File searching; `-exec/-ok` support is action-oriented and not a full GNU expression parser |
| `xargs` | ✅ Done | High | Build and execute command lines from input | `-n, --max-args=MAX-ARGS`: Use at most MAX-ARGS arguments per command line<br>`-L, --max-lines=MAX-LINES`: Use at most MAX-LINES nonblank input lines per command line<br>`-l[LINES]`: Deprecated alias for `-L`; bare form defaults to 1 line<br>`-I R-STR`: Replace occurrences of R-STR in the initial arguments with names read from standard input<br>`-i, --replace[=R-STR]`: Deprecated alias for `-I`; bare form defaults to `{}`<br>`-P, --max-procs=MAX-PROCS`: Run at most MAX-PROCS processes at a time<br>`-a, --arg-file=FILE`: Read items from FILE instead of standard input<br>`-E EOF`: Set the logical EOF string<br>`-e[EOF], --eof[=EOF]`: Deprecated alias for `-E`; bare form leaves EOF disabled<br>`--show-limits`: Display command-line length limits<br>`-t, --verbose`: Print the command line on standard error before executing it<br>`-0, --null`: Input items are terminated by a null character instead of white space<br>`-d, --delimiter=DELIM`: Input items are terminated by DELIM instead of white space<br>`-o, --open-tty`: Reopen standard input as the console for children<br>`-r, --no-run-if-empty`: Do not run the command when input is empty<br>`-x, --exit`: Exit if the size limit is exceeded<br>`--process-slot-var=VAR`: Export a slot variable to children | Execute commands from input, SmallVector optimization |

### Text Processing

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `echo` | ✅ Done | High | Display a line of text | `-n`: Do not output the trailing newline<br>`-e`: Enable interpretation of backslash escapes<br>`-E`: Disable interpretation of backslash escapes<br>`-u, --upper`: Convert text to uppercase<br>`-r, --repeat N`: Repeat output N times | Implemented with pipeline architecture, serves as reference implementation, supports escape sequences including `\n`, `\t`, `\xHH`, `\uHHHH` |
| `grep` | ✅ Done | High | Print lines matching a pattern | `-E, --extended-regexp`: Interpret PATTERNS as extended regular expressions<br>`-F, --fixed-strings`: Interpret PATTERNS as fixed strings<br>`-G, --basic-regexp`: Interpret PATTERNS as basic regular expressions<br>`-e, --regexp=PATTERNS`: Use PATTERNS for matching; repeatable<br>`-f, --file=FILE`: Read patterns from FILE; repeatable<br>`-i, --ignore-case`: Ignore case distinctions<br>`-w, --word-regexp`: Match only whole words<br>`-x, --line-regexp`: Match only whole lines<br>`-z, --null-data`: Use NUL-delimited records<br>`-v, --invert-match`: Invert the sense of matching<br>`-m, --max-count=NUM`: Stop after NUM selected lines<br>`-b, --byte-offset`: Prefix byte offsets<br>`-n, --line-number`: Prefix line numbers<br>`-H, --with-filename`: Print file name prefixes<br>`-h, --no-filename`: Suppress file name prefixes<br>`-o, --only-matching`: Print only matching parts<br>`-q, --quiet`: Suppress normal output<br>`-r, --recursive`: Recurse into directories<br>`-R, --dereference-recursive`: Recurse and follow directory symlinks<br>`--include=GLOB`, `--exclude=GLOB`, `--exclude-from=FILE`, `--exclude-dir=GLOB`: Filter searched files<br>`-A/-B/-C`: Print context<br>`--group-separator=SEP`, `--no-group-separator`: Control context group separators<br>`--binary-files=TYPE`, `-a`, `-I`: Binary-file policy<br>`-D, --devices=ACTION`: Use `read` or `skip` for devices/FIFOs/sockets<br>`--color`: Highlight matches | Pattern matching with regex support, wildcard file operands, repeatable option storage, and SmallVector optimization |
| `sort` | ✅ Done | Medium | Sort lines of text files | `-b, --ignore-leading-blanks`: Ignore leading blanks<br>`-d, --dictionary-order`: Consider only blanks and alphanumerics<br>`-f, --ignore-case`: Fold lower case to upper case characters<br>`-g, --general-numeric-sort`: Compare according to general numeric value<br>`-h, --human-numeric-sort`: Compare according to human-readable numeric value<br>`-i, --ignore-nonprinting`: Ignore nonprinting characters<br>`-k, --key`: Repeatable key sort using `F[.C][OPTS][,F[.C][OPTS]]` ranges<br>`-M, --month-sort`: Sort by month name<br>`-n, --numeric-sort`: Compare according to string numerical value<br>`-r, --reverse`: Reverse the result of comparisons<br>`-R, --random-sort`: Random sort order<br>`--random-source=FILE`: Get deterministic random bytes from FILE<br>`-S, --buffer-size=SIZE`: Accept GNU SIZE forms as a memory hint<br>`-s, --stable`: Stable sort<br>`-u, --unique`: With -c, check for strict ordering<br>`-t, --field-separator`: Use SEP instead of non-blank to blank transition<br>`-z, --zero-terminated`: Line delimiter is NUL, not newline<br>`-o FILE`: Write the result to FILE instead of standard output | Sorting with ordered multi-key, key ranges, character positions, and common per-key modifiers |
| `wc` | ✅ Done | Medium | Print newline, word, and byte counts for each file | `-c, --bytes`: Print the byte counts<br>`-l, --lines`: Print the newline counts<br>`-w, --words`: Print the word counts<br>`-m, --chars`: Print the character counts<br>`-L, --max-line-length`: Print the maximum display width<br>`--files0-from=FILE`: Read file names from a NUL-separated list<br>`--total=WHEN`: Control how totals are printed | Character counting with multiple modes, SmallVector optimization |
| `head` | ✅ Done | Low | Output the first part of files | `-c, --bytes=[-]NUM`: Print the first N bytes, or count backward from the end with a negative value<br>`-n, --lines=[-]NUM`: Print the first N lines, or count backward from the end with a negative value<br>`-q, --quiet, --silent`: Never print headers giving file names<br>`-v, --verbose`: Always print headers giving file names<br>`-z, --zero-terminated`: Line delimiter is NUL, not newline | File head extraction with byte/line options, SmallVector optimization |
| `tail` | ✅ Done | Low | Output the last part of files | `-c, --bytes=[+]NUM`: Output the last N bytes, or start from an offset with a positive value<br>`-n, --lines=[+]NUM`: Output the last N lines, or start from an offset with a positive value<br>`-q, --quiet, --silent`: Never print headers giving file names<br>`-v, --verbose`: Always print headers giving file names<br>`-z, --zero-terminated`: Line delimiter is NUL, not newline<br>`-f, --follow`: Output appended data as the file grows<br>`--follow=name`: Follow by file name instead of descriptor<br>`-F`: Same as `--follow=name --retry`<br>`-s, --sleep-interval=SECONDS`: Pause between follow iterations<br>`--pid=PID`: Stop following after PID dies<br>`--retry`: Keep trying to open inaccessible files<br>`--max-unchanged-stats=N`: Reopen an unchanged followed name after N iterations | File tail extraction with follow-by-name support, SmallVector and ConstexprMap optimization |
| `sed` | ✅ Done | Medium | Stream editor for filtering and transforming text | `-e, --expression`: Add SCRIPT to the commands to be executed; repeatable and order-preserving with `-f`<br>`-f, --file`: Add the contents of SCRIPT-FILE to the commands; repeatable and order-preserving with `-e`<br>`-n, --quiet, --silent`: Suppress automatic printing of pattern space<br>`-i, --in-place`: Edit files in place | Text stream editing with ordered repeatable script support, SmallVector optimization |
| `uniq` | ✅ Done | Medium | Report or omit repeated lines | `-c, --count`: Prefix lines by the number of occurrences<br>`-d, --repeated`: Only print duplicate lines, one for each group<br>`-D, --all-repeated[=METHOD]`: Print all repeated lines for each group<br>`-f, --skip-fields`: Avoid comparing the first N fields<br>`-i, --ignore-case`: Ignore differences in case when comparing<br>`-s, --skip-chars`: Avoid comparing the first N characters<br>`--group[=METHOD]`: Group repeated lines in the output<br>`-u, --unique`: Only print unique lines<br>`-w, --check-chars`: Compare no more than N characters in lines<br>`-z, --zero-terminated`: Line delimiter is NUL, not newline | Duplicate line detection and filtering, SmallVector optimization; grouping modes are wired, while exact GNU separator edge cases still need polish |
| `cut` | ✅ Done | Medium | Remove sections from each line of files | `-b, --bytes`: Cut by byte position<br>`-c, --characters`: Cut by character position<br>`-d, --delimiter`: Use DELIM instead of TAB for field delimiter<br>`-f, --fields`: Select only these fields<br>`--complement`: Select the complement of specified bytes, characters, or fields<br>`--output-delimiter=STRING`: Use STRING between selected fields or byte/character ranges<br>`-s, --only-delimited`: Do not print lines not containing delimiters<br>`-z, --zero-terminated`: Line delimiter is NUL, not newline | Field, byte, and character extraction |
| `ptx` | ✅ Done | Low | Produce a permuted index of file contents | `-f, --ignore-case`: Ignore case differences<br>`-r, --references`: Output word references only<br>`-w, --width`: Set output width | Permuted index generation |
| `expand` | ✅ Done | Low | Convert tabs to spaces | `-t, --tabs=tab1[,tab2]...`: Set tab stops<br>`-i, --initial`: Do not convert tabs after non-blanks | Tab to space conversion |
| `unexpand` | ✅ Done | Low | Convert spaces to tabs | `-t, --tabs=tab1[,tab2]...`: Set tab stops<br>`-a, --all`: Convert all whitespace, not just initial<br>`--first-only`: Only convert the first run of spaces | Space to tab conversion |
| `fold` | ✅ Done | Low | Wrap each input line to fit specified width | `-w, --width=WIDTH`: Use WIDTH columns instead of 80<br>`-b, --bytes`: Count bytes rather than columns<br>`-c, --characters`: Count characters rather than columns<br>`-s, --spaces`: Break at spaces | Line wrapping |
| `fmt` | ✅ Done | Low | Simple optimal text formatter | `-w, --width=WIDTH`: Maximum line width (default 75)<br>`-p, --prefix=STRING`: Only format lines starting with STRING<br>`-c, --crown-margin`: Preserve crown margins<br>`-t, --tagged-paragraph`: Preserve tagged paragraphs<br>`-s, --split-only`: Only split long lines<br>`-u, --uniform-spacing`: Fix spacing | Text formatting |
| `nl` | ✅ Done | Low | Number lines of files | `-b, --body-numbering=STYLE`: Use STYLE for numbering body lines<br>`-n, --number-format=FORMAT`: Insert line numbers according to FORMAT<br>`-s, --number-separator=STRING`: Add STRING after (possible) line numbers<br>`-f, --footer-numbering=STYLE`: Footer numbering style<br>`-h, --header-numbering=STYLE`: Header numbering style<br>`-i, --line-increment=NUMBER`: Line increment<br>`-l, --join-blank-lines=NUMBER`: Join blank lines<br>`-v, --starting-line-number=NUMBER`: Starting line number<br>`-w, --number-width=NUMBER`: Number width | Line numbering |
| `paste` | ✅ Done | Low | Merge lines of files | `-d, --delimiters=LIST`: Reuse characters from LIST instead of TABs<br>`-s, --serial`: Paste one file at a time instead of in parallel<br>`-z, --zero-terminated`: End lines with NUL | Line merging |
| `pr` | ✅ Done | Low | Convert text files for printing | `+FIRST_PAGE[:LAST_PAGE]`: Begin [stop] printing with page FIRST_PAGE<br>`-C, --columns=NUM`: Output NUM columns and print columns down<br>`-a, --across`: Print columns across<br>`-d, --double-space`: Double-space the output<br>`-h, --header=HEADER`: Use a centered HEADER instead of filename<br>`-l, --length=PAGE_LENGTH`: Set the page length to PAGE_LENGTH lines<br>`-o, --indent=MARGIN`: Offset each line with MARGIN spaces<br>`-T, --omit-pagination`: Omit pagination controls | Text file formatting for printing |
| `tr` | ✅ Done | Medium | Translate or delete characters | `-c, -C, --complement`: Use the complement of SET1<br>`-d, --delete`: Delete characters in SET1<br>`-s, --squeeze-repeats`: Replace each sequence of a repeated character<br>`-t, --truncate-set1`: First truncate SET1 to length of SET2 | Character translation and deletion |
| `rev` | ✅ Done | Low | Reverse lines of a file | No options | Line reversal |
| `tac` | ✅ Done | Low | Concatenate and print files in reverse | `-b, --before`: Attach the separator before instead of after<br>`-r, --regex`: Interpret the separator as a regular expression<br>`-s, --separator=STRING`: Use STRING as the separator | Reverse concatenation |
| `shuf` | ✅ Done | Low | Shuffle input lines | `-e, --echo`: Treat each ARG as an input line<br>`-i, --input-range=LO-HI`: Treat each number LO through HI as an input line<br>`-n, --head-count=COUNT`: Output at most COUNT lines<br>`-o, --output=FILE`: Write result to FILE instead of standard output<br>`--random-source=FILE`: Read random bytes from FILE<br>`-r, --repeat`: Allow repeated output<br>`-z, --zero-terminated`: Use NUL as line terminator | Line shuffling |
| `split` | ✅ Done | Low | Split a file into pieces | `-l, --lines=NUMBER`: Put NUMBER lines per output file<br>`-b, --bytes=SIZE`: Put SIZE bytes per output file<br>`-C, --line-bytes=SIZE`: Put SIZE bytes per output file without splitting lines<br>`--filter=COMMAND`: Run COMMAND on each output file<br>`-n, --number=CHUNKS`: Produce CHUNKS chunks<br>`-a, --suffix-length=N`: Generate suffixes of length N<br>`-d, --numeric-suffixes[=FROM]`: Use numeric suffixes<br>`-x, --hex-suffixes[=FROM]`: Use hexadecimal suffixes<br>`--additional-suffix=SUFFIX`: Append SUFFIX to output files<br>`-e, --elide-empty-files`: Do not generate empty output files<br>`-t, --separator=SEP`: Use SEP as the record separator<br>`-u, --unbuffered`: Flush output after each line<br>`--verbose`: Print output file names | File splitting; the input operand uses the shared wildcard policy and ambiguous matches are rejected unless they resolve to exactly one file |
| `csplit` | ✅ Done | Low | Split a file into context-determined pieces | `-f, --prefix=PREFIX`: Use PREFIX as output file name prefix<br>`-b, --suffix-format=FORMAT`: Use FORMAT for suffixes<br>`-n, --digits=DIGITS`: Use specified number of digits<br>`-k, --keep-files`: Do not remove output files on errors<br>`--suppress-matched`: Suppress matching lines in output files<br>`-z, --elide-empty-files`: Elide empty output files<br>`-s, -q, --quiet, --silent`: Do not print output file sizes | Context-determined file splitting; the input operand uses the shared wildcard policy and ambiguous matches are rejected unless they resolve to exactly one file |
| `join` | ✅ Done | Low | Join lines of two files on a common field | `-1 FIELD`: Join on this FIELD of file 1<br>`-2 FIELD`: Join on this FIELD of file 2<br>`-a FILE-NUMBER`: Print unpairable lines from file NUMBER<br>`--check-order`: Check that input is correctly sorted<br>`--nocheck-order`: Do not check sort order<br>`-e STRING`: Replace empty output fields with STRING<br>`--header`: Treat first line of each file as header<br>`-i, --ignore-case`: Ignore case when comparing keys<br>`-j FIELD`: Join on FIELD in both files<br>`-o FIELD-LIST`: Output fields according to FIELD-LIST<br>`-t, --separator=CHAR`: Use CHAR as input and output field separator<br>`-v FILE-NUMBER`: Print only unpairable lines from file NUMBER<br>`-z, --zero-terminated`: Use NUL as line terminator | Line joining |
| `comm` | ✅ Done | Low | Compare two sorted files line by line | `-1`: Suppress lines unique to FILE1<br>`-2`: Suppress lines unique to FILE2<br>`-3`: Suppress lines that appear in both files<br>`--check-order`: Check that input is correctly sorted<br>`--nocheck-order`: Do not check sort order<br>`--output-delimiter=STRING`: Use STRING between columns<br>`--total`: Print totals<br>`-z, --zero-terminated`: Use NUL as line terminator | Sorted file comparison |

### System Information

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `which` | ✅ Done | High | Locate a command in PATH | `-a, --all`: Print all matching pathnames of each argument<br>`--read-alias`, `--skip-alias`: Read or skip alias expansion<br>`--read-functions`, `--skip-functions`: Read or skip shell function expansion<br>`--skip-dot`: Skip directories in PATH that start with a dot<br>`--show-dot`: Allow directories that start with a dot to be shown<br>`--skip-tilde`: Skip directories in PATH that start with a tilde<br>`--show-tilde`: Allow directories that start with a tilde to be shown<br>`--tty-only`: Show matches only on a tty | Path searching with PATHEXT support, SmallVector optimization |
| `env` | ✅ Done | Medium | Run a command in a modified environment | `-i, --ignore-environment`: Start with an empty environment<br>`-u, --unset=NAME`: Remove variable from the environment; repeatable<br>`-0, --null`: End each printed environment entry with NUL<br>`-C, --chdir=DIR`: Change working directory before printing or running COMMAND<br>`NAME=VALUE`: Add or override an environment variable<br>`COMMAND [ARG]...`: Run COMMAND with the modified environment | Prints or executes with a materialized Windows environment block; `-S/--split-string`, `-a/--argv0`, debug, and signal controls remain gaps |
| `pwd` | ✅ Done | High | Print working directory | `-L, --logical`: Use PWD from environment, even if it contains symlinks<br>`-P, --physical`: Avoid all symlinks | Working directory display; GNU defaults to physical mode unless overridden |
| `ps` | ✅ Done | High | Report process status | `-e, -A`: Select all processes<br>`-a`: Select all processes except both session leaders and processes not associated with a terminal<br>`-x`: Select processes without controlling ttys<br>`-f`: Full-format listing<br>`-l`: Long format<br>`-u USER`: Select by effective user ID or name<br>`-w`: Wide output<br>`--no-headers`: Print no header<br>`--sort=KEY`: Sort by column | Process listing and filtering; this is a repo-local implementation, not a GNU Coreutils manual page |
| `tee` | ✅ Done | Medium | Read from stdin and write to stdout and files | `-a, --append`: Append to the given FILEs, do not overwrite<br>`-i, --ignore-interrupts`: Ignore interrupt signals<br>`-p, --diagnose`: Write errors to standard error as they occur | Standard input/output redirection, SmallVector optimization |
| `chmod` | ✅ Done | Medium | Change file mode bits | `-c, --changes`: Like verbose but report only when a change is made<br>`-f, --silent, --quiet`: Suppress most error messages<br>`-v, --verbose`: Output a diagnostic for every file processed<br>`-R, --recursive`: Change files and directories recursively<br>`--reference=RFILE`: Use RFILE's mode instead of MODE values | File permission modification; file operands use the shared wildcard policy |
| `chown` | ✅ Done | Medium | Change file owner and group | `-c, --changes`: Like verbose but report only when a change is made<br>`-f, --silent, --quiet`: Suppress most error messages<br>`-v, --verbose`: Output a diagnostic for every file processed<br>`-R, --recursive`: Change files and directories recursively<br>`--reference=RFILE`: Use RFILE's owner and group instead of USER:GROUP | File ownership modification |
| `chgrp` | ✅ Done | Medium | Change group ownership | `-c, --changes`: Like verbose but report only when a change is made<br>`-f, --silent, --quiet`: Suppress most error messages<br>`-v, --verbose`: Output a diagnostic for every file processed<br>`-R, --recursive`: Change files and directories recursively<br>`--reference=RFILE`: Use RFILE's group instead of GROUP | File group ownership modification |
| `ln` | ✅ Done | Medium | Make links between files | `-s, --symbolic`: Make symbolic links instead of hard links<br>`-f, --force`: Remove existing destination files<br>`-i, --interactive`: Prompt whether to remove destinations<br>`-v, --verbose`: Print name of each linked file<br>`-n, --no-dereference`: Treat LINK_NAME as a normal file if it is a symbolic link to a directory | File linking (hard/symbolic) |
| `diff` | ✅ Done | Medium | Compare files line by line | `-u, --unified=NUM`: Output NUM (default 3) lines of unified context<br>`-q, --brief`: Output only whether files differ<br>`-i, --ignore-case`: Ignore case differences in file contents<br>`-w, --ignore-all-space`: Ignore all white space<br>`-B, --ignore-blank-lines`: Ignore changes whose lines are all blank<br>`-y, --side-by-side`: Output in two columns<br>`-r, --recursive`: Recursively compare any subdirectories [NOT SUPPORT] | File comparison; file operands use the shared wildcard policy and must resolve to exactly two files |
| `diff3` | ✅ Done | Low | Compare three files | `-e`: Output an ed script<br>`-3`: Like -e, but incorporate changes from the third file<br>`-m, --merge`: Output the merged file<br>`-i`: Like -e, but bracket changes<br>`-A`: Incorporate all changes from older to yours<br>`-E`: Ignore changes from older to yours<br>`-X`: Overwrite overlapping changes from older to yours | Three-way file comparison; file operands use the shared wildcard policy and must resolve to exactly three files |
| `sdiff` | ✅ Done | Low | Side-by-side merge of file differences | `-o FILE`: Write output to FILE<br>`-w, --width=NUM`: Set output width to NUM columns<br>`-l, --left-only`: Print only the left column when lines are common<br>`-s, --suppress-common-lines`: Do not print common lines | Side-by-side diff |
| `patch` | ✅ Done | Medium | Apply a diff file to an original | `-p NUM`: Strip NUM leading components from file names<br>`-i, --input=PATCHFILE`: Read patch from PATCHFILE<br>`-d, --directory=DIR`: Change to DIR<br>`-R, --reverse`: Assume patch was created with old and new files swapped<br>`-N, --forward`: Assume patch was created with old and new files swapped | Patch file application |
| `cmp` | ✅ Done | Low | Compare two files byte by byte | `-b, --print-bytes`: Print differing bytes<br>`-i, --ignore-initial=SKIP`: Skip the first SKIP bytes of both inputs<br>`-l, --verbose`: Output byte numbers and differing byte values<br>`-n, --bytes=LIMIT`: Compare at most LIMIT bytes | Byte comparison |
| `file` | ✅ Done | Medium | Determine file type | `-b, --brief`: Do not prepend filenames to output lines<br>`-i, --mime`: Output mime type strings<br>`-z, --compress`: Try to look inside compressed files<br>`--mime-type`: Output the MIME type only<br>`--mime-encoding`: Output the MIME encoding only | File type detection |
| `cksum` | ✅ Done | Low | CRC checksum and byte counts | No options in WinuxCmd yet; GNU parity still needs `-a/--algorithm`, `-c/--check`, `--tag`, `--untagged`, `--zero`, `--raw`, `--base64`, `--length`, and `--debug` | Checksum calculation |
| `sum` | ✅ Done | Low | Print checksum and block counts | `-r`: Use BSD checksum format<br>`-s`: Use System V checksum format | Legacy checksum compatibility |
| `md5sum` | ✅ Done | Low | Compute and check MD5 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read MD5 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | MD5 checksum |
| `sha1sum` | ✅ Done | Low | Compute and check SHA1 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read SHA1 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | SHA1 checksum |
| `sha224sum` | ✅ Done | Low | Compute and check SHA224 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read SHA224 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | SHA224 checksum |
| `sha256sum` | ✅ Done | Low | Compute and check SHA256 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read SHA256 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | SHA256 checksum |
| `sha384sum` | ✅ Done | Low | Compute and check SHA384 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read SHA384 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | SHA384 checksum |
| `sha512sum` | ✅ Done | Low | Compute and check SHA512 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read SHA512 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | SHA512 checksum |
| `b2sum` | ✅ Done | Low | Compute and check BLAKE2 message digest | `-b, --binary`: Read in binary mode<br>`-c, --check`: Read BLAKE2 sums from the FILEs and check them<br>`--tag`: Create a BSD-style checksum<br>`-t, --text`: Read in text mode<br>`-z, --zero`: End each output line with NUL | BLAKE2 checksum |
| `base64` | ✅ Done | Low | Base64 encode/decode data | `-d, --decode`: Decode data<br>`-i, --ignore-garbage`: When decoding, ignore non-alphabet characters<br>`-w, --wrap=COLS`: Wrap encoded lines after COLS characters | Base64 encoding/decoding |
| `base32` | ✅ Done | Low | Base32 encode/decode data | `-d, --decode`: Decode data<br>`-w, --wrap=COLS`: Wrap encoded lines after COLS characters | Base32 encoding/decoding |
| `basenc` | ✅ Done | Low | Encode/decode data with various algorithms | `-d, --decode`: Decode data<br>`-b, --base64`: Base64<br>`-z, --z85`: Z85<br>`-w, --wrap=COLS`: Wrap encoded lines after COLS characters | Various encoding/decoding |
| `xxd` | ✅ Done | Low | Make a hexdump or do the reverse | `-b, --bits`: Switch to bits (binary digits)<br>`-c, --cols COLS`: Format COLS octets per line<br>`-g, --groupsize BYTES`: Number of octets per group in normal output<br>`-l, --len LENGTH`: Stop after LENGTH octets<br>`-p, --ps`: Output in postscript continuous hexdump style<br>`-r, --reverse`: Reverse operation: convert (or patch) hexdump into binary<br>`-s, --seek OFFSET`: Start at OFFSET | Hexdump and reverse |
| `od` | ✅ Done | Low | Dump files in octal and other formats | `-A, --address-radix=RADIX`: Select the base in which file offsets are printed<br>`-j, --skip-bytes=BYTES`: Skip BYTES input bytes first<br>`-N, --read-bytes=BYTES`: Limit dump to BYTES bytes<br>`-t, --format=TYPE`: Select output format(s)<br>`-v, --output-duplicates`: Do not use * to mark line suppression<br>`-w, --width BYTES`: Output BYTES bytes per output line | Octal dump |
| `cpio` | ✅ Done | Low | Copy files to and from archives | `-o, --create`: Create the archive<br>`-i, --extract`: Extract files from an archive<br>`-t, --list`: List the contents of an archive<br>`-d, --make-directories`: Create leading directories where needed<br>`-m, --preserve-modification-time`: Retain previous file modification times when creating files | Archive copying |
| `dd` | ✅ Done | Medium | Convert and copy a file | `if=FILE`: Read from FILE instead of stdin<br>`of=FILE`: Write to FILE instead of stdout<br>`bs=BYTES`: Read and write up to BYTES bytes at a time<br>`ibs=BYTES`: Read up to BYTES bytes at a time<br>`obs=BYTES`: Write BYTES bytes at a time<br>`cbs=BYTES`: Convert BYTES bytes at a time [parsed]<br>`count=N`: Copy only N input blocks<br>`skip=N`: Skip N ibs-sized input blocks<br>`seek=N`: Skip N obs-sized output blocks<br>`conv=CONVS`: Supports `notrunc` and `sync`; accepts `noerror` as a placeholder<br>`status=none|noxfer|progress`: Control diagnostic output | File conversion and copying |
| `expr` | ✅ Done | Medium | Evaluate expressions | `--help`: Display help<br>`--version`: Output version information | Expression evaluation |
| `factor` | ✅ Done | Low | Print prime factors | No options | Prime factorization |
| `tsort` | ✅ Done | Low | Perform topological sort | No options | Topological ordering of pair lists |
| `seq` | ✅ Done | Low | Print a sequence of numbers | `-f, --format=FORMAT`: Use printf style floating-point FORMAT<br>`-s, --separator=STRING`: Use STRING to separate numbers<br>`-w, --equal-width`: Equalize width by padding with leading zeroes | Sequence generation |
| `yes` | ✅ Done | Low | Output a string repeatedly | `STRING`: String to output | Repeated output |
| `sleep` | ✅ Done | Low | Delay for a specified time | `NUMBER[SUFFIX]`: Pause for NUMBER seconds | Sleep delay |
| `stdbuf` | ✅ Done | Medium | Run COMMAND with modified buffering operations for its standard streams | `-i, --input=MODE`: Adjust standard input stream buffering<br>`-o, --output=MODE`: Adjust standard output stream buffering<br>`-e, --error=MODE`: Adjust standard error stream buffering | Buffer modification |
| `shred` | ✅ Done | Medium | Overwrite a file to hide its contents | `-f, --force`: Change permissions to allow writing<br>`-n, --iterations=N`: Overwrite N times instead of the default (3)<br>`-s, --size=N`: Shred this many bytes<br>`-u, --remove`: Delete and truncate file after overwriting<br>`-v, --verbose`: Show progress<br>`-z, --zero`: Add a final overwrite with zeros | Secure file deletion |
| `pathchk` | ✅ Done | Low | Check whether file names are valid or portable | `-p, --portability`: Check based on POSIX<br>`-P, --posix`: Check based on POSIX<br>`--help`: Display help<br>`--version`: Output version information | Path validation |
| `tree` | ✅ Done | Medium | List contents of directories in a tree-like format | `-a, --all`: All files are listed<br>`-d, --directory-list`: List directories only<br>`-f, --full-path`: Print full path prefix for each file<br>`-L, --level=LEVEL`: Descend only level directories deep<br>`--help`: Display help<br>`--version`: Output version information | Tree display |
| `less` | ✅ Done | High | View file (or stdin) with scrollable page | `-E, --QUIT-AT-EOF`: Quit at EOF<br>`-F, --quit-if-one-screen`: Quit if entire file fits on first screen<br>`-R, --RAW-CONTROL-CHARS`: Output "raw" control characters<br>`-S, --chop-long-lines`: Chops long lines | File viewer |
| `cat` | ✅ Done | High | Concatenate files and print on the standard output | `-n, --number`: Number all output lines<br>`-b, --number-nonblank`: Number non-empty output lines<br>`-E, --show-ends`: Display $ at end of each line<br>`-s, --squeeze-blank`: Squeeze multiple adjacent empty lines<br>`-T, --show-tabs`: Display TAB characters as `^I` | Simple file reading and writing with pipeline structure, SmallVector optimization |
| `cp` | ✅ Done | High | Copy files and directories | `-i, --interactive`: Prompt before overwrite<br>`-r, --recursive`: Copy directories recursively<br>`-v, --verbose`: Explain what is being done<br>`-f, --force`: Force copy without prompt | File system operations with error handling |
| `mv` | ✅ Done | High | Move (rename) files | `-i, --interactive`: Prompt before overwrite<br>`-v, --verbose`: Explain what is being done<br>`-f, --force`: Do not prompt before overwriting<br>`-n, --no-clobber`: Do not overwrite an existing file | File system operations with error handling, SmallVector optimization |
| `rm` | ✅ Done | High | Remove files or directories | `-f, --force`: Ignore nonexistent files and arguments, never prompt<br>`-i, --interactive`: Prompt before every removal<br>`-r, -R, --recursive`: Remove directories and their contents recursively<br>`-v, --verbose`: Explain what is being done | File system operations with error handling |
| `mkdir` | ✅ Done | High | Make directories | `-p, --parents`: No error if existing, make parent directories as needed<br>`-v, --verbose`: Print a message for each created directory<br>`-m, --mode`: Set file mode (as in chmod) | File system operations with error handling |
| `rmdir` | ✅ Done | Medium | Remove empty directories | `--ignore-fail-on-non-empty`: Ignore each failure to remove a directory that is non-empty<br>`-p, --parents`: Remove DIRECTORY and its ancestors<br>`-v, --verbose`: Print a message for each removed directory | File system operations with error handling |

### Network

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `ping` | ❌ TODO | High | Send ICMP ECHO_REQUEST to network hosts | `-c COUNT`: Stop after sending COUNT ECHO_REQUEST packets<br>`-i INTERVAL`: Wait INTERVAL seconds between sending each packet | Not yet implemented |
| `curl` | ❌ TODO | Medium | Transfer data from or to a server | `-s, --silent`: Silent mode<br>`-o, --output`: Write output to a file instead of stdout | Not yet implemented |
| `wget` | ❌ TODO | Medium | Non-interactive network downloader | `-O, --output-document=FILE`: Write documents to FILE<br>`-q, --quiet`: Turn off wget's output | Not yet implemented |

### Testing and Terminal Control

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `test` | ✅ Done | High | Check file types and compare values | `-b FILE`: FILE exists and is block special<br>`-c FILE`: FILE exists and is character special<br>`-d FILE`: FILE exists and is a directory<br>`-e FILE`: FILE exists<br>`-f FILE`: FILE exists and is a regular file<br>`-g FILE`: FILE exists and is set-group-ID<br>`-G FILE`: FILE exists and is owned by the effective group ID<br>`-h FILE`: FILE exists and is a symbolic link<br>`-k FILE`: FILE exists and has its sticky bit set<br>`-L FILE`: FILE exists and is a symbolic link<br>`-O FILE`: FILE exists and is owned by the effective user ID<br>`-p FILE`: FILE exists and is a named pipe<br>`-r FILE`: FILE exists and read permission is granted<br>`-s FILE`: FILE exists and has a size greater than zero<br>`-S FILE`: FILE exists and is a socket<br>`-t FD`: File descriptor FD is opened on a terminal<br>`-u FILE`: FILE exists and its set-user-ID bit is set<br>`-w FILE`: FILE exists and write permission is granted<br>`-x FILE`: FILE exists and execute permission is granted<br>`-z STRING`: the length of STRING is zero<br>`-n STRING`: the length of STRING is nonzero<br>`STRING1 = STRING2`: the strings are equal<br>`STRING1 != STRING2`: the strings are not equal<br>`INTEGER1 -eq INTEGER2`: INTEGER1 is equal to INTEGER2<br>`INTEGER1 -ne INTEGER2`: INTEGER1 is not equal to INTEGER2<br>`INTEGER1 -gt INTEGER2`: INTEGER1 is greater than INTEGER2<br>`INTEGER1 -ge INTEGER2`: INTEGER1 is greater than or equal to INTEGER2<br>`INTEGER1 -lt INTEGER2`: INTEGER1 is less than INTEGER2<br>`INTEGER1 -le INTEGER2`: INTEGER1 is less than or equal to INTEGER2<br>`! EXPR`: EXPR is false<br>`EXPR1 -a EXPR2`: Both EXPR1 and EXPR2 are true<br>`EXPR1 -o EXPR2`: Either EXPR1 or EXPR2 is true | File type checking and value comparison |
| `[` | ✅ Done | High | Check file types and compare values (same as test) | Same as test | Alias for test |
| `test_bracket` | ✅ Done | High | Internal entry point for `[` | Same as test | Shared syntax and dispatch wrapper for `test` / `[` |
| `true` | ✅ Done | Low | Return a successful result | No options | Always returns 0 |
| `false` | ✅ Done | Low | Return an unsuccessful result | No options | Always returns 1 |
| `clear` | ✅ Done | High | Clear the terminal screen | No options | Uses system call to clear screen |
| `reset` | ✅ Done | Low | Reinitialize the terminal | No options | Terminal reset |
| `tput` | ✅ Done | Low | Initialize a terminal or query terminfo database | `STRING`: Terminfo capability to query | Terminal capability query |
| `tic` | ✅ Done | Low | Terminfo entry-description compiler | No options | Terminfo compilation |
| `toe` | ✅ Done | Low | Table of (terminfo) entries | No options | Terminfo table display |
| `tty` | ✅ Done | Low | Print the file name of the terminal connected to standard input | `-s, --silent, --quiet`: Print nothing, only return an exit status | Terminal file name display |
| `stty` | ✅ Done | Medium | Change and print terminal line settings | `-a, --all`: Print all current settings<br>`-g, --save`: Print all current settings in stty-readable form<br>`-F, --file=DEVICE`: Open and use the specified DEVICE instead of stdin | Terminal settings |
| `infocmp` | ✅ Done | Low | Compare or print out terminfo descriptions | `-d, --differences`: Print differences<br>`-c, --compare`: Print comparisons<br>`-I, --use-terminfo`: Use the terminfo names | Terminfo comparison |
| `getconf` | ✅ Done | Low | Query system configuration variables | `-a, --all`: Print all configuration variables<br>`-v, --variable-specification`: Return variable based on variable specification | System configuration query |
| `locale` | ✅ Done | Low | Get locale-specific information | `-a, --all-locales`: List all available locales<br>`-m, --charmaps`: List available charmaps<br>`-c, --category-name`: List names of available categories<br>`-k, --keyword-name`: List names of available keywords | Locale information |
| `localedef` | ✅ Done | Medium | Compile locale definition files | `-f, --charmap=FILE`: Symbolic character names defined in FILE<br>`-i, --inputfile=FILE`: Locale definition file<br>`-u, --alias-file=FILE`: Alias file for locale names | Locale definition compilation |
| `tzset` | ✅ Done | Low | Initialize timezone information | No options | Timezone initialization |
| `iconv` | ✅ Done | Medium | Convert text from one encoding to another | `-f, --from-code=NAME`: Encoding of original text<br>`-t, --to-code=NAME`: Encoding for output<br>`-l, --list`: List all known coded character sets<br>`-c`: Omit invalid characters<br>`-s`: Suppress warnings | Encoding conversion |
| `cygpath` | ✅ Done | Medium | Convert Windows and POSIX path names | `-u, --unix`: Print Unix (POSIX) form of path<br>`-w, --windows`: Print Windows form of path<br>`-m, --mixed`: Print Windows form, with regular slashes<br>`-a, --absolute`: Output absolute path | Path conversion |
| `dos2unix` | ✅ Done | Low | Convert DOS line endings to Unix format | No options | Line ending conversion |
| `unix2dos` | ✅ Done | Low | Convert Unix line endings to DOS format | No options | Line ending conversion |
| `d2u` | ✅ Done | Low | Alias for dos2unix | No options | Line ending conversion |
| `u2d` | ✅ Done | Low | Alias for unix2dos | No options | Line ending conversion |
| `printf` | ✅ Done | High | Format and print data | `FORMAT`: Format string<br>`ARGUMENTS`: Arguments to format | Formatted output |

### System Utilities

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `date` | ✅ Done | Medium | Print or set system date/time | `-d, --date=STRING`: Display time described by STRING<br>`--debug`: Print date-parsing diagnostics<br>`-f, --file=DATEFILE`: Read date strings from a file<br>`-I, --iso-8601[=TIMESPEC]`: ISO 8601 output<br>`-r, --reference=FILE`: Use FILE's time<br>`--resolution`: Print time resolution<br>`-R, --rfc-email`: RFC 5322-style date output<br>`--rfc-3339=TIMESPEC`: RFC 3339-style output<br>`-s, --set=STRING`: Set the system time<br>`-u, --utc, --universal`: Print or set Coordinated Universal Time (UTC)<br>`+FORMAT`: Output format string | Date/time display and formatting |
| `df` | ✅ Done | Medium | Report file system disk space usage | `-h, --human-readable`: Print sizes in powers of 1024<br>`-H, --si`: Print sizes in powers of 1000<br>`-k`: Use 1K blocks<br>`-T, --print-type`: Print file system type<br>`-i, --inodes`: List inode-shaped information instead of block usage; Windows prints placeholder inode columns<br>File operands use the shared wildcard policy<br>`-t, --type=TYPE`, `-x, --exclude-type=TYPE`, `-a, --all`: tracked in the GNU parity ledger | Disk space reporting |
| `du` | ✅ Done | Medium | Estimate file space usage | `-a, --all`: Write counts for all files, not just directories<br>`-B, --block-size=SIZE`: Scale sizes by SIZE before printing<br>`-b, --bytes`: Equivalent to apparent-size with block size 1<br>`-c, --total`: Produce a grand total<br>`-d, --max-depth=N`: Print entries only if they are N or fewer levels below the command line argument<br>`-h, --human-readable`: Print sizes in powers of 1024<br>`-H, --si`: Print sizes in powers of 1000<br>`-k`: Use 1K blocks<br>`-s, --summarize`: Display only a total for each argument | File/directory size estimation; file operands use the shared wildcard policy and block counts round up |
| `kill` | ✅ Done | High | Send a signal to processes | `-l, --list`: List all signal names<br>`-s, --signal=SIGNAL`: Send the specified signal<br>`-t, --table`: List signals in a table<br>`SIGNAL`: Signal number or name (e.g., -9, -KILL, -15, -TERM) | Process signal sending |
| `nice` | ✅ Done | Medium | Run a program with modified scheduling priority | `-n, --adjustment=N`: Add integer N to the scheduling priority<br>`--help`: Display help<br>`--version`: Output version information | Process priority modification |
| `nohup` | ✅ Done | Medium | Run a command immune to hangups | `--help`: Display help<br>`--version`: Output version information | Immune to hangups |
| `renice` | ✅ Done | Medium | Alter priority of running processes | `-n, --priority NUM`: Specify scheduling priority<br>`-p, --pid PID`: Interpret argument as process ID<br>`-u, --user USER`: Interpret argument as username<br>`-g, --pgrp PGID`: Interpret argument as process group ID | Alter process priority |
| `top` | ✅ Done | High | Display Linux processes | `-b, --batch`: Batch mode<br>`-d, --delay=SECONDS`: Delay between updates<br>`-n, --iterations=NUM`: Number of iterations<br>`-p, --pid=PID`: Monitor specific PIDs<br>`-u, --user=USER`: Monitor specific user<br>`-c, --command`: Show command line<br>`-o, --field-sort=FIELD`: Sort by field (CPU, MEM, TIME, PID, NAME)<br>`-h, --help`: Display help<br>`-v, --version`: Output version information | Real-time process monitor with interactive controls |
| `watch` | ✅ Done | Medium | Execute a program periodically | `-n, --interval=SECONDS`: Seconds to wait between updates<br>`-d, --differences[=cumulative]`: Highlight changes between updates<br>`-t, --no-title`: Turn off the header<br>`-x, --exec`: Pass command to exec instead of sh | Periodic execution |
| `timeout` | ✅ Done | Medium | Run a command with a time limit | `-s, --signal=SIGNAL`: Send this signal on timeout<br>`--preserve-status`: Exit with the same status as COMMAND<br>`--foreground`: When not running timeout directly from a shell prompt, allow COMMAND to read from the TTY<br>`-k, --kill-after=DURATION`: If the command is still running after the initial timeout, send this signal | Time-limited execution |
| `jobs` | ✅ Done | Medium | Display status of jobs in the current shell | `-l, --list`: List process IDs in addition to the normal information<br>`-n, --names`: List only jobs that have changed status since the last notification<br>`-p, --pid`: List only the process IDs of the job's process group leader<br>`-r, --running`: List only running jobs<br>`-s, --stopped`: List only stopped jobs | Job status display |
| `bg` | ✅ Done | Medium | Resume a job in the background | JOB_SPEC: Job specification or - for current job | Background job resume |
| `fg` | ✅ Done | Medium | Resume a job in the foreground | JOB_SPEC: Job specification or - for current job | Foreground job resume |
| `disown` | ✅ Done | Medium | Remove jobs from current shell | `-a, --all`: Remove all jobs if JOB_SPEC is not supplied<br>`-h, --help`: Display help<br>`-r, --running`: Remove only running jobs | Job removal |
| `wait` | ✅ Done | Medium | Wait for process to finish | PID: Process ID to wait for | Process wait |
| `env` | ✅ Done | Medium | Run a command in a modified environment | `-i, --ignore-environment`: Start with an empty environment<br>`-u, --unset=NAME`: Remove variable from the environment; repeatable<br>`-0, --null`: End each printed environment entry with NUL<br>`-C, --chdir=DIR`: Change working directory before printing or running COMMAND<br>`NAME=VALUE`: Add or override an environment variable<br>`COMMAND [ARG]...`: Run COMMAND with the modified environment | Prints or executes with a materialized Windows environment block; `-S/--split-string`, `-a/--argv0`, debug, and signal controls remain gaps |
| `printenv` | ✅ Done | Medium | Print all or part of environment | `--null`: End each output line with NUL<br>`-u, --unset=NAME`: Remove variable NAME | Environment variable display |
| `export` | ✅ Done | Medium | Set an environment variable | No options | Environment variable export |
| `unset` | ✅ Done | Medium | Unset values and attributes of shell variables | No options | Variable unset |
| `logname` | ✅ Done | Low | Print user's login name | No options | Login name display |
| `whoami` | ✅ Done | Low | Print effective userid | No options | User ID display |
| `id` | ✅ Done | Medium | Print user and group information | `-a, --all`: Ignore, for compatibility with other versions<br>`-g, --group`: Print only the effective group ID<br>`-G, --groups`: Print all group IDs<br>`-n, --name`: Print a name instead of a number<br>`-r, --real`: Print the real ID instead of the effective ID<br>`-u, --user`: Print only the effective user ID | User and group information |
| `users` | ✅ Done | Low | Print the user names of users currently logged in | No options | Logged-in users display |
| `who` | ✅ Done | Low | Show who is logged on | `-a, --all`: Same as -b -d --login -p -r -t -T -u<br>`-b, --boot`: Time of last system boot<br>`-d, --dead`: Print dead processes<br>`-H, --heading`: Print line of column headings<br>`-l, --login`: Print system login processes<br>`-m`: Only hostname and user associated with stdin<br>`-p, --process`: Print active processes spawned by init<br>`-q, --count`: All login names and number of users logged on<br>`-r, --runlevel`: Print current runlevel<br>`-s, --short`: Print only name, line, and time<br>`-t, --time`: Print last system clock change<br>`-u, --users`: List users logged in | User information |
| `groups` | ✅ Done | Low | Print the groups a user is in | `--help`: Display help<br>`--version`: Output version information | Group information |
| `hostname` | ✅ Done | Low | Show or set the system's host name | `-a, --alias`: Alias names<br>`-A, --all-fqdns`: All long host names<br>`-d, --domain`: DNS domain name<br>`-f, --fqdn`: FQDN<br>`-i, --ip-address`: Addresses for the host name<br>`-I, --all-ip-addresses`: All addresses for the host<br>`-s, --short`: Short host name<br>`-y, --yp, --nis`: NIS/YP domain name | Host name display |
| `hostid` | ✅ Done | Low | Print the numeric identifier of the current host | No options | Host ID display |
| `uname` | ✅ Done | Medium | Print system information | `-a, --all`: Print all information<br>`-s, --kernel-name`: Print the kernel name<br>`-n, --nodename`: Print the network node hostname<br>`-r, --kernel-release`: Print the kernel release<br>`-v, --kernel-version`: Print the kernel version<br>`-m, --machine`: Print the machine hardware name<br>`-p, --processor`: Print the processor type<br>`-i, --hardware-platform`: Print the hardware platform<br>`-o, --operating-system`: Print the operating system | System information |
| `arch` | ✅ Done | Low | Print machine architecture | No options | Architecture display |
| `uptime` | ✅ Done | Medium | Tell how long the system has been running | `-p, --pretty`: Show uptime in pretty format<br>`-s, --since`: System up since<br>`-h, --help`: Display help<br>`-V, --version`: Output version information | System uptime |
| `date` | ✅ Done | Medium | Print or set system date/time | `-d, --date=STRING`: Display time described by STRING<br>`--debug`: Print date-parsing diagnostics<br>`-f, --file=DATEFILE`: Read date strings from a file<br>`-I, --iso-8601[=TIMESPEC]`: ISO 8601 output<br>`-r, --reference=FILE`: Use FILE's time<br>`--resolution`: Print time resolution<br>`-R, --rfc-email`: RFC 5322-style date output<br>`--rfc-3339=TIMESPEC`: RFC 3339-style output<br>`-s, --set=STRING`: Set the system time<br>`-u, --utc, --universal`: Print or set Coordinated Universal Time (UTC)<br>`+FORMAT`: Output format string | Date/time display and formatting |
| `cal` | ✅ Done | Low | Display a calendar | `-1, --one`: Show only the current month (default)<br>`-3, --three`: Show previous, current and next month<br>`-s, --sunday`: Sunday as first day of week<br>`-m, --monday`: Monday as first day of week<br>`-j, --julian`: Output Julian dates<br>`-y, --year`: Show whole current year<br>`-Y, --year=YEAR`: Show whole year<br>`-w, --week[=TYPE]`: Show week numbers | Calendar display |
| `uptime` | ✅ Done | Medium | Tell how long the system has been running | `-p, --pretty`: Show uptime in pretty format<br>`-s, --since`: System up since | System uptime |
| `df` | ✅ Done | Medium | Report file system disk space usage | `-h, --human-readable`: Print sizes in powers of 1024<br>`-H, --si`: Print sizes in powers of 1000<br>`-k`: Use 1K blocks<br>`-T, --print-type`: Print file system type<br>`-i, --inodes`: List inode-shaped information instead of block usage; Windows prints placeholder inode columns<br>File operands use the shared wildcard policy<br>`-t, --type=TYPE`, `-x, --exclude-type=TYPE`, `-a, --all`: tracked in the GNU parity ledger | Disk space reporting |
| `du` | ✅ Done | Medium | Estimate file space usage | `-a, --all`: Write counts for all files, not just directories<br>`-B, --block-size=SIZE`: Scale sizes by SIZE before printing<br>`-b, --bytes`: Equivalent to apparent-size with block size 1<br>`-c, --total`: Produce a grand total<br>`-d, --max-depth=N`: Print entries only if they are N or fewer levels below the command line argument<br>`-h, --human-readable`: Print sizes in powers of 1024<br>`-H, --si`: Print sizes in powers of 1000<br>`-k`: Use 1K blocks<br>`-s, --summarize`: Display only a total for each argument | File/directory size estimation; file operands use the shared wildcard policy and block counts round up |
| `free` | ✅ Done | Medium | Display amount of free and used memory in the system | `-b, --bytes`: Display the amount of memory in bytes<br>`-k, --kilo`: Display the amount of memory in kilobytes<br>`-m, --mega`: Display the amount of memory in megabytes<br>`-g, --giga`: Display the amount of memory in gigabytes<br>`--tera`: Display the amount of memory in terabytes<br>`-h, --human`: Show human-readable output<br>`-l, --lohi`: Show detailed low and high memory statistics | Memory usage display |
| `lsof` | ✅ Done | Medium | List open files | No options | Open file listing |
| `pwd` | ✅ Done | High | Print working directory | `-L, --logical`: Use PWD from environment, even if it contains symlinks<br>`-P, --physical`: Avoid all symlinks | Working directory display; GNU defaults to physical mode unless overridden |
| `ps` | ✅ Done | High | Report process status | `-e, -A`: Select all processes<br>`-a`: Select all processes except both session leaders and processes not associated with a terminal<br>`-x`: Select processes without controlling ttys<br>`-f`: Full-format listing<br>`-l`: Long format<br>`-u USER`: Select by effective user ID or name<br>`-w`: Wide output<br>`--no-headers`: Print no header<br>`--sort=KEY`: Sort by column | Process listing and filtering; this is a repo-local implementation, not a GNU Coreutils manual page |
| `which` | ✅ Done | High | Locate a command in PATH | `-a, --all`: Print all matching pathnames of each argument<br>`--read-alias`, `--skip-alias`: Read or skip alias expansion<br>`--read-functions`, `--skip-functions`: Read or skip shell function expansion<br>`--skip-dot`: Skip directories in PATH that start with a dot<br>`--show-dot`: Allow directories that start with a dot to be shown<br>`--skip-tilde`: Skip directories in PATH that start with a tilde<br>`--show-tilde`: Allow directories that start with a tilde to be shown<br>`--tty-only`: Show matches only on a tty | Path searching with PATHEXT support, SmallVector optimization |
| `nproc` | ✅ Done | Low | Print the number of processing units available | `--all`: Print the number of installed processing units<br>`--ignore=N`: If possible, exclude N processing units | Processor count |
| `numfmt` | ✅ Done | Low | Convert numbers from/to human-readable strings | `-d, --delimiter=X`: Use X instead of whitespace for field delimiter<br>`-f, --format=FORMAT`: Use printf style floating-point FORMAT<br>`--from=UNIT`: Auto-scale input numbers to UNITs<br>`--to=UNIT`: Auto-scale output numbers to UNITs<br>`--round=METHOD`: Use METHOD for rounding<br>`--padding=N`: Pad the output to N characters | Number formatting |
| `column` | ✅ Done | Low | Columnate lists | `-c, --output-width=WIDTH`: Set output width<br>`-t, --table`: Determine the number of columns the table contains<br>`-s, --separator SEPARATOR`: Specify the possible input item delimiters<br>`-o, --output-separator STRING`: Specify the column separator for table output<br>`-x, --fillrows`: Fill rows before filling columns | Column formatting |
| `jq` | ✅ Done | Medium | Command-line JSON processor | `-c, --compact-output`: Compact instead of pretty-printed output<br>`-r, --raw-output`: Output raw strings<br>`-s, --slurp`: Read (slurp) all inputs into an array<br>`-R, --raw-input`: Read raw strings<br>`-M, --monochrome-output`: Don't colorize JSON | JSON processing |
| `tr` | ✅ Done | Medium | Translate or delete characters | `-c, -C, --complement`: Use the complement of SET1<br>`-d, --delete`: Delete characters in SET1<br>`-s, --squeeze-repeats`: Replace each sequence of a repeated character<br>`-t, --truncate-set1`: First truncate SET1 to length of SET2 | Character translation and deletion |
| `mktemp` | ✅ Done | Medium | Create a temporary file or directory | `-d, --directory`: Create a directory, not a file<br>`-u, --dry-run`: Do not actually create anything<br>`-q, --quiet`: Fail silently if an error occurs<br>`-p, --tmpdir[=DIR]`: Interpret TEMPLATE relative to DIR<br>`-t`: Generate a template (using the temporary directory's path) | Temporary file/directory creation |
| `mpicalc` | ✅ Done | Low | Simple arbitrary-precision calculator | No options | Arbitrary precision calculation |
| `hmac256` | ✅ Done | Low | Compute HMAC-SHA256 | `--help`: Display help<br>`--version`: Output version information | HMAC computation |
| `pinky` | ✅ Done | Low | Lightweight finger | `-l`: Produce long format output<br>`-b`: Omit the user's home directory and shell<br>`-h`: Omit the user's project file<br>`-p`: Omit the user's plan file<br>`-s`: Do short format output | User information |
| `seq` | ✅ Done | Low | Print a sequence of numbers | `-f, --format=FORMAT`: Use printf style floating-point FORMAT<br>`-s, --separator=STRING`: Use STRING to separate numbers<br>`-w, --equal-width`: Equalize width by padding with leading zeroes | Sequence generation |
| `sleep` | ✅ Done | Low | Delay for a specified time | `NUMBER[SUFFIX]`: Pause for NUMBER seconds | Sleep delay |
| `yes` | ✅ Done | Low | Output a string repeatedly | `STRING`: String to output | Repeated output |

### System Utilities

| Command | Status | Priority | Description | Parameters/Options | Implementation Notes |
|---------|--------|----------|-------------|-------------------|---------------------|
| `help` | ✅ Done | High | Display help information about commands | `COMMAND`: Display help for specific command | Uses command metadata for help generation |
| `man` | ✅ Done | Low | Display manual pages | `COMMAND`: Display the manual page for a command | Repo-local manual lookup with `.exe` handling |
| `exit` | ✅ Done | High | Exit the shell | No options | Simple exit call with pipeline structure |
| `clear` | ✅ Done | High | Clear the terminal screen | No options | Uses system call to clear screen |
| `cd` | ✅ Done | High | Change the current directory | `-L, --logical`: Force symbolic links to be followed<br>`-P, --physical`: Use the physical directory structure without following symbolic links | Uses SetCurrentDirectory API with pipeline structure |
| `type` | ❌ TODO | Medium | Describe command type | `COMMAND`: Describe specific command | Not yet implemented |
| `alias` | ❌ TODO | Low | Create an alias for a command | `NAME=VALUE`: Define an alias<br>`-p`: Print all defined aliases | Not yet implemented |

## Implementation Guidelines

### General Structure

Each command should follow this general structure:

1. **Module Declaration**: Start with `export module cmd.<command>;`
2. **Imports**: Import necessary modules (`import core;`, `import utils;`, etc.)
3. **Constants Namespace**: Define command-specific constants
4. **Option Definitions**: Define command options using the `OPTION` macro
5. **Pipeline Components**: Implement pipeline components for command processing
6. **Command Implementation**: Implement the main command logic
7. **Registration**: Register the command using `REGISTER_COMMAND` macro

### Example Structure

```cpp
export module cmd.echo;

import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

namespace echo_constants {
    // Command-specific constants
}

// Define options
export auto constexpr ECHO_OPTIONS =
    std::array{
        OPTION("-n", "--no-newline", "do not output the trailing newline"),
        OPTION("-e", "--escape", "enable interpretation of backslash escapes"),
    };

namespace echo_pipeline {
    namespace cp = core::pipeline;

    // Validate arguments
    auto validate_arguments(std::span<const std::string_view> args) -> cp::Result<std::vector<std::string>> {
        std::vector<std::string> validated_args;
        for (auto arg : args) {
            validated_args.push_back(std::string(arg));
        }
        return validated_args;
    }

    // Build text from arguments
    auto build_text(const std::vector<std::string>& args) -> std::string {
        std::string text;
        for (size_t i = 0; i < args.size(); ++i) {
            text += args[i];
            if (i < args.size() - 1) {
                text += " ";
            }
        }
        return text;
    }

    // Handle escape sequences
    auto to_uppercase(std::string text) -> std::string {
        std::ranges::transform(text, text.begin(), ::toupper);
        return text;
    }

    // Process command
    template<size_t N>
    auto process_command(const CommandContext<N>& ctx)
        -> cp::Result<std::string>
    {
        auto args_result = validate_arguments(ctx.positionals);
        if (!args_result) {
            return args_result.error();
        }
        auto args = *args_result;
        return build_text(args);
    }

}

REGISTER_COMMAND(echo,
                 /* name */
                 "echo",

                 /* synopsis */
                 "echo [SHORT-OPTION]... [STRING]...",

                 /* description */
                 "Display a line of text.",

                 /* examples */
                 "  echo Hello World      Display 'Hello World'\n"
                 "  echo -n Hello         Display 'Hello' without trailing newline\n"
                 "  echo -e Hello\\nWorld  Display 'Hello' and 'World' on separate lines",

                 /* see_also */
                 "cat, printf",

                 /* author */
                 "WinuxCmd Team",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 ECHO_OPTIONS
) {
    using namespace echo_pipeline;

    auto result = process_command(ctx);
    if (!result) {
        cp::report_error(result, L"echo");
        return 1;
    }

    auto text = *result;

    // Get options
    bool no_newline = ctx.get<bool>("--no-newline", false);
    bool escape = ctx.get<bool>("--escape", false);

    // Handle escape sequences if enabled
    if (escape) {
        // Implement escape sequence handling
    }

    // Output result
    std::cout << text;
    if (!no_newline) {
        std::cout << std::endl;
    }

    return 0;
}
```

### Best Practices

1. **Pipeline Architecture**: Use pipeline components for modular processing
2. **Type Safety**: Use `CommandContext` for type-safe option access
3. **Error Handling**: Use `core::pipeline::Result` for consistent error handling
4. **Documentation**: Provide clear documentation for each command
5. **Testing**: Test commands with various inputs and options
6. **Performance**: Optimize for performance where appropriate
7. **Compatibility**: Follow POSIX standards where possible

### Testing

Each command should be tested with:

1. **Basic functionality**: Test the command with no options
2. **All options**: Test each option individually
3. **Combined options**: Test multiple options together
4. **Edge cases**: Test with empty inputs, large inputs, etc.
5. **Error cases**: Test with invalid inputs and options

## Migration Guide

### From Legacy Implementation

To migrate a command from the legacy implementation to the new pipeline-based structure:

1. **Update module declaration**: Use `export module cmd.<command>;`
2. **Add imports**: Add necessary imports (`import core;`, `import utils;`)
3. **Define options**: Use the `OPTION` macro to define options
4. **Create pipeline namespace**: Implement pipeline components
5. **Update command implementation**: Use `CommandContext` for option access
6. **Register command**: Use the new `REGISTER_COMMAND` macro

### Example Migration

#### After (Pipeline-Based Architecture)

```cpp
export module cmd.echo;

import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

namespace echo_constants {
    // Command-specific constants can be defined here
}

// Define options
export auto constexpr ECHO_OPTIONS =
    std::array{
        OPTION("-n", "--no-newline", "do not output the trailing newline"),
        OPTION("-e", "--escape", "enable interpretation of backslash escapes"),
    };

namespace echo_pipeline {
    namespace cp = core::pipeline;

    // Validate arguments
    auto validate_arguments(std::span<const std::string_view> args) -> cp::Result<std::vector<std::string>> {
        std::vector<std::string> validated_args;
        for (auto arg : args) {
            validated_args.push_back(std::string(arg));
        }
        return validated_args;
    }

    // Build text from arguments
    auto build_text(const std::vector<std::string>& args) -> std::string {
        std::string text;
        for (size_t i = 0; i < args.size(); ++i) {
            text += args[i];
            if (i < args.size() - 1) {
                text += " ";
            }
        }
        return text;
    }

    // Process command
    template<size_t N>
    auto process_command(const CommandContext<N>& ctx)
        -> cp::Result<std::string>
    {
        auto args_result = validate_arguments(ctx.positionals);
        if (!args_result) {
            return args_result.error();
        }
        auto args = *args_result;
        return build_text(args);
    }

}

REGISTER_COMMAND(echo,
                 /* name */
                 "echo",

                 /* synopsis */
                 "echo [SHORT-OPTION]... [STRING]...",

                 /* description */
                 "Display a line of text.",

                 /* examples */
                 "  echo Hello World      Display 'Hello World'\n"
                 "  echo -n Hello         Display 'Hello' without trailing newline\n"
                 "  echo -e Hello\\nWorld  Display 'Hello' and 'World' on separate lines",

                 /* see_also */
                 "cat, printf",

                 /* author */
                 "WinuxCmd Team",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 ECHO_OPTIONS
) {
    using namespace echo_pipeline;

    auto result = process_command(ctx);
    if (!result) {
        cp::report_error(result, L"echo");
        return 1;
    }

    auto text = *result;

    // Get options using CommandContext
    bool no_newline = ctx.get<bool>("--no-newline", false);
    bool escape = ctx.get<bool>("--escape", false);

    // Handle escape sequences if enabled
    if (escape) {
        // Implement escape sequence handling
    }

    // Output result
    std::cout << text;
    if (!no_newline) {
        std::cout << std::endl;
    }

    return 0;
}
```

## Reference Implementation

The `echo.cppm` file serves as the reference implementation for the new pipeline-based architecture. It demonstrates:

1. **Pipeline Components**: Separate validation and processing logic
2. **Option Handling**: Use of `CommandContext` for type-safe option access
3. **Error Handling**: Use of `core::pipeline::Result` for error reporting
4. **Command Registration**: Proper use of `REGISTER_COMMAND` macro

All new commands should follow this pattern for consistency and maintainability.
