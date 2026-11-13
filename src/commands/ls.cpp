// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for ls.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief LS command options definition
 *
 * This array defines all the options supported by the ls command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --all: Do not ignore entries starting with . [IMPLEMENTED]
 * - @a -A, @a --almost-all: Do not list implied . and .. [IMPLEMENTED]
 * - @a -b, @a --escape: Print C-style escapes for nongraphic characters
 * [IMPLEMENTED]
 * - @a -B, @a --ignore-backups: Do not list implied entries ending with ~
 * [IMPLEMENTED]
 * - @a -c: With -lt: sort by, and show, ctime; with -l: show ctime and sort by
 * name; otherwise: sort by ctime, newest first [IMPLEMENTED]
 * - @a -C: List entries by columns [IMPLEMENTED]
 * - @a -d, @a --directory: List directories themselves, not their contents
 * [IMPLEMENTED]
 * - @a -f: List all entries in directory order [IMPLEMENTED]
 * - @a -F, @a --classify: Append indicator (one of *=>@|) to entries
 * [IMPLEMENTED]
 * - @a -g: Like -l, but do not list owner [IMPLEMENTED]
 * - @a -h, @a --human-readable: With -l and -s, print sizes like 1K 234M 2G
 * etc. [IMPLEMENTED]
 * - @a -i, @a --inode: Print the index number of each file [IMPLEMENTED]
 * -
 * @a -k, @a --kibibytes: Default to 1024-byte blocks for file system usage
 * [IMPLEMENTED]
 * - @a -L, @a --dereference: When showing file information for a symbolic link,
 * show information for the file the link references [IMPLEMENTED]
 * - @a -l, @a --long, @a --long-list: Use a long listing format [IMPLEMENTED]
 * - @a -m: Fill width with a comma separated list of entries [IMPLEMENTED]
 * - @a -n, @a --numeric-uid-gid: Like -l, but list numeric user and group IDs
 * [IMPLEMENTED]
 * - @a -N, @a --literal: Print entry names without quoting [IMPLEMENTED]
 * - @a -o: Like -l, but do not list group information [IMPLEMENTED]
 * - @a -p, @a --indicator-style=WORD: Append /, file-type, or classify
 *
 * suffixes to entries
 * [IMPLEMENTED]
 * - @a -q, @a --hide-control-chars:
 * Print ? instead of nongraphic characters [IMPLEMENTED]
 * - @a -Q, @a --quote-name: Enclose entry names in double quotes
 * [IMPLEMENTED]
 * - @a -r, @a --reverse: Reverse order while sorting [IMPLEMENTED]
 * - @a -R, @a --recursive: List subdirectories recursively [IMPLEMENTED]
 * - @a -s, @a --size: Print the allocated size of each file, in blocks
 *
 * [IMPLEMENTED]
 * - @a -S: Sort by file size, largest first [IMPLEMENTED]
 * - @a -t: Sort by time, newest first [IMPLEMENTED]
 * - @a -T, @a --tabsize: Assume tab stops at each COLS instead of 8
 * [IMPLEMENTED]
 * - @a -u: With -lt: sort by, and show, access time; with -l: show access time
 * and sort by name; otherwise: sort by access time, newest first [IMPLEMENTED]
 * - @a -U: Do not sort; list entries in directory order [IMPLEMENTED]
 * - @a -v: Natural sort of (version) numbers within text [IMPLEMENTED]
 * - @a -w, @a --width: Set output width to COLS. 0 means no limit [IMPLEMENTED]
 * - @a -x: List entries by lines instead of by columns [IMPLEMENTED]
 * - @a -X: Sort alphabetically by entry extension [IMPLEMENTED]
 * - @a -Z, @a --context: Print any security context of each file [DIFFERS]
 * - @a -1: List one file per line [IMPLEMENTED]
 */
auto constexpr LS_OPTIONS = std::array{
    OPTION("-a", "--all", "do not ignore entries starting with ."),
    OPTION("-A", "--almost-all", "do not list implied . and .."),
    OPTION("-b", "--escape", "print C-style escapes for nongraphic characters"),
    OPTION("-B", "--ignore-backups",
           "do not list implied entries ending with ~"),
    OPTION("-c", "",
           "with -lt: sort by, and show, ctime; with -l: show ctime and sort "
           "by name; otherwise: sort by ctime, newest first"),
    OPTION("-C", "", "list entries by columns"),
    OPTION("-d", "--directory",
           "list directories themselves, not their contents"),
    OPTION("-f", "", "list all entries in directory order"),
    OPTION("-F", "--classify", "append indicator (one of */=>@|) to entries"),
    OPTION("-g", "", "like -l, but do not list owner"),
    OPTION("-h", "--human-readable",
           "with -l and -s, print sizes like 1K 234M 2G etc."),
    OPTION("-i", "--inode", "print the index number of each file"),
    OPTION("-k", "--kibibytes",
           "default to 1024-byte blocks for file system usage"),
    OPTION("-I", "--ignore", "ignore entries matching PATTERN", STRING_TYPE),
    OPTION("-L", "--dereference",
           "when showing file information for a symbolic link, show "
           "information for the file the link references"),
    // [GNU] -l has no long form in GNU ls; "--long"/"--long-list" are
    // invented (#1411).
    OPTION("-l", "", "use a long listing format"),
    OPTION("-m", "", "fill width with a comma separated list of entries"),
    OPTION("-n", "--numeric-uid-gid",
           "like -l, but list numeric user and group IDs"),
    OPTION("-N", "--literal", "print entry names without quoting"),
    OPTION("-o", "", "like -l, but do not list group information"),
    OPTION("-p", "", "append / indicator to directories"),
    OPTION("-q", "--hide-control-chars",
           "print ? instead of nongraphic characters"),
    OPTION("-Q", "--quote-name", "enclose entry names in double quotes"),
    OPTION("-r", "--reverse", "reverse order while sorting"),
    OPTION("-R", "--recursive", "list subdirectories recursively"),
    OPTION("-s", "--size", "print the allocated size of each file, in blocks"),
    OPTION("-S", "", "sort by file size, largest first"),
    OPTION("-t", "", "sort by time, newest first"),
    // [GNU] -T takes a number in strtoumax base-0 syntax, so "0xff" is 255
    // while "-T=0xff" hands us the literal "=0xff" and is invalid.
    OPTION("-T", "--tabsize", "assume tab stops at each COLS instead of 8",
           STRING_TYPE),
    OPTION(
        "-u", "",
        "with -lt: sort by, and show, access time; with -l: show access time "
        "and sort by name; otherwise: sort by access time, newest first"),
    OPTION("-U", "", "do not sort; list entries in directory order"),
    OPTION("-v", "", "natural sort of (version) numbers within text"),
    // [GNU] -w takes a number in xstrtoumax base-0 syntax via
    // decode_line_length (ls.c:1867): "0x20" is 32, "010" is 8, and 0
    // or an overflowing value means "no limit" — none of which the
    // framework's base-10 int parse can express, so (like -T above) the
    // raw string is kept and decoded by ls itself (#1536).
    OPTION("-w", "--width", "set output width to COLS. 0 means no limit",
           STRING_TYPE),
    OPTION("-x", "", "list entries by lines instead of by columns"),
    OPTION("-X", "", "sort alphabetically by entry extension"),
    OPTION("-Z", "--context", "print any security context of each file"),
    OPTION("", "--sort", "sort entries by WORD", STRING_TYPE),
    OPTION("", "--format", "set output format", STRING_TYPE),
    OPTION("", "--time", "change time style", STRING_TYPE),
    OPTION("", "--block-size", "scale sizes by SIZE", STRING_TYPE),
    OPTION("", "--quoting-style", "use quoting style WORD", STRING_TYPE),
    OPTION("", "--show-control-chars",
           "show nongraphic characters as-is in file names"),
    OPTION("", "--indicator-style", "append indicator using WORD", STRING_TYPE),
    OPTION("", "--file-type", "append file type indicators, without *"),
    OPTION("", "--color",
           "colorize the output; WHEN can be 'always', 'auto', or 'never'",
           OPTIONAL_STRING_TYPE),
    OPTION("-1", "", "list one file per line"),
    OPTION("-D", "--dired", "generate output designed for Emacs dired mode"),
    OPTION("-G", "--no-group", "in a long listing, don't print group names"),
    OPTION("", "--group-directories-first", "group directories before files"),
    OPTION("", "--author", "show author in long format"),
    OPTION("-H", "--dereference-command-line",
           "follow symlinks listed on the command line"),
    OPTION("", "--dereference-command-line-symlink-to-dir",
           "follow each command-line symlink to a directory"),
    OPTION("", "--dereference-command-line-symlinks-to-dir",
           "follow each command-line symlink to a directory"),
    OPTION("", "--hide", "do not list implied entries matching PATTERN",
           STRING_TYPE),
    OPTION("", "--hyperlink",
           "hyperlink file names; WHEN can be 'always', 'auto', or 'never'",
           OPTIONAL_STRING_TYPE),
    OPTION("", "--si", "like -h, but use powers of 1000 not 1024"),
    OPTION("", "--full-time", "like -l --time-style=full-iso"),
    OPTION("", "--time-style",
           "time/date format with -l (e.g. full-iso, long-iso, iso, locale, "
           "+FORMAT)",
           STRING_TYPE),
    OPTION("", "--zero", "end each output line with NUL instead of newline")};

// ======================================================
// Constants
// ======================================================
namespace ls_constants {
constexpr int DEFAULT_TAB_SIZE = 8;
constexpr int DEFAULT_WIDTH = 0;

// File extensions for different types
const std::array<const wchar_t *, 10> COMPRESSED_EXTS = {
    L"zip", L"rar", L"7z",  L"tar", L"gz",
    L"bz2", L"xz",  L"iso", L"cab", L"arc"};

const std::array<const wchar_t *, 10> SCRIPT_EXTS = {
    L"sh", L"bat", L"cmd", L"py", L"pl", L"lua", L"js", L"php", L"rb", L"ps1"};
}  // namespace ls_constants

/**
 * @brief Check if a file handle is a terminal
 * @param stream Stream to check
 * @return True if stream is a terminal
 */
bool ls_is_terminal(FILE *stream) {
  DWORD mode;
  return GetConsoleMode((HANDLE)_get_osfhandle(_fileno(stream)), &mode) != 0;
}

