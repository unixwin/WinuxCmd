// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    std::array{
               // [DIFFERS]
               OPTION("-m", "--mode", "set file mode (as in chmod), not a=rwx - umask", STRING_TYPE),
               // [GNU]
               OPTION("-p", "--parents", "no error if existing, make parent directories as needed"),
               // [GNU]
               OPTION("-v", "--verbose", "print a message for each created directory"),
               // [DIFFERS] SELinux contexts are not available on Windows; accepted as a no-op.
               OPTION("-Z", "--context", "set SELinux security context of each created directory (ignored on Windows)", OPTIONAL_STRING_TYPE)};
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
  // [GNU] mkdir -p creates arbitrarily deep trees (GNU mkancesdirs chdirs
  // into components, so it is not bound by PATH_MAX).  Route through a
  // \\?\ extended-length path so Windows accepts >MAX_PATH results too
  // (uutils#11038, issue 297).
  return native_path::create_directories_w(
      native_path::to_extended_path(native_path::normalize_separators(wpath)));
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

  // Extended API path so -m works on >MAX_PATH directories too (#1061).
  const std::wstring wpath = native_path::to_extended_path(
      native_path::strip_trailing_separators(utf8_to_wstring(path)));
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
    // [DIFFERS] - SELinux not available on Windows
    (void)ctx.has("--context");
    std::string mode = ctx.get<std::string>("--mode", "");
    if (mode.empty()) mode = ctx.get<std::string>("-m", "");
    if (mode.empty()) return std::unexpected("invalid mode");
    auto writable = mode_allows_write(mode);
    if (!writable) return std::unexpected(writable.error());
    cfg.mode = mode;
  }
  return cfg;
}

auto is_separator(char ch) -> bool { return ch == '\\' || ch == '/'; }

auto progressive_directory_paths(std::string_view path)
    -> std::vector<std::string>;

/**
 * @brief True when PATH names an existing non-directory object.
 */
auto path_exists_not_directory(const std::string& path) -> bool {
  const auto operand = native_path::make_api_path_operand(path);
  DWORD attrs = GetFileAttributesW(operand.extended.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/**
 * @brief GNU-style mkdir error for a failed -p creation.  GNU reports
 * EEXIST ("File exists") when the final component exists as a
 * non-directory and ENOTDIR naming the offending intermediate component
 * when a prefix is not a directory (uutils#11038 follow-up, issue 297).
 */
auto report_parents_create_failure(const std::string& path) -> void {
  const auto components = progressive_directory_paths(path);
  for (size_t i = 0; i < components.size(); ++i) {
    if (!path_exists_not_directory(components[i])) continue;
    if (i + 1 == components.size()) {
      safeErrorPrint("mkdir: cannot create directory '");
      safeErrorPrint(components[i]);
      safeErrorPrint("': File exists\n");
    } else {
      safeErrorPrint("mkdir: cannot create directory '");
      safeErrorPrint(components[i]);
      safeErrorPrint("': Not a directory\n");
    }
    return;
  }
  safeErrorPrint("mkdir: cannot create directory '");
  safeErrorPrint(path);
  safeErrorPrint("': No such file or directory\n");
}

/**
 * @brief GNU-style mkdir error for the non -p CreateDirectoryW failure.
 */
auto report_create_failure(const std::string& path, DWORD error) -> void {
  const char* reason;
  switch (error) {
    case ERROR_ALREADY_EXISTS:
      reason = "File exists";
      break;
    case ERROR_PATH_NOT_FOUND:
    case ERROR_FILE_NOT_FOUND:
      reason = "No such file or directory";
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
      reason = "Permission denied";
      break;
    case ERROR_DIRECTORY:
      reason = "Not a directory";
      break;
    case ERROR_FILENAME_EXCED_RANGE:
      reason = "File name too long";
      break;
    default:
      reason = "No such file or directory";
      break;
  }
  safeErrorPrint("mkdir: cannot create directory '");
  safeErrorPrint(path);
  safeErrorPrint("': ");
  safeErrorPrint(reason);
  safeErrorPrint("\n");
}

auto directory_exists_utf8(const std::string& path) -> bool {
  // attributes_w applies the \\?\ extended prefix, so existence checks work
  // beyond MAX_PATH (#1061).
  return native_path::is_directory(path);
}

auto preferred_separator(std::string_view path) -> char {
  return path.find('/') != std::string_view::npos ? '/' : '\\';
}

auto progressive_directory_paths(std::string_view path)
    -> std::vector<std::string> {
  std::vector<std::string> paths;
  if (path.empty()) return paths;

  const char sep = preferred_separator(path);
  std::string current;
  size_t i = 0;

  if (path.size() >= 2 && path[1] == ':') {
    current = std::string(path.substr(0, 2));
    i = 2;
    if (i < path.size() && is_separator(path[i])) {
      current += path[i];
      ++i;
    }
  } else if (path.size() >= 2 && is_separator(path[0]) &&
             is_separator(path[1])) {
    // UNC path (\\server\share)
    current = std::string(path.substr(0, 2));
    i = 2;
    // Include the server name and share name as part of the base path
    while (i < path.size() && !is_separator(path[i])) {
      current += path[i];
      ++i;
    }
    if (i < path.size() && is_separator(path[i])) {
      current += path[i];
      ++i;
    }
    // Include the share name
    while (i < path.size() && !is_separator(path[i])) {
      current += path[i];
      ++i;
    }
    // [GNU] For UNC paths, the share itself is the base directory
    paths.push_back(current);
  } else if (is_separator(path[0])) {
    current = std::string(1, path[0]);
    while (i < path.size() && is_separator(path[i])) ++i;
  }

  while (i < path.size()) {
    while (i < path.size() && is_separator(path[i])) ++i;
    size_t segment_start = i;
    while (i < path.size() && !is_separator(path[i])) ++i;
    if (segment_start == i) break;
    if (!current.empty() && !is_separator(current.back())) current += sep;
    current.append(path.substr(segment_start, i - segment_start));
    paths.push_back(current);
  }

  return paths;
}

auto create_directory(const std::string& path, const Config& config) -> bool {
  std::wstring wpath = utf8_to_wstring(path);
  bool existed_before = directory_exists_utf8(path);

  if (config.parents) {
    std::vector<std::pair<std::string, bool>> creation_state;
    for (const auto& candidate : progressive_directory_paths(path)) {
      creation_state.emplace_back(candidate, directory_exists_utf8(candidate));
    }

    if (!create_directory_recursive(wpath)) {
      report_parents_create_failure(path);
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
      for (const auto& [candidate, existed] : creation_state) {
        if (!existed && directory_exists_utf8(candidate)) {
          safePrint("mkdir: created directory '");
          safePrint(candidate);
          safePrint("'\n");
        }
      }
    }
  } else {
    // Extended API path for >MAX_PATH operands (#1061); trailing separators
    // are stripped because the \\?\ namespace rejects them on create.
    const std::wstring api_path = native_path::to_extended_path(
        native_path::strip_trailing_separators(std::wstring_view(wpath)));
    if (!CreateDirectoryW(api_path.c_str(), NULL)) {
      report_create_failure(path, GetLastError());
      return false;
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
    if (result.error() == "missing operand") {
      safeErrorPrintLn("mkdir: missing operand");
      safeErrorPrintLn("Try 'mkdir --help' for more information.");
      return 1;
    }
    cp::report_error(result, L"mkdir");
    return 1;
  }

  return *result ? 0 : 1;
}
