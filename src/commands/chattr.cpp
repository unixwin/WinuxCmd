// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for chattr.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHATTR_OPTIONS = std::array{
    // [EXT]
    OPTION("-R", "--recursive", "recursively change attributes of directories"),
    // [EXT]
    OPTION("-V", "--verbose", "print changed files"),
    // [GNU]
    OPTION("-f", "--force", "suppress most error messages")};

namespace chattr_pipeline {

struct Config {
  bool recursive = false;
  bool verbose = false;
  bool force = false;
  char op = '\0';
  DWORD set_bits = 0;
  DWORD clear_bits = 0;
  std::vector<std::string> paths;
};

auto bit_for_flag(char flag) -> std::optional<DWORD> {
  switch (flag) {
    case 'R':
    case 'r':
      return FILE_ATTRIBUTE_READONLY;
    case 'H':
    case 'h':
      return FILE_ATTRIBUTE_HIDDEN;
    case 'S':
    case 's':
      return FILE_ATTRIBUTE_SYSTEM;
    case 'A':
    case 'a':
      return FILE_ATTRIBUTE_ARCHIVE;
    case 'I':
    case 'i':
      return FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    case 'T':
    case 't':
      return FILE_ATTRIBUTE_TEMPORARY;
    case 'O':
    case 'o':
      return FILE_ATTRIBUTE_OFFLINE;
    default:
      return std::nullopt;
  }
}

auto parse_mode(std::string_view mode, Config& cfg) -> bool {
  if (mode.size() < 2 ||
      (mode.front() != '+' && mode.front() != '-' && mode.front() != '=')) {
    safeErrorPrintLn("chattr: mode must start with '+', '-' or '='");
    return false;
  }

  cfg.op = mode.front();
  for (size_t i = 1; i < mode.size(); ++i) {
    auto bit = bit_for_flag(mode[i]);
    if (!bit) {
      safeErrorPrintLn(std::string("chattr: unsupported attribute '") +
                       mode[i] + "'");
      return false;
    }
    if (cfg.op == '-') {
      cfg.clear_bits |= *bit;
    } else {
      cfg.set_bits |= *bit;
    }
  }
  return true;
}

auto apply_one(const Config& cfg, std::wstring_view path) -> bool {
  DWORD attrs = GetFileAttributesW(std::wstring(path).c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    if (!cfg.force) {
      safeErrorPrintLn(L"chattr: cannot access '" + std::wstring(path) +
                       L"': " +
                       utf8_to_wstring(win32_posix_error_text(GetLastError())));
    }
    return false;
  }

  DWORD new_attrs = attrs;
  if (cfg.op == '=') {
    new_attrs &= ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                   FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE |
                   FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
                   FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE);
    new_attrs |= cfg.set_bits;
  } else {
    new_attrs |= cfg.set_bits;
    new_attrs &= ~cfg.clear_bits;
  }

  if (new_attrs == attrs) return true;
  if (!SetFileAttributesW(std::wstring(path).c_str(), new_attrs)) {
    if (!cfg.force) {
      safeErrorPrintLn(L"chattr: failed to set attributes for '" +
                       std::wstring(path) + L"': " +
                       utf8_to_wstring(win32_posix_error_text(GetLastError())));
    }
    return false;
  }

  if (cfg.verbose) {
    safePrintLn(L"chattr: changed attributes of '" + std::wstring(path) + L"'");
  }
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

auto apply_recursive(const Config& cfg, std::wstring_view path) -> bool {
  bool ok = apply_one(cfg, path);

  DWORD attrs = GetFileAttributesW(std::wstring(path).c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return ok;
  }

  std::wstring pattern = join_child_path(path, L"*");
  WIN32_FIND_DATAW data{};
  UniqueFindHandle find(FindFirstFileW(pattern.c_str(), &data));
  if (!find) return ok;

  do {
    std::wstring name = data.cFileName;
    if (name == L"." || name == L"..") continue;
    std::wstring child = join_child_path(path, name);
    ok = apply_recursive(cfg, child) && ok;
  } while (FindNextFileW(find.get(), &data));

  return ok;
}

auto run(const CommandContext<CHATTR_OPTIONS.size()>& ctx) -> int {
  if (ctx.positionals.size() < 2) {
    safeErrorPrintLn("chattr: missing mode or file operand");
    safeErrorPrintLn("Try 'chattr --help' for more information.");
    return 1;
  }

  Config cfg;
  cfg.recursive =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);
  cfg.verbose = ctx.get<bool>("-V", false) || ctx.get<bool>("--verbose", false);
  cfg.force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  if (!parse_mode(ctx.positionals.front(), cfg)) return 1;
  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    cfg.paths.emplace_back(ctx.positionals[i]);
  }

  bool ok = true;
  for (const auto& path : cfg.paths) {
    auto operand = native_path::make_api_path_operand(path);
    ok = (cfg.recursive ? apply_recursive(cfg, operand.extended)
                        : apply_one(cfg, operand.extended)) &&
         ok;
  }
  return ok ? 0 : 1;
}

}  // namespace chattr_pipeline

REGISTER_COMMAND(
    chattr, "chattr", "chattr [-RVf] MODE FILE...",
    "Change Windows file attributes.\n"
    "Supported flags are R,H,S,A,I,T,O for readonly, hidden, "
    "system, archive, not indexed, temporary, and offline.\n"
    "[DIFFERS] Attribute flags differ from GNU chattr (Linux ext4).",
    "  chattr +RH file.txt\n"
    "  chattr -R -H directory",
    "lsattr(1), chmod(1), attrib.exe", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CHATTR_OPTIONS) {
  return chattr_pipeline::run(ctx);
}
