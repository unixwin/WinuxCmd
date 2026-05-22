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
 *  - File: mkdir.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
///   - @description:
/// @Description: Implemention for mkdir.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;
using namespace core::pipeline;

/**
 * @brief MKDIR command options definition
 *
 * This array defines all the options supported by the mkdir command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -m, @a --mode: Set file mode (as in chmod), not a=rwx - umask
 * [IMPLEMENTED]
 * - @a -p, @a --parents: No error if existing, make parent directories as
 * needed [IMPLEMENTED]
 * - @a -v, @a --verbose: Print a message for each created directory
 * [IMPLEMENTED]
 * - @a -Z, @a --context: Accept SELinux security context option as a
 * Windows-compatible no-op [IMPLEMENTED]
 */
// clang-format off
auto constexpr MKDIR_OPTIONS =
    std::array{OPTION("-m", "--mode", "set file mode (as in chmod), not a=rwx - umask", STRING_TYPE),
               OPTION("-p", "--parents", "no error if existing, make parent directories as needed"),
               OPTION("-v", "--verbose", "print a message for each created directory"),
               OPTION("-Z", "--context", "set SELinux security context of each created directory", OPTIONAL_STRING_TYPE)};
// clang-format on

// ======================================================
// Pipeline components
// ======================================================
namespace mkdir_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool parents = false;
  bool verbose = false;
  std::optional<std::string> mode;
};

/**
 * @brief Check if paths are provided
 * @param paths Paths to check
 * @return Result with paths if valid, error otherwise
 */
auto check_paths(const std::vector<std::string>& paths)
    -> cp::Result<std::vector<std::string>> {
  if (paths.empty()) {
    return std::unexpected("missing operand");
  }
  return paths;
}

/**
 * @brief Create a directory recursively
 * @param wpath Path to create (can contain both / and \ separators)
 * @return true if directory was created successfully, false on error
 */
auto create_directory_recursive(const std::wstring& wpath) -> bool {
  std::string utf8_path = wstring_to_utf8(wpath);
  return path::create_directories(utf8_path);
}

auto mode_allows_write(std::string_view mode) -> cp::Result<bool> {
  if (mode.empty()) return std::unexpected("invalid mode");

  bool numeric = true;
  for (char ch : mode) {
    if (ch < '0' || ch > '7') {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    int value = 0;
    auto [ptr, ec] =
        std::from_chars(mode.data(), mode.data() + mode.size(), value, 8);
    if (ec != std::errc() || ptr != mode.data() + mode.size()) {
      return std::unexpected("invalid mode");
    }
    return (value & 0222) != 0;
  }

  if (mode.find('=') != std::string_view::npos) {
    size_t op = mode.find('=');
    return mode.substr(op + 1).find('w') != std::string_view::npos;
  }
  if (mode.find("+w") != std::string_view::npos) return true;
  if (mode.find("-w") != std::string_view::npos) return false;
  return std::unexpected("unsupported mode");
}

auto apply_directory_mode(const std::string& path, std::string_view mode)
    -> cp::Result<void> {
  auto writable = mode_allows_write(mode);
  if (!writable) return std::unexpected(writable.error());

  std::wstring wpath = utf8_to_wstring(path);
  DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected("cannot read directory attributes");
  }

  if (*writable) {
    attrs &= ~FILE_ATTRIBUTE_READONLY;
  } else {
    attrs |= FILE_ATTRIBUTE_READONLY;
  }
  if (!SetFileAttributesW(wpath.c_str(), attrs)) {
    return std::unexpected("cannot set directory mode");
  }
  return {};
}

