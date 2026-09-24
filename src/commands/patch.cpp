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
    // [GNU] -p, --strip=NUM: strip NUM leading components from file names
    OPTION("-p", "--strip", "strip NUM leading components from file names",
           INT_TYPE),
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
    // [GNU] -F, --fuzz=NUM: set maximum fuzz factor (default 2)
    OPTION("-F", "--fuzz", "set maximum fuzz factor", INT_TYPE),
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
    // [GNU] -v, --verbose: print verbose output
    OPTION("-v", "--verbose", "print verbose output"),
    // [GNU] --binary: read and write in binary mode
    OPTION("", "--binary", "read and write in binary mode"),
    // [GNU] --posix: conform to POSIX standard
    OPTION("", "--posix", "conform to POSIX standard"),
    // [GNU] --quoting-style / --strip-trailing-slashes are accepted for
    // compatibility; only the latter has effect here.
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

struct Hunk {
  long long old_start = 0;
  long long old_count = 0;
  long long new_start = 0;
  long long new_count = 0;
  std::vector<HunkLine> lines;
  bool old_no_newline = false;  // "\ No newline" seen after old side
  bool new_no_newline = false;  // "\ No newline" seen after new side
};

// One `--- / +++ ` header pair and its hunk group.
struct FilePatch {
  std::string old_name;
  std::string new_name;
  std::vector<Hunk> hunks;
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
  std::wstring wfile = utf8_to_wstring(filename);
  HANDLE hFile = CreateFileW(wfile.c_str(), GENERIC_READ,
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

bool file_exists(const std::string& filename) {
  std::wstring wfile = utf8_to_wstring(filename);
  DWORD attrs = GetFileAttributesW(wfile.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool backup_file(const std::string& filename, const std::string& suffix) {
  std::wstring wsrc = utf8_to_wstring(filename);
  std::wstring wdst = utf8_to_wstring(filename + suffix);
  return CopyFileW(wsrc.c_str(), wdst.c_str(), FALSE) != FALSE;
}

std::string strip_path(const std::string& path, int components) {
  if (components <= 0) return path;
  size_t pos = 0;
  int count = 0;
  for (size_t i = 0; i < path.length() && count < components; ++i) {
    if (path[i] == '/' || path[i] == '\\') {
      count++;
      pos = i + 1;
    }
  }
  return (pos < path.length()) ? path.substr(pos) : path;
}

Hunk reversed(const Hunk& h) {
  Hunk r;
  r.old_start = h.new_start;
  r.old_count = h.new_count;
  r.new_start = h.old_start;
  r.new_count = h.old_count;
  r.old_no_newline = h.new_no_newline;
  r.new_no_newline = h.old_no_newline;
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

// [GNU] patch_match (patch.c:1687): compare the pattern (minus prefix_fuzz
// leading and suffix_fuzz trailing lines) against the file at base+offset.
bool patch_match(const std::vector<TLine>& file,
                 const std::vector<std::string>& pat, long long base,
                 long long offset, long long prefix_fuzz,
                 long long suffix_fuzz) {
  long long n = static_cast<long long>(pat.size()) - prefix_fuzz - suffix_fuzz;
  if (n < 0) n = 0;
  long long start = base + offset + prefix_fuzz;
  if (start < 0 || start + n > static_cast<long long>(file.size()))
    return false;
  for (long long i = 0; i < n; ++i) {
    if (file[static_cast<size_t>(start + i)].text !=
        pat[static_cast<size_t>(prefix_fuzz + i)]) {
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
                      long long min_where /*0-based*/, long long& in_offset) {
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
        patch_match(file, pat, first_guess, offset, 0, suffix_fuzz)) {
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
        patch_match(file, pat, first_guess, -offset, prefix_fuzz, 0)) {
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
        patch_match(file, pat, first_guess, offset, prefix_fuzz, suffix_fuzz)) {
      in_offset += offset;
      return first_guess + offset;
    }
    if (offset <= max_neg_offset && patch_match(file, pat, first_guess, -offset,
                                                prefix_fuzz, suffix_fuzz)) {
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
                 long long hint, long long maxfuzz) {
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
                                  f, hint, st.copy_ptr, in_offset);
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
  int strip_components = 0;
  if (ctx.get<bool>("-p", false)) {
    try {
      strip_components = std::stoi(ctx.get<std::string>("-p", ""));
    } catch (...) {
      safeErrorPrintLn("patch: invalid strip count '-p'");
      return 2;
    }
  } else if (ctx.has("--strip")) {
    try {
      strip_components = std::stoi(ctx.get<std::string>("--strip", "0"));
    } catch (...) {
      safeErrorPrintLn("patch: invalid strip count '--strip'");
      return 2;
    }
  }

  long long maxfuzz = kDefaultFuzz;
  if (ctx.has("-F") || ctx.has("--fuzz")) {
    const int value =
        ctx.has("-F") ? ctx.get<int>("-F", 2) : ctx.get<int>("--fuzz", 2);
    if (value < 0) {
      safeErrorPrintLn("patch: fuzz factor must not be negative");
      return 2;
    }
    maxfuzz = value;
  }

  bool reverse =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--reverse", false);
  const bool force =
      ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false) ||
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

  std::string directory = ctx.get<std::string>("-d", "");
  if (directory.empty()) directory = ctx.get<std::string>("--directory", "");
  std::string patch_file = ctx.get<std::string>("-i", "");
  if (patch_file.empty()) patch_file = ctx.get<std::string>("--input", "");
  std::string orig_file_arg;
  if (!ctx.positionals.empty()) orig_file_arg = std::string(ctx.positionals[0]);
  if (patch_file.empty() && ctx.positionals.size() > 1) {
    patch_file = std::string(ctx.positionals[1]);
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
  if (ctx.has("-l") || ctx.has("--merge")) return unsupported("--merge/-l");
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
    patch_content = std::string(std::istreambuf_iterator<char>(std::cin),
                                std::istreambuf_iterator<char>());
  }

  // ----- parse into per-file hunk groups -----------------------------
  std::vector<FilePatch> groups;
  {
    std::vector<std::string> raw_lines;
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
    long long oc = 0;
    long long nc = 0;
    char last_tag = 0;

    auto close_hunk = [&]() {
      if (in_hunk) {
        cur_hunk.old_count = 0;
        cur_hunk.new_count = 0;
        for (const auto& l : cur_hunk.lines) {
          if (l.tag != '+') cur_hunk.old_count++;
          if (l.tag != '-') cur_hunk.new_count++;
        }
        cur.hunks.push_back(cur_hunk);
        cur_hunk = Hunk();
        in_hunk = false;
        oc = nc = 0;
        last_tag = 0;
      }
    };
    auto close_group = [&]() {
      close_hunk();
      if (group_open && (!cur.hunks.empty() || !cur.old_name.empty() ||
                         !cur.new_name.empty())) {
        groups.push_back(cur);
      }
      cur = FilePatch();
      group_open = false;
    };

    for (const auto& line : raw_lines) {
      // Hunk body lines take priority over header detection: a removed
      // line whose text is "--- ..." must still count toward the hunk.
      // But a non-body line where body lines are still expected ends the
      // (mangled) hunk without consuming the line.
      if (in_hunk && (oc < cur_hunk.old_count || nc < cur_hunk.new_count) &&
          !line.empty() && line[0] != '\\' && line[0] != ' ' &&
          line[0] != '-' && line[0] != '+') {
        close_hunk();
      }
      if (line.length() >= 4 && line.compare(0, 4, "--- ") == 0) {
        close_group();
        cur.old_name = header_name(line.substr(4));
        group_open = true;
        continue;
      }
      if (line.length() >= 4 && line.compare(0, 4, "+++ ") == 0) {
        if (!group_open) {
          cur.new_name = header_name(line.substr(4));
          group_open = true;
        } else {
          cur.new_name = header_name(line.substr(4));
        }
        continue;
      }
      Hunk parsed;
      if (line.length() >= 6 && line.compare(0, 4, "@@ -") == 0 &&
          parse_hunk_header(line, parsed)) {
        close_hunk();
        cur_hunk = parsed;
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
    safeErrorPrintLn("patch: Only garbage was found in the patch input.");
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

  for (auto& group : groups) {
    if (reverse) {
      std::swap(group.old_name, group.new_name);
      for (auto& h : group.hunks) h = reversed(h);
    }

    const bool creation = is_dev_null(group.old_name);
    const bool deletion = is_dev_null(group.new_name);

    std::string target;
    if (creation) {
      target = group.new_name;
    } else if (deletion) {
      target = group.old_name;
    } else {
      target = group.new_name.empty() ? group.old_name : group.new_name;
    }
    if (!orig_file_arg.empty()) target = orig_file_arg;

    if (target.empty() || is_dev_null(target)) {
      safeErrorPrintLn("patch: cannot find file to patch");
      return 2;
    }
    if (!is_dev_null(target)) {
      target = strip_path(target, strip_components);
    }
    if (!directory.empty()) {
      std::string sep = (!directory.empty() &&
                         (directory.back() == '/' || directory.back() == '\\'))
                            ? ""
                            : "/";
      target = directory + sep + target;
      for (char& c : target) {
        if (c == '\\') c = '/';
      }
    }

    say("patching file " + target);

    FileState st;
    st.exists = file_exists(target);
    std::string raw;
    if (st.exists) {
      if (!read_file_raw(target, raw)) {
        safeErrorPrintLn("patch: cannot open file '" + target + "'");
        return 2;
      }
      st.input = split_lines(raw, st.crlf);
    } else if (!creation) {
      // A nonexistent file is only valid when the patch creates it (old
      // side /dev/null).  An existing empty file is a valid target.
      safeErrorPrintLn("patch: cannot open file '" + target +
                       "': No such file or directory");
      return 2;
    }

    // ----- apply hunks (locate_hunk semantics) ------------------------
    std::vector<Hunk> rejects;
    long long failed = 0;
    long long hunk_no = 0;
    bool mismatch = false;  // fuzz > 0 or offset != 0 for this file

    for (const auto& hunk : group.hunks) {
      ++hunk_no;
      std::vector<std::string> pat = pattern_of(hunk);
      long long prefix_context = 0;
      long long suffix_context = 0;
      {
        // Count leading and trailing context (' ') lines of the hunk.
        long long idx = 0;
        while (idx < static_cast<long long>(hunk.lines.size()) &&
               hunk.lines[static_cast<size_t>(idx)].tag == ' ') {
          ++prefix_context;
          ++idx;
        }
        idx = static_cast<long long>(hunk.lines.size()) - 1;
        while (idx >= 0 && hunk.lines[static_cast<size_t>(idx)].tag == ' ') {
          ++suffix_context;
          --idx;
        }
      }

      Attempt att = try_hunk(st, pat, prefix_context, suffix_context,
                             hunk.old_start, maxfuzz);

      if (!att.ok && forward) {
        // [GNU] -N/--forward: diagnose reversed or already-applied hunks
        // and skip them without applying.
        Attempt rev_att;
        {
          Hunk rev = reversed(hunk);
          std::vector<std::string> rpat = pattern_of(rev);
          rev_att = try_hunk(st, rpat, prefix_context, suffix_context,
                             rev.old_start, maxfuzz);
        }
        if (rev_att.ok) {
          say("Hunk #" + std::to_string(hunk_no) + " ignored at " +
              std::to_string(hunk.old_start + st.in_offset) + ".");
          continue;
        }
      }

      if (!att.ok) {
        ++failed;
        ++failed_total;
        say("Hunk #" + std::to_string(hunk_no) + " FAILED at " +
            std::to_string(hunk.old_start + st.in_offset) + ".");
        rejects.push_back(hunk);
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
      for (const auto& pl : hunk.lines) {
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
      if (last_out >= 0 && hunk.new_no_newline) {
        st.out[static_cast<size_t>(last_out)].nl = false;
      }
      st.copy_ptr = window_start + window_len;

      if (att.fuzz > 0 || att.offset != 0) {
        mismatch = true;
        std::string msg = "Hunk #" + std::to_string(hunk_no) +
                          " succeeded at " + std::to_string(where0 + 1);
        if (att.fuzz > 0) {
          msg += " with fuzz " + std::to_string(att.fuzz);
        }
        if (att.offset != 0) {
          msg += " (offset " + std::to_string(att.offset) + " " +
                 plural_lines(att.offset) + ")";
        }
        say(msg + ".");
      }
    }

    // Copy the remainder of the input.
    for (long long i = st.copy_ptr; i < static_cast<long long>(st.input.size());
         ++i) {
      st.out.push_back(st.input[static_cast<size_t>(i)]);
    }

    // ----- rejects -----------------------------------------------------
    std::string rej_path =
        reject_file_opt.empty() ? target + ".rej" : reject_file_opt;
    if (!rejects.empty() && !dry_run) {
      std::string content;
      for (const auto& hunk : rejects) {
        content +=
            "@@ -" + range_str(hunk.old_start + st.in_offset, hunk.old_count) +
            " +" + range_str(hunk.new_start + st.in_offset, hunk.new_count) +
            " @@\n";
        for (const auto& l : hunk.lines) {
          content += std::string(1, l.tag) + l.text + "\n";
        }
        if (hunk.old_no_newline || hunk.new_no_newline) {
          content += "\\ No newline at end of file\n";
        }
      }
      if (!reject_file_opt.empty()) {
        reject_out += content;
      } else if (!write_file_raw(rej_path, content)) {
        safeErrorPrintLn("patch: cannot create reject file '" + rej_path + "'");
        return 2;
      }
      const std::string summary =
          std::to_string(failed) + " out of " +
          std::to_string(static_cast<long long>(group.hunks.size())) +
          (group.hunks.size() == 1 ? " hunk" : " hunks") +
          " FAILED -- saving rejects to file " + rej_path;
      say(summary);
    }

    // ----- backup ------------------------------------------------------
    const bool backup =
        make_backups || (backup_if_mismatch && (mismatch || failed > 0));
    const bool wrote_output_now = output_file.empty();
    if (backup && !dry_run && st.exists && !creation && wrote_output_now) {
      if (!backup_file(target, backup_suffix)) {
        safeErrorPrintLn("patch: cannot create backup file '" + target +
                         backup_suffix + "'");
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
  if (!reject_out.empty() && !reject_file_opt.empty() && !dry_run) {
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
