/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: ls.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
 * name; otherwise: sort by ctime, newest first [TODO]
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
 * [TODO]
 * - @a -L, @a --dereference: When showing file information for a symbolic link,
 * show information for the file the link references [TODO]
 * - @a -l, @a --long, @a --long-list: Use a long listing format [IMPLEMENTED]
 * - @a -m: Fill width with a comma separated list of entries [TODO]
 * - @a -n, @a --numeric-uid-gid: Like -l, but list numeric user and group IDs
 * [IMPLEMENTED]
 * - @a -N, @a --literal: Print entry names without quoting [TODO]
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
 * - @a -S: Sort by file size, largest first [TODO]
 * - @a -t: Sort by time, newest first [TODO]
 * - @a -T, @a --tabsize: Assume tab stops at each COLS instead of 8
 * [IMPLEMENTED]
 * - @a -u: With -lt: sort by, and show, access time; with -l: show access time
 * and sort by name; otherwise: sort by access time, newest first [TODO]
 * - @a -U: Do not sort; list entries in directory order [IMPLEMENTED]
 * - @a -v: Natural sort of (version) numbers within text [IMPLEMENTED]
 * - @a -w, @a --width: Set output width to COLS. 0 means no limit [IMPLEMENTED]
 * - @a -x: List entries by lines instead of by columns [TODO]
 * - @a -X: Sort alphabetically by entry extension [IMPLEMENTED]
 * - @a -Z, @a --context: Print any security context of each file [TODO]
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
    OPTION("-l", "--long-list", "use a long listing format"),
    OPTION("", "--long", "use a long listing format"),
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
    OPTION("-T", "--tabsize", "assume tab stops at each COLS instead of 8",
           INT_TYPE),
    OPTION(
        "-u", "",
        "with -lt: sort by, and show, access time; with -l: show access time "
        "and sort by name; otherwise: sort by access time, newest first"),
    OPTION("-U", "", "do not sort; list entries in directory order"),
    OPTION("-v", "", "natural sort of (version) numbers within text"),
    OPTION("-w", "--width", "set output width to COLS. 0 means no limit",
           INT_TYPE),
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
           STRING_TYPE),
    OPTION("-1", "", "list one file per line"),
    OPTION("-D", "--dired",
           "generate output designed for Emacs dired mode"),
    OPTION("-G", "--no-group",
           "in a long listing, don't print group names"),
    OPTION("", "--group-directories-first",
           "group directories before files"),
    OPTION("", "--author",
           "show author in long format"),
    OPTION("-H", "--dereference-command-line",
           "follow symlinks listed on the command line"),
    OPTION("", "--dereference-command-line-symlink-to-dir",
           "follow each command-line symlink to a directory"),
    OPTION("", "--dereference-command-line-symlinks-to-dir",
           "follow each command-line symlink to a directory"),
    OPTION("", "--hide",
           "do not list implied entries matching PATTERN", STRING_TYPE),
    OPTION("", "--hyperlink",
           "hyperlink file names when outputting to a terminal",
           OPTIONAL_STRING_TYPE),
    OPTION("", "--si",
           "like -h, but use powers of 1000 not 1024"),
    OPTION("", "--full-time",
           "like -l --time-style=full-iso"),
    OPTION("", "--time-style",
           "time/date format with -l (e.g. full-iso, long-iso, iso, locale, +FORMAT)",
           STRING_TYPE),
    OPTION("", "--zero",
           "end each output line with NUL instead of newline")};

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

enum class SortMode { Name, Size, Time, Version, Extension, None };
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
  ShellEscapeAlways
};

enum class DereferenceMode {
  None,
  CommandLine,
  CommandLineDirectories,
  All
};

auto resolve_dereference_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> DereferenceMode;
auto should_dereference_entry_metadata(
    DereferenceMode mode, const WIN32_FIND_DATAW &find_data,
    bool command_line_operand) -> bool;

struct SizeConfig {
  SizeMode file_mode = SizeMode::Bytes;
  SizeMode block_mode = SizeMode::Bytes;
  uint64_t file_block_size = 1;
  uint64_t block_size = 1;
};

auto get_terminal_width() -> int;
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

auto probe_path(const std::wstring &path) -> PathProbe {
  PathProbe probe{};
  const std::wstring lookup_path = normalize_lookup_path(path);

  probe.attributes = GetFileAttributesW(lookup_path.c_str());
  probe.attributes_valid = probe.attributes != INVALID_FILE_ATTRIBUTES;

  HANDLE hfind = FindFirstFileW(lookup_path.c_str(), &probe.find_data);
  if (hfind != INVALID_HANDLE_VALUE) {
    probe.found = true;
    FindClose(hfind);
  }

  return probe;
}

