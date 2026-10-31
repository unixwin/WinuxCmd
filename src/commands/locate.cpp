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
    // [GNU] locate.c:1599 "case 'A': op_and = 1": -A does not print the
    // whole database; it makes multiple patterns conjunctive.
    OPTION("-A", "--all", "only print entries that match all patterns"),
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
  bool all = false;  // -A/--all: patterns are conjunctive (GNU op_and)
  bool basename = false;
  bool count = false;
  bool existing = false;
  bool non_existing = false;
  bool ignore_case = false;
  bool use_regex = false;
  bool wholename = false;
  bool statistics = false;
  bool limit_set = false;
  int limit = 0;
  bool database_given = false;
  std::string database;  // raw -d value when database_given
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
  // "-l means --ignore-case" alias are gone).  An explicit -l 0 is a real
  // limit in GNU (nothing is printed, exit status 0), so track that the
  // option was given instead of treating 0 as "no limit".
  cfg.limit_set = ctx.has("-l") || ctx.has("--limit");
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
    cfg.database_given = true;
  } else if (ctx.has("--database")) {
    cfg.database = ctx.get<std::string>("--database", "");
    cfg.database_given = true;
  }
  for (auto arg : ctx.positionals) cfg.patterns.push_back(std::string(arg));
  // [GNU] locate.c dolocate: a pattern is required unless --statistics
  // was given ("pattern argument expected"); -A does not exempt it.
  if (cfg.patterns.empty() && !cfg.statistics)
    return std::unexpected("missing operand");
  return cfg;
}

auto split_db_lines(const std::string& data, std::vector<std::string>& lines)
    -> void {
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
}

auto read_db(const std::string& db_path, std::vector<std::string>& lines,
             DWORD& error) -> bool {
  // #1364: the -d database path is an argv operand: resolve it through the
  // shared operand boundary so a shell-namespace spelling names the payload
  // root, not the current drive root.
  std::wstring wpath = native_path::make_api_path_operand(db_path).extended;
  HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    return false;
  }
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0) {
    error = GetLastError();
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
  if (!ok) error = GetLastError();
  CloseHandle(h);
  if (!ok) return false;
  data.resize(got);
  split_db_lines(data, lines);
  return true;
}

auto read_stdin_db(std::vector<std::string>& lines) -> bool {
  std::string data((std::istreambuf_iterator<char>(std::cin)),
                   std::istreambuf_iterator<char>());
  split_db_lines(data, lines);
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
  // [GNU] locate.c: every pattern gets its own matcher, also under
  // -r/--regex; the patterns are combined with OR, or with AND when
  // -A/--all is given.  (Only patterns[0] was compiled before.)
  std::vector<std::regex> regexes;  // one per pattern when -r is used
  bool regex_ok = true;
};

