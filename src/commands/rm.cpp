// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
///   - @description:
/// @Description: Implemention for rm.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
#pragma comment(lib, "shlwapi.lib")
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

using namespace core::pipeline;

/**
 * @brief RM command options definition
 *
 * This array defines all the options supported by the rm command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -f, @a --force: Ignore nonexistent files and arguments, never prompt
 * [IMPLEMENTED]
 * - @a -i: Prompt before every removal [IMPLEMENTED]
 * - @a -I: Prompt once before removing more than three files, or when removing
 * recursively [IMPLEMENTED]
 * - @a -d, @a --dir: Remove empty directories [IMPLEMENTED]
 * - @a -r, @a --recursive: Remove directories and their contents recursively
 * [IMPLEMENTED]
 * - @a -R, @a --recursive: Remove directories and their contents recursively
 * [IMPLEMENTED]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a --interactive: Prompt according to WHEN: never, once (-I), or always
 * (-i) [IMPLEMENTED]
 * - @a --one-file-system: When removing a hierarchy recursively, skip any
 *
 * directory that is on a file system different from that of the corresponding

 * * command line argument [APPROXIMATE ON WINDOWS]
 * - @a --no-preserve-root:
 * Do not treat '/' specially [IMPLEMENTED]
 * - @a --preserve-root: Do not
 * remove '/' (default) [IMPLEMENTED]
 */
// clang-format off
constexpr auto RM_OPTIONS = std::array{
// [GNU]
    OPTION("-f", "--force", "ignore nonexistent files and arguments, never prompt"),
// [GNU]
    OPTION("-i", "", "prompt before every removal"),
// [GNU]
    OPTION("-I", "", "prompt once before removing more than three files, or when removing recursively"),
// [GNU]
    OPTION("-d", "--dir", "remove empty directories"),
// [GNU]
    OPTION("-r", "--recursive", "remove directories and their contents recursively"),
// [GNU]
    OPTION("-R", "--recursive", "remove directories and their contents recursively"),
// [GNU]
    OPTION("-v", "--verbose", "explain what is being done"),
// [GNU]
    OPTION("", "--interactive", "prompt according to WHEN: never, once (-I), or always (-i)", OPTIONAL_STRING_TYPE),
// [GNU]
    OPTION("", "--one-file-system", "when removing a hierarchy recursively, skip any directory that is on a file system different from that of the corresponding command line argument"),
// [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
// [DIFFERS]
    OPTION("", "--preserve-root", "do not remove '/' (default)", OPTIONAL_STRING_TYPE)};
// clang-format on

// ======================================================
// Pipeline components
// ======================================================
namespace rm_pipeline {
namespace cp = core::pipeline;

enum class InteractiveMode { never, once, always };

struct RmConfig {
  bool force = false;
  InteractiveMode interactive = InteractiveMode::never;
  bool recursive = false;
  bool remove_empty_dir = false;
  bool verbose = false;
  bool one_file_system = false;
  bool no_preserve_root = false;
  bool preserve_root = true;
  bool preserve_root_all = false;
};

auto get_system_error_message(DWORD error) -> std::wstring {
  LPWSTR raw = nullptr;
  DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS;
  // Use English to avoid multibyte encoding issues in non-Unicode console paths
  DWORD len = FormatMessageW(flags, nullptr, error,
                             MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                             (LPWSTR)&raw, 0, nullptr);
  if (len == 0 || raw == nullptr) {
    // Fallback to system default language
    len = FormatMessageW(flags, nullptr, error,
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                         (LPWSTR)&raw, 0, nullptr);
  }
  if (len == 0 || raw == nullptr) {
    return L"unknown error";
  }

  std::wstring message(raw, len);
  LocalFree(raw);
  while (!message.empty() &&
         (message.back() == L'\r' || message.back() == L'\n' ||
          message.back() == L' ' || message.back() == L'\t')) {
    message.pop_back();
  }
  return message;
}

/**
 * @brief Read a single y/n response from stdin, consuming the entire input
 * line to prevent leftover characters from polluting subsequent prompts.
 * @return true if the user answered 'y' or 'Y', false otherwise.
 */
auto read_yes_no_response() -> bool {
  std::string line;
  if (!std::getline(std::cin, line)) {
    return false;
  }
  return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
}

/**
 * @brief Prompt the user to confirm removal of a file or item.
 * @param path Path to display in the prompt.
 * @return true if the user confirms, false otherwise.
 */
auto prompt_remove_file(std::string_view path) -> bool {
  safeErrorPrint("rm: remove '");
  safeErrorPrint(path);
  safeErrorPrint("'? (y/n) ");
  return read_yes_no_response();
}

/**
 * @brief Prompt the user to confirm descending into a directory.
 * Matches GNU rm's "descend into directory" prompt for -ri.
 * @param path Path to display in the prompt.
 * @return true if the user confirms, false otherwise.
 */
auto prompt_descend_directory(std::string_view path) -> bool {
  safeErrorPrint("rm: descend into directory '");
  safeErrorPrint(path);
  safeErrorPrint("'? (y/n) ");
  return read_yes_no_response();
}

/**
 * @brief Check if paths are provided
 * @param paths Paths to check
 * @return Result with paths if valid, error otherwise
 */
auto check_paths(const std::vector<std::string>& paths)
    -> cp::Result<std::vector<std::string>> {
  if (paths.empty()) {
    return std::unexpected("missing file operand");
  }
  return paths;
}

auto is_root_path(std::string_view path) -> bool {
  if (path == "/" || path == "\\") {
    return true;
  }

  if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    for (size_t i = 3; i < path.size(); ++i) {
      if (path[i] != '\\' && path[i] != '/') {
        return false;
      }
    }
    return true;
  }