// ======================================================
// Pipeline components
// ======================================================
namespace ls_pipeline {
namespace cp = core::pipeline;

enum class SortMode {
  Name,
  Size,
  Time,
  Version,
  Extension,
  Type,
  Width,
  None
};
enum class IndicatorStyle { None, Slash, FileType, Classify };
enum class TimeMode { Modification, Access, Status, Birth };
enum class TimeStyle { Default, Locale, FullIso, LongIso, Iso, CustomFormat };
enum class FormatMode { Columns, Across, Commas, OnePerLine, Long };
enum class SizeMode { Bytes, Blocks, Human, SI };
enum class QuotingMode {
  Literal,
  Escape,
  HideControl,
  C,
  CMaybe,
  Shell,
  ShellAlways,
  ShellEscape,
  ShellEscapeAlways,
  Locale
};

enum class DereferenceMode { None, CommandLine, CommandLineDirectories, All };

auto resolve_dereference_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> DereferenceMode;
auto should_dereference_entry_metadata(DereferenceMode mode,
                                       const WIN32_FIND_DATAW &find_data,
                                       bool command_line_operand) -> bool;

struct SizeConfig {
  SizeMode file_mode = SizeMode::Bytes;
  SizeMode block_mode = SizeMode::Blocks;
  uint64_t file_block_size = 1;
  uint64_t block_size = 1024;
};

auto get_terminal_width() -> int;

// [GNU] ls.c:2307 with ls.c:1871: a decoded line length of 0 means "no
// limit" (one unwrapped line). The width chain carries that state as
// this sentinel instead of 0, so a decided "-w 0" / COLUMNS=0 can never
// be mistaken for "unset" by a layout consumer (#1536).
constexpr int kLsNoLineLimit = INT_MAX;
auto decode_line_length(std::string_view spec) -> int;
auto resolve_output_width(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> int;
auto parse_indicator_style(std::string_view value)
    -> std::optional<IndicatorStyle>;
auto long_format_requested(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool;
auto build_listing_prefix(const std::wstring &path,
                          const WIN32_FIND_DATAW &find_data,
                          const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::string;
auto try_get_dereferenced_find_data(
    const std::wstring &path, const WIN32_FIND_DATAW &original_find_data,
    const CommandContext<LS_OPTIONS.size()> &ctx,
    bool command_line_operand = false)
    -> std::optional<std::pair<WIN32_FIND_DATAW, std::wstring>>;

struct EntryInfo {
  std::wstring name;
  std::wstring full_path;
  WIN32_FIND_DATAW find_data;
  bool command_line_operand = false;
};

struct PathProbe {
  WIN32_FIND_DATAW find_data{};
  bool found = false;
  bool attributes_valid = false;
  DWORD attributes = INVALID_FILE_ATTRIBUTES;
};

struct SymlinkDisplayTarget {
  std::wstring display;
  std::filesystem::path resolved_path;
};

struct DisplayNameParts {
  std::wstring rendered_name;
  std::optional<SymlinkDisplayTarget> target;
};

auto get_entry_extension(const std::wstring &name) -> std::wstring {
  size_t dot_pos = name.find_last_of(L".");
  if (dot_pos == std::wstring::npos || dot_pos + 1 >= name.size()) {
    return {};
  }

  std::wstring ext = name.substr(dot_pos + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
  return ext;
}

auto get_entry_extension_raw(const std::wstring &name) -> std::wstring {
  size_t dot_pos = name.find_last_of(L".");
  if (dot_pos == std::wstring::npos || dot_pos + 1 >= name.size()) {
    return {};
  }
  return name.substr(dot_pos + 1);
}

auto is_archive_extension(std::wstring_view ext) -> bool {
  for (const auto *candidate : ls_constants::COMPRESSED_EXTS) {
    if (ext == candidate) {
      return true;
    }
  }
  return false;
}

auto is_script_extension(std::wstring_view ext) -> bool {
  for (const auto *candidate : ls_constants::SCRIPT_EXTS) {
    if (ext == candidate) {
      return true;
    }
  }
  return false;
}

auto is_executable_name(const std::wstring &name) -> bool {
  const std::wstring ext = get_entry_extension(name);
  return ext == L"exe" || ext == L"com" || ext == L"bat" || ext == L"cmd" ||
         ext == L"ps1";
}

auto should_ignore_backup(const std::wstring &name,
                          const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  const bool ignore_backups =
      ctx.get<bool>("-B", false) || ctx.get<bool>("--ignore-backups", false);
  return ignore_backups && !name.empty() && name.back() == L'~';
}

auto get_ignore_pattern(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::wstring {
  std::wstring pattern = utf8_to_wstring(ctx.get<std::string>("--ignore", ""));
  if (pattern.empty()) {
    pattern = utf8_to_wstring(ctx.get<std::string>("-I", ""));
  }
  return pattern;
}

auto get_hide_pattern(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::wstring {
  return utf8_to_wstring(ctx.get<std::string>("--hide", ""));
}

auto should_ignore_pattern(const std::wstring &name,
                           const std::wstring &pattern) -> bool {
  if (pattern.empty()) {
    return false;
  }
  return wildcard_match(pattern, name, true);
}

auto should_show_entry(const std::wstring &name,
                       const WIN32_FIND_DATAW &find_data,
                       const CommandContext<LS_OPTIONS.size()> &ctx,
                       const std::wstring &ignore_pattern,
                       const std::wstring &hide_pattern) -> bool {
  const bool show_all = ctx.get<bool>("-a", false) ||
                        ctx.get<bool>("--all", false) ||
                        ctx.get<bool>("-f", false);
  const bool almost_all =
      ctx.get<bool>("-A", false) || ctx.get<bool>("--almost-all", false);

  if (name == L"." || name == L"..") {
    return show_all;
  }

  if (!name.empty() && name.front() == L'.' && !show_all && !almost_all) {
    return false;
  }

  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !show_all &&
      !almost_all) {
    return false;
  }

  if (should_ignore_backup(name, ctx)) {
    return false;
  }

  if (!show_all && !almost_all && should_ignore_pattern(name, hide_pattern)) {
    return false;
  }

  return !should_ignore_pattern(name, ignore_pattern);
}

auto is_reparse_link(const WIN32_FIND_DATAW &find_data) -> bool {
  return (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

auto normalize_lookup_path(const std::wstring &path) -> std::wstring {
  if (path.empty()) {
    return path;
  }

  // [GNU/POSIX] An operand made only of separators ("/", "//", ...) names
  // the root directory (POSIX allows exactly two leading slashes to be
  // special; coreutils never treats them specially).  Collapse to a single
  // root so the pattern join and the dialect boundary both see "/" (#1145).
  if (path.find_first_not_of(L"\\/") == std::wstring::npos) {
    return L"/";
  }

  std::filesystem::path fs_path(path);
  size_t root_length = 0;
  if (fs_path.has_root_name()) {
    root_length += fs_path.root_name().native().size();
  }
  if (fs_path.has_root_directory()) {
    root_length += 1;
  }

  std::wstring normalized = path;
  while (normalized.size() > root_length &&
         (normalized.back() == L'\\' || normalized.back() == L'/')) {
    normalized.pop_back();
  }

  return normalized;
}

// [exe-transparency] (unixwin/WinuxCmd#1572) Resolve a bare-name operand to
// its `.exe` sibling: the engine's `test -e/-x` and the child-process PATHEXT
// completion already answer `bin/java` when only `bin/java.exe` is on disk,
// and the shell-tree funnel (`shell_tree_to_native_w` in native_path.cppm)
// does the same for `/usr/bin/X` spellings — but native-path operands never
// got the retry, so `ls`/`stat` reported ENOENT for the same file. Only
// `.exe` is tried, only when the final component names no extension (the
// childproc PATHEXT rule: an explicit extension is never completed), and
// only when the operand itself does not exist; the typed spelling is
// returned unchanged otherwise, so display keeps the operand as typed and
// directory operands, trailing-separator probes and pseudo-devices are
// untouched. This is intentionally ls-local, not a change to the shared
// operand boundary: completing names there would also rewrite create/open
// callers (`touch`, `cp`, ...) that must create exactly the name given.
auto resolve_bare_exe_operand_w(const std::wstring &path) -> std::wstring {
  if (path.empty()) return path;
  if (path.back() == L'/' || path.back() == L'\\') return path;
  if (native_path::resolve_pseudo_device_w(path)) return path;
  const size_t slash = path.find_last_of(L"/\\");
  const size_t dot = path.find_last_of(L'.');
  if (dot != std::wstring::npos &&
      (slash == std::wstring::npos || dot > slash)) {
    return path;  // explicit extension: never completed
  }
  // The dual probe mirrors probe_path below: attributes alone can miss a
  // path FindFirstFileW still answers for.
  auto exists_at_boundary = [](const std::wstring &candidate) {
    const auto operand = native_path::make_api_path_operand_w(candidate);
    if (native_path::attributes_w(operand.extended) !=
        INVALID_FILE_ATTRIBUTES) {
      return true;
    }
    WIN32_FIND_DATAW data{};
    HANDLE hfind = FindFirstFileW(operand.extended.c_str(), &data);
    if (hfind != INVALID_HANDLE_VALUE) {
      FindClose(hfind);
      return true;
    }
    return false;
  };
  if (exists_at_boundary(path)) return path;
  const std::wstring candidate = path + L".exe";
  if (exists_at_boundary(candidate)) return candidate;
  return path;
}

auto probe_path(const std::wstring &path) -> PathProbe {
  PathProbe probe{};
  // [exe-transparency] (#1572) A bare-name operand resolves to its `.exe`
  // sibling for the probe, so `ls bin/java` answers the same file the
  // engine's `test -e/-x` and the child-process PATHEXT completion already
  // answer. The typed spelling is preserved by the callers (display keeps
  // the operand as typed); only the existence/metadata probe sees the
  // resolved name.
  const std::wstring lookup_path =
      resolve_bare_exe_operand_w(normalize_lookup_path(path));
  const auto operand = native_path::make_api_path_operand_w(lookup_path);

  probe.attributes = native_path::attributes_w(operand.extended);
  probe.attributes_valid = probe.attributes != INVALID_FILE_ATTRIBUTES;

  HANDLE hfind = FindFirstFileW(operand.extended.c_str(), &probe.find_data);
  if (hfind != INVALID_HANDLE_VALUE) {
    probe.found = true;
    FindClose(hfind);
  }

  // [GNU] POSIX pseudo-devices (/dev/null, /dev/tty, /dev/stdin, ...) and
  // their DOS device spellings (NUL, CONIN$, ...) are real, openable objects
  // but have no directory entry, so both probes above fail.  Report them as
  // existing plain files so "ls /dev/null" lists instead of failing with
  // ENOENT (uutils #6540).
  if (!probe.attributes_valid && !probe.found &&
      native_path::resolve_pseudo_device_w(lookup_path)) {
    // FILE_ATTRIBUTE_DEVICE marks the entry as a character device so the
    // long-format listing renders a 'c' type like GNU ("crw-rw-rw-").
    probe.attributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DEVICE;
    probe.attributes_valid = true;
    probe.found = true;
    probe.find_data = WIN32_FIND_DATAW{};
    probe.find_data.dwFileAttributes =
        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DEVICE;
  }

  return probe;
}

auto normalize_metadata_probe_path(const std::wstring &path) -> std::wstring {
  // Resolve the MSYS drive dialect (/d/..., /cygdrive/d/...) BEFORE folding
  // the path against the current directory: std::filesystem::absolute treats
  // "/d/x" as a current-drive root-relative path and produces
  // "<curdrive>:\d\x", silently renaming the operand before the API-path
  // boundary can convert it — `ls /d/file` and `ls -d /d/dir` failed exactly
  // this way (#1145). The GNU runtime layer performs this resolution before
  // ls ever stats the operand (coreutils ls.c gobble_file receives a native
  // pathname), so the boundary conversion belongs first here too.
  const std::wstring resolved = native_path::normalize_api_operand_w(path);
  try {
    return std::filesystem::absolute(std::filesystem::path(resolved))
        .lexically_normal()
        .native();
  } catch (...) {
    return resolved;
  }
}

auto is_symbolic_reparse_link(const WIN32_FIND_DATAW &find_data) -> bool {
  return is_reparse_link(find_data) &&
         find_data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
}

auto is_plain_directory(const WIN32_FIND_DATAW &find_data) -> bool {
  return (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         !is_reparse_link(find_data);
}

// [GNU] The LS_COLORS environment variable is the whole color database for
// ls --color: "di=01;34:ln=01;36:*.zip=01;31:..."  Each value is an SGR
// parameter list that GNU wraps as "\033[<params>m".  When the variable is
// set it replaces the built-in table entirely (uutils: LS_COLORS support).
struct LsColorsTable {
  bool active = false;
  std::unordered_map<std::wstring, std::wstring> keys;
  std::vector<std::pair<std::wstring, std::wstring>> ext_patterns;
};

auto ls_colors_table() -> const LsColorsTable & {
  static const LsColorsTable table = [] {
    LsColorsTable parsed;
    wchar_t buffer[32767];
    const DWORD length = GetEnvironmentVariableW(
        L"LS_COLORS", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
      return parsed;
    }
    parsed.active = true;
    const std::wstring env(buffer, length);
    size_t pos = 0;
    while (pos <= env.size()) {
      const size_t colon = env.find(L':', pos);
      const std::wstring_view item = std::wstring_view(env).substr(
          pos,
          colon == std::wstring::npos ? std::wstring_view::npos : colon - pos);
      const size_t eq = item.find(L'=');
      if (eq != std::wstring_view::npos && eq > 0) {
        const std::wstring key(item.substr(0, eq));
        std::wstring sequence =
            L"\033[" + std::wstring(item.substr(eq + 1)) + L"m";
        if (!key.empty() && key.front() == L'*') {
          parsed.ext_patterns.emplace_back(key, std::move(sequence));
        } else {
          parsed.keys[key] = std::move(sequence);
        }
      }
      if (colon == std::wstring::npos) break;
      pos = colon + 1;
    }
    return parsed;
  }();
  return table;
}

auto ls_colors_lookup(const LsColorsTable &colors, const std::wstring &name,
                      const WIN32_FIND_DATAW &find_data) -> std::wstring_view {
  auto keyed = [&](std::wstring_view key) -> std::wstring_view {
    auto it = colors.keys.find(std::wstring(key));
    return it == colors.keys.end() ? std::wstring_view{}
                                   : std::wstring_view(it->second);
  };

  if (is_reparse_link(find_data)) {
    return keyed(L"ln");
  }
  if (is_plain_directory(find_data)) {
    return keyed(L"di");
  }
  // [GNU] "*.<ext>" entries match the file name case-sensitively.
  for (const auto &[pattern, sequence] : colors.ext_patterns) {
    if (wildcard_match(pattern, name, true)) {
      return sequence;
    }
  }
  if (is_executable_name(name)) {
    if (auto seq = keyed(L"ex"); !seq.empty()) {
      return seq;
    }
  }
  return keyed(L"fi");
}

auto get_color_for_entry(const std::wstring &name,
                         const WIN32_FIND_DATAW &find_data)
    -> std::wstring_view {
  const LsColorsTable &colors = ls_colors_table();
  if (colors.active) {
    return ls_colors_lookup(colors, name, find_data);
  }
  if (is_reparse_link(find_data)) {
    return COLOR_LINK;
  }
  if (is_plain_directory(find_data)) {
    return COLOR_DIR;
  }

  const std::wstring ext = get_entry_extension(name);
  if (is_archive_extension(ext)) {
    return COLOR_ARCHIVE;
  }
  if (is_script_extension(ext)) {
    return COLOR_SCRIPT;
  }
  if (is_executable_name(name)) {
    return COLOR_EXEC;
  }
  // [GNU] Plain files carry no color indicator at all: ls emits no escape
  // sequences for them (Savannah #15043).
  return {};
}

// [GNU] ls prints one reset indicator before the first colored entry of a
// run (the transition from default text), and no leading reset afterwards.
auto color_prefix_sequence() -> std::wstring_view {
  static bool used_color = false;
  if (used_color) {
    return {};
  }
  used_color = true;
  return COLOR_RESET;
}

auto get_indicator_suffix(const std::wstring &name,
                          const WIN32_FIND_DATAW &find_data,
                          const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::wstring {
  IndicatorStyle style = IndicatorStyle::None;
  const std::string indicator_option =
      ctx.get<std::string>("--indicator-style", "");
  if (!indicator_option.empty()) {
    if (auto parsed = parse_indicator_style(indicator_option)) {
      style = *parsed;
    }
  } else if (ctx.get<bool>("-F", false) || ctx.get<bool>("--classify", false)) {
    style = IndicatorStyle::Classify;
  } else if (ctx.get<bool>("--file-type", false)) {
    style = IndicatorStyle::FileType;
  } else if (ctx.get<bool>("-p", false)) {
    style = IndicatorStyle::Slash;
  }

  auto file_type_suffix = [&]() -> std::wstring {
    if (is_reparse_link(find_data)) return L"@";
    if (is_plain_directory(find_data)) return L"/";
    return {};
  };

  switch (style) {
    case IndicatorStyle::None:
      return {};
    case IndicatorStyle::Slash:
      return is_plain_directory(find_data) ? L"/" : L"";
    case IndicatorStyle::FileType:
      return file_type_suffix();
    case IndicatorStyle::Classify: {
      std::wstring suffix = file_type_suffix();
      if (!suffix.empty()) return suffix;
      if (is_executable_name(name)) return L"*";
      return {};
    }
  }

  return {};
}

auto escape_display_name(const std::wstring &name) -> std::wstring {
  std::wstring rendered;
  rendered.reserve(name.size() * 2);

  auto append_hex = [&](unsigned int value, int width, wchar_t prefix) {
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    rendered.push_back(L'\\');
    rendered.push_back(prefix);
    for (int shift = (width - 1) * 4; shift >= 0; shift -= 4) {
      rendered.push_back(digits[(value >> shift) & 0xF]);
    }
  };

  for (wchar_t ch : name) {
    switch (ch) {
      case L'\a':
        rendered += L"\\a";
        break;
      case L'\b':
        rendered += L"\\b";
        break;
      case L'\t':
        rendered += L"\\t";
        break;
      case L'\n':
        rendered += L"\\n";
        break;
      case L'\v':
        rendered += L"\\v";
        break;
      case L'\f':
        rendered += L"\\f";
        break;
      case L'\r':
        rendered += L"\\r";
        break;
      case L'\\':
        rendered += L"\\\\";
        break;
      case L'"':
        rendered += L"\\\"";
        break;
      default:
        // [GNU] quotearg's escape styles pass PRINTABLE characters
        // through raw, whatever their range; the old hard-coded
        // ASCII window mangled names like "€.txt" into € form.
        if (std::iswprint(static_cast<wint_t>(ch))) {
          rendered.push_back(ch);
        } else if (ch <= 0xFF) {
          append_hex(static_cast<unsigned int>(ch), 2, L'x');
        } else if (ch <= 0xFFFF) {
          append_hex(static_cast<unsigned int>(ch), 4, L'u');
        } else {
          append_hex(static_cast<unsigned int>(ch), 8, L'U');
        }
        break;
    }
  }

  return rendered;
}

// [GNU] --quoting-style=locale in the C locale: always single-quoted,
// with the C-style escapes except that ' itself is escaped and " stays
// literal. (The UTF-8-locale Unicode-quote form is not reproduced: this
// command has no locale machinery to select it.)
auto locale_quote_display_name(const std::wstring &name) -> std::wstring {
  std::wstring rendered;
  rendered.reserve(name.size() * 2);

  auto append_hex = [&](unsigned int value, int width, wchar_t prefix) {
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    rendered.push_back(L'\\');
    rendered.push_back(prefix);
    for (int shift = (width - 1) * 4; shift >= 0; shift -= 4) {
      rendered.push_back(digits[(value >> shift) & 0xF]);
    }
  };

  for (wchar_t ch : name) {
    switch (ch) {
      case L'\a':
        rendered += L"\\a";
        break;
      case L'\b':
        rendered += L"\\b";
        break;
      case L'\t':
        rendered += L"\\t";
        break;
      case L'\n':
        rendered += L"\\n";
        break;
      case L'\v':
        rendered += L"\\v";
        break;
      case L'\f':
        rendered += L"\\f";
        break;
      case L'\r':
        rendered += L"\\r";
        break;
      case L'\\':
        rendered += L"\\\\";
        break;
      case L'\'':
        rendered += L"\\'";
        break;
      default:
        if (ch >= 0x20 && ch < 0x7f && std::iswprint(static_cast<wint_t>(ch))) {
          rendered.push_back(ch);
        } else if (ch <= 0xFF) {
          append_hex(static_cast<unsigned int>(ch), 2, L'x');
        } else if (ch <= 0xFFFF) {
          append_hex(static_cast<unsigned int>(ch), 4, L'u');
        } else {
          append_hex(static_cast<unsigned int>(ch), 8, L'U');
        }
        break;
    }
  }

  return L"'" + rendered + L"'";
}

auto parse_quoting_mode(std::string_view value) -> std::optional<QuotingMode> {
  if (value == "literal") return QuotingMode::Literal;
  if (value == "escape") return QuotingMode::Escape;
  if (value == "c") return QuotingMode::C;
  if (value == "c-maybe") return QuotingMode::CMaybe;
  if (value == "shell") return QuotingMode::Shell;
  if (value == "shell-always") return QuotingMode::ShellAlways;
  if (value == "shell-escape") return QuotingMode::ShellEscape;
  if (value == "shell-escape-always") return QuotingMode::ShellEscapeAlways;
  if (value == "locale") return QuotingMode::Locale;
  if (value == "clocale") return QuotingMode::C;
  return std::nullopt;
}

auto resolve_quoting_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<QuotingMode> {
  QuotingMode mode = QuotingMode::Literal;
  bool explicit_style = false;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-N" || meta.long_name == "--literal" ||
        meta.long_name == "--show-control-chars") {
      mode = QuotingMode::Literal;
      explicit_style = true;
      continue;
    }

    if (meta.short_name == "-b" || meta.long_name == "--escape") {
      mode = QuotingMode::Escape;
      explicit_style = true;
      continue;
    }

    if (meta.short_name == "-q" || meta.long_name == "--hide-control-chars") {
      mode = QuotingMode::HideControl;
      explicit_style = true;
      continue;
    }

    if (meta.short_name == "-Q" || meta.long_name == "--quote-name") {
      mode = QuotingMode::C;
      explicit_style = true;
      continue;
    }

    if (meta.long_name == "--quoting-style") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) return std::unexpected("invalid quoting style");
      auto parsed = parse_quoting_mode(*value);
      if (!parsed) return std::unexpected("invalid quoting style");
      mode = *parsed;
      explicit_style = true;
    }
  }
  // [GNU] ls.c:2391: with no quoting-related option, the default style is
  // shell-escape-quoting when stdout is a terminal and literal otherwise.
  if (!explicit_style && ls_is_terminal(stdout)) {
    mode = QuotingMode::ShellEscape;
  }
  return mode;
}

auto needs_shell_quote(wchar_t ch) -> bool {
  if (std::iswspace(static_cast<wint_t>(ch)) != 0) return true;
  switch (ch) {
    case L'\'':
    case L'"':
    case L'\\':
    case L'$':
    case L'&':
    case L';':
    case L'(':
    case L')':
    case L'<':
    case L'>':
    case L'|':
    case L'*':
    case L'?':
    case L'[':
    case L']':
    case L'{':
    case L'}':
    case L'~':
    case L'#':
      return true;
    default:
      return !std::iswprint(static_cast<wint_t>(ch));
  }
}

auto shell_quote_display_name(const std::wstring &name, bool always)
    -> std::wstring {
  bool quote = always;
  for (wchar_t ch : name) {
    if (needs_shell_quote(ch)) {
      quote = true;
      break;
    }
  }
  if (!quote) return name;

  std::wstring rendered;
  rendered.reserve(name.size() + 2);
  rendered.push_back(L'\'');
  for (wchar_t ch : name) {
    if (ch == L'\'') {
      rendered += L"'\\''";
    } else {
      rendered.push_back(ch);
    }
  }
  rendered.push_back(L'\'');
  return rendered;
}

auto apply_quoting_mode(const std::wstring &value, QuotingMode quoting)
    -> std::wstring {
  std::wstring rendered = value;

  switch (quoting) {
    case QuotingMode::Literal:
      break;
    case QuotingMode::Escape:
      rendered = escape_display_name(rendered);
      break;
    case QuotingMode::HideControl:
      for (auto &ch : rendered) {
        if (!std::iswprint(static_cast<wint_t>(ch))) {
          ch = L'?';
        }
      }
      break;
    case QuotingMode::C:
      rendered = L"\"" + escape_display_name(rendered) + L"\"";
      break;
    case QuotingMode::CMaybe: {
      auto escaped = escape_display_name(rendered);
      if (escaped != rendered) {
        rendered = L"\"" + std::move(escaped) + L"\"";
      }
      break;
    }
    case QuotingMode::Shell:
    case QuotingMode::ShellEscape:
      rendered = shell_quote_display_name(rendered, false);
      break;
    case QuotingMode::ShellAlways:
    case QuotingMode::ShellEscapeAlways:
      rendered = shell_quote_display_name(rendered, true);
      break;
    case QuotingMode::Locale:
      rendered = locale_quote_display_name(rendered);
      break;
  }

  return rendered;
}

auto should_dereference_metadata(const WIN32_FIND_DATAW &find_data,
                                 const CommandContext<LS_OPTIONS.size()> &ctx,
                                 bool command_line_operand) -> bool {
  return should_dereference_entry_metadata(resolve_dereference_mode(ctx),
                                           find_data, command_line_operand);
}

auto read_symlink_display_target(const std::wstring &full_path,
                                 const WIN32_FIND_DATAW &find_data,
                                 const CommandContext<LS_OPTIONS.size()> &ctx,
                                 bool command_line_operand = false)
    -> std::optional<SymlinkDisplayTarget> {
  if (!long_format_requested(ctx)) {
    return std::nullopt;
  }

  if (should_dereference_metadata(find_data, ctx, command_line_operand) ||
      (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
    return std::nullopt;
  }

  std::error_code ec;
  std::filesystem::path target =
      std::filesystem::read_symlink(std::filesystem::path(full_path), ec);
  if (ec) {
    return std::nullopt;
  }

  std::filesystem::path resolved_target = target;
  if (resolved_target.is_relative()) {
    resolved_target =
        std::filesystem::path(full_path).parent_path() / resolved_target;
  }
  resolved_target = resolved_target.lexically_normal();

  std::wstring display_target = target.native();
  auto trim_windows_namespace_prefix = [](std::wstring value) {
    if (value.rfind(LR"(\??\)", 0) == 0) {
      return value.substr(4);
    }
    if (value.rfind(LR"(\\?\)", 0) == 0) {
      return value.substr(4);
    }
    return value;
  };
  display_target = trim_windows_namespace_prefix(std::move(display_target));
  if (target.is_absolute() && is_symbolic_reparse_link(find_data)) {
    auto normalize_windows_path = [](std::wstring value) {
      for (auto &ch : value) {
        if (ch == L'/') ch = L'\\';
        ch = std::towlower(ch);
      }
      return value;
    };

    std::wstring parent_native = std::filesystem::path(full_path)
                                     .parent_path()
                                     .lexically_normal()
                                     .native();
    std::wstring resolved_native = resolved_target.native();
    std::wstring normalized_parent = normalize_windows_path(parent_native);
    std::wstring normalized_resolved = normalize_windows_path(resolved_native);

    if (!normalized_parent.empty() &&
        normalized_resolved.size() > normalized_parent.size() &&
        normalized_resolved.compare(0, normalized_parent.size(),
                                    normalized_parent) == 0 &&
        (normalized_resolved[normalized_parent.size()] == L'\\')) {
      display_target = resolved_native.substr(parent_native.size() + 1);
    }
  }

  return SymlinkDisplayTarget{std::move(display_target),
                              std::move(resolved_target)};
}

// [GNU] -D/--dired: records the byte offset range of every printed file
// name and emits "//DIRED// start end ..." trailer lines for Emacs dired.
// Offsets are UTF-8 byte positions in the output stream, counted by the
// console layer (set_stdout_byte_counting).
bool g_hyperlink_enabled = false;

std::vector<std::pair<uint64_t, uint64_t>> g_dired_offsets;
std::vector<std::pair<uint64_t, uint64_t>> g_subdired_offsets;

auto dired_requested(const CommandContext<LS_OPTIONS.size()> &ctx) -> bool {
  return ctx.has("-D") || ctx.has("--dired");
}

// Records a name that will appear at |name_offset| bytes inside a text
// blob that is about to be printed.
void dired_note_text_entry(uint64_t name_offset, uint64_t name_length) {
  const uint64_t begin = stdout_bytes_written() + name_offset;
  g_dired_offsets.push_back({begin, begin + name_length});
}

// [GNU] --hyperlink[=WHEN] wraps each name in an OSC 8 escape with a
// file://HOST/path URI.  Bare --hyperlink means "always" (ls.c:2171-
// 2180: XARGMATCH runs only when optarg != nullptr); an attached empty
// value ("--hyperlink=") is an argmatch failure, not a bare occurrence
// (#1513).
auto hyperlink_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  if (!ctx.has("--hyperlink")) {
    return false;
  }
  auto occurrences = ctx.string_occurrences({"--hyperlink"});
  if (!occurrences.empty() && !occurrences.back().has_value) {
    return true;
  }
  const std::string when = ctx.get<std::string>("--hyperlink", "");
  if (when.empty()) {
    return std::unexpected("invalid --hyperlink argument ''");
  }
  if (when == "always" || when == "yes" || when == "force") {
    return true;
  }
  if (when == "never" || when == "no" || when == "none") {
    return false;
  }
  if (when == "auto" || when == "tty" || when == "if-tty") {
    return isOutputConsole();
  }
  return std::unexpected("invalid --hyperlink argument '" + when + "'");
}

auto hyperlink_wrap(const std::wstring &display_name,
                    const std::wstring &full_path) -> std::wstring {
  // RFC 8089 file URI: file://<host>/<drive>:/<path with '/' separators>,
  // percent-encoding non-unreserved bytes of the UTF-8 path.
  static const std::string host = [] {
    char buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD len = sizeof(buf);
    return GetComputerNameA(buf, &len) ? std::string(buf, len)
                                       : std::string("localhost");
  }();
  std::wstring abs = std::filesystem::absolute(std::filesystem::path(full_path))
                         .generic_wstring();
  const std::string utf8 = wstring_to_utf8(abs);
  std::string uri = "file://" + host + "/";
  uri.reserve(uri.size() + utf8.size() + 8);
  static constexpr char kUnreserved[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/@:";
  for (unsigned char ch : utf8) {
    if (strchr(kUnreserved, ch)) {
      uri.push_back(static_cast<char>(ch));
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", ch);
      uri += hex;
    }
  }
  const std::wstring wuri = utf8_to_wstring(uri);
  return L"\x1b]8;;" + wuri + L"\x07" + display_name + L"\x1b]8;;\x07";
}

auto build_display_name_parts(const std::wstring &name,
                              const std::wstring &full_path,
                              const WIN32_FIND_DATAW &find_data,
                              const CommandContext<LS_OPTIONS.size()> &ctx,
                              bool command_line_operand = false)
    -> DisplayNameParts {
  auto quoting_result = resolve_quoting_mode(ctx);
  QuotingMode quoting = quoting_result ? *quoting_result : QuotingMode::Literal;
  std::wstring rendered = apply_quoting_mode(name, quoting);

  auto target = read_symlink_display_target(full_path, find_data, ctx,
                                            command_line_operand);
  if (target) {
    target->display = apply_quoting_mode(target->display, quoting);
  }
  // [GNU] --hyperlink wraps the displayed name (and the symlink target
  // separately) in an OSC 8 file:// URI escape.
  if (g_hyperlink_enabled) {
    rendered = hyperlink_wrap(rendered, full_path);
    if (target) {
      target->display =
          hyperlink_wrap(target->display, target->resolved_path.native());
    }
  }
  if (!target) {
    rendered += get_indicator_suffix(name, find_data, ctx);
  } else {
    // [GNU] In long-format "name -> target" display the indicator is computed
    // from the link *target's* type and appended to the target name, not to
    // the link name (uutils #12036): "sdir -> realdir/", "s -> f".
    auto target_probe = probe_path(target->resolved_path.native());
    WIN32_FIND_DATAW target_find_data{};
    if (target_probe.found) {
      target_find_data = target_probe.find_data;
    } else if (target_probe.attributes_valid) {
      target_find_data.dwFileAttributes = target_probe.attributes;
    }
    if (target_probe.found || target_probe.attributes_valid) {
      target->display += get_indicator_suffix(
          target->resolved_path.filename().native(), target_find_data, ctx);
    }
  }

  return {std::move(rendered), std::move(target)};
}

auto render_display_name(const std::wstring &name,
                         const std::wstring &full_path,
                         const WIN32_FIND_DATAW &find_data,
                         const CommandContext<LS_OPTIONS.size()> &ctx,
                         bool command_line_operand = false) -> std::wstring {
  auto parts = build_display_name_parts(name, full_path, find_data, ctx,
                                        command_line_operand);
  if (parts.target) {
    parts.rendered_name += L" -> ";
    parts.rendered_name += parts.target->display;
  }

  return parts.rendered_name;
}

auto get_target_color_sequence(const std::filesystem::path &resolved_target)
    -> std::wstring {
  auto probe = probe_path(resolved_target.native());
  if (!probe.attributes_valid && !probe.found) {
    return L"";
  }

  if (probe.found) {
    return std::wstring(get_color_for_entry(resolved_target.filename().native(),
                                            probe.find_data));
  }

  if ((probe.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return std::wstring(COLOR_LINK);
  }
  if ((probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return std::wstring(COLOR_DIR);
  }
  return L"";
}

auto print_display_name(const std::wstring &name, const std::wstring &full_path,
                        const WIN32_FIND_DATAW &find_data,
                        const CommandContext<LS_OPTIONS.size()> &ctx,
                        bool color_enabled, bool command_line_operand = false)
    -> void {
  auto parts = build_display_name_parts(name, full_path, find_data, ctx,
                                        command_line_operand);

  const bool dired = dired_requested(ctx);
  if (!color_enabled) {
    const std::string name_utf8 = wstring_to_utf8(parts.rendered_name);
    const uint64_t name_begin = stdout_bytes_written();
    safePrint(name_utf8);
    if (dired) {
      g_dired_offsets.push_back({name_begin, name_begin + name_utf8.size()});
    }
    if (parts.target) {
      safePrint(" -> ");
      safePrint(wstring_to_utf8(parts.target->display));
    }
    return;
  }

  const std::wstring entry_color(get_color_for_entry(name, find_data));
  const bool has_color = color_enabled && !entry_color.empty();
  if (has_color) {
    safePrint(color_prefix_sequence());
    safePrint(entry_color);
  }
  const std::string name_utf8 = wstring_to_utf8(parts.rendered_name);
  const uint64_t name_begin = stdout_bytes_written();
  safePrint(name_utf8);
  if (dired) {
    g_dired_offsets.push_back({name_begin, name_begin + name_utf8.size()});
  }
  if (has_color) {
    safePrint(COLOR_RESET);
  }

  if (parts.target) {
    safePrint(" -> ");
    const std::wstring target_color =
        get_target_color_sequence(parts.target->resolved_path);
    if (!target_color.empty()) {
      safePrint(target_color);
    }
    safePrint(wstring_to_utf8(parts.target->display));
    if (!target_color.empty()) {
      safePrint(COLOR_RESET);
    }
  }
}

auto resolve_display_entry(const EntryInfo &entry,
                           const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::pair<WIN32_FIND_DATAW, std::wstring> {
  WIN32_FIND_DATAW display_find_data = entry.find_data;
  std::wstring metadata_name = entry.name;
  if (entry.name == L"." &&
      resolve_dereference_mode(ctx) == DereferenceMode::All) {
    // Under -L, a directory-symlink operand's "." entry should stay on the
    // dereferenced directory view rather than snapping back to the reparse
    // point itself.
  } else if (entry.name == L"." || entry.name == L"..") {
    // Keep explicit . / .. segments so dereferenced directory-symlink views
    // probe the effective listed directory rather than collapsing back to the
    // reparse-point operand path.
    auto probe = probe_path(entry.full_path);
    if (probe.found) {
      display_find_data = probe.find_data;
      wcsncpy_s(display_find_data.cFileName, entry.name.c_str(), _TRUNCATE);
    }
  }
  if (auto dereferenced = try_get_dereferenced_find_data(
          entry.full_path, entry.find_data, ctx, entry.command_line_operand)) {
    display_find_data = dereferenced->first;
    metadata_name = dereferenced->second;
    wcsncpy_s(display_find_data.cFileName, metadata_name.c_str(), _TRUNCATE);
  }
  if (entry.name == L"." &&
      resolve_dereference_mode(ctx) == DereferenceMode::All &&
      (display_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    // In a dereferenced directory-symlink view, Microsoft Coreutils reports the
    // synthetic "." entry as the target directory rather than as a link.
    display_find_data.dwFileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
    display_find_data.dwReserved0 = 0;
  }
  return {display_find_data, metadata_name};
}

// [GNU] filevercmp (lib/filevercmp.c), wide-char form with ASCII
// classification: order() ranks end-of-string at -1, '~' at -2,
// digits at 0, letters at their code value, everything else above
// all letters; digit runs compare by value via first-difference
// tracking after leading-zero skips, and a side whose digit run
// continues is GREATER (an exhausted non-digit side does not beat
// a digit).  The previous hand-rolled loop case-folded letters and
// ranked an exhausted side above a digit ("a" after "a1").
auto version_order_w(wchar_t c, bool at_end) -> int {
  if (at_end) return -1;
  if (c == L'~') return -2;
  if (c >= L'0' && c <= L'9') return 0;
  if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) {
    return static_cast<int>(c);
  }
  return static_cast<int>(c) + 256;
}

auto verrevcmp_w(std::wstring_view a, std::wstring_view b) -> int {
  size_t i = 0;
  size_t j = 0;
  auto is_digit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
  while (i < a.size() || j < b.size()) {
    int first_diff = 0;
    while ((i < a.size() && !is_digit(a[i])) ||
           (j < b.size() && !is_digit(b[j]))) {
      const int ac = version_order_w(i < a.size() ? a[i] : L'\0', i >= a.size());
      const int bc = version_order_w(j < b.size() ? b[j] : L'\0', j >= b.size());
      if (ac != bc) return ac < bc ? -1 : 1;
      ++i;
      ++j;
    }
    while (i < a.size() && a[i] == L'0') ++i;
    while (j < b.size() && b[j] == L'0') ++j;
    while (i < a.size() && j < b.size() && is_digit(a[i]) && is_digit(b[j])) {
      if (first_diff == 0) first_diff = static_cast<int>(a[i]) - static_cast<int>(b[j]);
      ++i;
      ++j;
    }
    if (i < a.size() && is_digit(a[i])) return 1;
    if (j < b.size() && is_digit(b[j])) return -1;
    if (first_diff != 0) return first_diff < 0 ? -1 : 1;
  }
  return 0;
}

auto file_prefixlen_w(std::wstring_view s) -> size_t {
  auto is_alnum = [](wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') ||
           (c >= L'A' && c <= L'Z');
  };
  auto is_alpha = [](wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
  };
  size_t cut = s.size();
  while (cut > 1) {
    size_t q = cut;
    while (q > 0 && (is_alnum(s[q - 1]) || s[q - 1] == L'~')) --q;
    if (q == 0 || q == cut) break;
    if (s[q - 1] != L'.') break;
    if (!is_alpha(s[q]) && s[q] != L'~') break;
    if (q - 1 == 0) break;
    cut = q - 1;
  }
  return cut;
}

auto compare_version_strings(std::wstring_view a, std::wstring_view b) -> int {
  // [GNU] filenvercmp's special cases, then the two-pass
  // prefix/suffix comparison (mirrors sort.cpp's port).
  if (a.empty() || b.empty()) {
    if (a.empty() && b.empty()) return 0;
    return a.empty() ? -1 : 1;
  }
  if (a == L".") return b == L"." ? 0 : -1;
  if (b == L".") return 1;
  if (a == L"..") return b == L".." ? 0 : -1;
  if (b == L"..") return 1;
  const bool a_dot = a[0] == L'.';
  const bool b_dot = b[0] == L'.';
  if (a_dot != b_dot) return a_dot ? -1 : 1;

  const size_t a_prefix = file_prefixlen_w(a);
  const size_t b_prefix = file_prefixlen_w(b);
  const int cmp = verrevcmp_w(a.substr(0, a_prefix), b.substr(0, b_prefix));
  if (cmp != 0) return cmp;
  if (a_prefix < a.size() || b_prefix < b.size()) {
    return verrevcmp_w(a, b);
  }
  return 0;
}

auto compare_extensions(const EntryInfo &a, const EntryInfo &b) -> bool {
  // [GNU] -X compares extensions bytewise (ls.c:3913-3925); folding
  // case here reordered ".Z" after ".a".
  const std::wstring a_ext = get_entry_extension_raw(a.name);
  const std::wstring b_ext = get_entry_extension_raw(b.name);
  if (a_ext < b_ext) return true;
  if (a_ext > b_ext) return false;
  return a.name < b.name;
}

auto parse_sort_mode(std::string_view value) -> std::optional<SortMode> {
  if (value.empty() || value == "name") return SortMode::Name;
  if (value == "size") return SortMode::Size;
  if (value == "time") return SortMode::Time;
  if (value == "version") return SortMode::Version;
  if (value == "extension") return SortMode::Extension;
  if (value == "type") return SortMode::Type;
  if (value == "width") return SortMode::Width;
  if (value == "none") return SortMode::None;
  return std::nullopt;
}

// [GNU] --sort values are matched exactly, then by unique prefix
// (argmatch): several prefix matches are ambiguous, none is invalid.
// The valid set is this command's superset of GNU's (GNU has no name or
// type value), so "n" and "t" stay ambiguous here although GNU resolves
// them to none and time.
auto resolve_sort_mode_value(std::string_view value) -> cp::Result<SortMode> {
  if (auto parsed = parse_sort_mode(value)) return *parsed;
  static constexpr std::string_view kNames[] = {
      "name", "size", "time", "version", "extension", "type", "width", "none",
  };
  int matches = 0;
  SortMode matched = SortMode::Name;
  for (std::string_view name : kNames) {
    if (name.starts_with(value)) {
      ++matches;
      matched = parse_sort_mode(name).value();
    }
  }
  if (matches == 1) return matched;
  std::string message =
      matches > 1 ? "ambiguous argument '" : "invalid argument '";
  message += value;
  message +=
      "' for '--sort'\n"
      "Valid arguments are:\n"
      "  - 'name'\n"
      "  - 'size'\n"
      "  - 'time'\n"
      "  - 'version'\n"
      "  - 'extension'\n"
      "  - 'type'\n"
      "  - 'width'\n"
      "  - 'none'\n"
      "Try 'ls --help' for more information.";
  return std::unexpected(message);
}

auto parse_time_mode(std::string_view value) -> std::optional<TimeMode> {
  if (value.empty() || value == "mtime" || value == "modification" ||
      value == "modified") {
    return TimeMode::Modification;
  }
  if (value == "atime" || value == "access" || value == "use") {
    return TimeMode::Access;
  }
  if (value == "ctime" || value == "status") {
    return TimeMode::Status;
  }
  if (value == "birth" || value == "creation") {
    return TimeMode::Birth;
  }
  return std::nullopt;
}

auto parse_format_mode(std::string_view value) -> std::optional<FormatMode> {
  if (value.empty() || value == "vertical" || value == "columns") {
    return FormatMode::Columns;
  }
  if (value == "across" || value == "horizontal") {
    return FormatMode::Across;
  }
  if (value == "commas") {
    return FormatMode::Commas;
  }
  if (value == "single-column" || value == "single" || value == "one") {
    return FormatMode::OnePerLine;
  }
  if (value == "long" || value == "verbose") {
    return FormatMode::Long;
  }
  return std::nullopt;
}

// [GNU] ls.c's when table (ls.c:938-949), shared by --color and
// --hyperlink: argmatch accepts any unambiguous prefix.
auto ls_argmatch_prefix(std::string_view value,
                        std::initializer_list<std::string_view> names)
    -> std::optional<std::string_view> {
  std::optional<std::string_view> hit;
  for (auto name : names) {
    if (name == value) return name;
    if (!value.empty() && name.starts_with(value)) {
      if (hit.has_value() && *hit != name) return std::nullopt;
      hit = name;
    }
  }
  return hit;
}

auto parse_indicator_style(std::string_view value)
    -> std::optional<IndicatorStyle> {
  if (value.empty()) return IndicatorStyle::None;
  // [GNU] XARGMATCH ("--indicator-style", ...): unambiguous prefixes
  // are valid values, not just the full spellings.
  if (auto full = ls_argmatch_prefix(
          value, {"none", "slash", "file-type", "classify"})) {
    value = *full;
  }
  if (value == "none") return IndicatorStyle::None;
  if (value == "slash") return IndicatorStyle::Slash;
  if (value == "file-type") return IndicatorStyle::FileType;
  if (value == "classify") return IndicatorStyle::Classify;
  return std::nullopt;
}

struct TimeSelection {
  TimeMode mode = TimeMode::Modification;
  bool explicit_time = false;
};

struct TimeStyleSelection {
  TimeStyle style = TimeStyle::Default;
  std::string format;
};

constexpr std::string_view kInvalidTimeStylePrefix =
    "invalid --time-style argument ";

auto build_invalid_time_style_message(std::string_view value) -> std::string {
  return "invalid --time-style argument '" + std::string(value) +
         "'\nPossible values are:\n"
         "  - [posix-]full-iso\n"
         "  - [posix-]long-iso\n"
         "  - [posix-]iso\n"
         "  - [posix-]locale\n"
         "  - +FORMAT (e.g., +%H:%M) for a 'date'-style format\n"
         "\nFor more information try --help";
}

auto resolve_time_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<TimeSelection> {
  TimeSelection selection;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.long_name == "--time") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) return std::unexpected("invalid time value");
      auto parsed = parse_time_mode(*value);
      if (!parsed) return std::unexpected("invalid time value");
      selection.mode = *parsed;
      selection.explicit_time = true;
      continue;
    }

    if (meta.short_name == "-u") {
      selection.mode = TimeMode::Access;
      selection.explicit_time = true;
      continue;
    }

    if (meta.short_name == "-c") {
      selection.mode = TimeMode::Status;
      selection.explicit_time = true;
    }
  }
  return selection;
}

auto parse_time_style(std::string_view value) -> std::optional<TimeStyle> {
  if (value == "locale" || value == "posix-locale") {
    return TimeStyle::Locale;
  }
  if (value == "full-iso" || value == "posix-full-iso") {
    return TimeStyle::FullIso;
  }
  if (value == "long-iso" || value == "posix-long-iso") {
    return TimeStyle::LongIso;
  }
  if (value == "iso" || value == "posix-iso") {
    return TimeStyle::Iso;
  }
  return std::nullopt;
}

auto format_mode_explicitly_requested(
    const CommandContext<LS_OPTIONS.size()> &ctx) -> bool {
  // Positional, like resolve_format_mode: a -f that cancels a long
  // format also cancels its "explicit" mark, so the output falls
  // back to the tty/pipe defaults exactly as GNU's format_opt = -1.
  bool explicit_request = false;
  bool long_active = false;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-m" || meta.short_name == "-x" ||
        meta.short_name == "-1" || meta.short_name == "-C") {
      explicit_request = true;
      long_active = false;
    } else if (meta.short_name == "-l" || meta.short_name == "-g" ||
               meta.short_name == "-n" || meta.short_name == "-o" ||
               meta.long_name == "--full-time") {
      explicit_request = true;
      long_active = true;
    } else if (meta.long_name == "--format") {
      explicit_request = true;
      auto value = std::get_if<std::string>(&occurrence.value);
      auto parsed = value ? parse_format_mode(*value) : std::nullopt;
      long_active = parsed && *parsed == FormatMode::Long;
    } else if (meta.short_name == "-f" && long_active) {
      explicit_request = false;
      long_active = false;
    }
  }

  return explicit_request;
}

auto resolve_dereference_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> DereferenceMode {
  DereferenceMode mode = DereferenceMode::None;

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-L" || meta.long_name == "--dereference") {
      mode = DereferenceMode::All;
      continue;
    }

    if (meta.short_name == "-H" ||
        meta.long_name == "--dereference-command-line") {
      mode = DereferenceMode::CommandLine;
      continue;
    }

    if (meta.long_name == "--dereference-command-line-symlink-to-dir" ||
        meta.long_name == "--dereference-command-line-symlinks-to-dir") {
      mode = DereferenceMode::CommandLineDirectories;
    }
  }

  return mode;
}

auto should_dereference_entry_metadata(DereferenceMode mode,
                                       const WIN32_FIND_DATAW &find_data,
                                       bool command_line_operand) -> bool {
  if (!is_reparse_link(find_data)) {
    return false;
  }

  switch (mode) {
    case DereferenceMode::All:
      return true;
    case DereferenceMode::CommandLine:
      return command_line_operand;
    case DereferenceMode::CommandLineDirectories:
      return command_line_operand &&
             (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    case DereferenceMode::None:
      return false;
  }

  return false;
}

auto resolve_format_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<FormatMode> {
  FormatMode mode = FormatMode::Columns;

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-m") {
      mode = FormatMode::Commas;
      continue;
    }

    if (meta.short_name == "-x") {
      mode = FormatMode::Across;
      continue;
    }

    if (meta.short_name == "-1") {
      mode = FormatMode::OnePerLine;
      continue;
    }

    if (meta.short_name == "-C") {
      mode = FormatMode::Columns;
      continue;
    }

    if (meta.short_name == "-l" || meta.short_name == "-g" ||
        meta.short_name == "-n" || meta.short_name == "-o") {
      mode = FormatMode::Long;
      continue;
    }

    if (meta.short_name == "-f") {
      // [GNU] ls.c case 'f': cancels a long format selected EARLIER
      // (format_opt = -1, back to the defaults); a later -l in this
      // same positional loop wins again.
      if (mode == FormatMode::Long) mode = FormatMode::Columns;
      continue;
    }

    if (meta.long_name == "--full-time") {
      mode = FormatMode::Long;
      continue;
    }

    if (meta.long_name == "--format") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) return std::unexpected("invalid format");
      auto parsed = parse_format_mode(*value);
      if (!parsed) return std::unexpected("invalid format");
      mode = *parsed;
    }
  }

  return mode;
}

