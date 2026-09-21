// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for locate (find files by name in database).
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// [GNU] locate.c longopts: the advertised options mirror GNU findutils 4.10
// locate(1).  Options that make no sense on this platform (follow/nofollow,
// mmap/stdio database back ends) are accepted as documented no-ops so that
// every advertised option works.
auto constexpr LOCATE_OPTIONS = std::array{
    OPTION("-0", "--null", "separate output entries with NUL, not newline"),
    OPTION("-A", "--all", "print all database entries"),
    OPTION("-b", "--basename", "match on filename, not full path"),
    OPTION("-c", "--count", "count matching entries"),
    OPTION("-d", "--database", "use given database", STRING_TYPE),
    OPTION("-e", "--existing", "print only entries that exist on disk"),
    OPTION("-E", "--non-existing",
           "print only entries that do not exist on disk"),
    OPTION("-i", "--ignore-case", "ignore case in search"),
    // [GNU] locate.c:1405 "-l, --limit=N": GNU has no -n option,
    // so the previous non-GNU -n alias is not advertised.
    OPTION("-l", "--limit", "limit results to at most N entries", INT_TYPE),
    OPTION("-L", "--follow",
           "accepted for GNU compatibility; the database stores "
           "plain paths and never follows links"),
    OPTION("-m", "--mmap", "accepted for GNU compatibility; ignored"),
    OPTION("-P", "--nofollow",
           "accepted for GNU compatibility; the database stores "
           "plain paths and never follows links"),
    OPTION("-p", "--print", "print matches (default)"),
    // [GNU] locate.c usage: "-r | --regex"
    OPTION("-r", "--regex",
           "treat patterns as POSIX basic regular expressions"),
    // [GNU] locate.c: "-S, --statistics"
    OPTION("-S", "--statistics", "print database statistics"),
    OPTION("-s", "--stdio", "accepted for GNU compatibility; ignored"),
    // [GNU] locate.c: "-w, --wholename: match only whole path
    // names" (whole-path matching is the default; -b matches
    // basenames instead)
    OPTION("-w", "--wholename",
           "match patterns against the whole path (default)")};

namespace locate_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool null_separator = false;
  bool all = false;
  bool basename = false;
  bool count = false;
  bool existing = false;
  bool non_existing = false;
  bool ignore_case = false;
  bool use_regex = false;
  bool wholename = false;
  bool statistics = false;
  int limit = 0;
  std::string database;
  SmallVector<std::string, 8> patterns;
};

auto get_env_utf8(const wchar_t* key) -> std::optional<std::string> {
  DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
  if (size == 0) return std::nullopt;
  std::wstring value;
  value.resize(size - 1);
  if (GetEnvironmentVariableW(key, value.data(), size) == 0)
    return std::nullopt;
  return wstring_to_utf8(value);
}

auto get_default_database() -> std::string {
  if (auto p = get_env_utf8(L"LOCALAPPDATA")) {
    return *p + "/WinuxCmd/locate.db";
  }
  return "locate.db";
}

auto build_config(const CommandContext<LOCATE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.null_separator = ctx.has("-0") || ctx.has("--null");
  cfg.all = ctx.has("-A") || ctx.has("--all");
  cfg.count = ctx.has("-c") || ctx.has("--count");
  cfg.existing = ctx.has("-e") || ctx.has("--existing");
  cfg.non_existing = ctx.has("-E") || ctx.has("--non-existing");
  cfg.ignore_case = ctx.has("-i") || ctx.has("--ignore-case");
  cfg.use_regex = ctx.has("-r") || ctx.has("--regex");
  cfg.wholename = ctx.has("-w") || ctx.has("--wholename");
  cfg.statistics = ctx.has("-S") || ctx.has("--statistics");
  // -b and -w select the match scope; the last one on the command line wins.
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= LOCATE_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (option_matches(meta, "-b", "--basename")) cfg.basename = true;
    if (option_matches(meta, "-w", "--wholename")) cfg.basename = false;
  }
  // [GNU] locate.c:1405: "-l, --limit=N" (the old non-GNU "-n" alias and the
  // "-l means --ignore-case" alias are gone).
  if (ctx.has("-l")) {
    cfg.limit = ctx.get<int>("-l", 0);
  } else if (ctx.has("--limit")) {
    cfg.limit = ctx.get<int>("--limit", 0);
  }
  if (cfg.limit < 0) {
    return std::unexpected("invalid negative limit");
  }
  if (ctx.has("-d")) {
    cfg.database = ctx.get<std::string>("-d", "");
  } else if (ctx.has("--database")) {
    cfg.database = ctx.get<std::string>("--database", "");
  }
  if (cfg.database.empty()) cfg.database = get_default_database();
  for (auto arg : ctx.positionals) cfg.patterns.push_back(std::string(arg));
  if (cfg.patterns.empty() && !cfg.all)
    return std::unexpected("missing operand");
  return cfg;
}

auto read_db(const std::string& db_path, std::vector<std::string>& lines)
    -> bool {
  std::wstring wpath = utf8_to_wstring(db_path);
  HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0) {
    CloseHandle(h);
    return false;
  }
  std::string data(static_cast<size_t>(sz.QuadPart), '\0');
  DWORD got = 0;
  BOOL ok = TRUE;
  if (!data.empty()) {
    ok = ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &got,
                  nullptr);
  }
  CloseHandle(h);
  if (!ok) return false;
  data.resize(got);
  size_t start = 0;
  while (start <= data.size()) {
    size_t nl = data.find('\n', start);
    if (nl == std::string::npos) {
      std::string line = data.substr(start);
      if (!line.empty()) lines.push_back(std::move(line));
      break;
    }
    std::string line = data.substr(start, nl - start);
    if (!line.empty()) lines.push_back(std::move(line));
    start = nl + 1;
  }
  return true;
}