auto normalize_metadata_probe_path(const std::wstring &path) -> std::wstring {
  try {
    return std::filesystem::absolute(std::filesystem::path(path))
        .lexically_normal()
        .native();
  } catch (...) {
    return path;
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

auto get_color_for_entry(const std::wstring &name,
                         const WIN32_FIND_DATAW &find_data)
    -> std::wstring_view {
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
  return COLOR_FILE;
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

  return rendered;
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
  return std::nullopt;
}

auto resolve_quoting_mode(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<QuotingMode> {
  QuotingMode mode = QuotingMode::Literal;
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-N" || meta.long_name == "--literal" ||
        meta.long_name == "--show-control-chars") {
      mode = QuotingMode::Literal;
      continue;
    }

    if (meta.short_name == "-b" || meta.long_name == "--escape") {
      mode = QuotingMode::Escape;
      continue;
    }

    if (meta.short_name == "-q" || meta.long_name == "--hide-control-chars") {
      mode = QuotingMode::HideControl;
      continue;
    }

    if (meta.short_name == "-Q" || meta.long_name == "--quote-name") {
      mode = QuotingMode::C;
      continue;
    }

    if (meta.long_name == "--quoting-style") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) return std::unexpected("invalid quoting style");
      auto parsed = parse_quoting_mode(*value);
      if (!parsed) return std::unexpected("invalid quoting style");
      mode = *parsed;
    }
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
  }

  return rendered;
}

auto should_dereference_metadata(
    const WIN32_FIND_DATAW &find_data,
    const CommandContext<LS_OPTIONS.size()> &ctx, bool command_line_operand)
    -> bool {
  return should_dereference_entry_metadata(resolve_dereference_mode(ctx),
                                           find_data, command_line_operand);
}