  return false;
}

auto get_volume_root(const std::wstring& path) -> std::wstring {
  wchar_t volume[MAX_PATH];
  if (!GetVolumePathNameW(path.c_str(), volume, MAX_PATH)) {
    return {};
  }
  return volume;
}

auto confirm_bulk_remove(size_t path_count, bool recursive) -> bool {
  safeErrorPrint("rm: remove ");
  safeErrorPrint(std::to_string(path_count));
  safeErrorPrint(recursive ? " arguments recursively? (y/n) "
                           : " arguments? (y/n) ");
  return read_yes_no_response();
}

auto parse_interactive_mode(std::string_view value)
    -> std::optional<InteractiveMode> {
  if (value.empty() || value == "always" || value == "yes") {
    return InteractiveMode::always;
  }
  if (value == "once") {
    return InteractiveMode::once;
  }
  if (value == "never" || value == "no" || value == "none") {
    return InteractiveMode::never;
  }
  return std::nullopt;
}

auto path_is_current_or_parent_directory(std::wstring_view path) -> bool {
  path = native_path::strip_trailing_separators(path);
  size_t pos = path.find_last_of(L"\\/");
  auto name = pos == std::wstring_view::npos ? path : path.substr(pos + 1);
  if (name.empty() && pos != std::wstring_view::npos) {
    auto parent = native_path::strip_trailing_separators(path.substr(0, pos));
    pos = parent.find_last_of(L"\\/");
    name = pos == std::wstring_view::npos ? parent : parent.substr(pos + 1);
  }
  return name == L"." || name == L"..";
}

auto clear_readonly_attribute(const std::wstring& path, DWORD attr) -> void {
  if (attr == INVALID_FILE_ATTRIBUTES ||
      (attr & FILE_ATTRIBUTE_READONLY) == 0) {
    return;
  }
  SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
}

auto remove_empty_directory_path(const std::wstring& wpath,
                                 std::string_view display_path,
                                 const RmConfig& cfg) -> bool {
  DWORD attr = GetFileAttributesW(wpath.c_str());
  clear_readonly_attribute(wpath, attr);
  if (!RemoveDirectoryW(wpath.c_str())) {
    DWORD error = GetLastError();
    std::wstring errorMsg = get_system_error_message(error);
    safeErrorPrint("rm: cannot remove directory '");
    safeErrorPrint(display_path);
    safeErrorPrint("': ");
    safeErrorPrint(errorMsg);
    safeErrorPrint("\n");
    return false;
  }

  if (cfg.verbose) {
    safePrint("removed '");
    safePrint(display_path);
    safePrint("'\n");
  }
  return true;
}

auto remove_file_path(const std::wstring& wpath, std::string_view display_path,
                      const RmConfig& cfg) -> bool {
  DWORD attr = GetFileAttributesW(wpath.c_str());
  clear_readonly_attribute(wpath, attr);
  if (!DeleteFileW(wpath.c_str())) {
    DWORD error = GetLastError();
    std::wstring errorMsg = get_system_error_message(error);
    safeErrorPrint("rm: cannot remove file '");
    safeErrorPrint(display_path);
    safeErrorPrint("': ");
    safeErrorPrint(errorMsg);
    safeErrorPrint("\n");
    return false;
  }

  if (cfg.verbose) {
    safePrint("removed '");
    safePrint(display_path);
    safePrint("'\n");
  }
  return true;
}