auto to_lower(std::string_view s) -> std::string {
  std::string out(s);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// [GNU] locate.c:212 "static const char * const metacharacters = "*?[]\\";"
// When the pattern contains any of these, it is matched with fnmatch()
// (flags 0, i.e. no FNM_PATHNAME: '*' also matches '/'); otherwise a plain
// substring search is used.
auto contains_metacharacter(std::string_view s) -> bool {
  return s.find_first_of("*?[]\\") != std::string_view::npos;
}

auto basename_of(const std::string& path) -> std::string {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

struct PatternMatcher {
  const Config* cfg = nullptr;
  std::unique_ptr<std::regex> regex;  // built once when -r/--regex is used
  bool regex_ok = false;
};

auto build_matcher(const Config& cfg) -> PatternMatcher {
  PatternMatcher matcher;
  matcher.cfg = &cfg;
  if (cfg.use_regex && !cfg.patterns.empty()) {
    auto flags = std::regex_constants::basic;
    if (cfg.ignore_case) flags |= std::regex_constants::icase;
    try {
      matcher.regex =
          std::make_unique<std::regex>(std::string(cfg.patterns[0]), flags);
      matcher.regex_ok = true;
    } catch (const std::regex_error&) {
      matcher.regex_ok = false;
    }
  }
  return matcher;
}

auto path_exists(const std::string& path) -> bool {
  DWORD attrs = GetFileAttributesW(utf8_to_wstring(path).c_str());
  return attrs != INVALID_FILE_ATTRIBUTES;
}

auto entry_matches(const PatternMatcher& matcher, const std::string& line)
    -> bool {
  const Config& cfg = *matcher.cfg;
  if (cfg.all) return true;  // [GNU] -A/--all accepts every entry
  const std::string target = cfg.basename ? basename_of(line) : line;

  for (const auto& pattern : cfg.patterns) {
    bool match = false;
    if (cfg.use_regex) {
      if (!matcher.regex_ok) return false;
      match = std::regex_search(target, *matcher.regex);
    } else if (contains_metacharacter(pattern)) {
      // [GNU] locate.c:724 visit_globmatch_nofold / visit_globmatch_casefold:
      // fnmatch() with no FNM_PATHNAME; case-folded only when -i is given.
      match = wildcard_match(pattern, target, !cfg.ignore_case);
    } else if (cfg.ignore_case) {
      match = to_lower(target).find(to_lower(pattern)) != std::string::npos;
    } else {
      match = target.find(pattern) != std::string::npos;
    }
    if (match) return true;
  }
  return false;
}

auto print_statistics(const std::string& database,
                      const std::vector<std::string>& lines) -> void {
  safeErrorPrint("Database: ");
  safeErrorPrintLn(database);
  safeErrorPrint("Entries: ");
  safeErrorPrint(std::to_string(lines.size()));
  safeErrorPrintLn("");
}

auto run(const Config& cfg) -> int {
  std::vector<std::string> db_lines;
  if (!read_db(cfg.database, db_lines)) {
    safeErrorPrintLn("locate: cannot open database '" + cfg.database + "'");
    safeErrorPrintLn("Use 'updatedb' to build the database first.");
    return 1;
  }

  if (cfg.statistics) {
    print_statistics(cfg.database, db_lines);
  }

  PatternMatcher matcher = build_matcher(cfg);
  if (cfg.use_regex && !cfg.all && !cfg.patterns.empty() && !matcher.regex_ok) {
    safeErrorPrintLn("locate: invalid regular expression");
    return 1;
  }

  size_t accepted = 0;
  std::string output;
  for (const auto& line : db_lines) {
    if (!entry_matches(matcher, line)) continue;
    if (cfg.existing && !path_exists(line)) continue;
    if (cfg.non_existing && path_exists(line)) continue;
    accepted++;
    // [GNU] locate.c:1405 "--limit=N: Limit the number of entries found to
    // be printed"; the count printed with -c is capped the same way.
    if (cfg.limit > 0 && accepted > static_cast<size_t>(cfg.limit)) {
      accepted = static_cast<size_t>(cfg.limit);
      break;
    }
    if (!cfg.count) {
      output += line;
      output += cfg.null_separator ? '\0' : '\n';
    }
  }

  if (cfg.count) {
    safePrint(std::to_string(accepted));
    safePrint("\n");
  } else {
    safePrint(output);
  }

  // [GNU] locate.c:1361: locate exits with status 1 when nothing matched.
  return accepted == 0 ? 1 : 0;
}

}  // namespace locate_pipeline

REGISTER_COMMAND(locate, "locate", "locate [OPTION]... PATTERN...",
                 "Search a database of file paths for matching names.\n"
                 "Run 'updatedb' first to build the database.\n"
                 "When a pattern contains any of *?[]\\ it is matched as a\n"
                 "shell glob against whole paths; otherwise a substring\n"
                 "search is performed.  Database entries are plain path\n"
                 "lines (a Windows platform deviation from LOCATE02).",
                 "  locate myprogram\n"
                 "  locate -i readme\n"
                 "  locate -c -i test\n"
                 "  locate -l 5 '*.cpp'\n"
                 "  locate -r '/doc.*\\.md$'",
                 "updatedb(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 LOCATE_OPTIONS) {
  using namespace locate_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("locate: missing operand");
      safeErrorPrintLn("Try 'locate --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"locate");
    return 1;
  }

  return run(*cfg_result);
}
