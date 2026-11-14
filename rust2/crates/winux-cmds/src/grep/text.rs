//! User-visible text and the stdout/stderr plumbing.
//!
//! The help and version texts are the WSL GNU grep 3.11 oracle bytes
//! (probed; egrep/fgrep wrappers print the same "grep" text because they
//! exec grep). `Ui` owns grep's stdout buffer: output is accumulated and
//! handed to the injected writer in large chunks (GNU stdio full
//! buffering; `--line-buffered` flushes after every output line), and every
//! diagnostic first flushes stdout, as gnulib `error()` does.

use std::io::Write;

pub const USAGE_LINE: &str = "Usage: grep [OPTION]... PATTERNS [FILE]...";
pub const TRY_HELP_LINE: &str = "Try 'grep --help' for more information.";

pub const HELP_TEXT: &str = concat!(
    "Usage: grep [OPTION]... PATTERNS [FILE]...\n",
    "Search for PATTERNS in each FILE.\n",
    "Example: grep -i 'hello world' menu.h main.c\n",
    "PATTERNS can contain multiple patterns separated by newlines.\n",
    "\n",
    "Pattern selection and interpretation:\n",
    "  -E, --extended-regexp     PATTERNS are extended regular expressions\n",
    "  -F, --fixed-strings       PATTERNS are strings\n",
    "  -G, --basic-regexp        PATTERNS are basic regular expressions\n",
    "  -P, --perl-regexp         PATTERNS are Perl regular expressions\n",
    "  -e, --regexp=PATTERNS     use PATTERNS for matching\n",
    "  -f, --file=FILE           take PATTERNS from FILE\n",
    "  -i, --ignore-case         ignore case distinctions in patterns and data\n",
    "      --no-ignore-case      do not ignore case distinctions (default)\n",
    "  -w, --word-regexp         match only whole words\n",
    "  -x, --line-regexp         match only whole lines\n",
    "  -z, --null-data           a data line ends in 0 byte, not newline\n",
    "\n",
    "Miscellaneous:\n",
    "  -s, --no-messages         suppress error messages\n",
    "  -v, --invert-match        select non-matching lines\n",
    "  -V, --version             display version information and exit\n",
    "      --help                display this help text and exit\n",
    "\n",
    "Output control:\n",
    "  -m, --max-count=NUM       stop after NUM selected lines\n",
    "  -b, --byte-offset         print the byte offset with output lines\n",
    "  -n, --line-number         print line number with output lines\n",
    "      --line-buffered       flush output on every line\n",
    "  -H, --with-filename       print file name with output lines\n",
    "  -h, --no-filename         suppress the file name prefix on output\n",
    "      --label=LABEL         use LABEL as the standard input file name prefix\n",
    "  -o, --only-matching       show only nonempty parts of lines that match\n",
    "  -q, --quiet, --silent     suppress all normal output\n",
    "      --binary-files=TYPE   assume that binary files are TYPE;\n",
    "                            TYPE is 'binary', 'text', or 'without-match'\n",
    "  -a, --text                equivalent to --binary-files=text\n",
    "  -I                        equivalent to --binary-files=without-match\n",
    "  -d, --directories=ACTION  how to handle directories;\n",
    "                            ACTION is 'read', 'recurse', or 'skip'\n",
    "  -D, --devices=ACTION      how to handle devices, FIFOs and sockets;\n",
    "                            ACTION is 'read' or 'skip'\n",
    "  -r, --recursive           like --directories=recurse\n",
    "  -R, --dereference-recursive  likewise, but follow all symlinks\n",
    "      --include=GLOB        search only files that match GLOB (a file pattern)\n",
    "      --exclude=GLOB        skip files that match GLOB\n",
    "      --exclude-from=FILE   skip files that match any file pattern from FILE\n",
    "      --exclude-dir=GLOB    skip directories that match GLOB\n",
    "  -L, --files-without-match  print only names of FILEs with no selected lines\n",
    "  -l, --files-with-matches  print only names of FILEs with selected lines\n",
    "  -c, --count               print only a count of selected lines per FILE\n",
    "  -T, --initial-tab         make tabs line up (if needed)\n",
    "  -Z, --null                print 0 byte after FILE name\n",
    "\n",
    "Context control:\n",
    "  -B, --before-context=NUM  print NUM lines of leading context\n",
    "  -A, --after-context=NUM   print NUM lines of trailing context\n",
    "  -C, --context=NUM         print NUM lines of output context\n",
    "  -NUM                      same as --context=NUM\n",
    "      --group-separator=SEP  print SEP on line between matches with context\n",
    "      --no-group-separator  do not print separator for matches with context\n",
    "      --color[=WHEN],\n",
    "      --colour[=WHEN]       use markers to highlight the matching strings;\n",
    "                            WHEN is 'always', 'never', or 'auto'\n",
    "  -U, --binary              do not strip CR characters at EOL (MSDOS/Windows)\n",
    "\n",
    "When FILE is '-', read standard input.  With no FILE, read '.' if\n",
    "recursive, '-' otherwise.  With fewer than two FILEs, assume -h.\n",
    "Exit status is 0 if any line is selected, 1 otherwise;\n",
    "if any error occurs and -q is not given, the exit status is 2.\n",
    "\n",
    "Report bugs to: bug-grep@gnu.org\n",
    "GNU grep home page: <https://www.gnu.org/software/grep/>\n",
    "General help using GNU software: <https://www.gnu.org/gethelp/>\n",
);

