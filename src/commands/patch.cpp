// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for patch command (GNU patch 2.7.6 engine
///               semantics).
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PATCH_OPTIONS = std::array{
    // [GNU] -p, --strip=NUM: strip NUM leading components from file names.
    // STRING_TYPE so the value is validated by patch's own
    // numeric_string clone (GNU wording), not the framework parser.
    OPTION("-p", "--strip", "strip NUM leading components from file names",
           STRING_TYPE),
    // [GNU] -i, --input=FILE: read patch from FILE
    OPTION("-i", "--input", "read patch from FILE", STRING_TYPE),
    // [GNU] -z, --suffix=SUF: set backup suffix (default ".orig")
    OPTION("-z", "--suffix", "set backup suffix", STRING_TYPE),
    // [GNU] -R, --reverse: assume patch was created with old and new swapped
    OPTION("-R", "--reverse",
           "assume patch was created with old and new files swapped"),
    // [GNU] -N, --forward: skip patches that seem to be reversed or already
    // applied
    OPTION("-N", "--forward",
           "skip patches that seem reversed or already applied"),
    // [GNU] -b, --backup: make backup files
    OPTION("-b", "--backup", "back up the original file"),
    // [GNU] --dry-run: don't actually change any files
    OPTION("", "--dry-run", "do not actually change any files"),
    // [GNU] -d, --directory=DIR: change to DIR first
    OPTION("-d", "--directory", "change to DIR first", STRING_TYPE),
    // [GNU] -D, --ifdef=WORD: use WORD to patch files
    OPTION("-D", "--ifdef", "use WORD to patch files", STRING_TYPE),
    // [GNU] -E, --remove-empty-files: remove empty output files
    OPTION("", "--remove-empty-files", "remove empty output files"),
    // [GNU] -f, --force: force this patch, even if it seems reversed
    OPTION("-f", "--force", "force this patch, even if it seems reversed"),
    // [GNU] -F, --fuzz=NUM: set maximum fuzz factor (default 2).
    // STRING_TYPE for the same numeric_string reason as -p.
    OPTION("-F", "--fuzz", "set maximum fuzz factor", STRING_TYPE),
    // [GNU] -o, --output=FILE: output to FILE instead of the patched file
    OPTION("-o", "--output", "output to FILE instead of the patched file",
           STRING_TYPE),
    // [GNU] --backup-if-mismatch: backup if patch does not match exactly
    OPTION("", "--backup-if-mismatch",
           "backup if patch does not match exactly"),
    // [GNU] --no-backup-if-mismatch: don't backup if patch matches exactly
    OPTION("", "--no-backup-if-mismatch", "don't backup if patch matches"),
    // [GNU] -r, --reject-file=FILE: output rejects to FILE
    OPTION("-r", "--reject-file", "output rejects to FILE", STRING_TYPE),
    // [GNU] --reject-format=FORMAT: produce output in FORMAT
    OPTION("", "--reject-format", "produce output in FORMAT", STRING_TYPE),
    // [GNU] -s, --silent, --quiet: work silently unless an error occurs
    OPTION("-s", "--silent", "work silently unless an error occurs"),
    OPTION("-q", "--quiet", "work silently unless an error occurs"),
    // [GNU] -t, --batch: same as --force, with diagnostics
    OPTION("-t", "--batch", "same as --force, with diagnostics"),
    // [GNU] -T, --set-time: set the patch's modification time
    OPTION("-T", "--set-time", "set the patch's modification time"),
    // [GNU] --set-utc: set the patch's modification time to UTC
    OPTION("", "--set-utc", "set the patch's modification time to UTC"),
    // [GNU] -u, --unified: interpret the patch as unified diff
    OPTION("-u", "--unified", "interpret the patch as unified diff"),
    // [GNU] -l, --ignore-whitespace: ignore whitespace runs when matching
    // (issue #1381; not to be confused with the unsupported --merge)
    OPTION("-l", "--ignore-whitespace",
           "ignore whitespace runs when matching"),
    // [GNU] -c/-n/-e, --context/--normal/--ed: force the input format.
    // Accepted so that forcing a non-unified format on unified input is
    // rejected as garbage, exactly like GNU (issue #1381); parsing those
    // formats themselves is tracked separately (#1346).
    OPTION("-c", "--context", "interpret the patch as a context diff"),
    OPTION("-n", "--normal", "interpret the patch as a normal diff"),
    OPTION("-e", "--ed", "interpret the patch as an ed script"),
    // [GNU] -V, --version-control: backup naming style (issue #1381)
    OPTION("-V", "--version-control", "use STYLE for backup file names",
           STRING_TYPE),
    // [GNU] --prefix / --basename-prefix: rename backups (issue #1381)
    OPTION("", "--prefix", "prefix backup file names with PREFIX",
           STRING_TYPE),
    OPTION("", "--basename-prefix",
           "prefix backup file basenames with PREFIX", STRING_TYPE),
    // [GNU] --quoting-style: quote file names in diagnostics (issue #1381)
    OPTION("", "--quoting-style", "quote file names in diagnostics",
           STRING_TYPE),
    // [GNU] --get: fetch from RCS/SCCS when positive; validated and
    // accepted, no backend on this port (issue #1381)
    OPTION("", "--get", "get files from RCS or SCCS if NUM is positive",
           STRING_TYPE),
    // [GNU] --read-only: how to treat read-only files (issue #1381)
    OPTION("", "--read-only", "how to handle read-only files: ignore, "
                              "warn, or fail",
           STRING_TYPE),
    // [GNU] -v, --verbose: print verbose output
    OPTION("-v", "--verbose", "print verbose output"),
    // [GNU] --binary: read and write in binary mode
    OPTION("", "--binary", "read and write in binary mode"),
    // [GNU] --posix: conform to POSIX standard
    OPTION("", "--posix", "conform to POSIX standard"),
    // [GNU] --strip-trailing-slashes is accepted for compatibility.
    OPTION("", "--strip-trailing-slashes",
           "strip trailing slashes from file names")};

// ======================================================
// Helper functions
// ======================================================

namespace {

constexpr long long kDefaultFuzz = 2;

bool is_dev_null(const std::string& name) {
  return name == "/dev/null" || name == "dev/null" || name == "NUL" ||
         name == "nul";
}

// One line of a target file: text plus whether a newline follows it.
struct TLine {
  std::string text;
  bool nl = true;
};

// One tagged line (' ', '-' or '+') of a hunk. Blank patch lines are kept
// as ' ' context so they count on both sides.
struct HunkLine {
  char tag = ' ';
  std::string text;
};

// One line of a context-diff section: tag is ' ' (context), '-' (old
// only), '+' (new only) or '!' (changed; present on both sides).
struct SecLine {
  char tag = ' ';
  std::string text;
};

struct Hunk {
  long long old_start = 0;
  long long old_count = 0;
  long long new_start = 0;
  long long new_count = 0;
  std::vector<HunkLine> lines;
  bool old_no_newline = false;  // "\ No newline" seen after old side
  bool new_no_newline = false;  // "\ No newline" seen after new side
  // Normal (ed-style) diffs carry no context: the pattern must match at
  // exactly old_start (already delta-adjusted at parse time), with no
  // offset search and no fuzz.
  bool exact_position = false;
  // [GNU] Text after the closing "@@" of a unified hunk header (the
  // function context, leading space included); reprinted in rejects
  // (abort_hunk_unified, patch.c:1293).
  std::string func_context;
  // Context-diff hunks keep their two original sections so rejects can
  // be written in context form (abort_hunk_context, patch.c:1330).
  std::vector<SecLine> ctx_old;
  std::vector<SecLine> ctx_new;
  long long ctx_old_first = 0;
  long long ctx_old_count = 0;
  long long ctx_new_first = 0;
  long long ctx_new_count = 0;
};

enum class PatchFormat { unified, context, normal };

// One `--- / +++ ` header pair and its hunk group.
struct FilePatch {
  std::string old_name;
  std::string new_name;
  std::vector<Hunk> hunks;
  PatchFormat format = PatchFormat::unified;
  std::string index_name;  // from an "Index: " line, if any
  // Verbatim header text after the "--- "/"*** " resp. "+++ "/"--- "
  // marker (file name plus timestamp, as written); reject files
  // reprint it (print_header_line, patch.c:1261).
  std::string old_header_raw;
  std::string new_header_raw;
  // Raw-input line span [lead_begin, first_hunk_li) precedes the first
  // hunk: header lines plus any skipped text.  GNU prints it in the
  // "The text leading up to this was:" block when no file is found
  // (skip_to, pch.c:1096).  first_hunk_line is 1-based for the
  // "can't find file to patch at input line N" diagnostic (pch.c:277).
  size_t lead_begin = 0;
  size_t first_hunk_li = 0;
  long long first_hunk_line = 0;
};

std::string header_name(const std::string& rest) {
  std::string name = rest;
  size_t tab = name.find('\t');
  if (tab != std::string::npos) name = name.substr(0, tab);
  while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) {
    name.pop_back();
  }
  return name;
}

// Parse "@@ -old[,count] +new[,count] @@".
bool parse_hunk_header(const std::string& line, Hunk& hunk) {
  if (line.length() < 6 || line.substr(0, 4) != "@@ -") return false;
  size_t at_pos = line.find("@@", 4);
  if (at_pos == std::string::npos) return false;
  // [GNU] Everything past the closing "@@" is the function context.
  hunk.func_context = line.substr(at_pos + 2);

  auto parse_range = [](const std::string& part, long long& start,
                        long long& count) {
    size_t comma = part.find(',');
    std::string start_str =
        comma == std::string::npos ? part : part.substr(0, comma);
    std::string count_str =
        comma == std::string::npos ? "1" : part.substr(comma + 1);
    try {
      start = std::stoll(start_str);
      count = std::stoll(count_str);
    } catch (...) {
      return false;
    }
    return true;
  };

  std::string info = line.substr(4, at_pos - 4);
  size_t plus = info.find('+');
  if (plus == std::string::npos) return false;
  if (!parse_range(info.substr(0, plus), hunk.old_start, hunk.old_count))
    return false;
  std::string new_part = info.substr(plus + 1);
  size_t sp = new_part.find_first_of(" \t");
  if (sp != std::string::npos) new_part = new_part.substr(0, sp);
  if (!parse_range(new_part, hunk.new_start, hunk.new_count)) return false;
  return true;
}

