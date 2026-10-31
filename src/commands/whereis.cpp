// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for whereis (locate binary/source/manual
/// files).
/// @Version: 0.1.0
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

// [GNU] -b: search only for binaries
// [GNU] -m: search only for manual sections
// [GNU] -s: search only for sources
// [GNU] -u: search for unusual entries (missing some category)
// [GNU] -B dir: define binaries lookup path
// [GNU] -M dir: define man lookup path
// [GNU] -S dir: define source lookup path
// [GNU] -f: terminate last dir list, signal start of filenames
// [GNU] -l: print the effective lookup paths being used
auto constexpr WHEREIS_OPTIONS = std::array{
    OPTION("-b", "", "search only for binaries"),
    OPTION("-m", "", "search only for manual sections"),
    OPTION("-s", "", "search only for sources"),
    OPTION("-u", "", "search for unusual entries"),
    OPTION("-B", "", "define a binary lookup directory", STRING_TYPE),
    OPTION("-M", "", "define a manual lookup directory", STRING_TYPE),
    OPTION("-S", "", "define a source lookup directory", STRING_TYPE),
    OPTION("-f", "", "terminate the last directory list"),
    OPTION("-l", "", "list the effective lookup paths"),
    // [GNU] -g: glob matching (whereis.c case 'g'); -h/-V print and exit
    OPTION("-g", "", "search using glob patterns", BOOL_TYPE),
    OPTION("-h", "", "", BOOL_TYPE), OPTION("-V", "", "", BOOL_TYPE)};