auto long_format_requested(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  auto format_mode = resolve_format_mode(ctx);
  return format_mode && *format_mode == FormatMode::Long;
}

auto apply_default_output_format(FormatMode mode,
                                 const CommandContext<LS_OPTIONS.size()> &ctx)
    -> FormatMode {
  if (mode != FormatMode::Columns) {
    return mode;
  }

  if (format_mode_explicitly_requested(ctx)) {
    return mode;
  }

  if (!ls_is_terminal(stdout)) {
    return FormatMode::OnePerLine;
  }

  return mode;
}

auto resolve_time_style(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<TimeStyleSelection> {
  TimeStyleSelection selection;
  bool explicit_style = false;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.long_name == "--full-time") {
      selection.style = TimeStyle::FullIso;
      selection.format.clear();
      explicit_style = true;
      continue;
    }

    if (meta.long_name != "--time-style") continue;

    explicit_style = true;
    auto value = std::get_if<std::string>(&occurrence.value);
    if (!value) return std::unexpected(build_invalid_time_style_message(""));
    if (!value->empty() && (*value)[0] == '+') {
      selection.style = TimeStyle::CustomFormat;
      selection.format = value->substr(1);
      continue;
    }

    auto parsed = parse_time_style(*value);
    if (!parsed)
      return std::unexpected(build_invalid_time_style_message(*value));
    selection.style = *parsed;
  }

  // [GNU] The TIME_STYLE environment variable supplies the style when no
  // --time-style/--full-time option was given (uutils #12676).  An
  // unparseable value is a usage-class error, exit 2.
  if (!explicit_style) {
    if (const char *env = std::getenv("TIME_STYLE")) {
      const std::string_view value(env);
      auto env_error = [value](bool ambiguous) {
        return std::unexpected(
            std::string("ls: ") +
            (ambiguous ? "ambiguous argument '" : "invalid argument '") +
            std::string(value) +
            "' for 'time style'\nValid arguments are:\n"
            "  - [posix-]full-iso\n"
            "  - [posix-]long-iso\n"
            "  - [posix-]iso\n"
            "  - [posix-]locale\n"
            "  - +FORMAT (e.g., +%H:%M) for a 'date'-style "
            "format\n"
            "Try 'ls --help' for more information.");
      };
      if (value.empty()) {
        return env_error(true);
      }
      if (value.front() == '+') {
        selection.style = TimeStyle::CustomFormat;
        selection.format = std::string(value.substr(1));
      } else if (auto parsed = parse_time_style(value)) {
        selection.style = *parsed;
      } else {
        return env_error(false);
      }
    }
  }
  return selection;
}