// Split raw bytes into lines. Tolerates CRLF in the patch input; records
// whether the target uses CRLF and per-line newline presence.
std::vector<TLine> split_lines(const std::string& raw, bool& crlf) {
  std::vector<TLine> lines;
  crlf = raw.find("\r\n") != std::string::npos;
  size_t start = 0;
  while (start <= raw.size()) {
    if (start == raw.size()) break;
    size_t nl = raw.find('\n', start);
    if (nl == std::string::npos) {
      TLine l;
      l.text = raw.substr(start);
      l.nl = false;
      if (!l.text.empty() && l.text.back() == '\r') l.text.pop_back();
      lines.push_back(std::move(l));
      break;
    }
    TLine l;
    l.text = raw.substr(start, nl - start);
    l.nl = true;
    if (!l.text.empty() && l.text.back() == '\r') l.text.pop_back();
    lines.push_back(std::move(l));
    start = nl + 1;
    if (start == raw.size()) break;  // trailing newline: no empty last line
  }
  return lines;
}

std::string join_lines(const std::vector<TLine>& lines, bool crlf) {
  const char* eol = crlf ? "\r\n" : "\n";
  std::string out;
  for (const auto& l : lines) {
    out += l.text;
    if (l.nl) out += eol;
  }
  return out;
}

bool read_file_raw(const std::string& filename, std::string& raw) {
  // Route the operand through the shared path boundary (rootaudit
  // #1364): a bare "/"-leading operand (an -i patch file, or a header
  // name in a POSIX drive spelling) must resolve through the shell
  // drive mapping / payload root, not land on the process current
  // drive root as a raw CreateFileW path would.
  const auto operand = native_path::make_api_path_operand(filename);
  HANDLE hFile = CreateFileW(operand.extended.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size;
  if (!GetFileSizeEx(hFile, &size)) size.QuadPart = 0;
  raw.resize(static_cast<size_t>(size.QuadPart));
  DWORD bytes_read = 0;
  if (!raw.empty()) {
    ReadFile(hFile, raw.data(), static_cast<DWORD>(raw.size()), &bytes_read,
             nullptr);
    raw.resize(bytes_read);
  }
  CloseHandle(hFile);
  return true;
}

bool write_file_raw(const std::string& filename, const std::string& content) {
  std::wstring wfilename = utf8_to_wstring(filename);
  HANDLE hFile = CreateFileW(wfilename.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  if (!content.empty()) {
    WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()),
              &written, nullptr);
  }
  CloseHandle(hFile);
  return true;
}

bool append_file_raw(const std::string& filename, const std::string& content) {
  std::wstring wfilename = utf8_to_wstring(filename);
  HANDLE hFile = CreateFileW(wfilename.c_str(), GENERIC_WRITE, 0, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) return false;
  SetFilePointer(hFile, 0, nullptr, FILE_END);
  DWORD written = 0;
  if (!content.empty()) {
    WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()),
              &written, nullptr);
  }
  CloseHandle(hFile);
  return true;
}

