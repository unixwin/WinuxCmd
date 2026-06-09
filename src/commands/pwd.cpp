/*
 *  Copyright  2026 [caomengxuan666]
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
 *  - File: pwd.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @contributors:
///   - @contributor1 arookieofc <2128194521@qq.com>
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for pwd.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright  2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

/**
 * @brief PWD command options definition
 *
 * This array defines all the options supported by the pwd command.
 * Each option is described with its short form, long form, and description.
 *
 * @par Options:
 *
 * - @a -L, @a --logical: Use PWD from environment, even if it contains symlinks
 * [IMPLEMENTED]
 * - @a -P, @a --physical: Avoid all symlinks [IMPLEMENTED]
 */

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PWD_OPTIONS =
    std::array{OPTION(
                   "-L", "--logical",
                   "use PWD from environment when it names the current directory"),
               OPTION("-P", "--physical", "avoid all symlinks")};

// ======================================================
// Pipeline components
// ======================================================
namespace pwd_pipeline {
namespace cp = core::pipeline;

enum class PathMode {
  physical,
  logical,
};

auto determine_path_mode(const CommandContext<PWD_OPTIONS.size()>& ctx)
    -> PathMode {
  PathMode mode = PathMode::physical;
  if (GetEnvironmentVariableW(L"POSIXLY_CORRECT", nullptr, 0) != 0) {
    mode = PathMode::logical;
  }

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= PWD_OPTIONS.size()) {
      continue;
    }

    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.long_name == "--logical" || meta.short_name == "-L") {
      mode = PathMode::logical;
      continue;
    }

    if (meta.long_name == "--physical" || meta.short_name == "-P") {
      mode = PathMode::physical;
    }
  }
  return mode;
}

auto get_pwd_environment() -> std::optional<std::string> {
  DWORD buffer_size = GetEnvironmentVariableW(L"PWD", nullptr, 0);
  if (buffer_size == 0) {
    return std::nullopt;
  }

  std::wstring value(buffer_size - 1, L'\0');
  if (GetEnvironmentVariableW(L"PWD", value.data(), buffer_size) == 0) {
    return std::nullopt;
  }

  return wstring_to_utf8(value);
}

auto is_absolute_pwd_value(const std::string& path) -> bool {
  if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    return true;
  }

  if (path.size() >= 2 &&
      ((path[0] == '\\' && path[1] == '\\') ||
       (path[0] == '/' && path[1] == '/'))) {
    return true;
  }

  return !path.empty() && (path[0] == '\\' || path[0] == '/');
}

auto has_dot_components(const std::string& path) -> bool {
  std::filesystem::path fs_path = utf8_to_wstring(path);
  for (const auto& component : fs_path) {
    if (component == L"." || component == L"..") {
      return true;
    }
  }
  return false;
}

auto get_physical_current_directory() -> cp::Result<std::string> {
  DWORD bufferSize = GetCurrentDirectoryW(0, NULL);
  if (bufferSize == 0) {
    return std::unexpected("cannot get current directory");
  }

  std::wstring wCurrentDir(bufferSize, L'\0');
  DWORD result = GetCurrentDirectoryW(bufferSize, &wCurrentDir[0]);
  if (result == 0 || result >= bufferSize) {
    return std::unexpected("cannot get current directory");
  }

  wCurrentDir.resize(result);
  return wstring_to_utf8(wCurrentDir);
}

auto get_validated_logical_pwd() -> std::optional<std::string> {
  auto pwd = get_pwd_environment();
  if (!pwd.has_value() || pwd->empty() || !is_absolute_pwd_value(*pwd) ||
      has_dot_components(*pwd)) {
    return std::nullopt;
  }

  auto current_dir = get_physical_current_directory();
  if (!current_dir) {
    return std::nullopt;
  }

  std::error_code ec;
  std::filesystem::path pwd_path = utf8_to_wstring(*pwd);
  if (!std::filesystem::exists(pwd_path, ec) || ec) {
    return std::nullopt;
  }

  ec.clear();
  std::filesystem::path current_path = utf8_to_wstring(*current_dir);
  if (std::filesystem::equivalent(pwd_path, current_path, ec) && !ec) {
    return *pwd;
  }

  return std::nullopt;
}

// ----------------------------------------------
// 1. Get current working directory
// ----------------------------------------------
auto get_current_directory(const CommandContext<PWD_OPTIONS.size()>& ctx)
    -> cp::Result<std::string> {
  auto mode = determine_path_mode(ctx);

  if (mode == PathMode::logical) {
    if (auto pwd = get_validated_logical_pwd(); pwd.has_value()) {
      return *pwd;
    }
  }

  return get_physical_current_directory();
}

// ----------------------------------------------
// 2. Print current directory
// ----------------------------------------------
auto print_directory(const std::string& path) -> cp::Result<bool> {
  safePrint(path);
  safePrint("\n");
  return true;
}

// ----------------------------------------------
// 3. Main pipeline
// ----------------------------------------------
template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  if (!ctx.positionals.empty()) {
    safeErrorPrint("pwd: ignoring non-option arguments\n");
  }
  return get_current_directory(ctx).and_then(
      [](const std::string& path) { return print_directory(path); });
}

}  // namespace pwd_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    pwd,
    /* name */
    "pwd",

    /* synopsis */
    "print name of current/working directory",

    /* description */
    "Print the full filename of the current working directory.\n"
    "\n"
    "The -L option uses PWD from environment when it still names the current\n"
    "directory.\n"
    "The -P option avoids all symlinks.",

    /* examples */
    "  pwd                  Print the current working directory\n"
    "  pwd -L               Print logical current directory\n"
    "  pwd -P               Print physical current directory",

    /* see also */
    "cd(1), ls(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright  2026 WinuxCmd",

    /* options */
    PWD_OPTIONS) {
  using namespace pwd_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"pwd");
    return 1;
  }

  return *result ? 0 : 1;
}