namespace whereis_pipeline {
namespace cp = core::pipeline;

inline constexpr int kWantBin = 1;
inline constexpr int kWantMan = 2;
inline constexpr int kWantSrc = 4;
inline constexpr int kWantAll = kWantBin | kWantMan | kWantSrc;

struct NameQuery {
  std::string name;
  int want = kWantAll;
};

struct Config {
  bool unusual = false;
  bool glob = false;
  bool list_paths = false;
  // user-specified replacement directories (-B/-M/-S replace the
  // defaults for their category, whereis.c: free_dirlist + construct)
  bool bin_replaced = false;
  bool man_replaced = false;
  bool src_replaced = false;
  SmallVector<std::string, 8> bin_dirs;
  SmallVector<std::string, 8> man_dirs;
  SmallVector<std::string, 8> src_dirs;
  SmallVector<NameQuery, 8> names;
};

// [GNU] whereis parses argv with its own hand loop (whereis.c main):
// -B/-M/-S consume every following token as a directory until an
// option token appears, -f terminates the requirement, and -b/-m/-s
// build a cumulative mask that applies per name and resets after a
// name has been looked up.  The framework's set-based binding cannot
// express any of that, so walk ctx.raw_args directly.
auto build_config(const CommandContext<WHEREIS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  int want = kWantAll;
  bool want_resetable = false;
  bool f_missing = false;
  enum class Sink { None, Bin, Man, Src };
  Sink sink = Sink::None;

  auto apply_mask = [&](int bit) {
    if (want_resetable) {
      want = kWantAll;
      want_resetable = false;
    }
    want = (want == kWantAll) ? bit : (want | bit);
    f_missing = false;
  };
  auto add_name = [&](std::string_view token) {
    // lookup() canonicalizes the pattern to its basename (whereis.c).
    std::string name(token);
    if (auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
      name = name.substr(slash + 1);
    }
    cfg.names.push_back(NameQuery{name, want});
    want_resetable = true;
  };
  auto add_dir = [&](std::string_view token) {
    switch (sink) {
      case Sink::Bin:
        cfg.bin_dirs.push_back(std::string(token));
        break;
      case Sink::Man:
        cfg.man_dirs.push_back(std::string(token));
        break;
      case Sink::Src:
        cfg.src_dirs.push_back(std::string(token));
        break;
      case Sink::None:
        break;
    }
  };

  const auto& args = ctx.raw_args;
  for (size_t i = 0; i < args.size(); ++i) {
    std::string_view token = args[i];
    if (sink != Sink::None &&
        !(token.size() > 1 && token.front() == '-')) {
      add_dir(token);
      continue;
    }
    sink = Sink::None;
    if (token == "--") {
      for (++i; i < args.size(); ++i) add_name(args[i]);
      break;
    }
    if (token.size() > 1 && token.front() == '-') {
      for (size_t j = 1; j < token.size(); ++j) {
        switch (token[j]) {
          case 'B':
            cfg.bin_replaced = true;
            cfg.bin_dirs.clear();
            sink = Sink::Bin;
            f_missing = true;
            break;
          case 'M':
            cfg.man_replaced = true;
            cfg.man_dirs.clear();
            sink = Sink::Man;
            f_missing = true;
            break;
          case 'S':
            cfg.src_replaced = true;
            cfg.src_dirs.clear();
            sink = Sink::Src;
            f_missing = true;
            break;
          case 'f':
            f_missing = false;
            break;
          case 'u':
            cfg.unusual = true;
            f_missing = false;
            break;
          case 'l':
            cfg.list_paths = true;
            break;
          case 'g':
            cfg.glob = true;
            break;
          case 'b':
            apply_mask(kWantBin);
            break;
          case 'm':
            apply_mask(kWantMan);
            break;
          case 's':
            apply_mask(kWantSrc);
            break;
          default:
            break;  // the framework validates unknown options
        }
        if (sink != Sink::None) break;  // -B/-M/-S end the cluster
      }
      continue;
    }
    add_name(token);
  }

  // [GNU] whereis.c:671: errx "option -f is missing" when a -B/-M/-S
  // list was never terminated.
  if (f_missing) return std::unexpected("option -f is missing");

  if (cfg.names.empty() && !cfg.list_paths) {
    return std::unexpected("missing operand");
  }
  return cfg;
}

auto get_env_utf8(const wchar_t* key) -> std::optional<std::string> {
  DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
  if (size == 0) return std::nullopt;
  std::wstring value;
  value.resize(size - 1);
  if (GetEnvironmentVariableW(key, value.data(), size) == 0)
    return std::nullopt;
  return wstring_to_utf8(value);
}

auto split_path(const std::string& pathlist) -> std::vector<std::string> {
  // Windows PATH uses ';' as the separator.  Do NOT split on ':' because
  // drive letters (C:) contain a colon that is part of the path.
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= pathlist.size()) {
    size_t pos = pathlist.find(';', start);
    if (pos == std::string::npos) {
      std::string s = pathlist.substr(start);
      if (!s.empty()) out.push_back(s);
      break;
    }
    std::string s = pathlist.substr(start, pos - start);
    if (!s.empty()) out.push_back(s);
    start = pos + 1;
  }
  return out;
}