bool file_exists(const std::string& filename) {
  std::wstring wfile = utf8_to_wstring(filename);
  DWORD attrs = GetFileAttributesW(wfile.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool backup_file_to(const std::string& filename, const std::string& dest) {
  std::wstring wsrc = utf8_to_wstring(filename);
  std::wstring wdst = utf8_to_wstring(dest);
  return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE) != FALSE;
}

// [GNU] strip_leading_slashes (util.c) + fetchname: prepare a header
// name for target selection.  With no -p (strip_specified false), only
// the basename survives.  With -pN, N leading slash-runs are stripped
// (a run of slashes counts once) and a name with fewer than N runs —
// or nothing left after stripping — is ignored entirely (nullopt).
std::optional<std::string> strip_name(const std::string& path,
                                      bool strip_specified,
                                      long long components) {
  if (!strip_specified) {
    const size_t slash = path.find_last_of("/\\");
    std::string base =
        slash == std::string::npos ? path : path.substr(slash + 1);
    if (base.empty()) return std::nullopt;
    return base;
  }
  long long s = components;
  size_t cut = 0;  // start of the surviving tail
  for (size_t i = 0; i < path.size();) {
    if (path[i] == '/' || path[i] == '\\') {
      while (i < path.size() && (path[i] == '/' || path[i] == '\\')) ++i;
      if (--s >= 0) cut = i;
    } else {
      ++i;
    }
  }
  if (s > 0 || cut >= path.size()) return std::nullopt;
  return path.substr(cut);
}

Hunk reversed(const Hunk& h) {
  Hunk r;
  r.old_start = h.new_start;
  r.old_count = h.new_count;
  r.new_start = h.old_start;
  r.new_count = h.old_count;
  r.old_no_newline = h.new_no_newline;
  r.new_no_newline = h.old_no_newline;
  r.exact_position = h.exact_position;
  r.func_context = h.func_context;
  // Context sections swap sides; '-' lines become '+' lines and back.
  auto swap_section = [](const std::vector<SecLine>& sec, char from, char to) {
    std::vector<SecLine> out;
    out.reserve(sec.size());
    for (const auto& l : sec) {
      out.push_back({l.tag == from ? to : l.tag, l.text});
    }
    return out;
  };
  r.ctx_old = swap_section(h.ctx_new, '+', '-');
  r.ctx_new = swap_section(h.ctx_old, '-', '+');
  r.ctx_old_first = h.ctx_new_first;
  r.ctx_old_count = h.ctx_new_count;
  r.ctx_new_first = h.ctx_old_first;
  r.ctx_new_count = h.ctx_old_count;
  r.lines.reserve(h.lines.size());
  for (const auto& l : h.lines) {
    char tag = l.tag == '-' ? '+' : (l.tag == '+' ? '-' : ' ');
    r.lines.push_back({tag, l.text});
  }
  return r;
}

// Pattern (match) side = context + minus lines; replacement side = context
// plus plus lines.
std::vector<std::string> pattern_of(const Hunk& h) {
  std::vector<std::string> pat;
  for (const auto& l : h.lines) {
    if (l.tag == ' ' || l.tag == '-') pat.push_back(l.text);
  }
  return pat;
}

// [GNU] patch -l/--ignore-whitespace (issue #1381): two lines match when
// they differ only in the LENGTH of whitespace runs (spaces/tabs collapse
// to a single space; leading/trailing runs vanish). A missing space is
// still a difference: "ab" does not match "a b".
std::string collapse_ws_runs(const std::string& s) {
  std::string out;
  bool in_ws = false;
  for (char c : s) {
    if (c == ' ' || c == '\t') {
      if (!in_ws) out += ' ';
      in_ws = true;
    } else {
      out += c;
      in_ws = false;
    }
  }
  if (!out.empty() && out.front() == ' ') out.erase(out.begin());
  if (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool lines_match(const std::string& a, const std::string& b, bool ignore_ws) {
  return ignore_ws ? collapse_ws_runs(a) == collapse_ws_runs(b) : a == b;
}

// [GNU] patch_match (patch.c:1687): compare the pattern (minus prefix_fuzz
// leading and suffix_fuzz trailing lines) against the file at base+offset.
bool patch_match(const std::vector<TLine>& file,
                 const std::vector<std::string>& pat, long long base,
                 long long offset, long long prefix_fuzz,
                 long long suffix_fuzz, bool ignore_ws) {
  long long n = static_cast<long long>(pat.size()) - prefix_fuzz - suffix_fuzz;
  if (n < 0) n = 0;
  long long start = base + offset + prefix_fuzz;
  if (start < 0 || start + n > static_cast<long long>(file.size()))
    return false;
  for (long long i = 0; i < n; ++i) {
    if (!lines_match(file[static_cast<size_t>(start + i)].text,
                     pat[static_cast<size_t>(prefix_fuzz + i)], ignore_ws)) {
      return false;
    }
  }
  return true;
}

// [GNU] locate_hunk (patch.c:1130): try the hinted position first, then
// increasing offset distance in both directions.  Returns the 0-based line
// where the full (fuzzed) pattern matched, or -1 on failure.  `in_offset`
// accumulates the found offsets so it seeds the next hunk's first guess.
long long locate_hunk(const std::vector<TLine>& file,
                      const std::vector<std::string>& pat,
                      long long prefix_context, long long suffix_context,
                      long long fuzz, long long first /*1-based hint*/,
                      long long min_where /*0-based*/, long long& in_offset,
                      bool ignore_ws) {
  const long long pat_lines = static_cast<long long>(pat.size());
  const long long input_lines = static_cast<long long>(file.size());
  const long long first_guess = first - 1 + in_offset;
  const long long context = std::max(prefix_context, suffix_context);
  long long prefix_fuzz = fuzz + prefix_context - context;
  long long suffix_fuzz = fuzz + suffix_context - context;

  if (pat_lines == 0) return first_guess;  // null range matches always

  const long long max_where =
      input_lines - (pat_lines - suffix_fuzz);  // 0-based last valid start
  const long long max_pos_offset = max_where - first_guess;
  long long max_neg_offset = first_guess - min_where;
  const long long max_offset = std::max(max_pos_offset, max_neg_offset);

  if (prefix_fuzz < 0 && first <= 1) {
    // Can only match start of file.
    if (suffix_fuzz < 0) {
      // Can only match entire file.
      if (pat_lines != input_lines || prefix_context < min_where) return -1;
    }
    long long offset = -first_guess;
    if (min_where <= prefix_context && offset <= max_pos_offset &&
        patch_match(file, pat, first_guess, offset, 0, suffix_fuzz,
                    ignore_ws)) {
      in_offset += offset;
      return first_guess + offset;
    }
    return -1;
  }
  if (prefix_fuzz < 0) prefix_fuzz = 0;

  if (suffix_fuzz < 0) {
    // Can only match end of file.
    long long offset = first_guess - (input_lines - pat_lines);
    if (offset <= max_neg_offset &&
        patch_match(file, pat, first_guess, -offset, prefix_fuzz, 0,
                    ignore_ws)) {
      in_offset -= offset;
      return first_guess - offset;
    }
    return -1;
  }

  long long min_offset =
      max_pos_offset < 0 ? first_guess - max_where
                         : (max_neg_offset < 0 ? first_guess - min_where : 0);
  for (long long offset = min_offset; offset <= max_offset; ++offset) {
    if (offset <= max_pos_offset &&
        patch_match(file, pat, first_guess, offset, prefix_fuzz, suffix_fuzz,
                    ignore_ws)) {
      in_offset += offset;
      return first_guess + offset;
    }
    if (offset <= max_neg_offset && patch_match(file, pat, first_guess, -offset,
                                                prefix_fuzz, suffix_fuzz,
                                                ignore_ws)) {
      in_offset -= offset;
      return first_guess - offset;
    }
  }
  return -1;
}

// Per-file application state: mirrors upstream's output-under-construction.
struct FileState {
  std::vector<TLine> input;
  std::vector<TLine> out;
  long long copy_ptr = 0;   // next input line (0-based) not yet emitted
  long long in_offset = 0;  // running offset from previous hunks
  // [GNU] out_offset (patch.c): cumulative new-minus-old line count of
  // the hunks applied so far; positions in diagnostics and reject
  // headers are output coordinates (old position + out_offset).
  long long out_offset = 0;
  bool crlf = false;
  bool exists = false;
};

struct Attempt {
  bool ok = false;
  long long where0 = -1;  // 0-based position of the match
  long long offset = 0;   // distance from the hinted position
  long long fuzz = 0;
  long long pf = 0;  // prefix context lines dropped by fuzz
  long long sf = 0;  // suffix context lines dropped by fuzz
};

// Try to locate a hunk at increasing fuzz, [GNU] patch.c:395-455.
Attempt try_hunk(const FileState& st, const std::vector<std::string>& pat,
                 long long prefix_context, long long suffix_context,
                 long long hint, long long maxfuzz, bool ignore_ws) {
  Attempt best;
  const long long context = std::max(prefix_context, suffix_context);
  const long long eff_max = std::min(maxfuzz, context < 0 ? 0 : context);
  if (pat.empty()) {
    // Null pattern (file creation): matches always at the hinted position.
    best.ok = true;
    best.where0 = hint - 1 + st.in_offset;
    best.offset = 0;
    return best;
  }
  for (long long f = 0; f <= eff_max; ++f) {
    long long in_offset = st.in_offset;
    long long where = locate_hunk(st.input, pat, prefix_context, suffix_context,
                                  f, hint, st.copy_ptr, in_offset, ignore_ws);
    if (where < 0) continue;
    best.ok = true;
    best.where0 = where;
    best.fuzz = f;
    best.offset = in_offset - st.in_offset;
    best.pf = std::max(0LL, f + prefix_context - context);
    best.sf = std::max(0LL, f + suffix_context - context);
    break;  // lowest fuzz wins; found position seeds the next guess
  }
  return best;
}

std::string plural_lines(long long n) { return n == 1 ? "line" : "lines"; }

std::string range_str(long long start, long long count) {
  if (count == 1) return std::to_string(start);
  return std::to_string(start) + "," + std::to_string(count);
}

// ======================================================
// Context and normal (ed-style) diff parsing
// ======================================================

// Parse "*** A[,B] ****" / "--- A[,B] ----" range headers.  A single
// number means a one-line range.
bool parse_context_range(const std::string& line, const char* marker,
                         const char* trailer, long long& first,
                         long long& last) {
  const std::string head = std::string(marker) + " ";
  const std::string tail = std::string(" ") + trailer;
  if (line.size() <= head.size() + tail.size()) return false;
  if (line.compare(0, head.size(), head) != 0) return false;
  if (line.compare(line.size() - tail.size(), tail.size(), tail) != 0) {
    return false;
  }
  const std::string mid =
      line.substr(head.size(), line.size() - head.size() - tail.size());
  auto num = [](const std::string& s, long long& v) {
    if (s.empty()) return false;
    for (char c : s) {
      if (c < '0' || c > '9') return false;
    }
    try {
      v = std::stoll(s);
    } catch (...) {
      return false;
    }
    return true;
  };
  const size_t comma = mid.find(',');
  if (comma == std::string::npos) {
    if (!num(mid, first)) return false;
    last = first;
    return true;
  }
  return num(mid.substr(0, comma), first) &&
         num(mid.substr(comma + 1), last);
}

bool is_context_separator(const std::string& line) {
  return line.size() >= 7 &&
         line.find_first_not_of('*') == std::string::npos;
}

std::string section_text(const std::string& line) {
  if (line.size() >= 2 && line[1] == ' ') return line.substr(2);
  if (!line.empty()) return line.substr(1);
  return "";
}

// Classify one context-diff section line.  The old section admits
// ' ', '-' and '!'; the new section admits ' ', '+' and '!'.  A blank
// line counts as an empty context line.
bool context_section_line(const std::string& line, bool old_side,
                          SecLine& out) {
  if (line.empty()) {
    out = {' ', ""};
    return true;
  }
  const char t = line[0];
  const bool ok = old_side ? (t == ' ' || t == '-' || t == '!')
                           : (t == ' ' || t == '+' || t == '!');
  if (!ok) return false;
  out = {t, section_text(line)};
  return true;
}

// Merge the two sections of a context hunk into unified hunk lines:
// context lines pair up, each maximal run of changed/removed old lines
// becomes '-' lines followed by the run of changed/added new lines as
// '+' lines.  GNU diff omits a section's lines entirely when that side
// has no non-context lines (pure addition/deletion hunks); the missing
// side's context spine is then taken from the other section.
Hunk convert_context_hunk(long long old_first, long long old_last,
                          const std::vector<SecLine>& old_in,
                          long long new_first,
                          const std::vector<SecLine>& new_in,
                          bool old_no_newline, bool new_no_newline) {
  std::vector<SecLine> old_seq = old_in;
  std::vector<SecLine> new_seq = new_in;
  if (old_seq.empty()) {
    for (const auto& l : new_seq) {
      if (l.tag == ' ') old_seq.push_back(l);
    }
  }
  if (new_seq.empty()) {
    for (const auto& l : old_seq) {
      if (l.tag == ' ') new_seq.push_back(l);
    }
  }
  Hunk h;
  size_t i = 0;
  size_t j = 0;
  while (i < old_seq.size() || j < new_seq.size()) {
    const bool old_ctx = i < old_seq.size() && old_seq[i].tag == ' ';
    const bool new_ctx = j < new_seq.size() && new_seq[j].tag == ' ';
    if (old_ctx && new_ctx) {
      h.lines.push_back({' ', old_seq[i].text});
      ++i;
      ++j;
      continue;
    }
    bool progressed = false;
    while (i < old_seq.size() && old_seq[i].tag != ' ') {
      h.lines.push_back({'-', old_seq[i].text});
      ++i;
      progressed = true;
    }
    while (j < new_seq.size() && new_seq[j].tag != ' ') {
      h.lines.push_back({'+', new_seq[j].text});
      ++j;
      progressed = true;
    }
    if (!progressed) {
      // Context present on only one side (mismatched sections): keep
      // it as context so the merge cannot stall.
      if (old_ctx) {
        h.lines.push_back({' ', old_seq[i].text});
        ++i;
      } else if (new_ctx) {
        h.lines.push_back({' ', new_seq[j].text});
        ++j;
      }
    }
  }
  for (const auto& l : h.lines) {
    if (l.tag != '+') h.old_count++;
    if (l.tag != '-') h.new_count++;
  }
  bool pattern_empty = true;
  for (const auto& l : h.lines) {
    if (l.tag != '+') {
      pattern_empty = false;
      break;
    }
  }
  if (pattern_empty) {
    // Pure insertion with no context anywhere: the old range denotes
    // the position — insertion happens after line
    // (old_first - 1 + range_count); following the unified convention
    // used by try_hunk, old_start is one past that line.
    h.old_start = old_first + (old_last - old_first + 1);
  } else {
    h.old_start = old_first;
  }
  h.new_start = new_first;
  h.old_no_newline = old_no_newline;
  h.new_no_newline = new_no_newline;
  return h;
}

// Parse one context hunk starting at the "***************" separator
// line `sep`; appends the converted hunk to `cur`.  Returns the index
// of the first unconsumed line.
size_t parse_context_hunk(const std::vector<std::string>& lines, size_t sep,
                          FilePatch& cur) {
  const size_t n = lines.size();
  size_t i = sep + 1;
  long long of = 0, ol = 0, nf = 0, nl = 0;
  bool have_old_range = false;
  bool have_new_range = false;
  std::vector<SecLine> old_seq;
  std::vector<SecLine> new_seq;
  bool old_nn = false;
  bool new_nn = false;
  if (i < n && parse_context_range(lines[i], "***", "****", of, ol)) {
    have_old_range = true;
    ++i;
    while (i < n) {
      const std::string& l = lines[i];
      long long f, la;
      if (parse_context_range(l, "---", "----", f, la)) break;
      if (is_context_separator(l)) break;
      if (!l.empty() && l[0] == '\\') {
        old_nn = true;
        ++i;
        continue;
      }
      SecLine sl;
      if (!context_section_line(l, true, sl)) break;
      old_seq.push_back(std::move(sl));
      ++i;
    }
  }
  if (i < n && parse_context_range(lines[i], "---", "----", nf, nl)) {
    have_new_range = true;
    ++i;
    while (i < n) {
      const std::string& l = lines[i];
      if (is_context_separator(l)) break;
      if (!l.empty() && l[0] == '\\') {
        new_nn = true;
        ++i;
        continue;
      }
      SecLine sl;
      if (!context_section_line(l, false, sl)) break;
      new_seq.push_back(std::move(sl));
      ++i;
    }
  }
  if (!have_old_range && !have_new_range) return sep + 1;  // not a hunk
  Hunk h = convert_context_hunk(of, ol, old_seq, nf, new_seq, old_nn, new_nn);
  // Retain the original sections and ranges for context-form rejects.
  h.ctx_old = old_seq;
  h.ctx_new = new_seq;
  if (have_old_range) {
    h.ctx_old_first = of;
    h.ctx_old_count = ol - of + 1;
  }
  if (have_new_range) {
    h.ctx_new_first = nf;
    h.ctx_new_count = nl - nf + 1;
  }
  if (cur.hunks.empty()) {
    cur.first_hunk_li = sep;
    cur.first_hunk_line = static_cast<long long>(sep) + 1;
  }
  cur.hunks.push_back(std::move(h));
  return i;
}

// Parse a normal-diff command line: "A[,B]a|C|D X[,Y]" with nothing
// else on the line.
bool parse_normal_head(const std::string& line, long long& of, long long& ol,
                       char& op, long long& nf, long long& nl) {
  size_t i = 0;
  const size_t n = line.size();
  auto num = [&]() {
    long long v = 0;
    const size_t start = i;
    while (i < n && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == start) return -1LL;
    try {
      v = std::stoll(line.substr(start, i - start));
    } catch (...) {
      return -1LL;
    }
    return v;
  };
  of = num();
  if (of < 0) return false;
  ol = of;
  if (i < n && line[i] == ',') {
    ++i;
    ol = num();
    if (ol < 0) return false;
  }
  if (i >= n || (line[i] != 'a' && line[i] != 'c' && line[i] != 'd')) {
    return false;
  }
  op = line[i];
  ++i;
  nf = num();
  if (nf < 0) return false;
  nl = nf;
  if (i < n && line[i] == ',') {
    ++i;
    nl = num();
    if (nl < 0) return false;
  }
  return i == n;
}

}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    patch,
    /* cmd_name */ "patch",
    /* cmd_synopsis */ "patch [OPTION]... [ORIGFILE [PATCHFILE]]",
    /* cmd_desc */
    "Apply a diff file to an original.\n"
    "Apply a patch (unified diff) to one or more original files, following "
    "GNU patch semantics: multi-file patches, offset and fuzz search, "
    "reject files, and backup-if-mismatch behavior.",
    /* examples */
    "  patch < changes.diff\n"
    "  patch -p1 < changes.diff\n"
    "  patch -i patch.txt original.c\n"
    "  patch -b -z .bak < changes.diff",
    /* see_also */ "diff, diff3",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ PATCH_OPTIONS) {
  const bool posixly_env = std::getenv("POSIXLY_CORRECT") != nullptr;
  const bool posix_mode = posixly_env || ctx.has("--posix");

  // ----- option decoding ---------------------------------------------
  // [GNU] numeric_string (patch.c:1094-1125): -p/-F values are parsed
  // by patch itself — optional sign, decimal digits only, 32-bit range
  // — and every failure is a fatal with patch's own wording.
  auto quotearg_simple = [](const std::string& s) -> std::string {
    if (s.empty()) return "''";
    for (unsigned char c : s) {
      if (!std::isalnum(c) && c != '_' && c != '@' && c != '%' && c != '+' &&
          c != '=' && c != ':' && c != ',' && c != '.' && c != '/' &&
          c != '-') {
        std::string out = "'";
        for (char ch : s) {
          if (ch == '\'') out += "'\\''";
          else out += ch;
        }
        return out + "'";
      }
    }
    return s;
  };
  auto numeric_string = [&](const std::string& string, const char* argtype,
                            long long& out) -> bool {
    size_t i = 0;
    long long sign = 1;
    if (i < string.size() && (string[i] == '-' || string[i] == '+')) {
      sign = string[i] == '-' ? -1 : 1;
      ++i;
    }
    long long value = 0;
    bool any_digits = false;
    for (; i < string.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(string[i]);
      if (c < '0' || c > '9') {
        safeErrorPrintLn(std::string("patch: **** ") + argtype + " " +
                         quotearg_simple(string) + " is not a number");
        return false;
      }
      any_digits = true;
      value = value * 10 + sign * (c - '0');
      if (value > std::numeric_limits<int>::max() ||
          value < std::numeric_limits<int>::min()) {
        safeErrorPrintLn(std::string("patch: **** ") + argtype + " " +
                         quotearg_simple(string) + " is too large");
        return false;
      }
    }
    if (!any_digits) {
      safeErrorPrintLn(std::string("patch: **** ") + argtype + " " +
                       quotearg_simple(string) + " is not a number");
      return false;
    }
    if (value < 0) {
      safeErrorPrintLn(std::string("patch: **** ") + argtype + " " +
                       quotearg_simple(string) + " is negative");
      return false;
    }
    out = value;
    return true;
  };

  // [GNU] -p with no argument-taking default: an unspecified strip is
  // "auto" (name as written, then basename); see target selection.
  const bool strip_specified = ctx.has("-p") || ctx.has("--strip");
  long long strip_components = 0;
  if (strip_specified) {
    const std::string value = ctx.has("-p")
                                  ? ctx.get<std::string>("-p", "")
                                  : ctx.get<std::string>("--strip", "");
    if (!numeric_string(value, "strip count", strip_components)) return 2;
  }

  long long maxfuzz = kDefaultFuzz;
  if (ctx.has("-F") || ctx.has("--fuzz")) {
    const std::string value = ctx.has("-F")
                                  ? ctx.get<std::string>("-F", "")
                                  : ctx.get<std::string>("--fuzz", "");
    if (!numeric_string(value, "fuzz factor", maxfuzz)) return 2;
  }

  bool reverse =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--reverse", false);
  // [GNU] -f and -t are distinct: -f suppresses the reversed-patch
  // DWIM entirely, while -t/--batch answers its questions by flipping
  // (ok_to_reverse, util.c:1043).  Both suppress file-name prompts.
  const bool force =
      ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  const bool batch =
      ctx.get<bool>("-t", false) || ctx.get<bool>("--batch", false);
  const bool forward = ctx.has("-N") || ctx.has("--forward");
  const bool silent =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--silent", false) ||
      ctx.get<bool>("-q", false) || ctx.get<bool>("--quiet", false);
  const bool verbose = ctx.has("-v") || ctx.has("--verbose");
  const bool dry_run = ctx.get<bool>("--dry-run", false);
  const bool make_backups = ctx.get<bool>("-b", false);
  // [GNU] patch.c:142-149: GNU mode defaults to backup-if-mismatch; this is
  // inverted (plain no-backup) under POSIXLY_CORRECT / --posix.
  const bool backup_if_mismatch = !posix_mode;

  std::string output_file = ctx.get<std::string>("-o", "");
  if (output_file.empty()) output_file = ctx.get<std::string>("--output", "");
  std::string reject_file_opt = ctx.get<std::string>("-r", "");
  if (reject_file_opt.empty())
    reject_file_opt = ctx.get<std::string>("--reject-file", "");
  std::string backup_suffix = ctx.get<std::string>("-z", "");
  if (backup_suffix.empty())
    backup_suffix = ctx.get<std::string>("--suffix", "");
  if (backup_suffix.empty()) {
    if (const char* env = std::getenv("SIMPLE_BACKUP_SUFFIX")) {
      backup_suffix = env;
    }
  }
  if (backup_suffix.empty()) backup_suffix = ".orig";

  // ----- issue #1381 option family ------------------------------------
  const bool ignore_ws =
      ctx.has("-l") || ctx.has("--ignore-whitespace");

  // --version-control / -V (fallback: VERSION_CONTROL env). GNU patch
  // 2.7.6 measured behavior: "none"/"off" selects NUMBERED naming (a
  // genuine upstream quirk, probed 2026-10-26), "simple"/"never" the
  // suffix form, "existing"/"nil" numbered only when a numbered backup
  // already exists, "numbered"/"t" always numbered.
  enum class VcMode { simple, numbered, existing };
  std::optional<VcMode> vc_mode;  // nullopt: port default (simple)
  {
    std::string vc = ctx.get<std::string>("--version-control", "");
    if (vc.empty()) vc = ctx.get<std::string>("-V", "");
    if (vc.empty()) {
      if (const char* env = std::getenv("VERSION_CONTROL")) vc = env;
    }
    if (!vc.empty()) {
      if (vc == "none" || vc == "off") {
        vc_mode = VcMode::numbered;
      } else if (vc == "simple" || vc == "never") {
        vc_mode = VcMode::simple;
      } else if (vc == "existing" || vc == "nil") {
        vc_mode = VcMode::existing;
      } else if (vc == "numbered" || vc == "t") {
        vc_mode = VcMode::numbered;
      } else {
        safeErrorPrintLn("patch: invalid argument '" + vc +
                         "' for '--version-control or -V option'");
        safeErrorPrintLn("Valid arguments are:");
        safeErrorPrintLn("  - 'none', 'off'");
        safeErrorPrintLn("  - 'simple', 'never'");
        safeErrorPrintLn("  - 'existing', 'nil'");
        safeErrorPrintLn("  - 'numbered', 't'");
        return 2;
      }
    }
  }
  const std::string backup_prefix = ctx.get<std::string>("--prefix", "");
  const std::string backup_basename_prefix =
      ctx.get<std::string>("--basename-prefix", "");

  auto compute_backup_name = [&](const std::string& target) -> std::string {
    // [GNU] --prefix replaces the whole naming scheme: PRE_ + name, the
    // suffix is dropped (probed). --basename-prefix keeps the directory.
    if (!backup_prefix.empty()) return backup_prefix + target;
    if (!backup_basename_prefix.empty()) {
      size_t sep = target.find_last_of("/\\");
      if (sep == std::string::npos) return backup_basename_prefix + target;
      return target.substr(0, sep + 1) + backup_basename_prefix +
             target.substr(sep + 1);
    }
    auto numbered_name = [&]() {
      for (long long n = 1;; ++n) {
        std::string cand = target + ".~" + std::to_string(n) + "~";
        if (!file_exists(cand)) return cand;
      }
    };
    switch (vc_mode.value_or(VcMode::simple)) {
      case VcMode::numbered:
        return numbered_name();
      case VcMode::existing: {
        bool any_numbered = false;
        for (long long n = 1; n <= 100000; ++n) {
          if (file_exists(target + ".~" + std::to_string(n) + "~")) {
            any_numbered = true;
            break;
          }
        }
        return any_numbered ? numbered_name() : target + backup_suffix;
      }
      case VcMode::simple:
      default:
        return target + backup_suffix;
    }
  };

  // --quoting-style: validated against GNU's style table; applied to the
  // "patching file" line and the rejects summary (issue #1381).
  const std::string quoting_style = ctx.get<std::string>("--quoting-style", "");
  if (!quoting_style.empty()) {
    static const char* kStyles[] = {
        "literal",   "shell",      "shell-always", "shell-escape",
        "shell-escape-always", "c", "c-maybe",     "escape",
        "locale",    "clocale"};
    bool known = false;
    for (const char* s : kStyles) known = known || quoting_style == s;
    if (!known) {
      safeErrorPrintLn("patch: invalid argument '" + quoting_style +
                       "' for 'quoting style'");
      safeErrorPrintLn("Try 'patch --help' for more information.");
      return 2;
    }
  }
  auto quote_name = [&](const std::string& name) -> std::string {
    if (quoting_style.empty() || quoting_style == "literal") return name;
    auto c_quote = [](const std::string& n) {
      std::string out = "\"";
      for (char c : n) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
      }
      return out + "\"";
    };
    auto shell_quote = [](const std::string& n) {
      std::string out = "'";
      for (char c : n) {
        if (c == '\'') {
          out += "'\\''";
        } else {
          out += c;
        }
      }
      return out + "'";
    };
    auto name_is_safe = [](const std::string& n) {
      for (unsigned char c : n) {
        if (!std::isalnum(c) && c != '.' && c != '_' && c != '-' &&
            c != '/' && c != '\\' && c != ':' && c != '~' && c != '+') {
          return false;
        }
      }
      return !n.empty();
    };
    if (quoting_style == "c" || quoting_style == "clocale") {
      return c_quote(name);
    }
    if (quoting_style == "c-maybe" || quoting_style == "locale") {
      return name_is_safe(name) ? name : c_quote(name);
    }
    if (quoting_style == "shell-always" ||
        quoting_style == "shell-escape-always") {
      return shell_quote(name);
    }
    if (quoting_style == "shell" || quoting_style == "shell-escape") {
      return name_is_safe(name) ? name : shell_quote(name);
    }
    // escape: backslash-escape anything non-alphanumeric.
    std::string out;
    for (unsigned char c : name) {
      if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') out += '\\';
      out += static_cast<char>(c);
    }
    return out;
  };

  // --get: a numeric value is accepted (no RCS/SCCS backend on this
  // port); anything else is a usage error, GNU wording (issue #1381).
  if (ctx.has("--get")) {
    const std::string get_value = ctx.get<std::string>("--get", "");
    long long parsed = 0;
    auto [gp, gec] = std::from_chars(get_value.data(),
                                     get_value.data() + get_value.size(),
                                     parsed);
    if (gec != std::errc() || gp != get_value.data() + get_value.size()) {
      safeErrorPrintLn("patch: **** get option value " + get_value +
                       " is not a number");
      safeErrorPrintLn("Try 'patch --help' for more information.");
      return 2;
    }
  }

  // --read-only: only the value set is validated here; "fail" is enforced
  // per target file below (issue #1381).
  const std::string read_only_mode = ctx.get<std::string>("--read-only", "");
  if (!read_only_mode.empty() && read_only_mode != "ignore" &&
      read_only_mode != "warn" && read_only_mode != "fail") {
    safeErrorPrintLn("patch: invalid argument '" + read_only_mode +
                     "' for '--read-only option'");
    safeErrorPrintLn("Valid arguments are:");
    safeErrorPrintLn("  - 'ignore'");
    safeErrorPrintLn("  - 'warn'");
    safeErrorPrintLn("  - 'fail'");
    return 2;
  }

  std::string directory = ctx.get<std::string>("-d", "");
  if (directory.empty()) directory = ctx.get<std::string>("--directory", "");
  std::string patch_file = ctx.get<std::string>("-i", "");
  if (patch_file.empty()) patch_file = ctx.get<std::string>("--input", "");
  // [native-glob] #1374: in WINUX_SHELL_GLOB=native mode the ORIGFILE
  // and PATCHFILE operands expand through the shared helper like their
  // siblings (first match each); the -i/--input option value is not an
  // operand and is left alone.
  auto expand_first_operand = [](std::string_view operand) -> std::string {
    auto expanded = expand_file_operand(operand);
    return expanded.empty() ? std::string(operand) : expanded.front();
  };
  std::string orig_file_arg;
  if (!ctx.positionals.empty()) {
    orig_file_arg = expand_first_operand(ctx.positionals[0]);
  }
  if (patch_file.empty() && ctx.positionals.size() > 1) {
    patch_file = expand_first_operand(ctx.positionals[1]);
  }

  const bool remove_empty_flag = ctx.has("--remove-empty-files");
  // [GNU] patch.c:547: GNU mode removes empty output files by default.
  const bool remove_empty_output = remove_empty_flag || !posix_mode;

  // Options not supported on this Windows port (serious trouble).
  const auto unsupported = [&](const char* name) -> int {
    safeErrorPrintLn(std::string("patch: ") + name +
                     " is not supported on Windows");
    return 2;
  };
  if (!ctx.get<std::string>("-D", "").empty() ||
      !ctx.get<std::string>("--ifdef", "").empty()) {
    return unsupported("--ifdef/-D");
  }
  if (ctx.has("--merge")) return unsupported("--merge");
  if (!ctx.get<std::string>("--reject-format", "").empty()) {
    return unsupported("--reject-format");
  }
  if (ctx.has("-T") || ctx.has("--set-time")) {
    return unsupported("--set-time/-T");
  }
  if (ctx.has("--set-utc")) return unsupported("--set-utc");
  if (ctx.has("--binary")) return unsupported("--binary");

  // [DIFFERS] -u/--unified is accepted silently; this implementation always
  // parses unified diff format.  --strip-trailing-slashes is accepted.

  // ----- read the patch ----------------------------------------------
  std::string patch_content;
  auto read_patch_file = [&](const std::string& path) -> bool {
    std::string raw;
    if (!read_file_raw(path, raw)) return false;
    patch_content = raw;
    return true;
  };
  if (!patch_file.empty()) {
    if (!read_patch_file(patch_file)) {
      safeErrorPrintLn("patch: cannot open patch file '" + patch_file +
                       "': No such file or directory");
      return 2;
    }
  } else {
    if (verbose) {
      safePrintLn("patch: reading patch from standard input");
    }
    file_io::set_stdin_binary();
    patch_content = std::string(std::istreambuf_iterator<char>(std::cin),
                                std::istreambuf_iterator<char>());
  }

  // ----- forced input format (issue #1381) ---------------------------
  // This engine parses unified diffs only; context/normal parsing is
  // #1346. Forcing one of those formats makes the (unified) input garbage
  // for the requested format, which GNU reports exactly this way.
  const bool force_unified = ctx.has("-u") || ctx.has("--unified");
  const bool force_other = ctx.has("-c") || ctx.has("--context") ||
                           ctx.has("-n") || ctx.has("--normal") ||
                           ctx.has("-e") || ctx.has("--ed");
  if (force_other && !force_unified) {
    // [GNU] fatal() prefix (util.c): "patch: **** ...".
    safeErrorPrintLn("patch: **** Only garbage was found in the patch input.");
    return 2;
  }

  // ----- parse into per-file hunk groups -----------------------------
  // raw_lines outlives the parse block: the missing-file dialogue in
  // the apply loop reprints a group's leading lines from it.
  std::vector<FilePatch> groups;
  std::vector<std::string> raw_lines;
  {
    {
      std::istringstream iss(patch_content);
      std::string line;
      while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        raw_lines.push_back(line);
      }
    }

    FilePatch cur;
    bool group_open = false;
    bool in_hunk = false;
    Hunk cur_hunk;
    size_t cur_hunk_li = 0;  // raw line index of cur_hunk's "@@" header
    long long oc = 0;
    long long nc = 0;
    char last_tag = 0;
    // Format of the group currently being read.  A group with no hunks
    // is not a patch at all: GNU patch reports such input ("--- /+++"
    // headers alone, a context header pair alone, ...) as garbage.
    PatchFormat cur_format = PatchFormat::unified;
    long long normal_delta = 0;  // cumulative line delta, normal groups
    size_t scan_li = 0;          // line index currently being scanned
    size_t group_end_li = 0;     // first line past the open group's hunks
    size_t pending_lead_begin = 0;  // lead start for the next group
    std::string pending_index;      // last "Index: " name seen

    auto close_hunk = [&]() {
      if (in_hunk) {
        cur_hunk.old_count = 0;
        cur_hunk.new_count = 0;
        for (const auto& l : cur_hunk.lines) {
          if (l.tag != '+') cur_hunk.old_count++;
          if (l.tag != '-') cur_hunk.new_count++;
        }
        if (cur.hunks.empty()) {
          cur.first_hunk_li = cur_hunk_li;
          cur.first_hunk_line = static_cast<long long>(cur_hunk_li) + 1;
        }
        cur.hunks.push_back(cur_hunk);
        cur_hunk = Hunk();
        in_hunk = false;
        oc = nc = 0;
        last_tag = 0;
        group_end_li = scan_li;
      }
    };
    auto close_group = [&]() {
      close_hunk();
      if (group_open && !cur.hunks.empty()) {
        cur.format = cur_format;
        groups.push_back(cur);
        pending_lead_begin = group_end_li;
      }
      cur = FilePatch();
      group_open = false;
      cur_format = PatchFormat::unified;
      normal_delta = 0;
    };

    // Consume the body lines of one normal-diff command whose head is
    // at raw_lines[head_idx] and append the converted (exact-position)
    // hunk to `cur`.  Returns the first unconsumed line index; on a
    // malformed body the hunk is abandoned and scanning resumes right
    // after the command line.
    auto parse_normal_body = [&](size_t head_idx, long long nof,
                                 long long nol, char nop, long long nnf,
                                 long long nnl) -> size_t {
      const size_t total = raw_lines.size();
      size_t i = head_idx + 1;
      const long long old_count = (nop == 'a') ? 0 : (nol - nof + 1);
      const long long new_count = (nop == 'd') ? 0 : (nnl - nnf + 1);
      Hunk h;
      h.exact_position = true;
      bool malformed = false;
      auto read_side = [&](long long count, char prefix, char tag,
                           bool& no_newline) {
        for (long long k = 0; k < count && !malformed; ++k) {
          if (i >= total) {
            malformed = true;
            break;
          }
          const std::string& l = raw_lines[i];
          if (l.size() >= 2 && l[0] == prefix && l[1] == ' ') {
            h.lines.push_back({tag, l.substr(2)});
          } else if (l.size() == 1 && l[0] == prefix) {
            h.lines.push_back({tag, ""});
          } else {
            malformed = true;
            break;
          }
          ++i;
          if (i < total && !raw_lines[i].empty() && raw_lines[i][0] == '\\') {
            no_newline = true;
            ++i;
          }
        }
      };
      read_side(old_count, '<', '-', h.old_no_newline);
      if (!malformed && nop == 'c') {
        if (i < total && raw_lines[i] == "---") {
          ++i;
        } else {
          malformed = true;
        }
      }
      if (!malformed) {
        read_side(new_count, '>', '+', h.new_no_newline);
      }
      if (malformed) return head_idx + 1;
      if (cur.hunks.empty()) {
        cur.first_hunk_li = head_idx;
        cur.first_hunk_line = static_cast<long long>(head_idx) + 1;
      }
      h.old_count = old_count;
      h.new_count = new_count;
      // 'a' inserts AFTER old line nof; 'c'/'d' start AT old line nof.
      // Both are stated against the original file, so fold in the
      // cumulative delta of the hunks already read for this group.
      h.old_start =
          (nop == 'a') ? (nof + normal_delta + 1) : (nof + normal_delta);
      h.new_start = nnf + normal_delta;
      normal_delta += new_count - old_count;
      cur.hunks.push_back(std::move(h));
      return i;
    };

    for (size_t li = 0; li < raw_lines.size(); ++li) {
      scan_li = li;
      const std::string& line = raw_lines[li];
      // Hunk body lines take priority over header detection: a removed
      // line whose text is "--- ..." must still count toward the hunk.
      // But a non-body line where body lines are still expected ends the
      // (mangled) hunk without consuming the line.
      if (in_hunk && (oc < cur_hunk.old_count || nc < cur_hunk.new_count) &&
          !line.empty() && line[0] != '\\' && line[0] != ' ' &&
          line[0] != '-' && line[0] != '+') {
        close_hunk();
      }
      // Context format: inside a context group, a separator line starts
      // the next hunk; anything else ends the group and is reprocessed
      // below as a potential new group header.
      if (group_open && cur_format == PatchFormat::context) {
        if (is_context_separator(line)) {
          const size_t next = parse_context_hunk(raw_lines, li, cur);
          group_end_li = next;
          li = next - 1;
          continue;
        }
        if (line.empty()) continue;  // tolerate blank lines between hunks
        close_group();
      }
      // Context group header pair: "*** old" immediately followed by
      // "--- new".  (A unified "--- " line is followed by "+++ ", and a
      // context section header by "***************", so this is
      // unambiguous.)
      if (line.length() >= 4 && line.compare(0, 4, "*** ") == 0 &&
          li + 1 < raw_lines.size() && raw_lines[li + 1].length() >= 4 &&
          raw_lines[li + 1].compare(0, 4, "--- ") == 0) {
        close_group();
        cur.old_name = header_name(line.substr(4));
        cur.new_name = header_name(raw_lines[li + 1].substr(4));
        cur.old_header_raw = line.substr(4);
        cur.new_header_raw = raw_lines[li + 1].substr(4);
        cur.lead_begin = pending_lead_begin;
        cur.index_name = pending_index;
        pending_index.clear();
        group_open = true;
        cur_format = PatchFormat::context;
        ++li;  // consume the "--- new" line
        continue;
      }
      if (line.length() >= 4 && line.compare(0, 4, "--- ") == 0) {
        close_group();
        cur.old_name = header_name(line.substr(4));
        cur.old_header_raw = line.substr(4);
        cur.lead_begin = pending_lead_begin;
        cur.index_name = pending_index;
        pending_index.clear();
        group_open = true;
        continue;
      }
      if (line.length() >= 4 && line.compare(0, 4, "+++ ") == 0) {
        if (!group_open) {
          cur.lead_begin = pending_lead_begin;
          cur.index_name = pending_index;
          pending_index.clear();
        }
        cur.new_name = header_name(line.substr(4));
        cur.new_header_raw = line.substr(4);
        group_open = true;
        continue;
      }
      // An "Index: " line names the file for the group that follows.
      if (!in_hunk && line.length() >= 7 && line.compare(0, 7, "Index: ") == 0) {
        pending_index = header_name(line.substr(7));
        continue;
      }
      // Normal (ed-style) format: a bare "A[,B]a|c|d X[,Y]" command
      // line.  Normal groups carry no file names; the target comes
      // from the ORIGFILE operand at apply time.
      if (!in_hunk) {
        long long nof, nol, nnf, nnl;
        char nop;
        if (parse_normal_head(line, nof, nol, nop, nnf, nnl)) {
          if (!group_open || cur_format != PatchFormat::normal) {
            close_group();
            group_open = true;
            cur_format = PatchFormat::normal;
            cur.lead_begin = pending_lead_begin;
            cur.index_name = pending_index;
            pending_index.clear();
            normal_delta = 0;
          }
          const size_t next =
              parse_normal_body(li, nof, nol, nop, nnf, nnl);
          group_end_li = next;
          li = next - 1;
          continue;
        }
      }
      Hunk parsed;
      if (line.length() >= 6 && line.compare(0, 4, "@@ -") == 0 &&
          parse_hunk_header(line, parsed)) {
        close_hunk();
        cur_hunk = parsed;
        cur_hunk_li = li;
        in_hunk = true;
        oc = nc = 0;
        last_tag = 0;
        continue;
      }
      if (!in_hunk) continue;

      if (!line.empty() && line[0] == '\\') {
        // "\ No newline at end of file": attaches to the last line of the
        // side it follows; never terminates the hunk.
        switch (last_tag) {
          case '+':
            cur_hunk.new_no_newline = true;
            break;
          case '-':
            cur_hunk.old_no_newline = true;
            break;
          case ' ':
            cur_hunk.old_no_newline = true;
            cur_hunk.new_no_newline = true;
            break;
          default:
            break;
        }
        continue;
      }

      // Hunk body: count lines per the @@ header.  A blank line is context
      // for BOTH sides.
      if (oc < cur_hunk.old_count || nc < cur_hunk.new_count) {
        char tag = line.empty() ? ' ' : line[0];
        std::string text = line.empty() ? "" : line.substr(1);
        if (tag == ' ') {
          cur_hunk.lines.push_back({' ', text});
          oc++;
          nc++;
        } else if (tag == '-') {
          cur_hunk.lines.push_back({'-', text});
          oc++;
        } else if (tag == '+') {
          cur_hunk.lines.push_back({'+', text});
          nc++;
        } else {
          // Mangled: stop the hunk here.
          close_hunk();
        }
        if (tag == ' ' || tag == '-' || tag == '+') last_tag = tag;
        continue;
      }
      // Counts satisfied: any other line ends the hunk.
      close_hunk();
    }
    close_group();
  }

  if (groups.empty()) {
    // [GNU] A completely empty patch input is not an error: another_
    // file simply finds nothing to do and patch exits 0 (pch.c:214:
    // the garbage fatal requires a nonempty input).  Anything else
    // without a single hunk group is garbage.
    if (patch_content.empty()) return 0;
    safeErrorPrintLn("patch: **** Only garbage was found in the patch input.");
    return 2;
  }

  // ----- apply each file group to its own target ----------------------
  const auto say = [&](const std::string& msg) {
    if (!silent) safePrintLn(msg);
  };

  long long failed_total = 0;
  bool any_output_written = false;
  std::string combined_out;  // for -o across groups
  bool reject_file_written = false;
  std::string reject_out;  // accumulated content for -r
  // Reject files created during this run: a later group patching the
  // same target appends, like GNU's temp-file hand-off (patch.c:660).
  std::set<std::string> rej_files_created;

  // [GNU] Per-file summary (patch.c:632): "N out of M hunk(s) FAILED"
  // — or "ignored" when the file was skipped whole.
  auto summary_text = [](long long failed_count, size_t total,
                         bool ignored) {
    return std::to_string(failed_count) + " out of " +
           std::to_string(static_cast<long long>(total)) +
           (total == 1 ? " hunk" : " hunks") +
           (ignored ? " ignored" : " FAILED");
  };

  for (auto& group : groups) {
    if (reverse) {
      std::swap(group.old_name, group.new_name);
      std::swap(group.old_header_raw, group.new_header_raw);
      for (auto& h : group.hunks) h = reversed(h);
    }

    const bool creation = is_dev_null(group.old_name);
    const bool deletion = is_dev_null(group.new_name);

    auto with_dir = [&](const std::string& name) -> std::string {
      if (directory.empty() || name.empty()) return name;
      std::string sep =
          (directory.back() == '/' || directory.back() == '\\') ? "" : "/";
      std::string out = directory + sep + name;
      for (char& c : out) {
        if (c == '\\') c = '/';
      }
      return out;
    };
    // [GNU] File-name selection (pch.c intuit_diff_type + best_name,
    // pch.c:862-975/1039-1083): candidates are the prepared old / new /
    // Index names; Index is discarded when a header name exists,
    // unless POSIX mode (pch.c:870).  POSIX takes the first candidate
    // that exists, in old/new/Index order; otherwise the best existing
    // candidate wins — fewest path components, then shortest basename,
    // then shortest name, then old/new/Index order.  When nothing
    // exists and the old side says nonexistent (a creation), the
    // candidate creating the fewest new directories wins, with the
    // same tie-breaks.  An ORIGFILE operand names the target outright
    // and is never stripped.
    std::string target;
    bool target_from_argv = false;
    if (!orig_file_arg.empty()) {
      target = with_dir(orig_file_arg);
      target_from_argv = true;
    } else {
      struct Candidate {
        std::string name;  // prepared (stripped) name
        int order = 0;     // 0 old, 1 new, 2 index
      };
      std::vector<Candidate> candidates;
      const std::string raw_names[3] = {group.old_name, group.new_name,
                                        group.index_name};
      for (int i = 0; i < 3; ++i) {
        if (raw_names[i].empty() || is_dev_null(raw_names[i])) continue;
        if (i == 2 && !posix_mode &&
            (!raw_names[0].empty() || !raw_names[1].empty()))
          continue;  // Index discarded beside a header name
        if (auto prepared = strip_name(raw_names[i], strip_specified,
                                       strip_components))
          candidates.push_back({*prepared, i});
      }
      auto components_of = [](const std::string& n) {
        int count = 0;
        for (size_t i = 0; i < n.size();) {
          if (n[i] == '/' || n[i] == '\\') {
            ++count;
            while (i < n.size() && (n[i] == '/' || n[i] == '\\')) ++i;
          } else {
            ++i;
          }
        }
        return count;
      };
      auto basename_len = [](const std::string& n) {
        const size_t slash = n.find_last_of("/\\");
        return slash == std::string::npos ? n.size() : n.size() - slash - 1;
      };
      auto best_of = [&](const std::vector<Candidate>& pool) -> std::string {
        const Candidate* best = nullptr;
        for (const auto& c : pool) {
          if (!best) {
            best = &c;
            continue;
          }
          const int cc = components_of(c.name);
          const int bc = components_of(best->name);
          const size_t cb = basename_len(c.name);
          const size_t bb = basename_len(best->name);
          if (cc < bc || (cc == bc && (cb < bb ||
                                       (cb == bb &&
                                        (c.name.size() < best->name.size() ||
                                         (c.name.size() == best->name.size() &&
                                          c.order < best->order))))))
            best = &c;
        }
        return best ? best->name : std::string();
      };
      std::vector<Candidate> existing;
      for (const auto& c : candidates) {
        if (file_exists(with_dir(c.name))) existing.push_back(c);
      }
      if (!existing.empty()) {
        target = with_dir(posix_mode ? existing.front().name
                                     : best_of(existing));
      } else if (creation && !candidates.empty()) {
        // [GNU] Creation (pch.c:949-975): fewest new directories.
        auto new_dirs = [&](const std::string& n) {
          int missing = 0;
          const size_t slash = n.find_last_of("/\\");
          if (slash == std::string::npos) return 0;
          std::string accum;
          for (size_t i = 0; i < slash;) {
            size_t j = n.find_first_of("/\\", i);
            if (j == std::string::npos || j > slash) j = slash;
            if (j == i) {  // slash run: not a component of its own
              i = j + 1;
              continue;
            }
            if (!accum.empty()) accum += "/";
            accum += n.substr(i, j - i);
            if (!file_exists(with_dir(accum))) ++missing;
            i = j + 1;
          }
          return missing;
        };
        int min_missing = std::numeric_limits<int>::max();
        for (const auto& c : candidates) {
          min_missing = std::min(min_missing, new_dirs(c.name));
        }
        std::vector<Candidate> nearest;
        for (const auto& c : candidates) {
          if (new_dirs(c.name) == min_missing) nearest.push_back(c);
        }
        target = with_dir(best_of(nearest));
      }
    }

    // [GNU] No candidate exists: the file-name dialogue (pch.c:270-
    // 320).  The group's hunks are counted as ignored, no rejects are
    // written, and the run continues with the next file.
    bool argv_missing = false;
    if (target.empty()) {
      if (!silent) {
        say("can't find file to patch at input line " +
            std::to_string(group.first_hunk_line));
        if (group.format != PatchFormat::normal) {
          say(strip_specified
                  ? "Perhaps you used the wrong -p or --strip option?"
                  : "Perhaps you should have used the -p or --strip option?");
        }
        safePrintLn("The text leading up to this was:");
        safePrintLn("--------------------------");
        for (size_t li = group.lead_begin;
             li < group.first_hunk_li && li < raw_lines.size(); ++li) {
          safePrintLn("|" + raw_lines[li]);
        }
        safePrintLn("--------------------------");
      }
      bool skip_group = false;
      if (force || batch) {
        say("No file to patch.  Skipping patch.");
        skip_group = true;
      } else {
        // Prompts go to stdout; answers are read from stdin (patch's
        // tty).  An exhausted stdin yields the default "yes, skip".
        while (target.empty() && !skip_group) {
          safePrint("File to patch: ");
          std::string answer;
          const bool got = static_cast<bool>(std::getline(std::cin, answer));
          if (got && !answer.empty()) {
            std::string cand = with_dir(answer);
            if (file_exists(cand)) {
              target = cand;
              break;
            }
            safeErrorPrintLn(answer + ": No such file or directory");
          }
          safePrint("Skip this patch? [y] ");
          std::string again;
          const bool got2 = static_cast<bool>(std::getline(std::cin, again));
          if (!got2 || again.empty() || again[0] != 'n') {
            say("Skipping patch.");
            skip_group = true;
          }
        }
      }
      if (skip_group) {
        failed_total += static_cast<long long>(group.hunks.size());
        say(summary_text(static_cast<long long>(group.hunks.size()),
                         group.hunks.size(), true));
        continue;
      }
    }
    if (target_from_argv && !creation && !file_exists(target)) {
      // [GNU] An ORIGFILE named on the command line is never prompted
      // for: the hunks run against a missing input and the output
      // stage then fatals (see below), exit 2.
      argv_missing = !dry_run;
    }

    say(std::string(dry_run ? "checking file " : "patching file ") +
        quote_name(target));

    FileState st;
    st.exists = file_exists(target);
    std::string raw;
    if (st.exists) {
      if (!read_file_raw(target, raw)) {
        safeErrorPrintLn("patch: cannot open file '" + target + "'");
        return 2;
      }
      st.input = split_lines(raw, st.crlf);
    } else if (!creation && !argv_missing) {
      // A nonexistent file is only valid when the patch creates it (old
      // side /dev/null).  An existing empty file is a valid target.
      safeErrorPrintLn("patch: cannot open file '" + target +
                       "': No such file or directory");
      return 2;
    }

    // [GNU] --read-only=fail (issue #1381): refuse up front, leaving the
    // file untouched, when the target carries the read-only attribute.
    if (read_only_mode == "fail" && st.exists) {
      DWORD attrs = GetFileAttributesW(utf8_to_wstring(target).c_str());
      if (attrs != INVALID_FILE_ATTRIBUTES &&
          (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
        safeErrorPrintLn("patch: file '" + target + "' is read-only");
        return 2;
      }
    }

    // ----- apply hunks (locate_hunk semantics) ------------------------
    // A reject records the hunk in the orientation in effect when it
    // failed, plus the output offset at that moment (patch.c:1290).
    struct RejectRec {
      Hunk hunk;
      long long out_offset = 0;
    };
    std::vector<RejectRec> rejects;
    long long failed = 0;
    long long hunk_no = 0;
    bool mismatch = false;   // fuzz > 0 or offset != 0 for this file
    bool skip_rest = false;  // whole-file skip (reversed DWIM)
    bool flipped = false;    // DWIM flipped this file's orientation
    bool dwim_decided = false;

    auto attempt_locate = [&](const Hunk& h, std::vector<std::string>& pat_out)
        -> Attempt {
      pat_out = pattern_of(h);
      long long prefix_context = 0;
      long long suffix_context = 0;
      {
        // Count leading and trailing context (' ') lines of the hunk.
        long long idx = 0;
        while (idx < static_cast<long long>(h.lines.size()) &&
               h.lines[static_cast<size_t>(idx)].tag == ' ') {
          ++prefix_context;
          ++idx;
        }
        idx = static_cast<long long>(h.lines.size()) - 1;
        while (idx >= 0 && h.lines[static_cast<size_t>(idx)].tag == ' ') {
          ++suffix_context;
          --idx;
        }
      }
      Attempt att;
      if (h.exact_position) {
        // [GNU] normal (ed-style) hunks carry no context to search
        // with: the old lines must match at exactly the stated
        // position (the parser folded in the cumulative delta of
        // earlier hunks of the group).  No offset search, no fuzz.
        att.where0 = h.old_start - 1;
        att.offset = 0;
        att.fuzz = 0;
        att.ok = att.where0 >= st.copy_ptr &&
                 att.where0 + static_cast<long long>(pat_out.size()) <=
                     static_cast<long long>(st.input.size());
        for (size_t k = 0; att.ok && k < pat_out.size(); ++k) {
          if (st.input[static_cast<size_t>(att.where0) + k].text !=
              pat_out[k]) {
            att.ok = false;
          }
        }
        return att;
      }
      return try_hunk(st, pat_out, prefix_context, suffix_context,
                      h.old_start, maxfuzz, ignore_ws);
    };

    for (const auto& stored_hunk : group.hunks) {
      ++hunk_no;
      if (skip_rest) {
        // [GNU] Once the file is skipped, its remaining hunks are
        // only counted and rejected (patch.c:476-487).
        ++failed;
        ++failed_total;
        rejects.push_back({stored_hunk, st.out_offset});
        continue;
      }
      Hunk eff = flipped ? reversed(stored_hunk) : stored_hunk;
      std::vector<std::string> pat;
      Attempt att = attempt_locate(eff, pat);

      if (!att.ok && hunk_no == 1 && !dwim_decided && !force) {
        // [GNU] First-hunk reversed DWIM (patch.c:424-458): when the
        // forward hunk does not match but the swapped one does, the
        // patch is reversed (or was already applied).  -N skips the
        // whole file; --batch flips it; otherwise the user is asked.
        dwim_decided = true;
        std::vector<std::string> spat;
        Attempt sw = attempt_locate(reversed(eff), spat);
        if (sw.ok) {
          const bool reversed_now = reverse != flipped;
          const std::string detected =
              std::string(reversed_now
                              ? "Unreversed"
                              : "Reversed (or previously applied)") +
              " patch detected!";
          if (forward) {
            if (!silent) {
              safePrint(detected);
              safePrintLn("  Skipping patch.");
            }
            skip_rest = true;
            ++failed;
            ++failed_total;
            rejects.push_back({stored_hunk, st.out_offset});
            continue;
          }
          if (batch) {
            if (!silent) {
              safePrint(detected);
              safePrintLn(reversed_now ? "  Ignoring -R."
                                       : "  Assuming -R.");
            }
            flipped = !flipped;
            eff = flipped ? reversed(stored_hunk) : stored_hunk;
            att = attempt_locate(eff, pat);
          } else {
            safePrint(detected);
            safePrint(reversed_now ? "  Ignore -R? [n] "
                                   : "  Assume -R? [n] ");
            std::string answer;
            const bool got =
                static_cast<bool>(std::getline(std::cin, answer));
            if (got && !answer.empty() && answer[0] == 'y') {
              flipped = !flipped;
              eff = flipped ? reversed(stored_hunk) : stored_hunk;
              att = attempt_locate(eff, pat);
            }
            // [GNU] A declined flip has no second prompt: the swapped
            // hunk is put back and this hunk simply fails through the
            // normal path below (patch.c:439-451 — apply_anyway is an
            // internal DWIM-suppression flag, not a question).
          }
        }
      }

      if (!att.ok) {
        ++failed;
        ++failed_total;
        // [GNU] FAILED positions are output coordinates (patch.c:478).
        say("Hunk #" + std::to_string(hunk_no) + " FAILED at " +
            std::to_string(eff.old_start + st.out_offset) + ".");
        rejects.push_back({eff, st.out_offset});
        continue;
      }

      // Splice: copy untouched input lines up to the match window, then
      // emit the new side.  Context lines dropped by fuzz stay in place
      // (they are copied verbatim before the window or left after it).
      // A null pattern (file creation) has no match position; insert at
      // the next line to emit.
      long long where0 = att.where0;
      if (where0 < st.copy_ptr) where0 = st.copy_ptr;
      const long long window_start = where0 + att.pf;
      const long long window_len =
          static_cast<long long>(pat.size()) - att.pf - att.sf;
      for (long long i = st.copy_ptr; i < window_start; ++i) {
        st.out.push_back(st.input[static_cast<size_t>(i)]);
      }
      long long win = 0;   // index within the match window
      long long pidx = 0;  // absolute pattern line index
      const long long pat_lines_total = static_cast<long long>(pat.size());
      long long last_out = -1;
      for (const auto& pl : eff.lines) {
        if (pl.tag == ' ') {
          // Context lines dropped by fuzz stay in the file untouched.
          if (pidx < att.pf || pidx >= pat_lines_total - att.sf) {
            pidx++;
            continue;
          }
          // Context line: preserve the target's own line ending.
          st.out.push_back(st.input[static_cast<size_t>(window_start + win)]);
          last_out = static_cast<long long>(st.out.size()) - 1;
          pidx++;
          win++;
        } else if (pl.tag == '-') {
          pidx++;
          win++;
        } else {
          st.out.push_back({pl.text, true});
          last_out = static_cast<long long>(st.out.size()) - 1;
        }
      }
      if (last_out >= 0 && eff.new_no_newline) {
        st.out[static_cast<size_t>(last_out)].nl = false;
      }
      st.copy_ptr = window_start + window_len;

      // [GNU] Bookkeeping (patch.c:476-499): in_offset is cumulative
      // across the file's hunks, the reported position is the output
      // coordinate (match position + out_offset from before this
      // hunk), and the success line prints whenever fuzz or the
      // standing in_offset is nonzero.
      const long long out_before = st.out_offset;
      st.in_offset += att.offset;
      st.out_offset += eff.new_count - eff.old_count;
      if (att.fuzz > 0 || st.in_offset != 0) {
        mismatch = true;
        std::string msg = "Hunk #" + std::to_string(hunk_no) +
                          " succeeded at " +
                          std::to_string(att.where0 + 1 + out_before);
        if (att.fuzz > 0) {
          msg += " with fuzz " + std::to_string(att.fuzz);
        }
        if (st.in_offset != 0) {
          msg += " (offset " + std::to_string(st.in_offset) + " " +
                 plural_lines(st.in_offset) + ")";
        }
        say(msg + ".");
      }
    }

    // Copy the remainder of the input.
    for (long long i = st.copy_ptr; i < static_cast<long long>(st.input.size());
         ++i) {
      st.out.push_back(st.input[static_cast<size_t>(i)]);
    }

    if (argv_missing) {
      // [GNU] The output stage cannot reopen a named-but-missing
      // ORIGFILE: fatal, with no summary and no rejects (the pfatal in
      // output_file, patch.c).
      safeErrorPrintLn("patch: **** Can't reopen file " + target +
                       " : No such file or directory");
      return 2;
    }

    // ----- rejects + summary (patch.c:625-670) ------------------------
    // The first reject of a file carries the header (an Index: line
    // when present, then the header pair verbatim from the input);
    // hunk coordinates are output coordinates using the out_offset
    // snapshot taken when the hunk failed (abort_hunk_unified /
    // abort_hunk_context, patch.c:1273/1330).
    auto render_rejects = [&]() -> std::string {
      std::string content;
      const std::string& old_raw =
          flipped ? group.new_header_raw : group.old_header_raw;
      const std::string& new_raw =
          flipped ? group.old_header_raw : group.new_header_raw;
      auto ctx_range = [](long long first, long long count) -> std::string {
        if (count <= 0) return "0";
        if (count == 1) return std::to_string(first);
        return std::to_string(first) + "," +
               std::to_string(first + count - 1);
      };
      bool first = true;
      for (const auto& rec : rejects) {
        const Hunk& h = rec.hunk;
        if (group.format == PatchFormat::unified) {
          if (first) {
            if (!group.index_name.empty()) {
              content += "Index: " + group.index_name + "\n";
            }
            content += "--- " + old_raw + "\n";
            content += "+++ " + new_raw + "\n";
          }
          content += "@@ -" +
                     range_str(h.old_start + rec.out_offset, h.old_count) +
                     " +" +
                     range_str(h.new_start + rec.out_offset, h.new_count) +
                     " @@" + h.func_context + "\n";
          // [GNU] Rejects are normalized: '-' lines precede '+' lines
          // within each change block (abort_hunk_unified).
          size_t li2 = 0;
          while (li2 < h.lines.size()) {
            if (h.lines[li2].tag == ' ') {
              content += " " + h.lines[li2].text + "\n";
              ++li2;
              continue;
            }
            std::string minus_lines, plus_lines;
            while (li2 < h.lines.size() && h.lines[li2].tag != ' ') {
              if (h.lines[li2].tag == '-') {
                minus_lines += "-" + h.lines[li2].text + "\n";
              } else {
                plus_lines += "+" + h.lines[li2].text + "\n";
              }
              ++li2;
            }
            content += minus_lines + plus_lines;
          }
          if (h.old_no_newline || h.new_no_newline) {
            content += "\\ No newline at end of file\n";
          }
        } else {
          // Context form; normal-diff input lands here too, because
          // abort_hunk dispatches everything non-unified to
          // abort_hunk_context (patch.c:1399).
          if (first) {
            if (!group.index_name.empty()) {
              content += "Index: " + group.index_name + "\n";
            }
            content += "*** " + (old_raw.empty() ? target : old_raw) + "\n";
            content += "--- " + (new_raw.empty() ? target : new_raw) + "\n";
          }
          content += "***************" + h.func_context + "\n";
          long long ofirst = h.ctx_old_first, ocount = h.ctx_old_count;
          long long nfirst = h.ctx_new_first, ncount = h.ctx_new_count;
          const std::vector<SecLine>* osec = &h.ctx_old;
          const std::vector<SecLine>* nsec = &h.ctx_new;
          std::vector<SecLine> synth_old, synth_new;
          if (osec->empty() && nsec->empty() && ocount == 0 && ncount == 0) {
            // Normal-diff hunk: its sections are the - / + lines.
            ofirst = h.old_start;
            ocount = h.old_count;
            nfirst = h.new_start;
            ncount = h.new_count;
            for (const auto& l : h.lines) {
              if (l.tag == '-') synth_old.push_back({'-', l.text});
              else if (l.tag == '+') synth_new.push_back({'+', l.text});
            }
            osec = &synth_old;
            nsec = &synth_new;
          }
          content += "*** " + ctx_range(ofirst + rec.out_offset, ocount) +
                     " ****\n";
          for (const auto& l : *osec) {
            content += std::string(1, l.tag) + " " + l.text + "\n";
          }
          content += "--- " + ctx_range(nfirst + rec.out_offset, ncount) +
                     " ----\n";
          for (const auto& l : *nsec) {
            content += std::string(1, l.tag) + " " + l.text + "\n";
          }
        }
        first = false;
      }
      return content;
    };

    const std::string rej_path =
        !reject_file_opt.empty()
            ? reject_file_opt
            : (output_file.empty() ? target : output_file) + ".rej";
    if (!rejects.empty()) {
      // [GNU] The summary prints whenever hunks failed — also under
      // --dry-run; the saving clause appears only when rejects really
      // go to a named file (patch.c:636-670).
      const bool write_rej = !dry_run && reject_file_opt != "-";
      if (write_rej) {
        std::string content = render_rejects();
        if (!reject_file_opt.empty()) {
          reject_out += content;
        } else {
          const bool append = rej_files_created.count(rej_path) != 0;
          const bool ok = append ? append_file_raw(rej_path, content)
                                 : write_file_raw(rej_path, content);
          if (!ok) {
            safeErrorPrintLn("patch: cannot create reject file '" + rej_path +
                             "'");
            return 2;
          }
          rej_files_created.insert(rej_path);
        }
      }
      say(summary_text(failed, group.hunks.size(), skip_rest) +
          (write_rej ? " -- saving rejects to file " + quote_name(rej_path)
                     : ""));
    }

    if (skip_rest) continue;  // whole-file skip: target stays untouched

    // ----- backup ------------------------------------------------------
    const bool backup =
        make_backups || (backup_if_mismatch && (mismatch || failed > 0));
    const bool wrote_output_now = output_file.empty();
    if (backup && !dry_run && st.exists && !creation && wrote_output_now) {
      const std::string backup_name = compute_backup_name(target);
      if (!backup_file_to(target, backup_name)) {
        safeErrorPrintLn("patch: cannot create backup file '" + backup_name +
                         "'");
        return 2;
      }
    }

    // ----- write output ------------------------------------------------
    const std::string content = join_lines(st.out, st.crlf);
    if (!output_file.empty()) {
      combined_out += content;
    } else if (!dry_run) {
      const bool empty_out = st.out.empty();
      if (empty_out && (remove_empty_output || deletion)) {
        if (st.exists) {
          DeleteFileW(utf8_to_wstring(target).c_str());
          if (verbose) {
            safePrintLn("patch: removed empty file '" + target + "'");
          }
        }
      } else {
        if (!write_file_raw(target, content)) {
          safeErrorPrintLn("patch: cannot write to file '" + target + "'");
          return 2;
        }
        any_output_written = true;
      }
    }
  }

  if (!output_file.empty() && !dry_run) {
    if (!write_file_raw(output_file, combined_out)) {
      safeErrorPrintLn("patch: cannot write to file '" + output_file + "'");
      return 2;
    }
    any_output_written = true;
  }
  if (!reject_out.empty() && !reject_file_opt.empty() &&
      reject_file_opt != "-" && !dry_run) {
    if (!write_file_raw(reject_file_opt, reject_out)) {
      safeErrorPrintLn("patch: cannot create reject file '" + reject_file_opt +
                       "'");
      return 2;
    }
    reject_file_written = true;
  }
  (void)any_output_written;
  (void)reject_file_written;

  return failed_total > 0 ? 1 : 0;
}