auto read_symlink_display_target(
    const std::wstring &full_path, const WIN32_FIND_DATAW &find_data,
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
  std::filesystem::path target = std::filesystem::read_symlink(
      std::filesystem::path(full_path), ec);
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

    std::wstring parent_native =
        std::filesystem::path(full_path).parent_path().lexically_normal().native();
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

auto build_display_name_parts(
    const std::wstring &name, const std::wstring &full_path,
    const WIN32_FIND_DATAW &find_data,
    const CommandContext<LS_OPTIONS.size()> &ctx,
    bool command_line_operand = false) -> DisplayNameParts {
  auto quoting_result = resolve_quoting_mode(ctx);
  QuotingMode quoting = quoting_result ? *quoting_result : QuotingMode::Literal;
  std::wstring rendered = apply_quoting_mode(name, quoting);

  auto target =
      read_symlink_display_target(full_path, find_data, ctx, command_line_operand);
  if (target) {
    target->display = apply_quoting_mode(target->display, quoting);
  }
  if (!target) {
    rendered += get_indicator_suffix(name, find_data, ctx);
  }

  if (target) {
    rendered += get_indicator_suffix(name, find_data, ctx);
  }

  return {std::move(rendered), std::move(target)};
}

auto render_display_name(const std::wstring &name, const std::wstring &full_path,
                         const WIN32_FIND_DATAW &find_data,
                         const CommandContext<LS_OPTIONS.size()> &ctx,
                         bool command_line_operand = false)
    -> std::wstring {
  auto parts =
      build_display_name_parts(name, full_path, find_data, ctx, command_line_operand);
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
                        bool color_enabled,
                        bool command_line_operand = false) -> void {
  auto parts =
      build_display_name_parts(name, full_path, find_data, ctx, command_line_operand);

  if (!color_enabled) {
    safePrint(wstring_to_utf8(parts.rendered_name));
    if (parts.target) {
      safePrint(" -> ");
      safePrint(wstring_to_utf8(parts.target->display));
    }
    return;
  }

  safePrint(get_color_for_entry(name, find_data));
  safePrint(wstring_to_utf8(parts.rendered_name));
  safePrint(COLOR_RESET);

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

auto resolve_display_entry(
    const EntryInfo &entry, const CommandContext<LS_OPTIONS.size()> &ctx)
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
  if (entry.name == L"." && resolve_dereference_mode(ctx) == DereferenceMode::All &&
      (display_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    // In a dereferenced directory-symlink view, Microsoft Coreutils reports the
    // synthetic "." entry as the target directory rather than as a link.
    display_find_data.dwFileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
    display_find_data.dwReserved0 = 0;
  }
  return {display_find_data, metadata_name};
}

auto compare_version_strings(std::wstring_view a, std::wstring_view b) -> int {
  size_t i = 0;
  size_t j = 0;

  auto is_digit = [](wchar_t ch) {
    return std::iswdigit(static_cast<wint_t>(ch)) != 0;
  };

  while (i < a.size() || j < b.size()) {
    const bool a_digit = i < a.size() && is_digit(a[i]);
    const bool b_digit = j < b.size() && is_digit(b[j]);

    if (a_digit && b_digit) {
      size_t a_start = i;
      size_t b_start = j;
      while (i < a.size() && is_digit(a[i])) ++i;
      while (j < b.size() && is_digit(b[j])) ++j;

      while (a_start < i && a[a_start] == L'0') ++a_start;
      while (b_start < j && b[b_start] == L'0') ++b_start;

      const size_t a_len = i - a_start;
      const size_t b_len = j - b_start;
      if (a_len < b_len) return -1;
      if (a_len > b_len) return 1;

      for (size_t k = 0; k < a_len; ++k) {
        const wchar_t ac = a[a_start + k];
        const wchar_t bc = b[b_start + k];
        if (ac < bc) return -1;
        if (ac > bc) return 1;
      }
      continue;
    }

    if (a_digit != b_digit) {
      return a_digit ? -1 : 1;
    }

    if (i < a.size() && j < b.size()) {
      const wchar_t ac = a[i];
      const wchar_t bc = b[j];
      if (ac != bc) {
        const wchar_t al = std::towlower(ac);
        const wchar_t bl = std::towlower(bc);
        if (al < bl) return -1;
        if (al > bl) return 1;
        if (ac < bc) return -1;
        if (ac > bc) return 1;
      }
      ++i;
      ++j;
      continue;
    }

    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
  }

  return 0;
}

auto compare_extensions(const EntryInfo &a, const EntryInfo &b) -> bool {
  const std::wstring a_ext = get_entry_extension(a.name);
  const std::wstring b_ext = get_entry_extension(b.name);
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
  if (value == "none") return SortMode::None;
  return std::nullopt;
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

auto parse_indicator_style(std::string_view value)
    -> std::optional<IndicatorStyle> {
  if (value.empty() || value == "none") return IndicatorStyle::None;
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

thread_local std::string g_ls_dynamic_error;

auto store_ls_dynamic_error(std::string message) -> std::string_view {
  g_ls_dynamic_error = std::move(message);
  return g_ls_dynamic_error;
}

auto build_invalid_time_style_message(std::string_view value)
    -> std::string_view {
  return store_ls_dynamic_error(
      "invalid --time-style argument '" + std::string(value) +
      "'\nPossible values are:\n"
      "  - [posix-]full-iso\n"
      "  - [posix-]long-iso\n"
      "  - [posix-]iso\n"
      "  - [posix-]locale\n"
      "  - +FORMAT (e.g., +%H:%M) for a 'date'-style format\n"
      "\nFor more information try --help");
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
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.short_name == "-m" || meta.short_name == "-x" ||
        meta.short_name == "-1" || meta.short_name == "-C" ||
        meta.short_name == "-l" || meta.short_name == "-g" ||
        meta.short_name == "-n" || meta.short_name == "-o" ||
        meta.long_name == "--long-list" || meta.long_name == "--long" ||
        meta.long_name == "--format") {
      return true;
    }
  }

  return false;
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

auto should_dereference_entry_metadata(
    DereferenceMode mode, const WIN32_FIND_DATAW &find_data,
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
        meta.short_name == "-n" || meta.short_name == "-o" ||
        meta.long_name == "--long-list" || meta.long_name == "--long") {
      mode = FormatMode::Long;
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

auto apply_default_output_format(
    FormatMode mode, const CommandContext<LS_OPTIONS.size()> &ctx)
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
  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];

    if (meta.long_name == "--full-time") {
      selection.style = TimeStyle::FullIso;
      selection.format.clear();
      continue;
    }

    if (meta.long_name != "--time-style") continue;

    auto value = std::get_if<std::string>(&occurrence.value);
    if (!value) return std::unexpected(build_invalid_time_style_message(""));
    if (!value->empty() && (*value)[0] == '+') {
      selection.style = TimeStyle::CustomFormat;
      selection.format = value->substr(1);
      continue;
    }

    auto parsed = parse_time_style(*value);
    if (!parsed) return std::unexpected(build_invalid_time_style_message(*value));
    selection.style = *parsed;
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
      auto parsed = parse_sort_mode(*value);
      if (!parsed) return std::unexpected("invalid sort value");
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

  constexpr uint64_t kTicksPerSecond = 10'000'000ULL;
  constexpr uint64_t kTicksPerHour = 60ULL * 60ULL * kTicksPerSecond;
  constexpr uint64_t kTicksPerDay = 24ULL * kTicksPerHour;
  constexpr uint64_t kRecentWindowTicks = (365ULL / 2ULL) * kTicksPerDay;

  if (file_value.QuadPart > now_value.QuadPart) {
    return (file_value.QuadPart - now_value.QuadPart) <= kTicksPerHour;
  }
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

  int64_t delta_ticks =
      static_cast<int64_t>(local_value.QuadPart) -
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
      translated += std::to_string(value.QuadPart / 10'000'000ULL - 11644473600ULL);
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
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02d %02d:%02d:%02d.%09llu %s", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond,
             static_cast<unsigned long long>(nanos), offset.c_str());
    return std::string(buf);
  }
  if (selection.style == TimeStyle::LongIso) {
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute);
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
    return format_strftime_like(st, file_time,
                                resolve_custom_time_format(file_time,
                                                           selection.format));
  }
  snprintf(buf, sizeof(buf), "%s %2d %02d:%02d", month_abbrs[st.wMonth],
           st.wDay, st.wHour, st.wMinute);
  return std::string(buf);
}