auto file_exists(const std::string& path) -> bool {
  std::wstring w = utf8_to_wstring(path);
  DWORD attr = GetFileAttributesW(w.c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

auto to_lower(std::string_view s) -> std::string {
  std::string out(s);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// Deduplicate a list of paths, case-insensitively (Windows FS is
// case-insensitive).
auto dedupe_paths(std::vector<std::string>& v) -> void {
  std::set<std::string> seen;
  std::vector<std::string> out;
  for (auto& p : v) {
    std::string key = to_lower(p);
    // also normalise backslashes to forward slashes for the key
    for (char& c : key)
      if (c == '\\') c = '/';
    if (seen.insert(key).second) out.push_back(p);
  }
  v = std::move(out);
}

auto join_path(const std::string& dir, const std::string& name,
               const std::vector<std::string>& exts)
    -> std::vector<std::string> {
  std::vector<std::string> hits;
  for (const auto& ext : exts) {
    std::string full = dir;
    if (!full.empty() && full.back() != '/' && full.back() != '\\') full += "/";
    full += name;
    full += ext;
    if (file_exists(full)) hits.push_back(full);
  }
  // also try exact name (no extension)
  std::string full = dir;
  if (!full.empty() && full.back() != '/' && full.back() != '\\') full += "/";
  full += name;
  if (file_exists(full)) hits.push_back(full);
  return hits;
}

// Default binary search directories on Windows.
auto default_bin_dirs() -> std::vector<std::string> {
  std::vector<std::string> dirs;
  if (auto p = get_env_utf8(L"PATH")) {
    for (auto& d : split_path(*p)) dirs.push_back(d);
  }
  // Common Windows system locations.
  const char* extra[] = {"C:/Windows/System32", "C:/Windows",
                         "C:/Windows/SysWOW64", "C:/Program Files/WinuxCmd",
                         "C:/Program Files (x86)/WinuxCmd"};
  for (auto* e : extra) {
    std::string s(e);
    if (file_exists(s)) dirs.push_back(s);
  }
  return dirs;
}

// Default manual directories (Windows has none standard; check a few).
auto default_man_dirs() -> std::vector<std::string> {
  std::vector<std::string> dirs;
  if (auto p = get_env_utf8(L"MANPATH")) {
    for (auto& d : split_path(*p)) dirs.push_back(d);
  }
  return dirs;
}

// Default source directories (none standard on Windows).
auto default_src_dirs() -> std::vector<std::string> { return {}; }

// fnmatch-lite for -g: '*' (any run) and '?' (one char), matched
// case-insensitively like the Windows file system.
auto glob_match(std::string_view pattern, std::string_view name) -> bool {
  if (pattern.empty()) return name.empty();
  if (pattern.front() == '*') {
    for (size_t skip = 0; skip <= name.size(); ++skip) {
      if (glob_match(pattern.substr(1), name.substr(skip))) return true;
    }
    return false;
  }
  if (name.empty()) return false;
  if (pattern.front() != '?' &&
      std::tolower(static_cast<unsigned char>(pattern.front())) !=
          std::tolower(static_cast<unsigned char>(name.front()))) {
    return false;
  }
  return glob_match(pattern.substr(1), name.substr(1));
}

// -g searches by enumerating directory entries (the oracle's findin
// model): an entry hits when the pattern matches its full name or its
// name without the final extension.
auto glob_hits_in_dir(const std::string& dir, const std::string& pattern)
    -> std::vector<std::string> {
  std::vector<std::string> hits;
  std::error_code ec;
  auto base = std::filesystem::u8path(native_path::normalize_api_operand(dir));
  for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    std::string filename = entry.path().filename().u8string();
    std::string stem = filename;
    if (auto dot = filename.rfind('.'); dot != std::string::npos && dot != 0) {
      stem = filename.substr(0, dot);
    }
    if (glob_match(pattern, filename) || glob_match(pattern, stem)) {
      std::string full = dir;
      if (!full.empty() && full.back() != '/' && full.back() != '\\') {
        full += "/";
      }
      full += filename;
      hits.push_back(full);
    }
  }
  return hits;
}

auto run(const Config& cfg) -> int {
  std::vector<std::string> bin_dirs =
      cfg.bin_replaced
          ? std::vector<std::string>(cfg.bin_dirs.begin(), cfg.bin_dirs.end())
          : default_bin_dirs();
  std::vector<std::string> man_dirs =
      cfg.man_replaced
          ? std::vector<std::string>(cfg.man_dirs.begin(), cfg.man_dirs.end())
          : default_man_dirs();
  std::vector<std::string> src_dirs =
      cfg.src_replaced
          ? std::vector<std::string>(cfg.src_dirs.begin(), cfg.src_dirs.end())
          : default_src_dirs();

  dedupe_paths(bin_dirs);
  dedupe_paths(man_dirs);
  dedupe_paths(src_dirs);

  if (cfg.list_paths) {
    // [GNU] list_dirlist prints one "bin: <dir>" line per directory
    // (whereis.c list_dirlist), not headed blocks.
    for (const auto& d : bin_dirs) safePrintLn("bin: " + d);
    for (const auto& d : man_dirs) safePrintLn("man: " + d);
    for (const auto& d : src_dirs) safePrintLn("src: " + d);
    if (cfg.names.empty()) return 0;
  }

  std::vector<std::string> bin_exts = {".exe", ".bat", ".cmd", ".com", ".ps1"};
  std::vector<std::string> man_exts = {".1", ".2", ".3", ".4", ".5",
                                       ".6", ".7", ".8", ".9", ""};
  std::vector<std::string> src_exts = {".c",  ".cpp", ".cc", ".h", ".hpp",
                                       ".rs", ".go",  ".py", ""};

  int rc = 0;
  for (const auto& query : cfg.names) {
    const std::string& name = query.name;
    std::vector<std::string> bins, mans, srcs;
    auto collect = [&](const std::vector<std::string>& dirs,
                       const std::vector<std::string>& exts,
                       std::vector<std::string>& out) {
      for (const auto& d : dirs) {
        if (cfg.glob) {
          auto h = glob_hits_in_dir(d, name);
          for (auto& x : h) out.push_back(x);
        } else {
          auto h = join_path(d, name, exts);
          for (auto& x : h) out.push_back(x);
        }
      }
    };
    if ((query.want & kWantBin) != 0) collect(bin_dirs, bin_exts, bins);
    if ((query.want & kWantMan) != 0) collect(man_dirs, man_exts, mans);
    if ((query.want & kWantSrc) != 0) collect(src_dirs, src_exts, srcs);

    dedupe_paths(bins);
    dedupe_paths(mans);
    dedupe_paths(srcs);

    // [GNU] -u prints a name only when the TOTAL number of entries
    // found differs from exactly one (whereis.c findin/lookup: the
    // first entry is held back and the line is emitted from the
    // second entry on; zero or one entry prints nothing).
    size_t total = bins.size() + mans.size() + srcs.size();
    if (cfg.unusual && total <= 1) continue;

    safePrint(name);
    safePrint(":");
    for (auto& b : bins) {
      safePrint(" ");
      safePrint(b);
    }
    for (auto& m : mans) {
      safePrint(" ");
      safePrint(m);
    }
    for (auto& s : srcs) {
      safePrint(" ");
      safePrint(s);
    }
    safePrintLn("");
  }
  return rc;
}

}  // namespace whereis_pipeline

REGISTER_COMMAND(
    whereis, "whereis", "whereis [OPTION]... NAME...",
    "Locate the binary, source, and manual-page files for each NAME.  By "
    "default all categories are searched in the standard locations (PATH "
    "for binaries).",
    "  whereis bash\n"
    "  whereis -b gcc\n"
    "  whereis -m ls",
    "which(1), find(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    WHEREIS_OPTIONS) {
  using namespace whereis_pipeline;

  if (ctx.get<bool>("-h", false)) {
    safePrintLn("Usage: whereis [OPTION]... NAME...");
    safePrintLn("Locate the binary, source, and manual-page files for NAME.");
    safePrintLn("");
    safePrintLn("  -b        search only for binaries");
    safePrintLn("  -m        search only for manual sections");
    safePrintLn("  -s        search only for sources");
    safePrintLn("  -u        print names with other than exactly one entry");
    safePrintLn("  -B DIR... define binary lookup directories (ends with -f)");
    safePrintLn("  -M DIR... define manual lookup directories (ends with -f)");
    safePrintLn("  -S DIR... define source lookup directories (ends with -f)");
    safePrintLn("  -f        terminate the last directory list");
    safePrintLn("  -l        list the effective lookup paths");
    safePrintLn("  -g        search using glob patterns");
    return 0;
  }
  if (ctx.get<bool>("-V", false)) {
    safePrintLn("whereis (WinuxCmd) 1.0");
    return 0;
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("whereis: missing operand");
      safeErrorPrintLn("Try 'whereis --help' for more information.");
      return 1;
    }
    if (cfg_result.error() == "option -f is missing") {
      safeErrorPrintLn("whereis: option -f is missing");
      return 1;
    }
    cp::report_error(cfg_result, L"whereis");
    return 1;
  }

  return run(*cfg_result);
}
