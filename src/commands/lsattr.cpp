// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for lsattr.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LSATTR_OPTIONS = std::array{
    // [EXT]
    OPTION("-R", "--recursive", "recursively list attributes of directories"),
    // [EXT]
    OPTION("-d", "--directory", "list directories themselves, not contents"),
    // [GNU]
    OPTION("-a", "--all", "list all files including those starting with '.'"),
    // [GNU]
    OPTION("-V", "--version", "display program version"),
    // [GNU]
    OPTION("-l", "--long",
           "print options using long names instead of abbreviations")};

namespace lsattr_pipeline {

struct Config {
  bool recursive = false;
  bool directory = false;
  bool all = false;
  bool long_names = false;
  std::vector<std::string> paths;
};

auto format_attrs(DWORD attrs) -> std::string {
  std::string out;
  out.reserve(12);
  out.push_back((attrs & FILE_ATTRIBUTE_READONLY) != 0 ? 'R' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_HIDDEN) != 0 ? 'H' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_SYSTEM) != 0 ? 'S' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_ARCHIVE) != 0 ? 'A' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 ? 'D' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_COMPRESSED) != 0 ? 'C' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_ENCRYPTED) != 0 ? 'E' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) != 0 ? 'I' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_TEMPORARY) != 0 ? 'T' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_OFFLINE) != 0 ? 'O' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_SPARSE_FILE) != 0 ? 'P' : '-');
  out.push_back((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? 'L' : '-');
  return out;
}

auto format_attrs_long(DWORD attrs) -> std::string {
  std::string out;
  auto add = [&](DWORD flag, const char* name) {
    if ((attrs & flag) != 0) {
      if (!out.empty()) out += ', ';
      out += name;
    }
  };
  add(FILE_ATTRIBUTE_READONLY, "readonly");
  add(FILE_ATTRIBUTE_HIDDEN, "hidden");
  add(FILE_ATTRIBUTE_SYSTEM, "system");
  add(FILE_ATTRIBUTE_ARCHIVE, "archive");
  add(FILE_ATTRIBUTE_DIRECTORY, "directory");
  add(FILE_ATTRIBUTE_COMPRESSED, "compressed");
  add(FILE_ATTRIBUTE_ENCRYPTED, "encrypted");
  add(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, "not-indexed");
  add(FILE_ATTRIBUTE_TEMPORARY, "temporary");
  add(FILE_ATTRIBUTE_OFFLINE, "offline");
  add(FILE_ATTRIBUTE_SPARSE_FILE, "sparse");
  add(FILE_ATTRIBUTE_REPARSE_POINT, "reparse-point");
  if (out.empty()) out = "none";
  return out;
}

auto print_one(std::string_view display_path) -> bool {
  auto operand = native_path::make_api_path_operand(display_path);
  DWORD attrs = GetFileAttributesW(operand.extended.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    safeErrorPrintLn("lsattr: cannot access '" + std::string(display_path) +
                     "': " + win32_posix_error_text(GetLastError()));
    return false;
  }

  safePrint(format_attrs(attrs));
  safePrint(" ");
  safePrintLn(std::string(display_path));
  return true;
}

auto join_child_path(std::wstring_view parent, std::wstring_view child)
    -> std::wstring {
  std::wstring out(parent);
  if (!out.empty() && out.back() != L'\\' && out.back() != L'/') {
    out.push_back(L'\\');
  }
  out.append(child);
  return out;
}

auto list_directory_children(std::wstring_view path, bool recursive, bool all,
                             bool long_names, bool& ok) -> void {
  std::wstring pattern = join_child_path(path, L"*");
  WIN32_FIND_DATAW data{};
  UniqueFindHandle find(FindFirstFileW(pattern.c_str(), &data));
  if (!find) {
    DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) {
      safeErrorPrintLn(L"lsattr: cannot read directory '" + std::wstring(path) +
                       L"': " + utf8_to_wstring(win32_posix_error_text(error)));
      ok = false;
    }
    return;
  }

  do {
    std::wstring name = data.cFileName;
    if (!all && (name == L"." || name == L"..")) continue;

    std::wstring child = join_child_path(path, name);
    std::string display = wstring_to_utf8(child);
    if (long_names) {
      safePrint(format_attrs_long(data.dwFileAttributes));
    } else {
      safePrint(format_attrs(data.dwFileAttributes));
    }
    safePrint(" ");
    safePrintLn(display);

    const bool is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool is_link =
        (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (recursive && is_dir && !is_link) {
      list_directory_children(child, true, all, long_names, ok);
    }
  } while (FindNextFileW(find.get(), &data));
}

auto list_path(const Config& cfg, std::string_view path) -> bool {
  auto operand = native_path::make_api_path_operand(path);
  DWORD attrs = GetFileAttributesW(operand.extended.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    safeErrorPrintLn("lsattr: cannot access '" + std::string(path) +
                     "': " + win32_posix_error_text(GetLastError()));
    return false;
  }

  if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || cfg.directory) {
    return print_one(path);
  }

  bool ok = true;
  list_directory_children(operand.extended, cfg.recursive, cfg.all,
                          cfg.long_names, ok);
  return ok;
}

auto run(const CommandContext<LSATTR_OPTIONS.size()>& ctx) -> int {
  Config cfg;
  cfg.recursive =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);
  cfg.directory =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--directory", false);
  cfg.all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);
  cfg.long_names = ctx.get<bool>("-l", false) || ctx.get<bool>("--long", false);

  // --version: print version and exit
  if (ctx.get<bool>("-V", false) || ctx.get<bool>("--version", false)) {
    safePrintLn("lsattr (WinuxCmd) 0.1.0");
    return 0;
  }

  for (auto arg : ctx.positionals) cfg.paths.emplace_back(arg);
  if (cfg.paths.empty()) cfg.paths.emplace_back(".");

  bool ok = true;
  for (const auto& path : cfg.paths) {
    ok = list_path(cfg, path) && ok;
  }
  return ok ? 0 : 1;
}

}  // namespace lsattr_pipeline

REGISTER_COMMAND(
    lsattr, "lsattr", "lsattr [-RVadl] [FILE]...",
    "List Windows file attributes in a stable flag string.\n"
    "Flags are RHSADCEITOPL for readonly, hidden, system, "
    "archive, directory, compressed, encrypted, not indexed, "
    "temporary, offline, sparse, and reparse point.\n"
    "[DIFFERS] Attribute flags differ from GNU lsattr (Linux ext4).",
    "  lsattr file.txt\n"
    "  lsattr -d directory",
    "chattr(1), stat(1), attrib.exe", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    LSATTR_OPTIONS) {
  return lsattr_pipeline::run(ctx);
}