struct SortSelection {
  SortMode mode = SortMode::Name;
  bool explicit_sort = false;
};

auto resolve_sort_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<SortSelection> {
  SortSelection selection;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.long_name == "--sort") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) return std::unexpected("invalid sort value");
      auto parsed = resolve_sort_mode_value(*value);
      if (!parsed) return std::unexpected(parsed.error());
      selection.mode = *parsed;
      selection.explicit_sort = true;
      continue;
    }

    if (meta.short_name == "-U" || meta.short_name == "-f") {
      selection.mode = SortMode::None;
      selection.explicit_sort = true;
      continue;
    }

    if (meta.short_name == "-S") {
      selection.mode = SortMode::Size;
      selection.explicit_sort = true;
      continue;
    }

    if (meta.short_name == "-t") {
      selection.mode = SortMode::Time;
      selection.explicit_sort = true;
      continue;
    }

    if (meta.short_name == "-v") {
      selection.mode = SortMode::Version;
      selection.explicit_sort = true;
      continue;
    }

    if (meta.short_name == "-X") {
      selection.mode = SortMode::Extension;
      selection.explicit_sort = true;
    }
  }
  return selection;
}

auto get_entry_time(const WIN32_FIND_DATAW &find_data, TimeMode mode)
    -> FILETIME {
  switch (mode) {
    case TimeMode::Modification:
      return find_data.ftLastWriteTime;
    case TimeMode::Access:
      return find_data.ftLastAccessTime;
    case TimeMode::Status:
    case TimeMode::Birth:
      return find_data.ftCreationTime;
  }
  return find_data.ftLastWriteTime;
}

auto compare_time_mode(const EntryInfo &a, const EntryInfo &b, TimeMode mode)
    -> bool {
  const FILETIME ta = get_entry_time(a.find_data, mode);
  const FILETIME tb = get_entry_time(b.find_data, mode);
  const int cmp = CompareFileTime(&ta, &tb);
  if (cmp != 0) return cmp > 0;
  return a.name < b.name;
}

auto is_directory_entry(const EntryInfo &entry) -> bool {
  return is_plain_directory(entry.find_data);
}

auto is_recent_ls_time(const FILETIME &file_time) -> bool {
  FILETIME now_ft{};
  GetSystemTimeAsFileTime(&now_ft);

  ULARGE_INTEGER now_value{};
  now_value.LowPart = now_ft.dwLowDateTime;
  now_value.HighPart = now_ft.dwHighDateTime;

  ULARGE_INTEGER file_value{};
  file_value.LowPart = file_time.dwLowDateTime;
  file_value.HighPart = file_time.dwHighDateTime;

  // [GNU] ls.c:4462-4466: "recent" is six_months_ago < t < now,
  // strictly, with six_months_ago = now - (31556952 / 2) seconds.
  // A truncated 182-day window plus a one-hour future grace made
  // near-boundary and future files render with the wrong form.
  constexpr uint64_t kTicksPerSecond = 10'000'000ULL;
  constexpr uint64_t kRecentWindowTicks = 15'778'476ULL * kTicksPerSecond;

  if (file_value.QuadPart >= now_value.QuadPart) return false;
  return (now_value.QuadPart - file_value.QuadPart) < kRecentWindowTicks;
}

auto get_timezone_offset_string(const FILETIME &utc_time,
                                const FILETIME &local_time) -> std::string {
  ULARGE_INTEGER utc_value{};
  utc_value.LowPart = utc_time.dwLowDateTime;
  utc_value.HighPart = utc_time.dwHighDateTime;

  ULARGE_INTEGER local_value{};
  local_value.LowPart = local_time.dwLowDateTime;
  local_value.HighPart = local_time.dwHighDateTime;

  int64_t delta_ticks = static_cast<int64_t>(local_value.QuadPart) -
                        static_cast<int64_t>(utc_value.QuadPart);
  int total_minutes = static_cast<int>(delta_ticks / (10'000'000LL * 60LL));
  char sign = '+';
  if (total_minutes < 0) {
    sign = '-';
    total_minutes = -total_minutes;
  }

  char buf[8];
  snprintf(buf, sizeof(buf), "%c%02d%02d", sign, total_minutes / 60,
           total_minutes % 60);
  return std::string(buf);
}

auto format_strftime_like(const SYSTEMTIME &st, const FILETIME &file_time,
                          std::string_view format) -> std::string {
  std::tm tm{};
  tm.tm_year = st.wYear - 1900;
  tm.tm_mon = st.wMonth - 1;
  tm.tm_mday = st.wDay;
  tm.tm_hour = st.wHour;
  tm.tm_min = st.wMinute;
  tm.tm_sec = st.wSecond;
  tm.tm_isdst = -1;

  std::string translated;
  translated.reserve(format.size() + 8);
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '%' && i + 1 < format.size() && format[i + 1] == 's') {
      ULARGE_INTEGER value{};
      value.LowPart = file_time.dwLowDateTime;
      value.HighPart = file_time.dwHighDateTime;
      translated +=
          std::to_string(value.QuadPart / 10'000'000ULL - 11644473600ULL);
      ++i;
      continue;
    }
    translated.push_back(format[i]);
  }

  char buf[256];
  if (std::strftime(buf, sizeof(buf), translated.c_str(), &tm) == 0) {
    return std::string();
  }
  return std::string(buf);
}

auto resolve_custom_time_format(const FILETIME &file_time,
                                std::string_view format) -> std::string_view {
  const size_t newline = format.find('\n');
  if (newline == std::string_view::npos) {
    return format;
  }

  if (is_recent_ls_time(file_time)) {
    return format.substr(newline + 1);
  }

  return format.substr(0, newline);
}

auto get_time_string(const WIN32_FIND_DATAW &find_data, TimeMode mode,
                     const TimeStyleSelection &selection = {}) -> std::string {
  FILETIME file_time = get_entry_time(find_data, mode);
  FILETIME local_ft{};
  FileTimeToLocalFileTime(&file_time, &local_ft);

  SYSTEMTIME st{};
  FileTimeToSystemTime(&local_ft, &st);

  const char *month_abbrs[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  char buf[64];
  if (selection.style == TimeStyle::FullIso) {
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    uint64_t nanos = (value.QuadPart % 10'000'000ULL) * 100ULL;
    auto offset = get_timezone_offset_string(file_time, local_ft);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%09llu %s",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             static_cast<unsigned long long>(nanos), offset.c_str());
    return std::string(buf);
  }
  if (selection.style == TimeStyle::LongIso) {
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute);
    return std::string(buf);
  }
  if (selection.style == TimeStyle::Iso) {
    if (is_recent_ls_time(file_time)) {
      snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d", st.wMonth, st.wDay,
               st.wHour, st.wMinute);
    } else {
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d ", st.wYear, st.wMonth,
               st.wDay);
    }
    return std::string(buf);
  }
  if (selection.style == TimeStyle::Locale) {
    if (is_recent_ls_time(file_time)) {
      snprintf(buf, sizeof(buf), "%s %2d %02d:%02d", month_abbrs[st.wMonth],
               st.wDay, st.wHour, st.wMinute);
    } else {
      snprintf(buf, sizeof(buf), "%s %2d  %04d", month_abbrs[st.wMonth],
               st.wDay, st.wYear);
    }
    return std::string(buf);
  }
  if (selection.style == TimeStyle::CustomFormat) {
    return format_strftime_like(
        st, file_time, resolve_custom_time_format(file_time, selection.format));
  }
  // [GNU] the DEFAULT style splits exactly like the locale style
  // (ls.c:4458-4470): recent files show the time of day, older and
  // future files show the year.  This branch used to print the time
  // form unconditionally.
  if (is_recent_ls_time(file_time)) {
    snprintf(buf, sizeof(buf), "%s %2d %02d:%02d", month_abbrs[st.wMonth],
             st.wDay, st.wHour, st.wMinute);
  } else {
    snprintf(buf, sizeof(buf), "%s %2d  %04d", month_abbrs[st.wMonth],
             st.wDay, st.wYear);
  }
  return std::string(buf);
}

auto make_generic_display_path(std::string path) -> std::string {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

enum class ColorWhen { Always, Never, Auto };

// [GNU] --color[=WHEN] (ls.c:2156-2169, when_args at ls.c:938-950): a
// bare --color means always; an attached value goes through argmatch
// over always/yes/force, never/no/none, auto/tty/if-tty -- exact or
// unambiguous prefix, where several spellings of one mode still match
// ("n" -> never). "" prefixes every spelling of every mode, so it is
// ambiguous (#1513).
auto resolve_color_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<ColorWhen> {
  auto occurrences = ctx.string_occurrences({"--color"});
  if (occurrences.empty()) return ColorWhen::Auto;
  const auto &occurrence = occurrences.back();
  if (!occurrence.has_value) return ColorWhen::Always;

  const std::string &value = occurrence.value;
  static constexpr std::pair<std::string_view, ColorWhen> kNames[] = {
      {"always", ColorWhen::Always}, {"yes", ColorWhen::Always},
      {"force", ColorWhen::Always},  {"never", ColorWhen::Never},
      {"no", ColorWhen::Never},      {"none", ColorWhen::Never},
      {"auto", ColorWhen::Auto},     {"tty", ColorWhen::Auto},
      {"if-tty", ColorWhen::Auto},
  };
  for (auto [name, when] : kNames) {
    if (value == name) return when;
  }
  std::optional<ColorWhen> matched;
  bool ambiguous = false;
  for (auto [name, when] : kNames) {
    if (!name.starts_with(value)) continue;
    if (matched && *matched != when) ambiguous = true;
    matched = when;
  }
  if (matched && !ambiguous) return *matched;
  std::string message =
      ambiguous ? "ambiguous argument '" : "invalid argument '";
  message += value;
  message +=
      "' for '--color'\n"
      "Valid arguments are:\n"
      "  - 'always', 'yes', 'force'\n"
      "  - 'never', 'no', 'none'\n"
      "  - 'auto', 'tty', 'if-tty'\n"
      "Try 'ls --help' for more information.";
  return std::unexpected(message);
}

// [GNU] ls.c case 'f' sets print_block_size = false at its
// position; a later -s/--size sets it again.  Positional scan.
auto print_block_size_enabled(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  bool enabled = false;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.short_name == "-s" || meta.long_name == "--size") {
      enabled = true;
    } else if (meta.short_name == "-f") {
      enabled = false;
    }
  }
  return enabled;
}

auto resolve_color_enabled(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  // [GNU] positional semantics: --color assigns print_with_color at
  // its position, and -f clears it at its position (ls.c case 'f');
  // the last of the two decides. The value itself (bare = always,
  // attached = argmatch over the when table) is resolved by
  // resolve_color_mode over the last --color occurrence (#1513).
  bool suppressed_by_f = false;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.long_name == "--color") {
      suppressed_by_f = false;
    } else if (meta.short_name == "-f") {
      suppressed_by_f = true;
    }
  }
  if (suppressed_by_f) return false;
  auto mode = resolve_color_mode(ctx);
  if (!mode) return ls_is_terminal(stdout);
  if (*mode == ColorWhen::Always) return true;
  if (*mode == ColorWhen::Never) return false;
  return ls_is_terminal(stdout);
}

// [GNU] XARGMATCH failure text for a WHEN-valued option (ls.c):
// "invalid argument '<v>' for '<opt>'" + the valid-arguments list +
// the Try-help line; argmatch_die exits with LS_FAILURE (2).
auto argmatch_error_text(std::string_view value, std::string_view option,
                         std::initializer_list<std::string_view> valid)
    -> std::string {
  bool ambiguous = false;
  for (auto name : valid) {
    if (name.starts_with(value) && name != value) {
      ambiguous = true;
    }
  }
  std::string text = std::string(ambiguous ? "ambiguous argument '"
                                           : "invalid argument '") +
                     std::string(value) + "' for '" +
                     std::string(option) + "'\nValid arguments are:";
  for (auto name : valid) {
    text += "\n  - '";
    text += name;
    text += "'";
  }
  text += "\nTry 'ls --help' for more information.";
  return text;
}

auto validate_color_option(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::optional<std::string> {
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.long_name != "--color") continue;
    auto value = std::get_if<std::string>(&occurrence.value);
    // [GNU] a bare --color means always and needs no validation; an
    // attached value -- including "" from --color= -- goes through
    // argmatch (#1513: value_attached carries the distinction).
    if (!occurrence.value_attached) continue;
    if (!value) continue;
    if (!ls_argmatch_prefix(*value, {"always", "yes", "force", "never",
                                     "no", "none", "auto", "tty",
                                     "if-tty"})) {
      return argmatch_error_text(*value, "--color",
                                 {"always", "yes", "force", "never", "no",
                                  "none", "auto", "tty", "if-tty"});
    }
  }
  return std::nullopt;
}