auto build_config(const CommandContext<RM_OPTIONS.size()>& ctx)
    -> cp::Result<RmConfig> {
  RmConfig cfg;
  cfg.recursive = ctx.get<bool>("--recursive", false) ||
                  ctx.get<bool>("-r", false) || ctx.get<bool>("-R", false);
  cfg.remove_empty_dir =
      ctx.get<bool>("--dir", false) || ctx.get<bool>("-d", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  cfg.one_file_system = ctx.get<bool>("--one-file-system", false);
  cfg.no_preserve_root = ctx.get<bool>("--no-preserve-root", false);
  std::string preserve_val = ctx.get<std::string>("--preserve-root", "");
  bool explicit_preserve = ctx.has("--preserve-root");
  if (explicit_preserve) {
    cfg.preserve_root = true;
    cfg.preserve_root_all = (preserve_val == "all");
  } else {
    cfg.preserve_root = !cfg.no_preserve_root;
  }

  if (!ctx.metas) {
    return cfg;
  }

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= ctx.metas->size()) {
      continue;
    }

    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.short_name == "-f" || meta.long_name == "--force") {
      cfg.force = true;
      cfg.interactive = InteractiveMode::never;
    } else if (meta.short_name == "-i") {
      cfg.force = false;
      cfg.interactive = InteractiveMode::always;
    } else if (meta.short_name == "-I") {
      cfg.force = false;
      cfg.interactive = InteractiveMode::once;
    } else if (meta.long_name == "--interactive") {
      cfg.force = false;
      auto value = std::get_if<std::string>(&occurrence.value);
      auto mode = parse_interactive_mode(value ? *value : std::string_view{});
      if (!mode.has_value()) {
        return std::unexpected("invalid argument '" + *value +
                               "' for '--interactive'");
      }
      cfg.interactive = *mode;
    }
  }

  return cfg;
}

/**
 * @brief Remove a file or directory
 * @param path Path to remove
 * @param
 * ctx Command context with options
 * @return true if removal was successful,
 * false on error
 */