auto make_generic_display_path(std::string path) -> std::string {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

auto resolve_color_enabled(const CommandContext<LS_OPTIONS.size()> &ctx)
    -> bool {
  std::string color_option = ctx.get<std::string>("--color", "auto");
  if (color_option == "never") return false;
  if (color_option == "always") return true;
  return ls_is_terminal(stdout);
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
};

auto render_inline_entry(const EntryInfo &entry,
                         const CommandContext<LS_OPTIONS.size()> &ctx,
                         bool color_enabled) -> RenderedEntry {
  auto [display_find_data, metadata_name] = resolve_display_entry(entry, ctx);
  std::wstring display_name = render_display_name(
      entry.name, entry.full_path, display_find_data, ctx,
      entry.command_line_operand);
  std::string prefix =
      build_listing_prefix(entry.full_path, display_find_data, ctx);
  std::string text = prefix;
  if (color_enabled) {
    text += wstring_to_utf8(
        std::wstring(get_color_for_entry(metadata_name, display_find_data)));
  }
  text += wstring_to_utf8(display_name);
  if (color_enabled) {
    text += wstring_to_utf8(COLOR_RESET);
  }

  return {std::move(text), prefix.size() + display_name.size()};
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
                            size_t width) -> void {
  size_t line_width = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) {
      const size_t separator_reserve =
          2 + ((i + 1) < entries.size() ? static_cast<size_t>(1) : 0);
      if (line_width > 0 &&
          line_width + separator_reserve + entries[i].visible_width > width) {
        safePrint(",\n");
        line_width = 0;
      } else {
        safePrint(", ");
        line_width += 2;
      }
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

  while (current_column < target_column) {
    size_t next_tab_stop =
        ((current_column / static_cast<size_t>(tab_size)) + 1) *
        static_cast<size_t>(tab_size);
    if (next_tab_stop <= target_column && next_tab_stop > current_column) {
      safePrint("\t");
      current_column = next_tab_stop;
      continue;
    }

    safePrint(" ");
    ++current_column;
  }
}

auto resolve_tab_size(const CommandContext<LS_OPTIONS.size()> &ctx) -> int {
  int tab_size = ls_constants::DEFAULT_TAB_SIZE;

  for (const auto &occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LS_OPTIONS.size()) continue;
    const auto &meta = LS_OPTIONS[occurrence.index];
    if (meta.short_name != "-T" && meta.long_name != "--tabsize") continue;

    const auto *value = std::get_if<int>(&occurrence.value);
    if (!value || *value <= 0) {
      tab_size = ls_constants::DEFAULT_TAB_SIZE;
      continue;
    }

    tab_size = *value;
  }

  return tab_size;
}

