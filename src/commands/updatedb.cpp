// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for updatedb (update the locate database).
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

// [GNU] updatedb.sh accepts --output=FILE, --localpaths='...' and
// --prunepaths='...'.  The local paths and prune paths are semicolon
// separated here (a documented deviation: GNU relies on shell word
// splitting, which our option parser does not perform).
auto constexpr UPDATEDB_OPTIONS = std::array{
    OPTION("-d", "--directory", "directory to start walking", STRING_TYPE),
    OPTION("-e", "--exclude", "prune entries whose path contains this pattern",
           STRING_TYPE),
    OPTION("-f", "", "accepted for compatibility; ignored", BOOL_TYPE),
    // [GNU] updatedb.sh: "--localpaths='path1 path2...'" (semicolon separated
    // in WinuxCmd because the option parser does not word-split arguments)
    OPTION("", "--localpaths",
           "walk these roots (semicolon separated) instead of --directory",
           STRING_TYPE),
    OPTION("-o", "--output", "database output file", STRING_TYPE),
    // [GNU] updatedb.sh: "--prunepaths='/tmp /var/spool'" (semicolon
    // separated in WinuxCmd; pruned directories are not descended into)
    OPTION("", "--prunepaths",
           "directories (semicolon separated) not to descend into",
           STRING_TYPE),
    OPTION("-s", "", "accepted for compatibility; ignored", BOOL_TYPE),
    OPTION("-v", "--verbose", "be verbose", BOOL_TYPE),
    OPTION("-x", "--exclude-dir", "directory name pattern to exclude",
           STRING_TYPE)};

namespace updatedb_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string directory = ".";
  std::string output;
  std::string localpaths;
  std::string prunepaths;
  std::string exclude;
  std::string exclude_dir;
  bool verbose = false;
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

auto build_config(const CommandContext<UPDATEDB_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  if (ctx.has("-d")) cfg.directory = ctx.get<std::string>("-d", ".");
  if (ctx.has("--directory"))
    cfg.directory = ctx.get<std::string>("--directory", ".");
  if (ctx.has("-o")) cfg.output = ctx.get<std::string>("-o", "");
  if (ctx.has("--output")) cfg.output = ctx.get<std::string>("--output", "");
  if (ctx.has("--localpaths"))
    cfg.localpaths = ctx.get<std::string>("--localpaths", "");
  if (ctx.has("--prunepaths"))
    cfg.prunepaths = ctx.get<std::string>("--prunepaths", "");
  if (ctx.has("-e")) cfg.exclude = ctx.get<std::string>("-e", "");
  if (ctx.has("--exclude")) cfg.exclude = ctx.get<std::string>("--exclude", "");
  if (ctx.has("-x")) cfg.exclude_dir = ctx.get<std::string>("-x", "");
  if (ctx.has("--exclude-dir"))
    cfg.exclude_dir = ctx.get<std::string>("--exclude-dir", "");
  cfg.verbose = ctx.has("-v") || ctx.has("--verbose");
  if (cfg.output.empty()) cfg.output = get_default_database();
  return cfg;
}

// Normalize a stored path to forward slashes with no trailing separator so
// that prefix comparisons for pruning are stable.
auto normalize_stored(const std::string& path) -> std::string {
  std::string out = path;
  for (char& c : out) {
    if (c == '\\') c = '/';
  }
  while (out.size() > 1 && out.back() == '/') out.pop_back();
  return out;
}

auto split_semicolon_list(const std::string& text) -> std::vector<std::string> {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= text.size()) {
    size_t sep = text.find(';', start);
    if (sep == std::string::npos) {
      std::string part = text.substr(start);
      if (!part.empty()) parts.push_back(part);
      break;
    }
    std::string part = text.substr(start, sep - start);
    if (!part.empty()) parts.push_back(part);
    start = sep + 1;
  }
  return parts;
}

// Make a walk root absolute so that --prunepaths prefix comparisons (which
// users give as absolute paths, like GNU updatedb) match stored entries.
auto absolute_path(const std::string& path) -> std::string {
  std::wstring w = utf8_to_wstring(path);
  DWORD size = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
  if (size == 0) return path;
  std::wstring buffer(size, L'\0');
  DWORD written = GetFullPathNameW(w.c_str(), size, buffer.data(), nullptr);
  buffer.resize(written);
  return wstring_to_utf8(buffer);
}