auto remove_path(const std::string& path, const RmConfig& cfg) -> bool {
  // Use extended-length path to bypass Windows reserved device names
  // (nul, con, prn, aux, com0-9, lpt0-9) which would otherwise redirect
  // file operations to the corresponding device instead of the actual file.
  auto operand = native_path::make_api_path_operand(path);
  const std::wstring& wpath = operand.extended;
  DWORD attr = native_path::operand_target_attributes_w(operand);

  if (cfg.recursive &&
      path_is_current_or_parent_directory(utf8_to_wstring(path))) {
    safeErrorPrint("rm: refusing to remove '.' or '..' directory: skipping '");
    safeErrorPrint(path);
    safeErrorPrint("'\n");
    return false;
  }

  if (cfg.preserve_root && is_root_path(path)) {
    safeErrorPrint("rm: it is dangerous to operate recursively on root '");
    safeErrorPrint(path);
    safeErrorPrint("'\n");
    return false;
  }

  // --preserve-root=all: also protect mount points
  if (cfg.preserve_root_all && cfg.recursive) {
    std::wstring wvol = get_volume_root(wpath);
    if (!wvol.empty()) {
      // Normalize: ensure trailing backslash for comparison
      std::wstring norm_path = wpath;
      if (!norm_path.empty() && norm_path.back() != L'\\' &&
          norm_path.back() != L'/') {
        norm_path += L'\\';
      }
      std::wstring norm_vol = wvol;
      if (!norm_vol.empty() && norm_vol.back() != L'\\' &&
          norm_vol.back() != L'/') {
        norm_vol += L'\\';
      }
      if (_wcsicmp(norm_path.c_str(), norm_vol.c_str()) == 0) {
        safeErrorPrint("rm: it is dangerous to operate recursively on '");
        safeErrorPrint(path);
        safeErrorPrint("' (same as '");
        safeErrorPrint(wstring_to_utf8(wvol));
        safeErrorPrint("')\n");
        return false;
      }
    }
  }

  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (cfg.force) {
      return true;
    } else {
      // OPTIMIZED: Avoid wstring concatenation
      safeErrorPrint("rm: cannot remove '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      return false;
    }
  }

  if (operand.had_trailing_separator &&
      (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    safeErrorPrint("rm: cannot remove '");
    safeErrorPrint(path);
    safeErrorPrint("': Not a directory\n");
    return false;
  }

  const bool is_directory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const bool is_reparse_point = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  // Interactive prompt before removal
  if (cfg.interactive == InteractiveMode::always) {
    bool response;
    if (is_directory && cfg.recursive && !is_reparse_point) {
      // For directories with -r, prompt to descend (GNU-style)
      response = prompt_descend_directory(path);
    } else {
      // For files, symlinks, or non-recursive directories, prompt to remove
      response = prompt_remove_file(path);
    }
    if (!response) {
      return true;  // user declined, treat as success
    }
  }

  if ((attr & FILE_ATTRIBUTE_DIRECTORY) && !cfg.recursive) {
    if (!cfg.remove_empty_dir) {
      // OPTIMIZED: Avoid wstring concatenation
      safeErrorPrint("rm: cannot remove '");
      safeErrorPrint(path);
      safeErrorPrint("': Is a directory\n");
      return false;
    }

    return remove_empty_directory_path(wpath, path, cfg);
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
      return remove_empty_directory_path(wpath, path, cfg);
    }

    // Recursive function to delete directory with post-order traversal
    std::function<bool(const std::wstring&)> remove_directory_recursive;
    std::wstring root_volume =
        cfg.one_file_system ? get_volume_root(wpath) : L"";
    remove_directory_recursive = [&](const std::wstring& dirPath) -> bool {
      if (cfg.one_file_system && !root_volume.empty()) {
        std::wstring current_volume = get_volume_root(dirPath);
        if (!current_volume.empty() && current_volume != root_volume) {
          if (cfg.verbose) {
            std::string dirPathStr = wstring_to_utf8(dirPath);
            safePrint("skipping directory '");
            safePrint(dirPathStr);
            safePrintLn("' on a different file system");
          }
          return true;
        }
      }

      // First, enumerate all items in the directory
      std::wstring searchPath = dirPath + L"\\*";
      WIN32_FIND_DATAW findData;
      HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);

      if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
          // Directory is empty, this is expected and not an error
          return true;
        } else {
          // Directory is inaccessible or other error
          std::string dirPathStr = wstring_to_utf8(dirPath);
          std::wstring errorMsg = get_system_error_message(error);
          safeErrorPrint("rm: cannot access directory '");
          safeErrorPrint(dirPathStr);
          safeErrorPrint("': ");
          safeErrorPrint(errorMsg);
          safeErrorPrint("\n");
          return false;
        }
      }

      std::vector<std::wstring> subdirs;
      bool success = true;
      bool all_removed = true;

      do {
        std::wstring itemName(findData.cFileName);
        if (itemName != L"." && itemName != L"..") {
          std::wstring itemPath = dirPath + L"\\" + itemName;
          std::string itemStr = wstring_to_utf8(itemPath);

          if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Prompt before descending into subdirectories
            if (cfg.interactive == InteractiveMode::always &&
                !(findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
              if (!prompt_descend_directory(itemStr)) {
                all_removed = false;  // user declined, leave subtree
                continue;
              }
            }
            // Store subdirectory for later recursive deletion
            subdirs.push_back(itemPath);
          } else {
            // Prompt before removing files
            if (cfg.interactive == InteractiveMode::always) {
              if (!prompt_remove_file(itemStr)) {
                all_removed = false;  // user declined
                continue;
              }
            }
            if (!remove_file_path(itemPath, itemStr, cfg)) {
              success = false;
              all_removed = false;
            }
          }
        }
      } while (FindNextFileW(hFind, &findData));

      FindClose(hFind);

      // If we encountered errors, don't continue
      if (!success) {
        return false;
      }

      // Recursively delete all subdirectories (post-order traversal)
      for (const auto& subdir : subdirs) {
        DWORD sub_attr = GetFileAttributesW(subdir.c_str());
        bool sub_success = false;
        if (sub_attr != INVALID_FILE_ATTRIBUTES &&
            (sub_attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
          // Prompt before removing reparse points (symlinks/junctions)
          if (cfg.interactive == InteractiveMode::always) {
            if (!prompt_remove_file(wstring_to_utf8(subdir))) {
              all_removed = false;
              continue;
            }
          }
          sub_success =
              remove_empty_directory_path(subdir, wstring_to_utf8(subdir), cfg);
        } else {
          sub_success = remove_directory_recursive(subdir);
        }
        if (!sub_success) {
          success = false;
          all_removed = false;
        }
      }

      if (!success) {
        return false;
      }

      // If the user declined to remove any child, the directory is still
      // non-empty — leave it alone (matches GNU rm behavior).
      if (!all_removed) {
        return true;
      }

      // Finally, remove the directory itself
      return remove_empty_directory_path(dirPath, wstring_to_utf8(dirPath),
                                         cfg);
    };

    // Start recursive directory deletion
    return remove_directory_recursive(wpath);
  } else {
    return remove_file_path(wpath, path, cfg);
  }

  return true;
}