pub const VERSION_TEXT: &str = concat!(
    "grep (GNU grep) 3.11\n",
    "Copyright (C) 2023 Free Software Foundation, Inc.\n",
    "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n",
    "This is free software: you are free to change and redistribute it.\n",
    "There is NO WARRANTY, to the extent permitted by law.\n",
    "\n",
    "Written by Mike Haertel and others; see\n",
    "<https://git.savannah.gnu.org/cgit/grep.git/tree/AUTHORS>.\n",
    "\n",
    "grep -P uses PCRE2 10.42 2022-12-11\n",
);

/// gnulib `quote()` for argmatch diagnostics: curly quotes in a UTF-8
/// locale, ASCII apostrophes in the C locale (probed both).
pub fn argmatch_quote(s: &str, utf8: bool) -> String {
    if utf8 {
        format!("\u{2018}{s}\u{2019}")
    } else {
        format!("'{s}'")
    }
}

/// Flush threshold for the stdout buffer.
const OUT_CHUNK: usize = 64 * 1024;

/// stdout buffer + stderr access for one grep run.
pub struct Ui<'a> {
    pub stdout: &'a mut dyn Write,
    pub stderr: &'a mut dyn Write,
    pub out: Vec<u8>,
    /// first stdout write error (GNU stdout_errno)
    pub write_error: Option<std::io::Error>,
}

impl<'a> Ui<'a> {
    pub fn new(stdout: &'a mut dyn Write, stderr: &'a mut dyn Write) -> Ui<'a> {
        Ui {
            stdout,
            stderr,
            out: Vec::with_capacity(OUT_CHUNK + 4096),
            write_error: None,
        }
    }

    /// Hand buffered stdout bytes to the writer once the chunk is full.
    #[inline]
    pub fn maybe_flush(&mut self) {
        if self.out.len() >= OUT_CHUNK {
            self.flush_out();
        }
    }

    /// Write every buffered byte and flush the writer.
    pub fn flush_out(&mut self) {
        if self.write_error.is_some() {
            self.out.clear();
            return;
        }
        if !self.out.is_empty() {
            if let Err(e) = self.stdout.write_all(&self.out) {
                self.write_error = Some(e);
            }
            self.out.clear();
        }
        if self.write_error.is_none() {
            if let Err(e) = self.stdout.flush() {
                self.write_error = Some(e);
            }
        }
    }

    /// `error (0, 0, "%s", msg)`: "grep: msg" on stderr after flushing
    /// stdout.
    pub fn error(&mut self, msg: &str) {
        self.flush_out();
        let _ = writeln!(self.stderr, "grep: {msg}");
    }

    /// Raw stderr bytes (no prefix).
    pub fn raw(&mut self, text: &str) {
        self.flush_out();
        let _ = self.stderr.write_all(text.as_bytes());
    }

    /// `usage (EXIT_TROUBLE)`'s two lines on stderr.
    pub fn usage_lines(&mut self) {
        self.raw(&format!("{USAGE_LINE}\n{TRY_HELP_LINE}\n"));
    }

    /// getopt diagnostic followed by usage(2).
    pub fn option_error(&mut self, msg: &str) {
        self.error(msg);
        self.usage_lines();
    }
}