auto validate_indicator_style_option(
    const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::optional<std::string> {
  if (!ctx.has("--indicator-style")) return std::nullopt;
  const std::string value = ctx.get<std::string>("--indicator-style", "");
  if (value.empty() ||
      !ls_argmatch_prefix(value, {"none", "slash", "file-type",
                                  "classify"})) {
    return argmatch_error_text(value, "--indicator-style",
                               {"none", "slash", "file-type", "classify"});
  }
  return std::nullopt;
}

auto use_zero_terminated_output(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  return ctx.get<bool>("--zero", false);
}

auto print_record_terminator(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> void {
  if (use_zero_terminated_output(ctx)) {
    safePrint(std::string(1, '\0'));
  } else {
    safePrintLn(L"");
  }
}

struct RenderedEntry {
  std::string text;
  size_t visible_width = 0;
  // [GNU] --dired: byte offset/length of the file name inside |text|.
  size_t name_offset = 0;
  size_t name_length = 0;
};

auto render_inline_entry(const EntryInfo &entry,
                         const CommandContext<LS_OPTIONS.size()> &ctx,
                         bool color_enabled) -> RenderedEntry {
  auto [display_find_data, metadata_name] = resolve_display_entry(entry, ctx);
  std::wstring display_name =
      render_display_name(entry.name, entry.full_path, display_find_data, ctx,
                          entry.command_line_operand);
  std::string prefix =
      build_listing_prefix(entry.full_path, display_find_data, ctx);
  std::string text = prefix;
  const std::wstring entry_color(
      get_color_for_entry(metadata_name, display_find_data));
  if (color_enabled && !entry_color.empty()) {
    text += wstring_to_utf8(color_prefix_sequence());
    text += wstring_to_utf8(entry_color);
  }
  const std::string name_utf8 = wstring_to_utf8(display_name);
  const size_t name_offset = text.size();
  text += name_utf8;
  if (color_enabled && !entry_color.empty()) {
    text += wstring_to_utf8(COLOR_RESET);
  }

  return {std::move(text), prefix.size() + string_display_width(display_name),
          name_offset, name_utf8.size()};
}

auto build_rendered_entries(const std::vector<EntryInfo> &entries,
                            const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::vector<RenderedEntry> {
  const bool color_enabled = resolve_color_enabled(ctx);
  std::vector<RenderedEntry> rendered;
  rendered.reserve(entries.size());
  for (const auto &entry : entries) {
    rendered.push_back(render_inline_entry(entry, ctx, color_enabled));
  }
  return rendered;
}

auto print_rendered_entries(const std::vector<RenderedEntry> &entries,
                            size_t width,
                            const CommandContext<LS_OPTIONS.size()> &ctx)
    -> void {
  size_t line_width = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) {
      const size_t separator_reserve =
          2 + ((i + 1) < entries.size() ? static_cast<size_t>(1) : 0);
      // [GNU] #1504: width 0 is "unlimited" — no wrapping.
      if (width > 0 && line_width > 0 &&
          line_width + separator_reserve + entries[i].visible_width > width) {
        safePrint(",\n");
        line_width = 0;
      } else {
        safePrint(", ");
        line_width += 2;
      }
    }
    if (dired_requested(ctx)) {
      dired_note_text_entry(entries[i].name_offset, entries[i].name_length);
    }
    safePrint(entries[i].text);
    line_width += entries[i].visible_width;
  }
}

auto print_tab_aligned_padding(size_t current_column, size_t target_column,
                               int tab_size) -> void {
  if (target_column <= current_column) {
    return;
  }

  if (tab_size <= 0) {
    tab_size = ls_constants::DEFAULT_TAB_SIZE;
  }

  // [GNU] indent(): use a TAB instead of two or more spaces, but
  // only when the tab lands strictly inside the pad run
  // (to / tabsize > (from + 1) / tabsize in ls.c).
  const auto ts = static_cast<size_t>(tab_size);
  while (current_column < target_column) {
    if (target_column / ts > (current_column + 1) / ts) {
      safePrint("\t");
      current_column += ts - current_column % ts;
      continue;
    }

    safePrint(" ");
    ++current_column;
  }
}

// [GNU] -T/--tabsize values use strtoumax base-0 syntax: "8" is decimal,
// "010" is octal, "0xff" is 255.  Rejects empty, non-numeric, non-positive,
// and out-of-range values (uutils #12841).
auto parse_tab_size_value(std::string_view value) -> std::optional<uintmax_t> {
  if (value.empty()) {
    return std::nullopt;
  }
  errno = 0;
  char *end = nullptr;
  const std::string text(value);
  const uintmax_t parsed = std::strtoumax(text.c_str(), &end, 0);
  if (end != text.c_str() + text.size() || errno == ERANGE ||
      parsed > static_cast<uintmax_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return parsed;
}

auto resolve_tab_size(const CommandContext<LS_OPTIONS.size()> &ctx) -> int {
  int tab_size = ls_constants::DEFAULT_TAB_SIZE;

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.short_name != "-T" && meta.long_name != "--tabsize") continue;

    const auto *value = std::get_if<std::string>(&occurrence.value);
    if (!value) continue;
    if (auto parsed = parse_tab_size_value(*value); parsed && *parsed > 0) {
      tab_size = static_cast<int>(*parsed);
    }
  }

  return tab_size;
}

auto print_grid(const std::vector<EntryInfo> &entries,
                const CommandContext<LS_OPTIONS.size()> &ctx,
                bool across_layout) -> void {
  if (entries.empty()) return;

  const auto rendered = build_rendered_entries(entries, ctx);

  const int width = resolve_output_width(ctx);

  // [GNU] ls.c:4142-4153: a "no limit" line length (a decided -w 0,
  // COLUMNS=0, or an overflowing width) makes -C and -x alike print
  // every entry on one unwrapped line with plain two-space gutters
  // and no tab alignment (print_with_separator(' ')).
  if (width == kLsNoLineLimit) {
    for (size_t idx = 0; idx < rendered.size(); ++idx) {
      if (dired_requested(ctx)) {
        dired_note_text_entry(rendered[idx].name_offset,
                              rendered[idx].name_length);
      }
      safePrint(rendered[idx].text);
      if (idx + 1 < rendered.size()) {
        safePrint("  ");
      }
    }
    safePrintLn(L"");
    return;
  }

  auto compute_column_widths = [&](int candidate_cols) {
    int candidate_rows = static_cast<int>(
        (entries.size() + candidate_cols - 1) / candidate_cols);
    std::vector<size_t> widths(static_cast<size_t>(candidate_cols), 0);
    for (size_t idx = 0; idx < rendered.size(); ++idx) {
      size_t col = across_layout ? idx % static_cast<size_t>(candidate_cols)
                                 : idx / static_cast<size_t>(candidate_rows);
      widths[col] = std::max(widths[col], rendered[idx].visible_width);
    }
    return widths;
  };
  // [GNU] ls.c calculate_columns with init_column_info: a candidate
  // of k columns starts from a per-column baseline of
  // MIN_COLUMN_WIDTH (3), not from the raw name widths. Each column's
  // effective width is max(3, name width + 2), except the last
  // column's, which is max(3, name width); the running line length is
  // the sum of the effective widths. GNU re-tests validity
  // (line_len < line_length) only when a column grows past its
  // current width, so a candidate whose columns never grow (summed
  // effective width == 3k) stays valid even at or past the line
  // width. The candidate count is likewise capped at
  // ceil(width / MIN_COLUMN_WIDTH) (ls.c: max_idx).
  auto candidate_line_len = [](const std::vector<size_t> &widths) -> size_t {
    size_t total = 0;
    for (size_t i = 0; i < widths.size(); ++i) {
      size_t real = widths[i] + (i + 1 < widths.size() ? 2 : 0);
      total += std::max<size_t>(real, 3);
    }
    return total;
  };
  const int max_cols_by_width = (width + 2) / 3;
  int max_cols = static_cast<int>(std::min<size_t>(
      entries.size(), static_cast<size_t>(std::max(max_cols_by_width, 1))));
  int cols = 1;
  std::vector<size_t> col_widths = compute_column_widths(cols);
  for (int candidate_cols = max_cols; candidate_cols >= 1; --candidate_cols) {
    auto candidate_widths = compute_column_widths(candidate_cols);
    size_t line_len = candidate_line_len(candidate_widths);
    if (line_len < static_cast<size_t>(width) ||
        line_len == 3 * static_cast<size_t>(candidate_cols)) {
      cols = candidate_cols;
      col_widths = std::move(candidate_widths);
      break;
    }
  }
  int rows = static_cast<int>((entries.size() + cols - 1) / cols);
  std::vector<size_t> col_starts(static_cast<size_t>(cols), 0);
  for (int col = 1; col < cols; ++col) {
    col_starts[static_cast<size_t>(col)] =
        col_starts[static_cast<size_t>(col - 1)] +
        col_widths[static_cast<size_t>(col - 1)] + 2;
  }

  int tab_size = resolve_tab_size(ctx);

  for (int row = 0; row < rows; ++row) {
    size_t current_column = 0;
    for (int col = 0; col < cols; ++col) {
      size_t index = across_layout ? static_cast<size_t>(row * cols + col)
                                   : static_cast<size_t>(row + col * rows);
      if (index >= rendered.size()) continue;

      if (dired_requested(ctx)) {
        dired_note_text_entry(rendered[index].name_offset,
                              rendered[index].name_length);
      }
      safePrint(rendered[index].text);
      current_column += rendered[index].visible_width;
      bool has_later_entry_in_row = false;
      for (int next_col = col + 1; next_col < cols; ++next_col) {
        size_t next_index = across_layout
                                ? static_cast<size_t>(row * cols + next_col)
                                : static_cast<size_t>(row + next_col * rows);
        if (next_index < rendered.size()) {
          has_later_entry_in_row = true;
          break;
        }
      }

      if (has_later_entry_in_row) {
        size_t next_start = col_starts[static_cast<size_t>(col + 1)];
        print_tab_aligned_padding(current_column, next_start, tab_size);
        current_column = next_start;
      }
    }
    safePrintLn(L"");
  }
}

/**
 * @brief Validate arguments
 * @param ctx Command context
 * @return Result
 * with paths to process
 */
auto validate_arguments(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> paths;
  for (auto arg : ctx.positionals) {
    paths.push_back(std::string(arg));
  }

  if (paths.empty()) {
    paths.push_back(".");
  }

  // [GNU] rejects a tab size that is not a positive strtoumax base-0 number
  // ("invalid tab size: '-1'", "invalid tab size: '=0xff'").
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.short_name != "-T" && meta.long_name != "--tabsize") continue;
    const auto *value = std::get_if<std::string>(&occurrence.value);
    const std::string raw = value ? *value : std::string();
    auto parsed = parse_tab_size_value(raw);
    if (!parsed || *parsed == 0) {
      return std::unexpected("invalid tab size: '" + raw + "'");
    }
  }

  // [GNU] ls.c:2023-2027: -w is decoded by decode_line_length during
  // the option pass, and an undecodable value dies there with
  // "invalid line width" (LS_FAILURE, i.e. exit 2 via the caller's
  // prefix table). quote() marks are the UTF-8 form, as for the
  // COLUMNS warning in get_terminal_width().
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.short_name != "-w" && meta.long_name != "--width") continue;
    const auto *value = std::get_if<std::string>(&occurrence.value);
    const std::string raw = value ? *value : std::string();
    if (decode_line_length(raw) < 0) {
      return std::unexpected("invalid line width: \xE2\x80\x98" + raw +
                             "\xE2\x80\x99");
    }
  }

  return paths;
}

/**
 * @brief Get file permissions string
 * @param find_data WIN32_FIND_DATAW structure
 * @return Permissions string in ls format
 */
/**
 * @brief Get file permissions string (improved: simulate real Linux
 * permissions)
 * @param find_data WIN32_FIND_DATAW structure
 * @return Permissions string in ls format
 */
auto get_permissions_string(const WIN32_FIND_DATAW &find_data) -> std::string {
  char perms[11] = "----------";
  perms[10] = '\0';

  // Set file type
  const bool is_char_device =
      (find_data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0;
  if (is_char_device) {
    perms[0] = 'c';  // Character device (e.g. /dev/null)
  } else if (is_reparse_link(find_data)) {
    perms[0] = 'l';  // Symbolic link / junction style display
  } else if (is_plain_directory(find_data)) {
    perms[0] = 'd';
  } else {
    perms[0] = '-';
  }

  const bool read_only =
      (find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  const char write_char = read_only ? '-' : 'w';
  // [GNU] Device files are not executable: /dev/null shows "crw-rw-rw-".
  const char exec_char = is_char_device ? '-' : 'x';

  // Match Microsoft coreutils' Windows approximation:
  // every non-link entry is readable and "searchable/executable" for all,
  // while the read-only attribute removes the write bit triplet-wide.
  perms[1] = 'r';
  perms[2] = write_char;
  perms[3] = exec_char;
  perms[4] = 'r';
  perms[5] = write_char;
  perms[6] = exec_char;
  perms[7] = 'r';
  perms[8] = write_char;
  perms[9] = exec_char;

  // Handle hidden files (optional)
  // Note: Keeping 10 characters for consistent formatting
  return std::string(perms, 10);
}

auto try_get_dereferenced_find_data(
    const std::wstring &path, const WIN32_FIND_DATAW &original_find_data,
    const CommandContext<LS_OPTIONS.size()> &ctx, bool command_line_operand)
    -> std::optional<std::pair<WIN32_FIND_DATAW, std::wstring>> {
  if (!should_dereference_metadata(original_find_data, ctx,
                                   command_line_operand)) {
    return std::nullopt;
  }

  // Canonicalize the dialect-resolved form: std::filesystem::canonical on a
  // POSIX drive-form operand resolves against the current drive root (#1145).
  std::error_code ec;
  const std::filesystem::path target = std::filesystem::canonical(
      native_path::make_api_path_operand_w(path).normalized, ec);
  if (ec) {
    return std::nullopt;
  }

  auto target_operand = native_path::make_api_path_operand_w(target.wstring());
  WIN32_FILE_ATTRIBUTE_DATA attrs{};
  if (!GetFileAttributesExW(target_operand.extended.c_str(),
                            GetFileExInfoStandard, &attrs)) {
    return std::nullopt;
  }

  WIN32_FIND_DATAW dereferenced = original_find_data;
  dereferenced.dwFileAttributes = attrs.dwFileAttributes;
  dereferenced.ftCreationTime = attrs.ftCreationTime;
  dereferenced.ftLastAccessTime = attrs.ftLastAccessTime;
  dereferenced.ftLastWriteTime = attrs.ftLastWriteTime;
  dereferenced.nFileSizeHigh = attrs.nFileSizeHigh;
  dereferenced.nFileSizeLow = attrs.nFileSizeLow;

  std::wstring target_name = target.filename().native();
  if (target_name.empty()) {
    target_name = std::wstring(original_find_data.cFileName);
  }
  return std::make_pair(dereferenced, std::move(target_name));
}

auto ceil_div(uint64_t value, uint64_t divisor) -> uint64_t {
  if (value == 0) return 0;
  return 1 + ((value - 1) / divisor);
}

auto pow_u64(uint64_t base, int exponent) -> std::optional<uint64_t> {
  uint64_t value = 1;
  for (int i = 0; i < exponent; ++i) {
    if (value > std::numeric_limits<uint64_t>::max() / base) {
      return std::nullopt;
    }
    value *= base;
  }
  return value;
}

auto parse_block_size(std::string_view text) -> std::optional<uint64_t> {
  if (text.empty()) return std::nullopt;

  struct Unit {
    std::string_view suffix;
    uint64_t multiplier;
  };

  const std::array units{Unit{"KiB", *pow_u64(1024, 1)},
                         Unit{"MiB", *pow_u64(1024, 2)},
                         Unit{"GiB", *pow_u64(1024, 3)},
                         Unit{"TiB", *pow_u64(1024, 4)},
                         Unit{"PiB", *pow_u64(1024, 5)},
                         Unit{"EiB", *pow_u64(1024, 6)},
                         Unit{"KB", *pow_u64(1000, 1)},
                         Unit{"MB", *pow_u64(1000, 2)},
                         Unit{"GB", *pow_u64(1000, 3)},
                         Unit{"TB", *pow_u64(1000, 4)},
                         Unit{"PB", *pow_u64(1000, 5)},
                         Unit{"EB", *pow_u64(1000, 6)},
                         Unit{"K", *pow_u64(1024, 1)},
                         Unit{"M", *pow_u64(1024, 2)},
                         Unit{"G", *pow_u64(1024, 3)},
                         Unit{"T", *pow_u64(1024, 4)},
                         Unit{"P", *pow_u64(1024, 5)},
                         Unit{"E", *pow_u64(1024, 6)},
                         Unit{"k", *pow_u64(1024, 1)},
                         Unit{"m", *pow_u64(1024, 2)},
                         Unit{"g", *pow_u64(1024, 3)},
                         Unit{"t", *pow_u64(1024, 4)},
                         Unit{"p", *pow_u64(1024, 5)},
                         Unit{"e", *pow_u64(1024, 6)},
                         Unit{"b", 512},
                         Unit{"B", 1}};

  uint64_t multiplier = 1;
  std::string_view number = text;
  bool suffix_matched = false;
  for (const auto &unit : units) {
    if (text.size() >= unit.suffix.size() &&
        text.substr(text.size() - unit.suffix.size()) == unit.suffix) {
      multiplier = unit.multiplier;
      number = text.substr(0, text.size() - unit.suffix.size());
      suffix_matched = true;
      break;
    }
  }

  uint64_t value = 1;
  if (!number.empty()) {
    auto [ptr, ec] =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (ec != std::errc() || ptr != number.data() + number.size()) {
      return std::nullopt;
    }
  } else if (!suffix_matched) {
    return std::nullopt;
  }

  if (value == 0 || value > std::numeric_limits<uint64_t>::max() / multiplier) {
    return std::nullopt;
  }
  return value * multiplier;
}

auto configure_sizes(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<SizeConfig> {
  SizeConfig cfg;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-k" || meta.long_name == "--kibibytes") {
      cfg.file_mode = SizeMode::Blocks;
      cfg.file_block_size = 1024;
      cfg.block_mode = SizeMode::Blocks;
      cfg.block_size = 1024;
      continue;
    }

    if (meta.short_name == "-h" || meta.long_name == "--human-readable") {
      cfg.file_mode = SizeMode::Human;
      cfg.block_mode = SizeMode::Human;
      continue;
    }

    if (meta.long_name == "--si") {
      cfg.file_mode = SizeMode::SI;
      cfg.block_mode = SizeMode::SI;
      continue;
    }

    if (meta.long_name == "--block-size") {
      auto value = std::get_if<std::string>(&occurrence.value);
      auto bad = [&]() {
        return std::unexpected("invalid --block-size argument '" +
                               (value ? *value : std::string()) + "'");
      };
      if (!value) return bad();

      if (*value == "human-readable") {
        cfg.file_mode = SizeMode::Human;
        cfg.block_mode = SizeMode::Human;
        continue;
      }
      if (*value == "si") {
        cfg.file_mode = SizeMode::SI;
        cfg.block_mode = SizeMode::SI;
        continue;
      }

      auto parsed = parse_block_size(*value);
      if (!parsed) return bad();
      cfg.file_mode = SizeMode::Blocks;
      cfg.file_block_size = *parsed;
      cfg.block_mode = SizeMode::Blocks;
      cfg.block_size = *parsed;
    }
  }
  return cfg;
}

auto format_human_size(uint64_t size, bool si) -> std::string {
  const char *units = si ? "BKMGTPE" : "BKMGTP";
  const double base = si ? 1000.0 : 1024.0;
  int unit_index = 0;
  double value = static_cast<double>(size);

  while (value >= base && unit_index < 6) {
    value /= base;
    ++unit_index;
  }

  char buf[32];
  if (unit_index == 0) {
    snprintf(buf, sizeof(buf), "%.0f", value);
  } else if (value < 10.0) {
    snprintf(buf, sizeof(buf), "%.1f%c", value, units[unit_index]);
  } else if (std::round(value) >= base && unit_index < 6) {
    // [GNU] human.c: a value that ROUNDS UP to the base promotes to
    // 1.0 of the next unit (1048575 bytes is "1.0M", not "1024K").
    snprintf(buf, sizeof(buf), "%.1f%c", 1.0, units[unit_index + 1]);
  } else {
    snprintf(buf, sizeof(buf), "%.0f%c", value, units[unit_index]);
  }
  return std::string(buf);
}

auto format_scaled_size(uint64_t size, SizeMode mode, uint64_t block_size)
    -> std::string {
  switch (mode) {
    case SizeMode::Bytes:
      return std::to_string(size);
    case SizeMode::Blocks:
      return std::to_string(ceil_div(size, block_size));
    case SizeMode::Human:
      return format_human_size(size, false);
    case SizeMode::SI:
      return format_human_size(size, true);
  }
  return std::to_string(size);
}

auto query_directory_standard_size_bytes(const std::wstring &path,
                                         bool allocation_size)
    -> std::optional<uint64_t> {
  auto operand = native_path::make_api_path_operand_w(path);
  HANDLE handle =
      CreateFileW(operand.extended.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  FILE_STANDARD_INFO info{};
  const bool ok = GetFileInformationByHandleEx(handle, FileStandardInfo, &info,
                                               sizeof(info)) != FALSE;
  CloseHandle(handle);
  if (!ok) {
    return std::nullopt;
  }

  const LARGE_INTEGER value =
      allocation_size ? info.AllocationSize : info.EndOfFile;
  return value.QuadPart >= 0
             ? std::optional<uint64_t>(static_cast<uint64_t>(value.QuadPart))
             : std::nullopt;
}

/**
 * @brief Get file size string
 * @param find_data WIN32_FIND_DATAW
 * structure
 * @param ctx Command context
 * @return File size string
 */
auto get_file_size_string(const std::wstring &path,
                          const WIN32_FIND_DATAW &display_find_data,
                          const WIN32_FIND_DATAW &original_find_data,
                          const CommandContext<LS_OPTIONS.size()> &ctx,
                          const SizeConfig &size_cfg,
                          bool command_line_operand = false) -> std::string {
  uint64_t fileSize = 0;
  if (!native_path::is_winux_fifo_w(path) &&
      is_symbolic_reparse_link(original_find_data) &&
      !should_dereference_entry_metadata(resolve_dereference_mode(ctx),
                                         original_find_data,
                                         command_line_operand)) {
    if (auto target = read_symlink_display_target(path, original_find_data, ctx,
                                                  command_line_operand)) {
      fileSize = wstring_to_utf8(target->display).size();
    }
  }

  if (fileSize == 0) {
    if (is_plain_directory(display_find_data)) {
      if (auto queried = query_directory_standard_size_bytes(
              normalize_metadata_probe_path(path), false)) {
        fileSize = *queried;
      }
    }
  }

  if (fileSize == 0) {
    fileSize = static_cast<uint64_t>(display_find_data.nFileSizeLow) |
               (static_cast<uint64_t>(display_find_data.nFileSizeHigh) << 32);
  }
  return format_scaled_size(fileSize, size_cfg.file_mode,
                            size_cfg.file_block_size);
}

auto get_file_index_string(const std::wstring &path,
                           const WIN32_FIND_DATAW &find_data,
                           const CommandContext<LS_OPTIONS.size()> &ctx,
                           bool command_line_operand = false) -> std::string {
  DWORD flags = 0;
  if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }

  const bool dereference = should_dereference_entry_metadata(
      resolve_dereference_mode(ctx), find_data, command_line_operand);
  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !dereference) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(native_path::make_api_path_operand_w(path).extended.c_str(),
                  FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {};
  }

  BY_HANDLE_FILE_INFORMATION info{};
  std::string result;
  if (GetFileInformationByHandle(handle, &info)) {
    ULONGLONG file_index = (static_cast<ULONGLONG>(info.nFileIndexHigh) << 32) |
                           static_cast<ULONGLONG>(info.nFileIndexLow);
    result = std::to_string(file_index);
  }
  CloseHandle(handle);
  return result;
}

