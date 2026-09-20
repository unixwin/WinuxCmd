// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for namei (display path resolution steps).
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

auto constexpr NAMEI_OPTIONS = std::array{
    OPTION("-a", "--access", "also print access permissions"),
    OPTION("-d", "--noreport", "do not follow symlink if final component"),
    OPTION("-m", "", "show permissions as symbols (rwx)"),
    OPTION("-l", "--long", "long format (permissions and mode info)")};

namespace namei_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool access = false;
  bool noreport = false;
  bool long_format = false;
  SmallVector<std::string, 16> paths;
};

auto build_config(const CommandContext<NAMEI_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.access = ctx.has("-a");
  cfg.long_format = ctx.has("-l") || ctx.has("--long");
  if (ctx.has("-d")) cfg.noreport = true;
  for (auto arg : ctx.positionals) cfg.paths.push_back(std::string(arg));
  if (cfg.paths.empty()) return std::unexpected("missing operand");
  return cfg;
}

// Check if a path component is a symlink (reparse point) and return target.
auto resolve_component(const std::string& path,
                       std::optional<std::string>& target) -> bool {
  target = std::nullopt;
  std::wstring wpath = utf8_to_wstring(path);
  DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return false;

  if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
    HANDLE h = CreateFileW(
        wpath.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    // Query symlink target
    constexpr size_t buf_size = 4096;
    std::vector<wchar_t> buf(buf_size);
    DWORD ret = 0;
    BOOL ok = GetFinalPathNameByHandleW(
        h, buf.data(), static_cast<DWORD>(buf_size), VOLUME_NAME_DOS);
    CloseHandle(h);
    if (ok && ret > 0 && ret <= buf_size) {
      target = wstring_to_utf8(buf.data());
    }
  }
  return true;
}

auto run(const Config& cfg) -> int {
  for (const auto& path : cfg.paths) {
    safePrintLn("");
    safePrintLn(path);

    // Split into components
    SmallVector<std::string, 16> parts;
    size_t start = 0;
    while (start <= path.size()) {
      size_t pos = path.find_first_of("/\\", start);
      if (pos == std::string::npos) {
        std::string part = path.substr(start);
        if (!part.empty()) parts.push_back(part);
        break;
      }
      std::string part = path.substr(start, pos - start);
      if (!part.empty()) parts.push_back(part);
      start = pos + 1;
    }

    // Walk path component by component
    std::string current = "";
    for (size_t i = 0; i < parts.size(); ++i) {
      if (!current.empty() && current.back() != '/' && current.back() != '\\')
        current += "/";
      current += parts[i];

      if (i == 0 && parts[0].empty()) continue;

      std::optional<std::string> target;
      bool is_file = resolve_component(current, target);

      if (!is_file) {
        safePrint("   ");
        for (int j = 0; j < (int)i - 1; j++) safePrint(" ");
        safePrint("f ");
        safePrint(parts[i]);
        safePrintLn(" (No such file or directory)");
        break;
      }

      safePrint("   ");
      for (int j = 0; j < (int)i - 1; j++) safePrint(" ");

      if (target.has_value()) {
        // Symlink detected
        safePrint("s ");
        safePrint(parts[i]);
        safePrintLn(" -> " + *target);
      } else if (target.has_value()) {
        safePrint("L ");
        safePrint(parts[i]);
        safePrintLn("");
      } else {
        // Regular file/dir
        std::wstring wcur = utf8_to_wstring(current);
        DWORD attrs = GetFileAttributesW(wcur.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
          safePrint("f ");
        } else {
          safePrint("d ");
        }
        safePrint(parts[i]);
        safePrintLn("");
      }
    }
  }
  return 0;
}

}  // namespace namei_pipeline

REGISTER_COMMAND(namei, "namei", "namei [OPTION]... PATH...",
                 "Call namei on each PATH, showing any symlink resolution.\n"
                 "Displays the resolution steps for each path component.",
                 "  namei \/usr\/bin\/ls\n"
                 "  namei -l /path/to/file",
                 "readlink(1), realpath(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", NAMEI_OPTIONS) {
  using namespace namei_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("namei: missing operand");
      safeErrorPrintLn("Try 'namei --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"namei");
    return 1;
  }

  return run(*cfg_result);
}
