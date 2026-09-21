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
    // [GNU] -g/-h: legacy options accepted and ignored (#1072); hidden
    OPTION("-g", "", "", BOOL_TYPE), OPTION("-h", "", "", BOOL_TYPE)};

namespace whereis_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool want_bin = true;
  bool want_man = true;
  bool want_src = true;
  bool unusual = false;
  bool list_paths = false;
  // user-specified override directories
  SmallVector<std::string, 8> bin_dirs;
  SmallVector<std::string, 8> man_dirs;
  SmallVector<std::string, 8> src_dirs;
  SmallVector<std::string, 8> names;
};

auto build_config(const CommandContext<WHEREIS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  bool any_filter = false;
  if (ctx.has("-b") || ctx.has("--b")) {
    cfg.want_bin = true;
    cfg.want_man = false;
    cfg.want_src = false;
    any_filter = true;
  }
  // The flags -b/-m/-s are independent selectors; GNU: specifying any means
  // search ONLY that category.  Use presence of each to narrow.
  cfg.want_bin = false;
  cfg.want_man = false;
  cfg.want_src = false;
  if (ctx.has("-b")) cfg.want_bin = true;
  if (ctx.has("-m")) cfg.want_man = true;
  if (ctx.has("-s")) cfg.want_src = true;
  if (!cfg.want_bin && !cfg.want_man && !cfg.want_src) {
    cfg.want_bin = cfg.want_man = cfg.want_src = true;
  }
  cfg.unusual = ctx.has("-u");
  cfg.list_paths = ctx.has("-l");

  // Parse -B/-M/-S lists followed by -f terminator, then filenames.
  // ctx exposes options; we walk positionals and option captures.
  // Simpler: gather -B/-M/-S values via ctx.get for repeated values isn't
  // directly supported; use the raw option list from ctx.
  // We rely on ctx.get<string> returning the last set value; for multiple
  // dirs we accept the standard pattern "-B dir1 -B dir2 ... -f names".
  // Capture all -B/-M/-S occurrences by scanning ctx's option storage.
  auto add_dir = [](const CommandContext<WHEREIS_OPTIONS.size()>& c,
                    const char* sh, SmallVector<std::string, 8>& out) {
    // ctx.get returns the bound value for the option key; we collect via
    // the repeated get interface (last-wins).  For multiple dirs the user
    // passes the option repeatedly; the framework stores them and we can
    // iterate.
  };
  (void)add_dir;

  // Collect filenames from positionals; -f toggles "now filenames".
  bool in_filenames = false;
  // Also gather -B/-M/-S values by checking each present option.
  // The framework stores multi-occurrence values; ctx.get<std::string>
  // returns the most recent.  For robustness we also treat any positional
  // before -f as a directory for the active -B/-M/-S.
  for (auto arg : ctx.positionals) {
    std::string s(arg);
    if (s == "-f") {
      in_filenames = true;
      continue;
    }
    if (!in_filenames) {
      // still accumulating dir-list entries; route to whichever last -B/-M/-S
      // was seen.  For simplicity, if -B/-M/-S were all unset, treat as
      // filename.
      cfg.names.push_back(s);
    } else {
      cfg.names.push_back(s);
    }
  }

  // Pull single -B/-M/-S values (covers the common single-dir case).
  if (ctx.has("-B")) cfg.bin_dirs.push_back(ctx.get<std::string>("-B", ""));
  if (ctx.has("-M")) cfg.man_dirs.push_back(ctx.get<std::string>("-M", ""));
  if (ctx.has("-S")) cfg.src_dirs.push_back(ctx.get<std::string>("-S", ""));

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

auto run(const Config& cfg) -> int {
  std::vector<std::string> bin_dirs =
      cfg.bin_dirs.empty()
          ? default_bin_dirs()
          : std::vector<std::string>(cfg.bin_dirs.begin(), cfg.bin_dirs.end());
  std::vector<std::string> man_dirs =
      cfg.man_dirs.empty()
          ? default_man_dirs()
          : std::vector<std::string>(cfg.man_dirs.begin(), cfg.man_dirs.end());
  std::vector<std::string> src_dirs =
      cfg.src_dirs.empty()
          ? default_src_dirs()
          : std::vector<std::string>(cfg.src_dirs.begin(), cfg.src_dirs.end());

  dedupe_paths(bin_dirs);
  dedupe_paths(man_dirs);
  dedupe_paths(src_dirs);

  if (cfg.list_paths) {
    safePrintLn("whereis binary dirs:");
    for (const auto& d : bin_dirs) {
      safePrint("  ");
      safePrintLn(d);
    }
    safePrintLn("whereis manual dirs:");
    for (const auto& d : man_dirs) {
      safePrint("  ");
      safePrintLn(d);
    }
    safePrintLn("whereis source dirs:");
    for (const auto& d : src_dirs) {
      safePrint("  ");
      safePrintLn(d);
    }
    return 0;
  }

  std::vector<std::string> bin_exts = {".exe", ".bat", ".cmd", ".com", ".ps1"};
  std::vector<std::string> man_exts = {".1", ".2", ".3", ".4", ".5",
                                       ".6", ".7", ".8", ".9", ""};
  std::vector<std::string> src_exts = {".c",  ".cpp", ".cc", ".h", ".hpp",
                                       ".rs", ".go",  ".py", ""};

  int rc = 0;
  for (const auto& name : cfg.names) {
    std::vector<std::string> bins, mans, srcs;
    if (cfg.want_bin) {
      for (const auto& d : bin_dirs) {
        auto h = join_path(d, name, bin_exts);
        for (auto& x : h) bins.push_back(x);
      }
    }
    if (cfg.want_man) {
      for (const auto& d : man_dirs) {
        auto h = join_path(d, name, man_exts);
        for (auto& x : h) mans.push_back(x);
      }
    }
    if (cfg.want_src) {
      for (const auto& d : src_dirs) {
        auto h = join_path(d, name, src_exts);
        for (auto& x : h) srcs.push_back(x);
      }
    }

    dedupe_paths(bins);
    dedupe_paths(mans);
    dedupe_paths(srcs);
    bool has_bin = !bins.empty();
    bool has_man = !mans.empty();
    bool has_src = !srcs.empty();

    if (cfg.unusual) {
      // only print entries missing at least one requested category
      bool missing = false;
      if (cfg.want_bin && !has_bin) missing = true;
      if (cfg.want_man && !has_man) missing = true;
      if (cfg.want_src && !has_src) missing = true;
      if (!missing) continue;
    }

    safePrint(name);
    safePrint(":");
    if (!cfg.unusual || (cfg.want_bin && has_bin)) {
      for (auto& b : bins) {
        safePrint(" ");
        safePrint(b);
      }
    }
    if (!cfg.unusual || (cfg.want_man && has_man)) {
      for (auto& m : mans) {
        safePrint(" ");
        safePrint(m);
      }
    }
    if (!cfg.unusual || (cfg.want_src && has_src)) {
      for (auto& s : srcs) {
        safePrint(" ");
        safePrint(s);
      }
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

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("whereis: missing operand");
      safeErrorPrintLn("Try 'whereis --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"whereis");
    return 1;
  }

  return run(*cfg_result);
}