auto get_link_count(const std::wstring &path, const WIN32_FIND_DATAW &find_data,
                    const CommandContext<LS_OPTIONS.size()> &ctx,
                    bool command_line_operand = false) -> unsigned long {
  DWORD flags = 0;
  if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  }

  const bool dereference = should_dereference_entry_metadata(
      resolve_dereference_mode(ctx), find_data, command_line_operand);
  if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      !dereference) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  HANDLE handle =
      CreateFileW(native_path::make_api_path_operand_w(path).extended.c_str(),
                  FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return 1;
  }

  BY_HANDLE_FILE_INFORMATION info{};
  DWORD link_count = 1;
  if (GetFileInformationByHandle(handle, &info)) {
    link_count = std::max<DWORD>(info.nNumberOfLinks, 1);
  }
  CloseHandle(handle);
  return static_cast<unsigned long>(link_count);
}

auto get_allocated_size_bytes(const std::wstring &path,
                              const WIN32_FIND_DATAW &find_data) -> uint64_t {
  if (is_plain_directory(find_data)) {
    if (auto queried = query_directory_standard_size_bytes(
            normalize_metadata_probe_path(path), true)) {
      return *queried;
    }
  }

  ULARGE_INTEGER allocated{};
  DWORD high = 0;
  DWORD low = GetCompressedFileSizeW(path.c_str(), &high);
  if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
    uint64_t logical_size =
        static_cast<uint64_t>(find_data.nFileSizeLow) |
        (static_cast<uint64_t>(find_data.nFileSizeHigh) << 32);
    allocated.QuadPart = logical_size;
  } else {
    allocated.LowPart = low;
    allocated.HighPart = high;
  }

  return allocated.QuadPart;
}

auto round_allocated_size_to_block_bytes(uint64_t allocated_size,
                                         uint64_t block_size) -> uint64_t {
  uint64_t blocks = ceil_div(allocated_size, block_size);
  if (blocks > std::numeric_limits<uint64_t>::max() / block_size) {
    return std::numeric_limits<uint64_t>::max();
  }
  return blocks * block_size;
}

auto get_allocated_usage_bytes(const std::wstring &path,
                               const WIN32_FIND_DATAW &find_data) -> uint64_t {
  if (native_path::is_winux_fifo_w(path)) {
    return 0;
  }
  const uint64_t allocated_size = get_allocated_size_bytes(path, find_data);
  if (allocated_size == 0) {
    return 0;
  }
  return round_allocated_size_to_block_bytes(allocated_size, 1024);
}

auto get_allocated_block_count(const std::wstring &path,
                               const WIN32_FIND_DATAW &find_data,
                               const SizeConfig &size_cfg) -> uint64_t {
  return ceil_div(get_allocated_usage_bytes(path, find_data),
                  size_cfg.block_size);
}

auto get_allocated_usage_value(const std::wstring &path,
                               const WIN32_FIND_DATAW &find_data,
                               const SizeConfig &size_cfg) -> uint64_t {
  const uint64_t usage_bytes = get_allocated_usage_bytes(path, find_data);
  switch (size_cfg.block_mode) {
    case SizeMode::Bytes:
      return usage_bytes;
    case SizeMode::Blocks:
      return ceil_div(usage_bytes, size_cfg.block_size);
    case SizeMode::Human:
    case SizeMode::SI:
      return usage_bytes;
  }
  return usage_bytes;
}

auto get_allocated_blocks_string(const std::wstring &path,
                                 const WIN32_FIND_DATAW &find_data,
                                 const SizeConfig &size_cfg) -> std::string {
  const uint64_t usage_value =
      get_allocated_usage_value(path, find_data, size_cfg);
  if (size_cfg.block_mode == SizeMode::Blocks) {
    return std::to_string(usage_value);
  }
  return format_scaled_size(usage_value, size_cfg.block_mode,
                            size_cfg.block_size);
}

auto print_directory_total_line(const std::vector<EntryInfo> &entries,
                                const SizeConfig &size_cfg,
                                const CommandContext<LS_OPTIONS.size()> &ctx)
    -> void {
  uint64_t total_blocks = 0;
  uint64_t total_usage_value = 0;

  for (const auto &entry : entries) {
    auto [display_find_data, _metadata_name] =
        resolve_display_entry(entry, ctx);
    total_blocks +=
        get_allocated_block_count(entry.full_path, display_find_data, size_cfg);
    total_usage_value +=
        get_allocated_usage_value(entry.full_path, display_find_data, size_cfg);
  }

  safePrint("total ");
  if (size_cfg.block_mode == SizeMode::Blocks) {
    safePrint(std::to_string(total_blocks));
  } else {
    safePrint(format_scaled_size(total_usage_value, size_cfg.block_mode,
                                 size_cfg.block_size));
  }
  print_record_terminator(ctx);
}

auto build_listing_prefix(const std::wstring &path,
                          const WIN32_FIND_DATAW &find_data,
                          const CommandContext<LS_OPTIONS.size()> &ctx)
    -> std::string {
  auto size_cfg = configure_sizes(ctx).value_or(SizeConfig{});
  std::string prefix;
  if (ctx.get<bool>("-i", false) || ctx.get<bool>("--inode", false)) {
    prefix = get_file_index_string(path, find_data, ctx);
    if (!prefix.empty()) {
      prefix.push_back(' ');
    }
  }
  if (print_block_size_enabled(ctx)) {
    prefix += get_allocated_blocks_string(path, find_data, size_cfg);
    prefix.push_back(' ');
  }
  return prefix;
}

/**
 * @brief Get file modification time string (improved: timezone support)
 *
 * @param find_data WIN32_FIND_DATAW structure
 * @param use_utc Whether to use
 * UTC time (default: false = local time)
 * @return Modification time string
 */
auto get_modification_time_string(const WIN32_FIND_DATAW &find_data,
                                  bool use_utc = false) -> std::string {
  if (use_utc) {
    FILETIME file_time = find_data.ftLastWriteTime;
    SYSTEMTIME st{};
    FileTimeToSystemTime(&file_time, &st);
    const char *month_abbrs[] = {"",    "Jan", "Feb", "Mar", "Apr",
                                 "May", "Jun", "Jul", "Aug", "Sep",
                                 "Oct", "Nov", "Dec"};
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %2d %02d:%02d", month_abbrs[st.wMonth],
             st.wDay, st.wHour, st.wMinute);
    return std::string(buf);
  }
  return get_time_string(find_data, TimeMode::Modification);
}

/**
 * @brief Get file owner and group information (improved: support numeric ID)
 * @param use_numeric Whether to return numeric UID/GID (-n option)
 * @return Pair of (owner, group) strings
 */
auto get_file_owner_and_group(std::wstring_view path, bool use_numeric = false)
    -> std::pair<std::string, std::string> {
  auto operand = native_path::make_api_path_operand_w(path);
  auto accounts = win32_file_accounts(operand.extended);
  if (use_numeric) {
    return {accounts.owner.id.empty() ? "0" : accounts.owner.id,
            accounts.group.id.empty() ? "0" : accounts.group.id};
  }
  return {accounts.owner.name.empty() ? "UNKNOWN" : accounts.owner.name,
          accounts.group.name.empty() ? "UNKNOWN" : accounts.group.name};
}

// [GNU] ls.c:1867-1886 decode_line_length(): xstrtoumax base-0
// semantics, shared by -w (ls.c:2024) and COLUMNS (ls.c:2297).
// Leading whitespace is skipped; '+', decimal, 0x hex and leading-0
// octal are accepted; 0 and values too large to fit mean "no limit";
// '-', trailing junk, or no digits at all are invalid (-1). Probed
// against GNU 9.4: "010" -> 8, "0x20" -> 32, " 40" -> 40, "40 " /
// "08" / "0x" / "-5" invalid, a 26-digit value is "no limit", never
// an error (#1536).
auto decode_line_length(std::string_view spec) -> int {
  auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
  };
  size_t i = 0;
  while (i < spec.size() && is_space(spec[i])) ++i;
  if (i < spec.size() && spec[i] == '+') ++i;
  int base = 10;
  if (i + 1 < spec.size() && spec[i] == '0' &&
      (spec[i + 1] == 'x' || spec[i + 1] == 'X')) {
    base = 16;
    i += 2;
  } else if (i < spec.size() && spec[i] == '0') {
    base = 8;
  }
  constexpr unsigned long long kPtrdiffMax = 0x7fffffffffffffffULL;
  unsigned long long value = 0;
  bool overflowed = false;
  const size_t start = i;
  for (; i < spec.size(); ++i) {
    const char c = spec[i];
    unsigned digit;
    if (c >= '0' && c <= '9')
      digit = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f')
      digit = static_cast<unsigned>(10 + (c - 'a'));
    else if (c >= 'A' && c <= 'F')
      digit = static_cast<unsigned>(10 + (c - 'A'));
    else
      break;
    if (digit >= static_cast<unsigned>(base)) break;
    if (value > (kPtrdiffMax - digit) / static_cast<unsigned long long>(base))
      overflowed = true;
    else
      value = value * static_cast<unsigned long long>(base) + digit;
  }
  if (i == start || i != spec.size()) return -1;  // no digits / trailing junk
  // [GNU] 0, an overflow past PTRDIFF_MAX, and any value too large to
  // carry in this int width domain all mean "no limit" (ls.c:1874-1883
  // maps LONGINT_OVERFLOW and val > PTRDIFF_MAX to 0).
  if (overflowed || value == 0 ||
      value > static_cast<unsigned long long>(INT_MAX)) {
    return kLsNoLineLimit;
  }
  return static_cast<int>(value);
}

/**
 * @brief Get terminal width
 * @return Terminal width in columns, or kLsNoLineLimit for "no limit"
 *
 * [GNU] #1504: ls.c compute_line_length() priority is -w (decided by
 * resolve_output_width), then a valid terminal width on stdout, then
 * the COLUMNS environment variable, then the 80-column default.  The
 * tty probe outranks COLUMNS: a 40-column pty with COLUMNS=200 lays
 * out at 40 (probe 2026-11-07).  COLUMNS decodes with
 * decode_line_length() (ls.c:2297); an invalid value warns ("ignoring
 * invalid width in environment variable COLUMNS") and falls through
 * to 80, while 0 or an overflowing value means "no limit".  The value
 * is computed once per invocation, exactly like GNU's single
 * compute_line_length() call.
 */
auto get_terminal_width() -> int {
  static int cached_width = -2;
  if (cached_width != -2) {
    return cached_width;
  }
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    const LONG window_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (window_cols > 0) {
      cached_width = static_cast<int>(window_cols);
      return cached_width;
    }
  }
  const char *columns = std::getenv("COLUMNS");
  if (columns != nullptr && *columns != '\0') {
    const int decoded = decode_line_length(columns);
    if (decoded >= 0) {
      cached_width = decoded;
      return cached_width;
    }
    // [GNU] ls.c:2299-2302: the value is rendered with quote(), whose
    // marks are the locale's quotation marks — the UTF-8 U+2018/U+2019
    // form under C.UTF-8 (#1536).
    safeErrorPrint("ls: ignoring invalid width in environment variable "
                   "COLUMNS: \xE2\x80\x98");
    safeErrorPrint(std::string(columns));
    safeErrorPrint("\xE2\x80\x99\n");
  }
  cached_width = 80;  // Default to 80 columns if we can't get terminal width
  return cached_width;
}

// [GNU] ls.c:2270-2307: an explicit -w/--width is decoded first and
// wins over every other width source; its 0 is a decided "no limit",
// never "unset", so presence — not the value — decides whether the
// terminal/COLUMNS chain runs at all (#1536). Both spellings share
// one registry entry, and the last occurrence wins, as in GNU's
// getopt sequence.
auto resolve_output_width(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> int {
  const std::string *raw = nullptr;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.short_name != "-w" && meta.long_name != "--width") continue;
    if (const auto *value = std::get_if<std::string>(&occurrence.value)) {
      raw = value;
    }
  }
  if (raw != nullptr) {
    const int decoded = decode_line_length(*raw);
    if (decoded >= 0) {
      return decoded;
    }
    // Unreachable through process_command: validate_arguments has
    // already rejected an undecodable -w with the GNU diagnostic.
  }
  return get_terminal_width();
}

/**
 * @brief Calculate optimal column layout
 * @param entries List of entries
 * @param terminal_width Terminal width in columns
 * @return Number of columns and number of rows
 */
auto calculate_layout(const std::vector<std::wstring> &entries,
                      int terminal_width) -> std::pair<int, int> {
  if (entries.empty()) {
    return {0, 0};
  }

  // Find the longest entry display width
  size_t max_display_width = 0;
  for (const auto &entry : entries) {
    max_display_width =
        std::max(max_display_width, string_display_width(entry));
  }

  // Minimum column width = max display width + 2 spaces padding
  int min_column_width = static_cast<int>(max_display_width) + 2;
  if (min_column_width <= 0) {
    min_column_width = 1;
  }

  // Calculate maximum possible columns with minimum width
  int max_cols = terminal_width / min_column_width;
  if (max_cols < 1) {
    max_cols = 1;
  }

  // Try to find optimal column width that fills the terminal
  int best_cols = max_cols;
  int best_column_width = min_column_width;

  // If we can fit more than 1 column, try to adjust column width to fill the
  // screen
  if (max_cols > 1) {
    // Calculate how much space is left with minimum column width
    int remaining_space = terminal_width % min_column_width;

    // If there's remaining space, distribute it among columns
    if (remaining_space > 0) {
      // Calculate how much extra space per column
      int extra_per_column = remaining_space / max_cols;

      // New column width with extra space
      int new_column_width = min_column_width + extra_per_column;

      // Calculate new number of columns with adjusted width
      int new_cols = terminal_width / new_column_width;
      if (new_cols > 0) {
        best_cols = new_cols;
        best_column_width = new_column_width;
      }
    }
  }

  // Calculate number of rows
  int rows = (entries.size() + best_cols - 1) / best_cols;

  return {best_cols, rows};
}

/**
 * @brief List a single file (not a directory) - forward declaration
 * @param path Path to file
 * @param ctx Command context
 * @return Result with success status
 */