/**
 * @brief Process all paths
 * @param paths Paths to process
 * @param ctx Command context with options
 * @return Result with success status
 */
auto process_paths(const std::vector<std::string>& paths, const RmConfig& cfg)
    -> cp::Result<bool> {
  bool success = true;
  for (const auto& path : paths) {
    if (!remove_path(path, cfg)) {
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
auto process_command(const CommandContext<RM_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    return std::unexpected(cfg_result.error());
  }
  const auto& cfg = *cfg_result;

  std::vector<std::string> paths;
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          paths.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    paths.push_back(file_arg);
  }

  if (paths.empty() && cfg.force) {
    return true;
  }

  return check_paths(paths).and_then(
      [&](const std::vector<std::string>& valid_paths) {
        if (cfg.interactive == InteractiveMode::once &&
            (cfg.recursive || valid_paths.size() > 3)) {
          if (!confirm_bulk_remove(valid_paths.size(), cfg.recursive)) {
            return cp::Result<bool>{true};
          }
        }
        return process_paths(valid_paths, cfg);
      });
}
}  // namespace rm_pipeline

REGISTER_COMMAND(
    rm,
    /* cmd_name */ "rm",
    /* cmd_synopsis */ "remove files or directories",
    /* cmd_desc */
    "Remove the FILE(s).\n"
    "\n"
    "By default, rm does not remove directories. Use the --recursive (-r) "
    "option\n"
    "to remove each listed directory, too, along with all of its contents.\n",
    /* examples */
    "  rm file.txt               Remove file.txt\n"
    "  rm -r dir/                Recursively remove directory dir/\n"
    "  rm -v file1.txt file2.txt Verbose remove\n"
    "  rm -i file.txt            Interactive remove (prompt before removal)",
    /* see_also */ "cp(1), mv(1), mkdir(1), rmdir(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    RM_OPTIONS) {
  using namespace rm_pipeline;

  // [GNU] rm.c refuses ANY abbreviated spelling of --no-preserve-root
  // (getopt_long resolves the prefix, then rm errors out before acting):
  //   rm --no-p f  ->  "rm: you may not abbreviate the --no-preserve-root
  //   option" (exit 1, nothing removed).
  // ctx.raw_args is post-normalization: the dispatcher already expands
  // unambiguous long-option prefixes (getopt_long behaviour), so the
  // abbreviation is invisible there.  Inspect the original process
  // arguments via the CRT globals instead (uutils#10188, issue 962).
  {
    constexpr std::string_view kNoPreserveRoot = "--no-preserve-root";
    bool end_of_options = false;
    for (int i = 1; i < __argc; ++i) {
      const std::string token = wstring_to_utf8(__wargv[i]);
      std::string_view arg(token);
      if (end_of_options || arg == "--") {
        end_of_options = true;
        continue;
      }
      if (!arg.starts_with("--")) continue;
      const auto eq = arg.find('=');
      const bool has_arg = eq != std::string_view::npos;
      const std::string_view name = has_arg ? arg.substr(0, eq) : arg;
      if (name.size() <= 2 || !kNoPreserveRoot.starts_with(name)) continue;
      if (has_arg) {
        // getopt_long reports the argument on the *resolved* option name
        // before rm's abbreviation check runs.
        safeErrorPrintLn(
            "rm: option '--no-preserve-root' doesn't allow an argument");
        safeErrorPrintLn("Try 'rm --help' for more information.");
        return 1;
      }
      if (name.size() < kNoPreserveRoot.size()) {
        safeErrorPrintLn(
            "rm: you may not abbreviate the --no-preserve-root option");
        return 1;
      }
    }
  }

  auto result = process_command(ctx);
  if (!result) {
    if (result.error() == "missing file operand") {
      safeErrorPrintLn("rm: missing file operand");
      safeErrorPrintLn("Try 'rm --help' for more information.");
      return 1;
    }
    cp::report_error(result, L"rm");
    return 1;
  }

  return *result ? 0 : 1;
}