auto build_matcher(const Config& cfg) -> PatternMatcher {
  PatternMatcher matcher;
  matcher.cfg = &cfg;
  if (cfg.use_regex) {
    auto flags = std::regex_constants::basic;
    if (cfg.ignore_case) flags |= std::regex_constants::icase;
    try {
      for (const auto& pattern : cfg.patterns) {
        matcher.regexes.emplace_back(std::string(pattern), flags);
      }
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
  const std::string target = cfg.basename ? basename_of(line) : line;

  // With no patterns (only reachable with --statistics), every entry is
  // accepted for counting purposes.
  if (cfg.patterns.empty()) return true;

  for (size_t i = 0; i < cfg.patterns.size(); ++i) {
    const auto& pattern = cfg.patterns[i];
    bool match = false;
    if (cfg.use_regex) {
      if (!matcher.regex_ok) return false;
      match = std::regex_search(target, matcher.regexes[i]);
    } else if (contains_metacharacter(pattern)) {
      // [GNU] locate.c:724 visit_globmatch_nofold / visit_globmatch_casefold:
      // fnmatch() with no FNM_PATHNAME; case-folded only when -i is given.
      match = wildcard_match(pattern, target, !cfg.ignore_case);
    } else if (cfg.ignore_case) {
      match = to_lower(target).find(to_lower(pattern)) != std::string::npos;
    } else {
      match = target.find(pattern) != std::string::npos;
    }
    // [GNU] locate.c process_or / process_and (selected by op_and, the
    // -A flag): by default one matching pattern is enough; with -A an
    // entry must match every pattern.
    if (match && !cfg.all) return true;
    if (!match && cfg.all) return false;
  }
  return cfg.all;
}

auto print_statistics(const std::string& database,
                      const std::vector<std::string>& lines) -> void {
  // [GNU] locate.c print_stats writes to stdout.  The GNU block carries
  // LOCATE02-format data (bigram compression ratio, database format
  // name) that a plain-line database does not have; the compact form
  // stays as the documented format deviation.
  safePrint("Database: ");
  safePrint(database);
  safePrint("\n");
  safePrint("Entries: ");
  safePrint(std::to_string(lines.size()));
  safePrint("\n");
}

// [GNU] locate.c dolocate: the databases to search come from -d when
// given, else from $LOCATE_PATH, else the default database.  The list
// is split into elements; an empty element or "." names the default
// database and "-" names standard input.  GNU splits on ':' because
// its paths are POSIX spellings; native Windows spellings contain ':'
// in every drive letter, so the separator here is ';' (the same
// documented deviation as updatedb's --localpaths/--prunepaths).
struct DatabaseSet {
  bool user_selected = false;  // via -d or LOCATE_PATH
  std::vector<std::string> elements;  // raw elements, "-" = stdin
};

auto resolve_databases(const Config& cfg) -> DatabaseSet {
  DatabaseSet set;
  std::string spec;
  if (cfg.database_given) {
    set.user_selected = true;
    spec = cfg.database;
  } else if (auto env = get_env_utf8(L"LOCATE_PATH")) {
    set.user_selected = true;
    spec = *env;
  } else {
    set.elements.push_back(get_default_database());
    return set;
  }
  size_t start = 0;
  while (true) {
    size_t sep = spec.find(';', start);
    std::string element = spec.substr(
        start, sep == std::string::npos ? std::string::npos : sep - start);
    if (element.empty() || element == ".") {
      element = get_default_database();
    }
    set.elements.push_back(std::move(element));
    if (sep == std::string::npos) break;
    start = sep + 1;
  }
  return set;
}

auto run(const Config& cfg) -> int {
  PatternMatcher matcher = build_matcher(cfg);
  if (cfg.use_regex && !cfg.patterns.empty() && !matcher.regex_ok) {
    safeErrorPrintLn("locate: invalid regular expression");
    return 1;
  }

  // [GNU] locate.c dolocate: with a limit of 0 the database loop never
  // runs; the exit status is nevertheless 0 (and -c prints 0).
  if (cfg.limit_set && cfg.limit == 0) {
    if (cfg.count) safePrint("0\n");
    return 0;
  }

  auto databases = resolve_databases(cfg);
  size_t accepted = 0;
  std::string output;
  bool did_stdin = false;
  bool limit_reached = false;

  for (const auto& element : databases.elements) {
    if (limit_reached) break;
    std::vector<std::string> db_lines;
    if (element == "-") {
      // [GNU] locate.c: the stdin database can be read only once; a
      // second "-" element warns and ends the run with status 0.
      if (did_stdin) {
        safeErrorPrintLn(
            "locate: warning: the locate database can only be read "
            "from stdin once.");
        return 0;
      }
      did_stdin = true;
      read_stdin_db(db_lines);
    } else {
      DWORD error = 0;
      if (!read_db(element, db_lines, error)) {
        if (databases.user_selected) {
          // [GNU] locate.c dolocate: a user-selected database that
          // cannot be opened is reported as "locate: NAME: <error>"
          // and the run ends there -- with status 0.
          safeErrorPrint("locate: ");
          safeErrorPrint(element);
          safeErrorPrint(": ");
          safeErrorPrintLn(win32_posix_error_text(error));
          return 0;
        }
        // The default database missing is silent; the run simply
        // finds nothing and exits 1 below.
        break;
      }
    }

    if (cfg.statistics) {
      print_statistics(element == "-" ? "<stdin>" : element, db_lines);
    }

    for (const auto& line : db_lines) {
      if (!entry_matches(matcher, line)) continue;
      if (cfg.existing && !path_exists(line)) continue;
      if (cfg.non_existing && path_exists(line)) continue;
      accepted++;
      // [GNU] locate.c: with --statistics the print visitor is never
      // installed, so matching entries are counted, not printed.
      if (!cfg.count && !cfg.statistics) {
        output += line;
        output += cfg.null_separator ? '\0' : '\n';
      }
      // [GNU] locate.c: the limit counts accepted entries across all
      // databases and stops the whole search once reached; the count
      // printed with -c is capped the same way.
      if (cfg.limit_set && accepted >= static_cast<size_t>(cfg.limit)) {
        limit_reached = true;
        break;
      }
    }
  }

  if (cfg.count) {
    safePrint(std::to_string(accepted));
    safePrint("\n");
  } else if (!cfg.statistics) {
    safePrint(output);
  }

  // [GNU] locate.c dolocate tail: exit 0 when anything was found or
  // statistics were requested; a positive limit with no match still
  // exits 1 (only a limit of 0 exits 0 unconditionally, handled above).
  if (cfg.statistics) return 0;
  if (accepted > 0) return 0;
  return 1;
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