auto list_file(const std::string &path,
               const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<bool>;

// [GNU] ls has three exit states: 0 for success, 1 for minor problems (an
// entry inside a listed directory could not be statted, e.g. a dangling
// symlink under -L), and 2 for serious trouble (an inaccessible operand).
// Failures inside a directory listing are recorded here so the command can
// exit 1 while still printing the rest of the listing.
bool g_had_minor_errors = false;

// [GNU] Under -L/-H, an entry whose dereference fails is still listed: its
// metadata columns degrade to '?' placeholders and an error is reported.
auto is_dangling_dereference(const EntryInfo &entry,
                             const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  if (!is_reparse_link(entry.find_data) ||
      !should_dereference_entry_metadata(resolve_dereference_mode(ctx),
                                         entry.find_data,
                                         entry.command_line_operand)) {
    return false;
  }
  std::error_code ec;
  (void)std::filesystem::canonical(
      native_path::make_api_path_operand_w(entry.full_path).normalized, ec);
  return static_cast<bool>(ec);
}

/**
 * @brief List directory contents
 * @param path Path to directory
 * @param ctx Command context
 * @return Result with success status
 */
auto list_directory(const std::string &path,
                    const CommandContext<LS_OPTIONS.size()> &ctx,
                    const std::wstring &lookup_override = {})
    -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  // #1572: a bare-name operand resolves to its `.exe` sibling here too, so
  // `ls -d bin/java` answers instead of failing at the FindFirstFileW
  // below; the display name stays the typed operand (list_file's
  // operand_display_name).
  const std::wstring lookup_wpath = resolve_bare_exe_operand_w(
      normalize_lookup_path(lookup_override.empty() ? wpath : lookup_override));
  const auto lookup_operand =
      native_path::make_api_path_operand_w(lookup_wpath);

  // Check -d option: list directories themselves, not their contents
  bool list_dir_only =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--directory", false);

  if (list_dir_only) {
    // Get directory attributes
    WIN32_FIND_DATAW dir_data;
    HANDLE hFind = FindFirstFileW(lookup_operand.extended.c_str(), &dir_data);

    if (hFind == INVALID_HANDLE_VALUE) {
      return std::unexpected("cannot access '" + path +
                             "': No such file or directory");
    }

    // Display directory itself as a file
    FindClose(hFind);
    return list_file(path, ctx);
  }

  // Normal directory listing
  // join_w (not string concatenation): a root operand ("/") concatenated
  // with "\\*" produces "/\*", which GetFullPathNameW mis-parses as the
  // garbage UNC form "\\*" — `ls /` failed exactly this way (#1145).
  std::wstring search_path =
      native_path::join_w(lookup_operand.normalized, L"*");
  search_path = native_path::make_api_path_operand_w(search_path).extended;
  const std::wstring ignore_pattern = get_ignore_pattern(ctx);
  const std::wstring hide_pattern = get_hide_pattern(ctx);

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    DWORD error_code = GetLastError();
    // [GNU] Empty directory should succeed with no output
    if (error_code == ERROR_FILE_NOT_FOUND) {
      // Check if path exists as directory (through the dialect boundary so a
      // POSIX drive-form operand answers for the right path, #1145).
      if (native_path::is_directory_w(lookup_operand.normalized)) {
        return true;  // Empty directory, no entries
      }
    }
    std::string error_msg;
    if (error_code == ERROR_ACCESS_DENIED) {
      error_msg = "Permission denied";
    } else {
      error_msg = "No such file or directory";
    }
    return std::unexpected("cannot access '" + path + "': " + error_msg);
  }

  std::vector<EntryInfo> entries;
  do {
    std::wstring filename = find_data.cFileName;
    if (!should_show_entry(filename, find_data, ctx, ignore_pattern,
                           hide_pattern)) {
      continue;
    }
    entries.push_back({filename,
                       native_path::join_w(lookup_operand.normalized, filename),
                       find_data, false});
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  auto size_cfg_result = configure_sizes(ctx);
  if (!size_cfg_result) return std::unexpected(size_cfg_result.error());
  SizeConfig size_cfg = *size_cfg_result;
  auto time_selection = resolve_time_mode(ctx);
  if (!time_selection) return std::unexpected(time_selection.error());
  auto time_style_selection = resolve_time_style(ctx);
  if (!time_style_selection)
    return std::unexpected(time_style_selection.error());
  auto sort_selection = resolve_sort_mode(ctx);
  if (!sort_selection) return std::unexpected(sort_selection.error());
  auto quoting_mode = resolve_quoting_mode(ctx);
  if (!quoting_mode) return std::unexpected(quoting_mode.error());

  auto format_mode_result = resolve_format_mode(ctx);
  if (!format_mode_result) return std::unexpected(format_mode_result.error());
  FormatMode format_mode =
      apply_default_output_format(*format_mode_result, ctx);
  if (use_zero_terminated_output(ctx) && format_mode != FormatMode::Long) {
    format_mode = FormatMode::OnePerLine;
  }

  TimeMode time_mode = time_selection->mode;
  SortMode sort_mode = sort_selection->mode;
  // [GNU] ls.c:2387-2391: specifying a time field implies time
  // sorting only for ctime/atime/btime — --time=mtime (the default
  // field) never does.
  if (!sort_selection->explicit_sort && format_mode != FormatMode::Long &&
      time_selection->explicit_time &&
      time_mode != TimeMode::Modification) {
    sort_mode = SortMode::Time;
  }

  if (sort_mode != SortMode::None) {
    switch (sort_mode) {
      case SortMode::Time:
        std::sort(entries.begin(), entries.end(),
                  [&](const EntryInfo &a, const EntryInfo &b) {
                    return compare_time_mode(a, b, time_mode);
                  });
        break;
      case SortMode::Size:
        std::sort(
            entries.begin(), entries.end(),
            [](const EntryInfo &a, const EntryInfo &b) {
              uint64_t size_a =
                  static_cast<uint64_t>(a.find_data.nFileSizeLow) |
                  (static_cast<uint64_t>(a.find_data.nFileSizeHigh) << 32);
              uint64_t size_b =
                  static_cast<uint64_t>(b.find_data.nFileSizeLow) |
                  (static_cast<uint64_t>(b.find_data.nFileSizeHigh) << 32);
              if (size_a != size_b) {
                return size_a > size_b;
              }
              return a.name < b.name;
            });
        break;
      case SortMode::Version:
        std::sort(entries.begin(), entries.end(),
                  [](const EntryInfo &a, const EntryInfo &b) {
                    int cmp = compare_version_strings(a.name, b.name);
                    if (cmp != 0) return cmp < 0;
                    return a.name < b.name;
                  });
        break;
      case SortMode::Extension:
        std::sort(entries.begin(), entries.end(), compare_extensions);
        break;
      case SortMode::Type:
        // [GNU] --sort=type: directories first, then files, alphabetical within
        std::sort(entries.begin(), entries.end(),
                  [](const EntryInfo &a, const EntryInfo &b) {
                    bool a_dir = is_directory_entry(a);
                    bool b_dir = is_directory_entry(b);
                    if (a_dir != b_dir) return a_dir;  // dirs first
                    return a.name < b.name;
                  });
        break;
      case SortMode::Width:
        // [GNU] --sort=width: order by the display width of the quoted
        // file name, breaking ties by name (coreutils-9.4 ls.c
        // cmp_width via fileinfo_name_width).
        std::sort(entries.begin(), entries.end(),
                  [&](const EntryInfo &a, const EntryInfo &b) {
                    size_t width_a = string_display_width(
                        apply_quoting_mode(a.name, *quoting_mode));
                    size_t width_b = string_display_width(
                        apply_quoting_mode(b.name, *quoting_mode));
                    if (width_a != width_b) return width_a < width_b;
                    return a.name < b.name;
                  });
        break;
      case SortMode::Name:
        std::sort(entries.begin(), entries.end(),
                  [](const EntryInfo &a, const EntryInfo &b) {
                    return a.name < b.name;
                  });
        break;
      case SortMode::None:
        break;
    }

    // Apply --reverse first, then --group-directories-first to match GNU ls
    // behavior GNU ls reverses the sort order, then partitions directories
    // before files while preserving the reversed order within each group.
    if (ctx.get<bool>("-r", false) || ctx.get<bool>("--reverse", false)) {
      std::reverse(entries.begin(), entries.end());
    }

    if (ctx.get<bool>("--group-directories-first", false)) {
      std::stable_partition(entries.begin(), entries.end(), is_directory_entry);
    }
  }

  bool long_format = format_mode == FormatMode::Long;
  bool use_numeric =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--numeric-uid-gid", false);
  const bool show_owner = !ctx.get<bool>("-g", false);
  const bool show_group = !ctx.get<bool>("-o", false) &&
                          !ctx.get<bool>("-G", false) &&
                          !ctx.get<bool>("--no-group", false);
  const bool show_author = ctx.get<bool>("--author", false);
  const bool show_blocks = print_block_size_enabled(ctx);

  if (long_format) {
    const bool show_inode =
        ctx.get<bool>("-i", false) || ctx.get<bool>("--inode", false);

    struct FileInfo {
      std::wstring name;
      WIN32_FIND_DATAW find_data;
      std::wstring full_path;
      std::string perms;
      std::string inode;
      std::string blocks;
      std::string link_count;
      std::string size;
      std::string mtime;
      std::string owner;
      std::string author;
      std::string group;
    };

    // Collect file information (we already have find_data from entries)
    std::vector<FileInfo> files;
    for (const auto &entry : entries) {
      FileInfo info;
      auto [display_find_data, _metadata_name] =
          resolve_display_entry(entry, ctx);
      info.name = entry.name;
      info.find_data = display_find_data;
      info.full_path = entry.full_path;
      if (is_dangling_dereference(entry, ctx)) {
        // [GNU] A dangling symlink under -L is reported and listed with '?'
        // placeholders instead of disappearing or showing link metadata
        // (Savannah #11124).
        const std::string sep =
            (!path.empty() && path.back() != '/' && path.back() != '\\') ? "/"
                                                                         : "";
        safeErrorPrintLn(std::wstring(L"ls: cannot access '") +
                         utf8_to_wstring(path) + utf8_to_wstring(sep) +
                         entry.name + L"': No such file or directory");
        g_had_minor_errors = true;
        info.perms = "l?????????";
        info.inode = "?";
        info.blocks = "?";
        info.link_count = "?";
        info.size = "?";
        info.mtime = "?";
        info.owner = "?";
        info.author = "?";
        info.group = "?";
        files.push_back(info);
        continue;
      }
      info.perms = get_permissions_string(display_find_data);
      // WinuxCmd fifo markers (#1038) list with a 'p' type char.
      if (info.perms[0] == '-' &&
          (display_find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0 &&
          native_path::is_winux_fifo_w(entry.full_path)) {
        info.perms[0] = 'p';
      }
      if (show_inode) {
        info.inode =
            get_file_index_string(entry.full_path, display_find_data, ctx);
      }
      if (show_blocks) {
        info.blocks = get_allocated_blocks_string(entry.full_path,
                                                  display_find_data, size_cfg);
      }
      info.link_count = std::to_string(
          get_link_count(entry.full_path, display_find_data, ctx));
      info.size = get_file_size_string(entry.full_path, display_find_data,
                                       entry.find_data, ctx, size_cfg,
                                       entry.command_line_operand);
      info.mtime =
          get_time_string(display_find_data, time_mode, *time_style_selection);

      // Get owner and group
      auto [owner, group] =
          get_file_owner_and_group(entry.full_path, use_numeric);
      info.owner = owner;
      info.author = owner;
      info.group = group;

      files.push_back(info);
    }

    // Calculate maximum widths for alignment
    size_t max_owner_len = 0;
    size_t max_author_len = 0;
    size_t max_group_len = 0;
    size_t max_inode_len = 0;
    size_t max_blocks_len = 0;
    size_t max_link_len = 0;
    size_t max_size_len = 0;
    for (const auto &file : files) {
      if (show_owner) {
        max_owner_len = std::max(max_owner_len, file.owner.length());
      }
      if (show_author) {
        max_author_len = std::max(max_author_len, file.author.length());
      }
      if (show_group) {
        max_group_len = std::max(max_group_len, file.group.length());
      }
      if (show_inode) {
        max_inode_len = std::max(max_inode_len, file.inode.length());
      }
      if (show_blocks) {
        max_blocks_len = std::max(max_blocks_len, file.blocks.length());
      }
      max_link_len = std::max(max_link_len, file.link_count.length());
      max_size_len = std::max(max_size_len, file.size.length());
    }

    // Set minimum widths to avoid empty values
    if (show_owner && max_owner_len == 0) max_owner_len = 1;
    if (show_author && max_author_len == 0) max_author_len = 1;
    if (show_group && max_group_len == 0) max_group_len = 1;
    if (show_inode && max_inode_len == 0) max_inode_len = 1;
    if (show_blocks && max_blocks_len == 0) max_blocks_len = 1;
    if (max_link_len == 0) max_link_len = 1;
    if (max_size_len == 0) max_size_len = 1;

    print_directory_total_line(entries, size_cfg, ctx);

    // Long format output
    for (const auto &file_info : files) {
      std::wstring display_name = render_display_name(
          file_info.name, file_info.full_path, file_info.find_data, ctx);

      if (show_inode) {
        int inode_padding = static_cast<int>(max_inode_len) -
                            static_cast<int>(file_info.inode.length());
        for (int i = 0; i < inode_padding; ++i) {
          safePrint(" ");
        }
        safePrint(std::string_view(file_info.inode));
        safePrint(" ");
      }

      if (show_blocks) {
        int blocks_padding = static_cast<int>(max_blocks_len) -
                             static_cast<int>(file_info.blocks.length());
        for (int i = 0; i < blocks_padding; ++i) {
          safePrint(" ");
        }
        safePrint(std::string_view(file_info.blocks));
        safePrint(" ");
      }

      // 1. Permissions and link count
      safePrint(std::string_view(file_info.perms));
      safePrint(" ");
      int link_padding = static_cast<int>(max_link_len) -
                         static_cast<int>(file_info.link_count.length());
      for (int i = 0; i < link_padding; i++) {
        safePrint(" ");
      }
      safePrint(std::string_view(file_info.link_count));
      safePrint(" ");

      if (show_owner) {
        // 2. Owner (left-aligned)
        safePrint(std::string_view(file_info.owner));
        int owner_padding = static_cast<int>(max_owner_len) -
                            static_cast<int>(file_info.owner.length());
        for (int i = 0; i < owner_padding; i++) {
          safePrint(" ");
        }
        safePrint(" ");
      }

      if (show_author) {
        // 3. Author (left-aligned)
        safePrint(std::string_view(file_info.author));
        int author_padding = static_cast<int>(max_author_len) -
                             static_cast<int>(file_info.author.length());
        for (int i = 0; i < author_padding; i++) {
          safePrint(" ");
        }
        safePrint(" ");
      }

      if (show_group) {
        // 4. Group (left-aligned)
        safePrint(std::string_view(file_info.group));
        int group_padding = static_cast<int>(max_group_len) -
                            static_cast<int>(file_info.group.length());
        for (int i = 0; i < group_padding; i++) {
          safePrint(" ");
        }
        safePrint(" ");
      }

      // 5. File size (right-aligned)
      int size_padding = static_cast<int>(max_size_len) -
                         static_cast<int>(file_info.size.length());
      for (int i = 0; i < size_padding; i++) {
        safePrint(" ");
      }
      safePrint(std::string_view(file_info.size));
      safePrint(" ");

      // 6. Modification time
      safePrint(std::string_view(file_info.mtime));
      safePrint(" ");

      bool color_enabled = resolve_color_enabled(ctx);
      print_display_name(file_info.name, file_info.full_path,
                         file_info.find_data, ctx, color_enabled);
      print_record_terminator(ctx);
    }
  } else if (format_mode == FormatMode::Commas) {
    if (show_blocks) {
      print_directory_total_line(entries, size_cfg, ctx);
    }
    auto rendered = build_rendered_entries(entries, ctx);
    const int width = resolve_output_width(ctx);
    print_rendered_entries(rendered, static_cast<size_t>(width), ctx);
    safePrintLn(L"");
  } else if (format_mode == FormatMode::OnePerLine) {
    if (show_blocks) {
      print_directory_total_line(entries, size_cfg, ctx);
    }
    auto rendered = build_rendered_entries(entries, ctx);
    for (const auto &entry : rendered) {
      if (dired_requested(ctx)) {
        dired_note_text_entry(entry.name_offset, entry.name_length);
      }
      safePrint(entry.text);
      print_record_terminator(ctx);
    }
  } else if (format_mode == FormatMode::Across) {
    if (show_blocks) {
      print_directory_total_line(entries, size_cfg, ctx);
    }
    print_grid(entries, ctx, true);
  } else {
    if (show_blocks) {
      print_directory_total_line(entries, size_cfg, ctx);
    }
    print_grid(entries, ctx, false);
  }

  return true;
}

/**
 * @brief List a single file (not a directory)
 * @param path Path to file
 * @param ctx Command context
 * @return Result with success status
 */
auto list_file(const std::string &path,
               const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  const std::wstring lookup_wpath = normalize_lookup_path(wpath);
  // #1572: metadata lookups go through the resolved (`.exe`) spelling;
  // the display name stays the typed operand (operand_display_name below),
  // matching GNU, which echoes the operand as typed.
  const std::wstring resolved_lookup_wpath =
      resolve_bare_exe_operand_w(lookup_wpath);
  const std::wstring metadata_probe_wpath =
      normalize_metadata_probe_path(resolved_lookup_wpath);
  const auto metadata_operand =
      native_path::make_api_path_operand_w(metadata_probe_wpath);
  const std::wstring operand_display_name = wpath;

  // Extract just the filename for display
  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(metadata_operand.extended.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    // [GNU] A drive-root operand ("ls -d /", "ls -d /d") names a real
    // directory that no FindFirstFileW pattern can express; stat it through
    // the boundary and synthesize the metadata record instead of reporting
    // ENOENT (#1145).
    if (!native_path::resolve_pseudo_device_w(resolved_lookup_wpath)) {
      WIN32_FILE_ATTRIBUTE_DATA root_data{};
      const std::wstring &normalized = metadata_operand.normalized;
      const bool is_drive_root =
          (normalized.size() == 3 && normalized[1] == L':') ||
          (normalized.size() == 2 && normalized[1] == L':');
      if (is_drive_root &&
          native_path::file_attribute_data_w(normalized, root_data)) {
        find_data = WIN32_FIND_DATAW{};
        find_data.dwFileAttributes = root_data.dwFileAttributes;
        find_data.ftCreationTime = root_data.ftCreationTime;
        find_data.ftLastAccessTime = root_data.ftLastAccessTime;
        find_data.ftLastWriteTime = root_data.ftLastWriteTime;
        find_data.nFileSizeHigh = root_data.nFileSizeHigh;
        find_data.nFileSizeLow = root_data.nFileSizeLow;
        find_data.cFileName[0] = L'\0';  // a root has no leaf name
        hFind = nullptr;
      } else {
        return std::unexpected("cannot access '" + path +
                               "': No such file or directory");
      }
    } else {
      find_data = WIN32_FIND_DATAW{};
      // [GNU] Pseudo-devices are character devices: mark them so the long
      // listing shows a 'c' type ("crw-rw-rw- ... /dev/null").
      find_data.dwFileAttributes =
          FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DEVICE;
      const std::wstring leaf =
          std::filesystem::path(lookup_wpath).filename().wstring();
      wcsncpy_s(find_data.cFileName, leaf.c_str(), _TRUNCATE);
      FILETIME now{};
      GetSystemTimeAsFileTime(&now);
      find_data.ftCreationTime = now;
      find_data.ftLastAccessTime = now;
      find_data.ftLastWriteTime = now;
      hFind = nullptr;
    }
  } else {
    FindClose(hFind);
  }

  std::wstring filename = find_data.cFileName;

  WIN32_FIND_DATAW display_find_data = find_data;
  std::wstring metadata_name = filename;
  if (auto dereferenced = try_get_dereferenced_find_data(
          resolved_lookup_wpath, find_data, ctx, true)) {
    display_find_data = dereferenced->first;
    metadata_name = dereferenced->second;
    wcsncpy_s(display_find_data.cFileName, metadata_name.c_str(), _TRUNCATE);
  } else if (is_reparse_link(find_data) &&
             should_dereference_metadata(find_data, ctx,
                                         /*command_line_operand=*/true)) {
    // [GNU] stat() on the operand failed under -L/-H: a dangling symlink
    // operand is reported, not listed (Savannah #11124).
    return std::unexpected("cannot access '" + path +
                           "': No such file or directory");
  }

  auto size_cfg_result = configure_sizes(ctx);
  if (!size_cfg_result) return std::unexpected(size_cfg_result.error());
  SizeConfig size_cfg = *size_cfg_result;
  auto time_selection = resolve_time_mode(ctx);
  if (!time_selection) return std::unexpected(time_selection.error());
  auto time_style_selection = resolve_time_style(ctx);
  if (!time_style_selection)
    return std::unexpected(time_style_selection.error());
  auto quoting_mode = resolve_quoting_mode(ctx);
  if (!quoting_mode) return std::unexpected(quoting_mode.error());

  auto format_mode_result = resolve_format_mode(ctx);
  if (!format_mode_result) return std::unexpected(format_mode_result.error());
  FormatMode format_mode =
      apply_default_output_format(*format_mode_result, ctx);
  if (use_zero_terminated_output(ctx) && format_mode != FormatMode::Long) {
    format_mode = FormatMode::OnePerLine;
  }

  TimeMode time_mode = time_selection->mode;

  bool long_format = format_mode == FormatMode::Long;
  bool use_numeric =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--numeric-uid-gid", false);
  const bool show_owner = !ctx.get<bool>("-g", false);
  const bool show_group = !ctx.get<bool>("-o", false) &&
                          !ctx.get<bool>("-G", false) &&
                          !ctx.get<bool>("--no-group", false);
  const bool show_author = ctx.get<bool>("--author", false);
  bool show_inode =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--inode", false);
  bool show_blocks = print_block_size_enabled(ctx);

  if (long_format) {
    // Long format output for single file
    auto perms = get_permissions_string(display_find_data);
    if (perms[0] == '-' &&
        (display_find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0 &&
        native_path::is_winux_fifo_w(resolved_lookup_wpath)) {
      perms[0] = 'p';
    }
    auto inode = show_inode
                     ? get_file_index_string(resolved_lookup_wpath, find_data, ctx, true)
                     : "";
    auto blocks = show_blocks ? get_allocated_blocks_string(
                                    resolved_lookup_wpath, display_find_data, size_cfg)
                              : "";
    auto link_count =
        std::to_string(get_link_count(resolved_lookup_wpath, find_data, ctx, true));
    auto size = get_file_size_string(resolved_lookup_wpath, display_find_data, find_data,
                                     ctx, size_cfg, true);
    auto mtime =
        get_time_string(display_find_data, time_mode, *time_style_selection);
    auto [owner, group] = get_file_owner_and_group(resolved_lookup_wpath, use_numeric);

    if (!inode.empty()) {
      safePrint(std::string_view(inode));
      safePrint(" ");
    }
    if (!blocks.empty()) {
      safePrint(std::string_view(blocks));
      safePrint(" ");
    }

    // 1. Permissions and link count
    safePrint(std::string_view(perms));
    safePrint(" ");
    safePrint(std::string_view(link_count));
    safePrint(" ");

    if (show_owner) {
      // 2. Owner (left-aligned, with padding to match column width)
      safePrint(std::string_view(owner));
      safePrint(" ");
    }

    if (show_author) {
      // 3. Author (same Windows approximation as owner)
      safePrint(std::string_view(owner));
      safePrint(" ");
    }

    if (show_group) {
      // 4. Group (left-aligned)
      safePrint(std::string_view(group));
      safePrint(" ");
    }

    // 5. File size.  [GNU] a single-operand -l pads the size only
    // to its own width (ls.c:3663-3675 computes widths from the
    // operands themselves); there is no minimum width of 8.
    safePrint(std::string_view(size));
    safePrint(" ");

    // 6. Modification time
    safePrint(std::string_view(mtime));
    safePrint(" ");

    bool color_enabled = resolve_color_enabled(ctx);
    print_display_name(operand_display_name, resolved_lookup_wpath, display_find_data,
                       ctx, color_enabled, true);
    print_record_terminator(ctx);
  } else {
    auto rendered = render_inline_entry(
        {operand_display_name, resolved_lookup_wpath, find_data, true}, ctx,
        resolve_color_enabled(ctx));
    if (dired_requested(ctx)) {
      dired_note_text_entry(rendered.name_offset, rendered.name_length);
    }
    safePrint(rendered.text);
    print_record_terminator(ctx);
  }

  return true;
}

/**
 * @brief List directory recursively with symlink loop detection
 * @param path Path to directory
 * @param ctx Command context
 * @param print_current_header Whether to print directory header
 * @param visited Set of already-visited directory paths (for loop detection)
 * @return Result with success status
 */
auto list_directory_recursive(const std::string &path,
                              const CommandContext<LS_OPTIONS.size()> &ctx,
                              bool print_current_header,
                              std::set<std::string> &visited,
                              const std::wstring &lookup_override = {})
    -> cp::Result<bool> {
  // Check for symlink loops - normalize path for consistent comparison
  std::string normalized_path = make_generic_display_path(path);
  if (visited.count(normalized_path) > 0) {
    return true;  // Already visited - skip to prevent infinite recursion
  }
  visited.insert(normalized_path);
  // GNU ls prints a directory header for every directory in recursive mode,
  // including a single command-line directory operand.
  if (print_current_header) {
    const std::string display_path = make_generic_display_path(path);
    if (dired_requested(ctx)) {
      const uint64_t begin = stdout_bytes_written();
      g_subdired_offsets.push_back({begin, begin + display_path.size()});
    }
    safePrintLn(utf8_to_wstring(display_path) + L":");
  }

  // List current directory
  auto result = list_directory(path, ctx, lookup_override);
  if (!result) {
    return result;
  }

  // Collect subdirectories for recursion
  std::wstring wpath = utf8_to_wstring(path);
  const auto lookup_operand = native_path::make_api_path_operand_w(
      normalize_lookup_path(lookup_override.empty() ? wpath : lookup_override));
  // join_w for the same root-operand reason as list_directory (#1145).
  std::wstring search_path =
      native_path::join_w(lookup_operand.normalized, L"*");
  search_path = native_path::make_api_path_operand_w(search_path).extended;

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return true;  // No entries
  }

  std::vector<std::pair<std::string, std::wstring>> subdirs;
  const std::wstring ignore_pattern = get_ignore_pattern(ctx);
  const std::wstring hide_pattern = get_hide_pattern(ctx);
  const bool follow_directory_symlinks =
      resolve_dereference_mode(ctx) == DereferenceMode::All;
  do {
    std::wstring filename = find_data.cFileName;

    if (filename == L"." || filename == L"..") {
      continue;
    }

    const bool recurse_into_entry =
        is_plain_directory(find_data) ||
        (follow_directory_symlinks &&
         (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
         (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    if (!recurse_into_entry) {
      continue;
    }

    if (!should_show_entry(filename, find_data, ctx, ignore_pattern,
                           hide_pattern)) {
      continue;
    }

    std::wstring full_path = native_path::join_w(wpath, filename);
    std::string subdir_path =
        make_generic_display_path(wstring_to_utf8(full_path));
    // Skip if already visited (symlink loop detection)
    if (visited.count(subdir_path) > 0) {
      continue;
    }
    subdirs.emplace_back(
        subdir_path, native_path::join_w(lookup_operand.normalized, filename));
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  // Recursively list subdirectories.  GNU ls separates each directory
  // section with a blank line before the next header.
  for (const auto &[subdir, lookup_subdir] : subdirs) {
    safePrintLn(L"");
    auto subdir_result =
        list_directory_recursive(subdir, ctx, true, visited, lookup_subdir);
    if (!subdir_result) {
      return subdir_result;
    }
  }

  return true;
}

auto print_ls_error(const std::string &message) -> void {
  safeErrorPrintLn(std::wstring(L"ls: ") + utf8_to_wstring(message));
}

auto directory_only_requested(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  return ctx.get<bool>("-d", false) || ctx.get<bool>("--directory", false);
}

auto should_follow_command_line_directory_symlink(
    const CommandContext<LS_OPTIONS.size()> &ctx) -> bool {
  if (resolve_dereference_mode(ctx) != DereferenceMode::None) {
    return true;
  }

  if (long_format_requested(ctx)) {
    return false;
  }

  if (ctx.get<bool>("-F", false) || ctx.get<bool>("--classify", false)) {
    return false;
  }

  return true;
}

auto recursive_requested(const CommandContext<LS_OPTIONS.size()> &ctx) -> bool {
  return ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);
}

struct ExpandedPathOperands {
  std::vector<std::string> paths;
  bool success = true;
  size_t logical_operand_count = 0;
};

// [GNU] An operand containing '*' or '?' can never name a real file on
// Windows: those characters are illegal in file names, but FindFirstFileW
// treats them as wildcard metacharacters and would "find" an unrelated
// match.  When glob expansion left such an operand untouched (the shell owns
// expansion, or the pattern matched nothing) it must be reported missing
// instead of fabricating a listing for the literal pattern text.
auto contains_unmatchable_wildcard(std::wstring_view path) -> bool {
  return path.find(L'*') != std::wstring_view::npos ||
         path.find(L'?') != std::wstring_view::npos;
}

// [GNU] An operand whose last component is empty (a trailing '/' or '\\',
// e.g. "dir/") or is "." / ".." requires traversal through the named file as
// a directory.  On a regular file GNU ls fails with ENOTDIR rather than
// listing the file.
auto operand_requires_directory(std::wstring_view path) -> bool {
  size_t end = path.size();
  while (end > 0 && (path[end - 1] == L'/' || path[end - 1] == L'\\')) {
    --end;
  }
  if (end != path.size()) {
    return true;
  }
  if (end == 0 || path[end - 1] != L'.') {
    return false;
  }
  if (end == 1) {
    return true;  // "."
  }
  const wchar_t prev = path[end - 2];
  if (prev == L'/' || prev == L'\\') {
    return true;  // "/."
  }
  if (prev == L'.') {
    if (end == 2) {
      return true;  // ".."
    }
    const wchar_t prev2 = path[end - 3];
    if (prev2 == L'/' || prev2 == L'\\') {
      return true;  // "/.."
    }
  }
  return false;
}

// [GNU] Trailing-slash/dotted operands follow symlinks: "link-to-dir/" is a
// directory while "link-to-file/" is ENOTDIR, so directory-ness is tested on
// the resolved target, not the link itself.
auto resolved_operand_is_directory(std::wstring_view path) -> bool {
  // Probe through the API-path boundary: std::filesystem::status folds a
  // POSIX drive-form operand against the current drive root and answers for
  // the wrong path (#1145).
  const auto operand = native_path::make_api_path_operand_w(path);
  return native_path::attributes_are_directory(
      native_path::attributes_w(operand.extended));
}

auto expand_path_operands(const std::vector<std::string> &paths)
    -> ExpandedPathOperands {
  std::vector<std::string> expanded_paths;
  bool success = true;
  size_t logical_operand_count = 0;

  for (const auto &path : paths) {
    std::wstring wpath = utf8_to_wstring(path);
    if (contains_wildcard(wpath)) {
      auto glob_result = glob_expand(wpath);
      if (glob_result.expanded && !glob_result.files.empty()) {
        logical_operand_count += glob_result.files.size();
        for (const auto &file : glob_result.files) {
          expanded_paths.push_back(wstring_to_utf8(file));
        }
        continue;
      }

      ++logical_operand_count;
      if (contains_unmatchable_wildcard(wpath)) {
        safeErrorPrintLn(std::wstring(L"ls: cannot access '") +
                         utf8_to_wstring(path) +
                         L"': No such file or directory");
        success = false;
        continue;
      }
      // A bracket-class-only operand may still name a literal file ('[' and
      // ']' are legal in Windows file names); fall through and probe it.
    }

    auto probe = probe_path(wpath);
    ++logical_operand_count;
    if (probe.attributes_valid || probe.found) {
      if (operand_requires_directory(wpath) &&
          !resolved_operand_is_directory(wpath)) {
        // [GNU] "ls file/" fails with ENOTDIR instead of listing "file/".
        safeErrorPrintLn(std::wstring(L"ls: cannot access '") +
                         utf8_to_wstring(path) + L"': Not a directory");
        success = false;
        continue;
      }
      expanded_paths.push_back(path);
    } else {
      safeErrorPrintLn(std::wstring(L"ls: cannot access '") +
                       utf8_to_wstring(path) + L"': No such file or directory");
      success = false;
    }
  }

  return {std::move(expanded_paths), success, logical_operand_count};
}

/**
 * @brief Process all paths
 * @param paths Paths to process
 * @param ctx
 * Command context
 * @return Result with success status
 */
auto process_paths(const std::vector<std::string> &paths,
                   const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  auto expanded = expand_path_operands(paths);
  auto &expanded_paths = expanded.paths;
  bool success = expanded.success;
  bool printed_any = false;
  const bool multiple_operands = expanded.logical_operand_count > 1;

  bool all_file_operands = !expanded_paths.empty();
  for (const auto &path : expanded_paths) {
    auto probe = probe_path(utf8_to_wstring(path));
    if (!probe.attributes_valid ||
        (probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      all_file_operands = false;
      break;
    }
  }
  if (all_file_operands) {
    std::sort(expanded_paths.begin(), expanded_paths.end());
    if (ctx.get<bool>("-r", false) || ctx.get<bool>("--reverse", false)) {
      std::reverse(expanded_paths.begin(), expanded_paths.end());
    }
  } else if (expanded_paths.size() > 1) {
    // [GNU coreutils] compare_qsort: non-directory operands are sorted and
    // printed first, then directory operands are sorted and printed after.
    // This matches `ls lib/**` where glob expands to a mix of files and
    // directories: GNU prints all files up front, then each directory block.
    std::vector<std::string> file_operands;
    std::vector<std::string> dir_operands;
    for (const auto &path : expanded_paths) {
      auto probe = probe_path(utf8_to_wstring(path));
      if (probe.attributes_valid &&
          (probe.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        dir_operands.push_back(path);
      } else {
        file_operands.push_back(path);
      }
    }
    std::sort(file_operands.begin(), file_operands.end());
    std::sort(dir_operands.begin(), dir_operands.end());
    if (ctx.get<bool>("-r", false) || ctx.get<bool>("--reverse", false)) {
      std::reverse(file_operands.begin(), file_operands.end());
      std::reverse(dir_operands.begin(), dir_operands.end());
    }
    expanded_paths.clear();
    expanded_paths.insert(expanded_paths.end(), file_operands.begin(),
                          file_operands.end());
    expanded_paths.insert(expanded_paths.end(), dir_operands.begin(),
                          dir_operands.end());
  }

  for (const auto &path : expanded_paths) {
    // Check if path exists and determine its type
    std::wstring wpath = utf8_to_wstring(path);
    auto probe = probe_path(wpath);

    if (!probe.attributes_valid && !probe.found) {
      success = false;
      continue;
    }

    const DWORD effective_attributes = probe.attributes_valid
                                           ? probe.attributes
                                           : probe.find_data.dwFileAttributes;
    const bool is_directory =
        (effective_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool is_reparse_point =
        (effective_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    const bool is_directory_symlink = is_directory && is_reparse_point;
    const bool follow_command_line_directory_symlink =
        probe.attributes_valid && is_directory_symlink &&
        should_follow_command_line_directory_symlink(ctx);
    const bool treat_as_directory =
        probe.attributes_valid && is_directory &&
        !directory_only_requested(ctx) &&
        (!is_directory_symlink || follow_command_line_directory_symlink);

    if (treat_as_directory) {
      if (printed_any) {
        safePrintLn(L"");
      }

      if (recursive_requested(ctx)) {
        std::set<std::string> visited;
        auto result = list_directory_recursive(path, ctx, true, visited);
        if (!result) {
          print_ls_error(std::string(result.error()));
          success = false;
        }
      } else {
        if (multiple_operands) {
          if (dired_requested(ctx)) {
            const uint64_t begin = stdout_bytes_written();
            g_subdired_offsets.push_back({begin, begin + path.size()});
            g_dired_offsets.push_back({begin, begin + path.size()});
          }
          safePrintLn(utf8_to_wstring(path) + L":");
        }

        auto result = list_directory(path, ctx);
        if (!result) {
          print_ls_error(std::string(result.error()));
          success = false;
        }
      }
    } else {
      auto result = list_file(path, ctx);
      if (!result) {
        print_ls_error(std::string(result.error()));
        success = false;
      }
    }

    printed_any = true;
  }

  return success;
}

/**
 * @brief Main pipeline
 * @param ctx Command context
 * @return Result with success status
 */
template <size_t N>
auto process_command(const CommandContext<N> &ctx) -> cp::Result<bool> {
  auto time_style_selection = resolve_time_style(ctx);
  if (!time_style_selection) {
    return std::unexpected(time_style_selection.error());
  }

  return validate_arguments(ctx).and_then(
      [&](const std::vector<std::string> &paths) {
        return process_paths(paths, ctx);
      });
}

}  // namespace ls_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    ls,
    /* cmd_name */ "ls",
    /* cmd_synopsis */ "list directory contents",
    /* cmd_desc */
    "List information about the FILEs (the current directory by default).\n"
    "Sort entries alphabetically if none of -cftuvSUX nor --sort is "
    "specified.\n"
    "\n"
    "With no FILE, list the current directory contents. With a FILE that is a\n"
    "directory, list the files and subdirectories inside that directory.\n"
    "With a FILE that is not a directory, list just that file.\n",
    /* examples */
    "  ls                      List files in current directory\n"
    "  ls -l                   Long listing format\n"
    "  ls -la                  Long listing format including hidden files\n"
    "  ls -lh                  Long listing format with human-readable sizes",
    /* see_also */ "find(1), grep(1), sort(1), wc(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    LS_OPTIONS) {
  using namespace ls_pipeline;
  using namespace core::pipeline;

  // [GNU] --color[=WHEN]: validate the argument up front (ls.c:2156-
  // 2169 XARGMATCH) so an invalid or ambiguous value fails before any
  // listing output, like --hyperlink below (#1513).
  if (ctx.has("--color")) {
    auto color = ls_pipeline::resolve_color_mode(ctx);
    if (!color) {
      safeErrorPrintLn("ls: " + color.error());
      return 2;
    }
  }

  // [GNU] --hyperlink[=WHEN]: validate the argument up front so an invalid
  // value fails before any listing output.
  ls_pipeline::g_dired_offsets.clear();
  ls_pipeline::g_subdired_offsets.clear();
  if (ctx.has("--hyperlink")) {
    auto link = ls_pipeline::hyperlink_mode(ctx);
    if (!link) {
      safeErrorPrintLn("ls: " + link.error());
      safeErrorPrintLn("Valid arguments are:");
      safeErrorPrintLn("  - 'always', 'yes', 'force'");
      safeErrorPrintLn("  - 'never', 'no', 'none'");
      safeErrorPrintLn("  - 'auto', 'tty', 'if-tty'");
      return 1;
    }
    ls_pipeline::g_hyperlink_enabled = *link;
  } else {
    ls_pipeline::g_hyperlink_enabled = false;
  }
  // [GNU] --color / --indicator-style values go through XARGMATCH
  // (ls.c:2156-2160, 2186-2188): an invalid value is a hard error
  // (argmatch_die -> LS_FAILURE = 2) before any listing, never a
  // silent fall back to defaults.
  if (auto color_error = ls_pipeline::validate_color_option(ctx)) {
    safeErrorPrintLn("ls: " + *color_error);
    return 2;
  }
  if (auto style_error =
          ls_pipeline::validate_indicator_style_option(ctx)) {
    safeErrorPrintLn("ls: " + *style_error);
    return 2;
  }
  set_stdout_byte_counting(ls_pipeline::dired_requested(ctx));

  // [DIFFERS] -Z/--context: SELinux security context not applicable on Windows
  if (ctx.has("-Z") || ctx.has("--context")) {
    safeErrorPrintLn(
        winux::i18n::translate("command.ls.error.context-unsupported",
                               "ls: --context is not supported on Windows"));
    return 1;
  }

  auto result = process_command(ctx);
  if (ls_pipeline::dired_requested(ctx)) {
    // [GNU] trailers: //DIRED// name ranges, //SUBDIRED// directory header
    // ranges, then the //DIRED-OPTIONS// line.
    std::string trailer = "//DIRED//";
    for (const auto &[b, e] : ls_pipeline::g_dired_offsets) {
      trailer += " " + std::to_string(b) + " " + std::to_string(e);
    }
    trailer += "\n";
    if (!ls_pipeline::g_subdired_offsets.empty()) {
      trailer += "//SUBDIRED//";
      for (const auto &[b, e] : ls_pipeline::g_subdired_offsets) {
        trailer += " " + std::to_string(b) + " " + std::to_string(e);
      }
      trailer += "\n";
    }
    trailer += "//DIRED-OPTIONS// --quoting-style=literal\n";
    safePrint(trailer);
    set_stdout_byte_counting(false);
  }
  if (!result) {
    const std::string error = std::string(result.error());
    // [GNU] Bad --time-style values and an unparseable TIME_STYLE
    // environment variable are usage-class errors: exit 2.
    if (error.starts_with(kInvalidTimeStylePrefix) ||
        error.find("for 'time style'") != std::string::npos) {
      safeErrorPrint(error);
      safeErrorPrint("\n");
      return 2;
    }
    // [GNU] Bad command-line option values (e.g. "invalid tab size: '-1'")
    // are serious usage errors: exit 2, not 1 (uutils #12835). The
    // "invalid line width" diagnostic is LS_FAILURE in ls.c:2026 too.
    if (error.starts_with("invalid tab size") ||
        error.starts_with("invalid line width")) {
      report_error(result, L"ls");
      return 2;
    }
    report_error(result, L"ls");
    return 1;
  }

  // GNU/Microsoft-style ls uses exit 2 for serious runtime trouble such as
  // missing-path "cannot access" failures, and exit 1 for minor problems
  // such as entries inside a listed directory that could not be statted.
  // [GNU] ls.c registers close_stdout (ls.c:1661): a failed stdout write is
  // diagnosed ("ls: write error: <strerror>", closeout.c close_stdout) and
  // escalates the exit status to LS_FAILURE = 2 (#1368). A closed
  // downstream pipe keeps the quiet SIGPIPE-style status.
  if (int write_error_rc = report_stdout_write_error("ls", 2)) {
    return write_error_rc;
  }
  if (*result) {
    return g_had_minor_errors ? 1 : 0;
  }
  return 2;
}