auto print_grid(const std::vector<EntryInfo> &entries,
                const CommandContext<LS_OPTIONS.size()> &ctx,
                bool across_layout) -> void {
  if (entries.empty()) return;

  const auto rendered = build_rendered_entries(entries, ctx);
  size_t max_visible_width = 0;
  for (const auto &entry : rendered) {
    max_visible_width = std::max(max_visible_width, entry.visible_width);
  }

  int width = ctx.get<int>("-w", 0);
  if (width <= 0) {
    width = ctx.get<int>("--width", 0);
  }
  if (width <= 0) {
    width = get_terminal_width();
  }

  int cols = (width + 2) / (static_cast<int>(max_visible_width) + 2);
  if (cols < 1) cols = 1;
  int rows = static_cast<int>((entries.size() + cols - 1) / cols);
  std::vector<size_t> col_widths(static_cast<size_t>(cols), max_visible_width);
  std::vector<size_t> col_starts(static_cast<size_t>(cols), 0);

  for (size_t idx = 0; idx < rendered.size(); ++idx) {
    size_t col = across_layout ? idx % static_cast<size_t>(cols)
                               : idx / static_cast<size_t>(rows);
    col_widths[col] = std::max(col_widths[col], rendered[idx].visible_width);
  }

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

      safePrint(rendered[index].text);
      current_column += rendered[index].visible_width;
      bool has_later_entry_in_row = false;
      for (int next_col = col + 1; next_col < cols; ++next_col) {
        size_t next_index =
            across_layout ? static_cast<size_t>(row * cols + next_col)
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
  if (is_reparse_link(find_data)) {
    perms[0] = 'l';  // Symbolic link / junction style display
  } else if (is_plain_directory(find_data)) {
    perms[0] = 'd';
  } else {
    perms[0] = '-';
  }

  const bool read_only =
      (find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  const char write_char = read_only ? '-' : 'w';

  // Match Microsoft coreutils' Windows approximation:
  // every non-link entry is readable and "searchable/executable" for all,
  // while the read-only attribute removes the write bit triplet-wide.
  perms[1] = 'r';
  perms[2] = write_char;
  perms[3] = 'x';
  perms[4] = 'r';
  perms[5] = write_char;
  perms[6] = 'x';
  perms[7] = 'r';
  perms[8] = write_char;
  perms[9] = 'x';

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

  std::error_code ec;
  std::filesystem::path target = std::filesystem::canonical(path, ec);
  if (ec) {
    return std::nullopt;
  }

  WIN32_FILE_ATTRIBUTE_DATA attrs{};
  if (!GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &attrs)) {
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
      if (!value) return std::unexpected("invalid block size");

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
      if (!parsed) return std::unexpected("invalid block size");
      cfg.file_mode = SizeMode::Blocks;
      cfg.file_block_size = *parsed;
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
  HANDLE handle =
      CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  FILE_STANDARD_INFO info{};
  const bool ok =
      GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info)) !=
      FALSE;
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
  if (is_symbolic_reparse_link(original_find_data) &&
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
                           bool command_line_operand = false)
    -> std::string {
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
      CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
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
                    bool command_line_operand = false)
    -> unsigned long {
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
      CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
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

auto get_allocated_block_count(const std::wstring &path,
                               const WIN32_FIND_DATAW &find_data,
                               const SizeConfig &size_cfg) -> uint64_t {
  return ceil_div(get_allocated_size_bytes(path, find_data),
                  size_cfg.block_size);
}

auto get_allocated_blocks_string(const std::wstring &path,
                                 const WIN32_FIND_DATAW &find_data,
                                 const SizeConfig &size_cfg) -> std::string {
  return format_scaled_size(get_allocated_size_bytes(path, find_data),
                            size_cfg.block_mode, size_cfg.block_size);
}

auto print_directory_total_line(const std::vector<EntryInfo> &entries,
                                const SizeConfig &size_cfg,
                                const CommandContext<LS_OPTIONS.size()> &ctx)
    -> void {
  uint64_t total_blocks = 0;
  uint64_t total_allocated_bytes = 0;

  for (const auto &entry : entries) {
    auto [display_find_data, _metadata_name] = resolve_display_entry(entry, ctx);
    total_blocks +=
        get_allocated_block_count(entry.full_path, display_find_data, size_cfg);
    total_allocated_bytes +=
        get_allocated_size_bytes(entry.full_path, display_find_data);
  }

  safePrint("total ");
  if (size_cfg.block_mode == SizeMode::Blocks) {
    safePrint(std::to_string(total_blocks));
  } else {
    safePrint(format_scaled_size(total_allocated_bytes, size_cfg.block_mode,
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
  if (ctx.get<bool>("-s", false) || ctx.get<bool>("--size", false)) {
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
auto get_file_owner_and_group(bool use_numeric = false)
    -> std::pair<std::string, std::string> {
  if (use_numeric) {
    // Get Windows SID (simulate UID/GID)
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
      DWORD bufferSize = 0;
      GetTokenInformation(hToken, TokenUser, nullptr, 0, &bufferSize);
      std::vector<BYTE> buffer(bufferSize);
      PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
      if (GetTokenInformation(hToken, TokenUser, pTokenUser, bufferSize,
                              &bufferSize)) {
        LPWSTR sidStr = nullptr;
        if (ConvertSidToStringSidW(pTokenUser->User.Sid, &sidStr)) {
          // Extract numeric part from SID (simulate UID 197121)
          std::wstring sid(sidStr,
                           wcslen(sidStr));  // Construct from known length
          LocalFree(sidStr);

          size_t lastDash = sid.find_last_of(L'-');
          std::wstring uid_wstr = (lastDash != std::wstring::npos)
                                      ? sid.substr(lastDash + 1)
                                      : L"197121";

          // Convert wide string to UTF-8
          std::string uid = wstring_to_utf8(uid_wstr);
          CloseHandle(hToken);
          return {uid, uid};  // UID = GID (Windows default)
        }
      }
      CloseHandle(hToken);
    }
    return {"197121", "197121"};  // Fallback
  }

  // Return username using ANSI version for efficiency
  char username[UNLEN + 1];
  DWORD username_len = UNLEN + 1;
  if (!GetUserNameA(username, &username_len)) {
    return {"user", "group"};
  }

  std::string username_str = username;
  size_t pos = username_str.find('\\');
  if (pos != std::string::npos) {
    username_str = username_str.substr(pos + 1);
  }
  return {username_str, username_str};
}

/**
 * @brief Get terminal width
 * @return Terminal width in columns
 */
auto get_terminal_width() -> int {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }
  return 80;  // Default to 80 columns if we can't get terminal width
}

/**
 * @brief Get string display width (simplified, assumes 1 character = 1 column)
 * @param str String to measure
 * @return Display width in columns
 */
auto string_display_width(const std::wstring &str) -> size_t {
  return str.length();
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
 * @brief Print entries in column format
 * @param entries List of entries
 * @param ctx Command context
 */
auto print_columns(const std::vector<EntryInfo> &entries,
                   const CommandContext<LS_OPTIONS.size()> &ctx) {
  if (entries.empty()) {
    return;
  }

  std::vector<std::string> prefixes;
  std::vector<std::wstring> display_names;
  prefixes.reserve(entries.size());
  display_names.reserve(entries.size());
  for (const auto &entry : entries) {
    auto [display_find_data, metadata_name] = resolve_display_entry(entry, ctx);
    prefixes.push_back(
        build_listing_prefix(entry.full_path, display_find_data, ctx));
    display_names.push_back(render_display_name(
        entry.name, entry.full_path, display_find_data, ctx,
        entry.command_line_operand));
  }

  // Check if color is enabled based on --color option
  bool color_enabled = true;  // Default to enabled
  std::string color_option = ctx.get<std::string>("--color", "auto");
  if (color_option == "never") {
    color_enabled = false;
  } else if (color_option == "auto") {
    color_enabled = ls_is_terminal(stdout);
  }

  // Get terminal width or use specified width
  int width = ctx.get<int>("-w", 0);
  if (width <= 0) {
    width = ctx.get<int>("--width", 0);
  }
  if (width <= 0) {
    width = get_terminal_width();
  }

  // Calculate column widths to fill the terminal
  size_t max_display_width = 0;
  for (size_t i = 0; i < display_names.size(); ++i) {
    const auto &entry = display_names[i];
    max_display_width = std::max(
        max_display_width, prefixes[i].size() + string_display_width(entry));
  }

  // Calculate base column width
  int base_col_width = static_cast<int>(max_display_width) + 2;
  if (base_col_width <= 0) {
    base_col_width = 1;
  }

  int cols = width / base_col_width;
  if (cols < 1) cols = 1;
  int rows = (entries.size() + cols - 1) / cols;

  // Calculate total width used by all columns
  int total_used_width = cols * base_col_width;
  int remaining_space = width - total_used_width;

  // Distribute remaining space among columns
  std::vector<int> col_widths(cols, base_col_width);
  if (remaining_space > 0 && cols > 0) {
    int extra_per_col = remaining_space / cols;
    int extra_remaining = remaining_space % cols;

    for (int i = 0; i < cols; ++i) {
      col_widths[i] += extra_per_col;
      if (i < extra_remaining) {
        col_widths[i] += 1;
      }
    }
  }

  // Print entries in columns
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      size_t index = row + col * rows;
      if (index < entries.size()) {
        const auto &entry = entries[index];
        const auto &prefix = prefixes[index];
        const auto &display_name = display_names[index];

        safePrint(std::string_view(prefix));
        if (color_enabled) {
          safePrint(get_color_for_entry(entry.name, entry.find_data));
        }

        safePrint(wstring_to_utf8(display_name));
        if (color_enabled) {
          safePrint(COLOR_RESET);
        }

        if (col < cols - 1) {
          size_t current_width =
              prefix.size() + string_display_width(display_name);
          int spaces_needed = col_widths[col] - static_cast<int>(current_width);
          if (spaces_needed > 0) {
            for (int i = 0; i < spaces_needed; ++i) {
              safePrint(L" ");
            }
          } else {
            safePrint(L"  ");
          }
        }
      }
    }
    safePrintLn(L"");
  }
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

/**
 * @brief List directory contents
 * @param path Path to directory
 * @param ctx Command context
 * @return Result with success status
 */
auto list_directory(const std::string &path,
                    const CommandContext<LS_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);
  const std::wstring lookup_wpath = normalize_lookup_path(wpath);

  // Check -d option: list directories themselves, not their contents
  bool list_dir_only =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--directory", false);

  if (list_dir_only) {
    // Get directory attributes
    WIN32_FIND_DATAW dir_data;
    HANDLE hFind = FindFirstFileW(lookup_wpath.c_str(), &dir_data);

    if (hFind == INVALID_HANDLE_VALUE) {
      return std::unexpected("cannot access '" + path +
                             "': No such file or directory");
    }

    // Display directory itself as a file
    FindClose(hFind);
    return list_file(path, ctx);
  }

  // Normal directory listing
  std::wstring search_path = lookup_wpath + L"\\*";
  const std::wstring ignore_pattern = get_ignore_pattern(ctx);
  const std::wstring hide_pattern = get_hide_pattern(ctx);

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return std::unexpected("cannot access '" + path +
                           "': No such file or directory");
  }

  std::vector<EntryInfo> entries;
  do {
    std::wstring filename = find_data.cFileName;
    if (!should_show_entry(filename, find_data, ctx, ignore_pattern,
                           hide_pattern)) {
      continue;
    }
    entries.push_back({filename, wpath + L"\\" + filename, find_data, false});
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  auto size_cfg_result = configure_sizes(ctx);
  if (!size_cfg_result) return std::unexpected(size_cfg_result.error());
  SizeConfig size_cfg = *size_cfg_result;
  auto time_selection = resolve_time_mode(ctx);
  if (!time_selection) return std::unexpected(time_selection.error());
  auto time_style_selection = resolve_time_style(ctx);
  if (!time_style_selection) return std::unexpected(time_style_selection.error());
  auto sort_selection = resolve_sort_mode(ctx);
  if (!sort_selection) return std::unexpected(sort_selection.error());
  auto quoting_mode = resolve_quoting_mode(ctx);
  if (!quoting_mode) return std::unexpected(quoting_mode.error());

  auto format_mode_result = resolve_format_mode(ctx);
  if (!format_mode_result) return std::unexpected(format_mode_result.error());
  FormatMode format_mode = apply_default_output_format(*format_mode_result, ctx);
  if (use_zero_terminated_output(ctx) && format_mode != FormatMode::Long) {
    format_mode = FormatMode::OnePerLine;
  }

  TimeMode time_mode = time_selection->mode;
  SortMode sort_mode = sort_selection->mode;
  if (!sort_selection->explicit_sort && format_mode != FormatMode::Long &&
      time_selection->explicit_time) {
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
      case SortMode::Name:
        std::sort(entries.begin(), entries.end(),
                  [](const EntryInfo &a, const EntryInfo &b) {
                    return a.name < b.name;
                  });
        break;
      case SortMode::None:
        break;
    }

    if (ctx.get<bool>("-r", false) || ctx.get<bool>("--reverse", false)) {
      std::reverse(entries.begin(), entries.end());
    }
  }

  if (sort_mode != SortMode::None &&
      ctx.get<bool>("--group-directories-first", false)) {
    std::stable_partition(entries.begin(), entries.end(), is_directory_entry);
  }

  bool long_format = format_mode == FormatMode::Long;
  bool use_numeric =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--numeric-uid-gid", false);
  const bool show_owner = !ctx.get<bool>("-g", false);
  const bool show_group =
      !ctx.get<bool>("-o", false) && !ctx.get<bool>("-G", false) &&
      !ctx.get<bool>("--no-group", false);
  const bool show_author = ctx.get<bool>("--author", false);
  const bool show_blocks =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--size", false);

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
      auto [display_find_data, _metadata_name] = resolve_display_entry(entry, ctx);
      info.name = entry.name;
      info.find_data = display_find_data;
      info.full_path = entry.full_path;
      info.perms = get_permissions_string(display_find_data);
      if (show_inode) {
        info.inode =
            get_file_index_string(entry.full_path, display_find_data, ctx);
      }
      if (show_blocks) {
        info.blocks = get_allocated_blocks_string(entry.full_path,
                                                  display_find_data, size_cfg);
      }
      info.link_count =
          std::to_string(get_link_count(entry.full_path, display_find_data, ctx));
      info.size = get_file_size_string(entry.full_path, display_find_data,
                                       entry.find_data, ctx, size_cfg,
                                       entry.command_line_operand);
      info.mtime =
          get_time_string(display_find_data, time_mode, *time_style_selection);

      // Get owner and group
      auto [owner, group] = get_file_owner_and_group(use_numeric);
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
      print_display_name(file_info.name, file_info.full_path, file_info.find_data,
                         ctx, color_enabled);
      print_record_terminator(ctx);
    }
  } else if (format_mode == FormatMode::Commas) {
    if (show_blocks) {
      print_directory_total_line(entries, size_cfg, ctx);
    }
    auto rendered = build_rendered_entries(entries, ctx);
    int width = ctx.get<int>("-w", 0);
    if (width <= 0) {
      width = ctx.get<int>("--width", 0);
    }
    if (width <= 0) {
      width = get_terminal_width();
    }
    print_rendered_entries(rendered, static_cast<size_t>(width));
    safePrintLn(L"");
  } else if (format_mode == FormatMode::OnePerLine) {
    if (show_blocks) {
      print_directory_total_line(entries, size_cfg, ctx);
    }
    auto rendered = build_rendered_entries(entries, ctx);
    for (const auto &entry : rendered) {
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
  const std::wstring metadata_probe_wpath =
      normalize_metadata_probe_path(lookup_wpath);
  const std::wstring operand_display_name = wpath;

  // Extract just the filename for display
  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(metadata_probe_wpath.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return std::unexpected("cannot access '" + path +
                           "': No such file or directory");
  }

  std::wstring filename = find_data.cFileName;
  FindClose(hFind);

  WIN32_FIND_DATAW display_find_data = find_data;
  std::wstring metadata_name = filename;
  if (auto dereferenced =
          try_get_dereferenced_find_data(lookup_wpath, find_data, ctx, true)) {
    display_find_data = dereferenced->first;
    metadata_name = dereferenced->second;
    wcsncpy_s(display_find_data.cFileName, metadata_name.c_str(), _TRUNCATE);
  }

  auto size_cfg_result = configure_sizes(ctx);
  if (!size_cfg_result) return std::unexpected(size_cfg_result.error());
  SizeConfig size_cfg = *size_cfg_result;
  auto time_selection = resolve_time_mode(ctx);
  if (!time_selection) return std::unexpected(time_selection.error());
  auto time_style_selection = resolve_time_style(ctx);
  if (!time_style_selection) return std::unexpected(time_style_selection.error());
  auto quoting_mode = resolve_quoting_mode(ctx);
  if (!quoting_mode) return std::unexpected(quoting_mode.error());

  auto format_mode_result = resolve_format_mode(ctx);
  if (!format_mode_result) return std::unexpected(format_mode_result.error());
  FormatMode format_mode = apply_default_output_format(*format_mode_result, ctx);
  if (use_zero_terminated_output(ctx) && format_mode != FormatMode::Long) {
    format_mode = FormatMode::OnePerLine;
  }

  TimeMode time_mode = time_selection->mode;

  bool long_format = format_mode == FormatMode::Long;
  bool use_numeric =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--numeric-uid-gid", false);
  const bool show_owner = !ctx.get<bool>("-g", false);
  const bool show_group =
      !ctx.get<bool>("-o", false) && !ctx.get<bool>("-G", false) &&
      !ctx.get<bool>("--no-group", false);
  const bool show_author = ctx.get<bool>("--author", false);
  bool show_inode =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--inode", false);
  bool show_blocks =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--size", false);

  if (long_format) {
    // Long format output for single file
    auto perms = get_permissions_string(display_find_data);
    auto inode = show_inode
                     ? get_file_index_string(lookup_wpath, find_data, ctx, true)
                     : "";
    auto blocks = show_blocks
                      ? get_allocated_blocks_string(lookup_wpath, display_find_data,
                                                    size_cfg)
                      : "";
    auto link_count =
        std::to_string(get_link_count(lookup_wpath, find_data, ctx, true));
    auto size =
        get_file_size_string(lookup_wpath, display_find_data, find_data, ctx,
                             size_cfg, true);
    auto mtime =
        get_time_string(display_find_data, time_mode, *time_style_selection);
    auto [owner, group] = get_file_owner_and_group(use_numeric);

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

    // 5. File size (right-aligned, pad to at least 8 chars)
    if (size.length() < 8) {
      for (size_t i = 0; i < 8 - size.length(); i++) {
        safePrint(" ");
      }
    }
    safePrint(std::string_view(size));
    safePrint(" ");

    // 6. Modification time
    safePrint(std::string_view(mtime));
    safePrint(" ");

    bool color_enabled = resolve_color_enabled(ctx);
    print_display_name(operand_display_name, lookup_wpath, display_find_data, ctx,
                       color_enabled, true);
    print_record_terminator(ctx);
  } else {
    auto rendered =
        render_inline_entry(
            {operand_display_name, lookup_wpath, find_data, true}, ctx,
                            resolve_color_enabled(ctx));
    safePrint(rendered.text);
    print_record_terminator(ctx);
  }

  return true;
}

/**
 * @brief List directory recursively
 * @param path Path to directory
 * @param ctx Command context
 * @param depth Current recursion depth
 * @return Result with success status
 */
auto list_directory_recursive(const std::string &path,
                              const CommandContext<LS_OPTIONS.size()> &ctx,
                              int depth = 0) -> cp::Result<bool> {
  // Print header for subdirectories
  if (depth > 0) {
    const std::string display_path = make_generic_display_path(path);
    safePrintLn(std::wstring(display_path.begin(), display_path.end()) + L":");
  }

  // List current directory
  auto result = list_directory(path, ctx);
  if (!result) {
    return result;
  }

  // Collect subdirectories for recursion
  std::wstring wpath = utf8_to_wstring(path);
  std::wstring search_path = wpath + L"\\*";

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return true;  // No entries
  }

  std::vector<std::string> subdirs;
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

    std::wstring full_path = wpath + L"\\" + filename;
    subdirs.push_back(make_generic_display_path(wstring_to_utf8(full_path)));
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  // Recursively list subdirectories
  for (const auto &subdir : subdirs) {
    auto subdir_result = list_directory_recursive(subdir, ctx, depth + 1);
    if (!subdir_result) {
      return subdir_result;
    }

    // Add newline between directories
    if (&subdir != &subdirs.back()) {
      safePrintLn(L"");
    }
  }

  return true;
}

auto print_ls_error(const std::string &message) -> void {
  safeErrorPrintLn(std::wstring(L"ls: ") +
                   std::wstring(message.begin(), message.end()));
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
    }

    auto probe = probe_path(wpath);
    ++logical_operand_count;
    if (probe.attributes_valid || probe.found) {
      expanded_paths.push_back(path);
    } else {
      safeErrorPrintLn(std::wstring(L"ls: cannot access '") +
                       std::wstring(path.begin(), path.end()) +
                       L"': No such file or directory");
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

  for (const auto &path : expanded_paths) {
    // Check if path exists and determine its type
    std::wstring wpath = utf8_to_wstring(path);
    auto probe = probe_path(wpath);

    if (!probe.attributes_valid && !probe.found) {
      success = false;
      continue;
    }

    const DWORD effective_attributes =
        probe.attributes_valid ? probe.attributes : probe.find_data.dwFileAttributes;
    const bool is_directory = (effective_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool is_reparse_point =
        (effective_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    const bool is_directory_symlink = is_directory && is_reparse_point;
    const bool follow_command_line_directory_symlink =
        probe.attributes_valid && is_directory_symlink &&
        should_follow_command_line_directory_symlink(ctx);
    const bool treat_as_directory =
        probe.attributes_valid && is_directory && !directory_only_requested(ctx) &&
        (!is_directory_symlink || follow_command_line_directory_symlink);

    if (treat_as_directory) {
      if (printed_any) {
        safePrintLn(L"");
      }

      if (recursive_requested(ctx)) {
        auto result =
            list_directory_recursive(path, ctx, multiple_operands ? 1 : 0);
        if (!result) {
          print_ls_error(std::string(result.error()));
          success = false;
        }
      } else {
        if (multiple_operands) {
          safePrintLn(std::wstring(path.begin(), path.end()) + L":");
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

  auto result = process_command(ctx);
  if (!result) {
    const std::string error = std::string(result.error());
    if (error.starts_with(kInvalidTimeStylePrefix)) {
      safeErrorPrint(error);
      safeErrorPrint("\n");
      return 2;
    }
    report_error(result, L"ls");
    return 1;
  }

  // GNU/Microsoft-style ls uses exit 2 for serious runtime trouble such as
  // missing-path "cannot access" failures.
  return *result ? 0 : 2;
}