auto build_config(const CommandContext<MKDIR_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.parents = ctx.get<bool>("--parents", false) || ctx.get<bool>("-p", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  if (ctx.has("--mode") || ctx.has("-m")) {
    std::string mode = ctx.get<std::string>("--mode", "");
    if (mode.empty()) mode = ctx.get<std::string>("-m", "");
    if (mode.empty()) return std::unexpected("invalid mode");
    auto writable = mode_allows_write(mode);
    if (!writable) return std::unexpected(writable.error());
    cfg.mode = mode;
  }
  return cfg;
}

auto create_directory(const std::string& path, const Config& config) -> bool {
  std::wstring wpath = utf8_to_wstring(path);
  bool existed_before =
      std::filesystem::is_directory(std::filesystem::path(path));

  if (config.parents) {
    if (!create_directory_recursive(wpath)) {
      // OPTIMIZED: Use string literals instead of wstring concatenation
      safeErrorPrint("mkdir: cannot create directory '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      return false;
    }
    if (config.mode && !existed_before) {
      auto mode_result = apply_directory_mode(path, *config.mode);
      if (!mode_result) {
        safeErrorPrint("mkdir: ");
        safeErrorPrint(mode_result.error());
        safeErrorPrint(": '");
        safeErrorPrint(path);
        safeErrorPrint("'\n");
        return false;
      }
    }
    if (config.verbose) {
      safePrint("mkdir: created directory '");
      safePrint(path);
      safePrint("'\n");
    }
  } else {
    if (!CreateDirectoryW(wpath.c_str(), NULL)) {
      DWORD error = GetLastError();
      if (error == ERROR_ALREADY_EXISTS) {
        safeErrorPrint("mkdir: cannot create directory '");
        safeErrorPrint(path);
        safeErrorPrint("': File exists\n");
        return false;
      } else {
        safeErrorPrint("mkdir: cannot create directory '");
        safeErrorPrint(path);
        safeErrorPrint("': No such file or directory\n");
        return false;
      }
    }
    if (config.mode) {
      auto mode_result = apply_directory_mode(path, *config.mode);
      if (!mode_result) {
        safeErrorPrint("mkdir: ");
        safeErrorPrint(mode_result.error());
        safeErrorPrint(": '");
        safeErrorPrint(path);
        safeErrorPrint("'\n");
        return false;
      }
    }
    if (config.verbose) {
      safePrint("mkdir: created directory '");
      safePrint(path);
      safePrint("'\n");
    }
  }
  return true;
}

/**
 * @brief Process all paths
 * @param paths Paths to process
 * @param ctx Command context with options
 * @return Result with success status
 */
auto process_paths(const std::vector<std::string>& paths, const Config& config)
    -> cp::Result<bool> {
  bool success = true;
  for (const auto& path : paths) {
    if (!create_directory(path, config)) {
      success = false;
    }
  }
  return success;
}

/**
 * @brief Main pipeline
 * @param ctx Command context
 * @return Result with success status
 */
auto process_command(const CommandContext<MKDIR_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  auto config = build_config(ctx);
  if (!config) return std::unexpected(config.error());

  std::vector<std::string> paths;
  for (auto arg : ctx.positionals) {
    paths.push_back(std::string(arg));
  }

  return check_paths(paths).and_then(
      [&](const std::vector<std::string>& valid_paths) {
        return process_paths(valid_paths, *config);
      });
}
}  // namespace mkdir_pipeline

REGISTER_COMMAND(
    mkdir,
    /* cmd_name */ "mkdir",
    /* cmd_synopsis */ "make directories",
    /* cmd_desc */
    "Create the DIRECTORY(ies), if they do not already exist.\n"
    "\n"
    "If the directory already exists, mkdir will fail unless the -p option is "
    "used.\n"
    "With -p, mkdir will create parent directories as needed.\n",
    /* examples */
    "  mkdir dir1                Create directory dir1\n"
    "  mkdir -p dir1/dir2/dir3    Create nested directories\n"
    "  mkdir -v dir1 dir2         Verbose create directories\n"
    "  mkdir -pv dir1/dir2        Verbose create nested directories",
    /* see_also */ "rmdir(1), rm(1), cp(1), mv(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    MKDIR_OPTIONS) {
  using namespace mkdir_pipeline;

  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"mkdir");
    return 1;
  }

  return *result ? 0 : 1;
}