// Recursively walk a directory and collect all file/directory paths.
// [GNU] updatedb.sh skip_names / PRUNEFS: pruned directories are never
// descended into, and their paths are not recorded either.
auto walk_dir(const std::string& dir, const Config& cfg,
              const std::vector<std::string>& pruned_prefixes,
              std::vector<std::string>& results) -> void {
  std::wstring pattern = utf8_to_wstring(dir) + L"\\*";
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;

  do {
    std::wstring name(fd.cFileName);
    if (name == L"." || name == L"..") continue;
    std::string utf8name = wstring_to_utf8(name);

    std::string full = dir;
    if (full.back() != '/' && full.back() != '\\') full += "/";
    full += utf8name;

    std::string stored = normalize_stored(full);

    // --prunepaths: skip directories whose stored path is a pruned prefix
    // (matched on path-component boundaries).
    bool pruned = false;
    for (const auto& prefix : pruned_prefixes) {
      std::string norm_prefix = normalize_stored(prefix);
      if (stored == norm_prefix || (stored.size() > norm_prefix.size() &&
                                    stored.starts_with(norm_prefix) &&
                                    stored[norm_prefix.size()] == '/')) {
        pruned = true;
        break;
      }
    }
    if (pruned) continue;

    // -x/--exclude-dir: never descend into directories whose *name* matches
    // the given wildcard pattern.
    if (!cfg.exclude_dir.empty() &&
        fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
        wildcard_match(cfg.exclude_dir, utf8name, false)) {
      continue;
    }

    // -e/--exclude: record nothing for entries whose path contains the
    // pattern (kept for compatibility with the previous behaviour).
    if (!cfg.exclude.empty() && stored.find(cfg.exclude) != std::string::npos) {
      continue;
    }

    results.push_back(stored);

    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      walk_dir(full, cfg, pruned_prefixes, results);
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

// [GNU] updatedb.sh writes the database to a temporary file and renames it
// into place, so a failed run never leaves a truncated database behind.
auto write_database_atomically(const std::string& output,
                               const std::string& content) -> bool {
  std::wstring woutput = utf8_to_wstring(output);
  std::wstring wtemp = woutput + L".tmp";

  // Create parent directory if needed
  {
    size_t last_slash = woutput.find_last_of(L"/\\");
    if (last_slash != std::wstring::npos) {
      std::wstring parent = woutput.substr(0, last_slash);
      CreateDirectoryW(parent.c_str(), nullptr);
    }
  }

  HANDLE h = CreateFileW(wtemp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    safeErrorPrintLn("updatedb: cannot create database '" + output + "'");
    return false;
  }

  DWORD written = 0;
  BOOL ok = WriteFile(h, content.data(), static_cast<DWORD>(content.size()),
                      &written, nullptr);
  CloseHandle(h);
  if (!ok || written != static_cast<DWORD>(content.size())) {
    safeErrorPrintLn("updatedb: failed to write database");
    DeleteFileW(wtemp.c_str());
    return false;
  }

  if (!MoveFileExW(wtemp.c_str(), woutput.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    safeErrorPrintLn("updatedb: failed to replace database '" + output + "'");
    DeleteFileW(wtemp.c_str());
    return false;
  }
  return true;
}

auto run(const Config& cfg) -> int {
  std::vector<std::string> roots;
  if (!cfg.localpaths.empty()) {
    // [GNU] updatedb.sh: --localpaths replaces the default walk roots.
    for (const auto& root : split_semicolon_list(cfg.localpaths)) {
      roots.push_back(absolute_path(root));
    }
  } else {
    roots.push_back(absolute_path(cfg.directory));
  }

  auto pruned_prefixes = split_semicolon_list(cfg.prunepaths);

  std::vector<std::string> paths;
  for (const auto& root : roots) {
    if (cfg.verbose) {
      safePrintLn("Walking " + root + "...");
    }
    walk_dir(root, cfg, pruned_prefixes, paths);
  }

  if (cfg.verbose) {
    safePrint("Found " + std::to_string(paths.size()) + " paths. ");
  }

  // Sort
  std::sort(paths.begin(), paths.end());

  std::string content;
  for (const auto& p : paths) {
    content += p;
    content += "\n";
  }

  if (!write_database_atomically(cfg.output, content)) {
    return 1;
  }

  if (cfg.verbose) {
    safePrint("Database written to " + cfg.output + ".");
    safePrintLn("");
  } else {
    safePrintLn("Database created at " + cfg.output);
  }
  return 0;
}

}  // namespace updatedb_pipeline

REGISTER_COMMAND(updatedb, "updatedb", "updatedb [OPTION]...",
                 "Update the filename database used by locate.\n"
                 "Walks the filesystem and writes a sorted path list.\n"
                 "The database is written to a temporary file and renamed\n"
                 "into place, replacing any previous database atomically.\n"
                 "Database entries are plain path lines (a Windows platform\n"
                 "deviation from GNU LOCATE02).",
                 "  updatedb\n"
                 "  updatedb -d /path\n"
                 "  updatedb --localpaths='C:/Users;D:/data' "
                 "--prunepaths='C:/Users/Default'\n"
                 "  updatedb -d C:\\Windows -v",
                 "locate(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 UPDATEDB_OPTIONS) {
  using namespace updatedb_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"updatedb");
    return 1;
  }

  return run(*cfg_result);
}
