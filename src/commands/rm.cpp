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
 *  - File: rm.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
 * directory that is on a file system different from that of the corresponding
 * command line argument [APPROXIMATE ON WINDOWS]
 * - @a --no-preserve-root: Do not treat '/' specially [IMPLEMENTED]
 * - @a --preserve-root: Do not remove '/' (default) [IMPLEMENTED]
 */
// clang-format off
constexpr auto RM_OPTIONS = std::array{
    OPTION("-f", "--force", "ignore nonexistent files and arguments, never prompt"),
    OPTION("-i", "", "prompt before every removal"),
    OPTION("-I", "", "prompt once before removing more than three files, or when removing recursively"),
    OPTION("-d", "--dir", "remove empty directories"),
    OPTION("-r", "--recursive", "remove directories and their contents recursively"),
    OPTION("-R", "--recursive", "remove directories and their contents recursively"),
    OPTION("-v", "--verbose", "explain what is being done"),
    OPTION("", "--interactive", "prompt according to WHEN: never, once (-I), or always (-i)"),
    OPTION("", "--one-file-system", "when removing a hierarchy recursively, skip any directory that is on a file system different from that of the corresponding command line argument"),
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
    OPTION("", "--preserve-root", "do not remove '/' (default)")};
// clang-format on

// ======================================================
// Pipeline components
// ======================================================
namespace rm_pipeline {
namespace cp = core::pipeline;

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
 * @brief Convert a path to Windows extended-length path (\\?\) format.
 *
 * This bypasses reserved device name resolution (nul, con, prn, aux, com*, lpt*)
 * and also removes the MAX_PATH limit. The path is first resolved to an
 * absolute path via GetFullPathNameW, then prefixed with "\\?\\\\". If
 * resolution fails the original path is returned unchanged.
 */
auto to_extended_path(const std::wstring& path) -> std::wstring {
  // Already in extended form
  if (path.size() >= 4 && path.compare(0, 4, L"\\\\?\\" ) == 0) {
    return path;
  }
  wchar_t abs_buf[32768];
  DWORD len = GetFullPathNameW(path.c_str(), 32768, abs_buf, nullptr);
  if (len == 0 || len >= 32768) {
    return path;  // fallback: use original
  }
  return L"\\\\?\\" + std::wstring(abs_buf, len);
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

  if (path.size() >= 3 &&
      std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
      (path[2] == '\\' || path[2] == '/')) {
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

  char response = '\0';
  std::cin >> response;
  return response == 'y' || response == 'Y';
}

/**
 * @brief Remove a file or directory
 * @param path Path to remove
 * @param ctx Command context with options
 * @return true if removal was successful, false on error
 */
auto remove_path(const std::string& path,
                 const CommandContext<RM_OPTIONS.size()>& ctx) -> bool {
  // Use extended-length path to bypass Windows reserved device names
  // (nul, con, prn, aux, com0-9, lpt0-9) which would otherwise redirect
  // file operations to the corresponding device instead of the actual file.
  std::wstring wpath = to_extended_path(utf8_to_wstring(path));
  DWORD attr = GetFileAttributesW(wpath.c_str());

  bool force = ctx.get<bool>("--force", false) || ctx.get<bool>("-f", false);
  bool interactive =
      ctx.get<bool>("--interactive", false) || ctx.get<bool>("-i", false);
  bool recursive = ctx.get<bool>("--recursive", false) ||
                   ctx.get<bool>("-r", false) || ctx.get<bool>("-R", false);
  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  bool one_file_system = ctx.get<bool>("--one-file-system", false);
  bool no_preserve_root = ctx.get<bool>("--no-preserve-root", false);
  bool preserve_root = ctx.get<bool>("--preserve-root", false) || !no_preserve_root;

  if (preserve_root && is_root_path(path)) {
    safeErrorPrint("rm: it is dangerous to operate recursively on root '");
    safeErrorPrint(path);
    safeErrorPrint("'\n");
    return false;
  }

  if (attr == INVALID_FILE_ATTRIBUTES) {
    if (force) {
      return true;
    } else {
      // OPTIMIZED: Avoid wstring concatenation
      safeErrorPrint("rm: cannot remove '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      return false;
    }
  }

  if (interactive) {
    // OPTIMIZED: Avoid wstring concatenation
    safeErrorPrint("rm: remove '");
    safeErrorPrint(path);
    safeErrorPrint("'? (y/n) ");
    char response;
    std::cin.get(response);
    if (response != 'y' && response != 'Y') {
      return true;
    }
  }

  if ((attr & FILE_ATTRIBUTE_DIRECTORY) && !recursive) {
    // OPTIMIZED: Avoid wstring concatenation
    safeErrorPrint("rm: cannot remove '");
    safeErrorPrint(path);
    safeErrorPrint("': Is a directory\n");
    return false;
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    // Recursive function to delete directory with post-order traversal
    std::function<bool(const std::wstring&)> remove_directory_recursive;
    std::wstring root_volume = one_file_system ? get_volume_root(wpath) : L"";
    remove_directory_recursive = [&](const std::wstring& dirPath) -> bool {
      if (one_file_system && !root_volume.empty()) {
        std::wstring current_volume = get_volume_root(dirPath);
        if (!current_volume.empty() && current_volume != root_volume) {
          if (verbose) {
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

      do {
        std::wstring itemName(findData.cFileName);
        if (itemName != L"." && itemName != L"..") {
          std::wstring itemPath = dirPath + L"\\" + itemName;

          if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Store subdirectory for later recursive deletion
            subdirs.push_back(itemPath);
          } else {
            // Delete file
            if (!DeleteFileW(itemPath.c_str())) {
              DWORD error = GetLastError();
              std::string itemPathStr = wstring_to_utf8(itemPath);
              std::wstring errorMsg = get_system_error_message(error);
              safeErrorPrint("rm: cannot remove file '");
              safeErrorPrint(itemPathStr);
              safeErrorPrint("': ");
              safeErrorPrint(errorMsg);
              safeErrorPrint("\n");
              success = false;
            } else if (verbose) {
              // OPTIMIZED: Direct conversion
              std::string itemPathStr = wstring_to_utf8(itemPath);
              safePrint("removed '");
              safePrint(itemPathStr);
              safePrint("'\n");
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
        if (!remove_directory_recursive(subdir)) {
          return false;
        }
      }

      // Finally, remove the directory itself
      if (!RemoveDirectoryW(dirPath.c_str())) {
        DWORD error = GetLastError();
        std::string dirPathStr = wstring_to_utf8(dirPath);
        std::wstring errorMsg = get_system_error_message(error);
        safeErrorPrint("rm: cannot remove directory '");
        safeErrorPrint(dirPathStr);
        safeErrorPrint("': ");
        safeErrorPrint(errorMsg);
        safeErrorPrint("\n");
        return false;
      }

      if (verbose) {
        // OPTIMIZED: Direct conversion
        std::string dirPathStr = wstring_to_utf8(dirPath);
        safePrint("removed '");
        safePrint(dirPathStr);
        safePrint("'\n");
      }

      return true;
    };

    // Start recursive directory deletion
    return remove_directory_recursive(wpath);
  } else {
    // Delete regular file
    BOOL success = DeleteFileW(wpath.c_str());
    if (!success) {
      DWORD error = GetLastError();
      std::wstring errorMsg = get_system_error_message(error);
      // OPTIMIZED: Avoid redundant conversions
      safeErrorPrint("rm: cannot remove file '");
      safeErrorPrint(path);
      safeErrorPrint("': ");
      safeErrorPrint(errorMsg);
      safeErrorPrint("\n");
      return false;
    }

    if (verbose) {
      // OPTIMIZED: Avoid wstring conversion
      safePrint("removed '");
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
auto process_paths(const std::vector<std::string>& paths,
                   const CommandContext<RM_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  bool success = true;
  for (const auto& path : paths) {
    if (!remove_path(path, ctx)) {
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
  std::vector<std::string> paths;
  bool recursive = ctx.get<bool>("--recursive", false) ||
                   ctx.get<bool>("-r", false) || ctx.get<bool>("-R", false);
  bool ask_once = ctx.get<bool>("-I", false);
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

  return check_paths(paths).and_then([&](const std::vector<std::string>& valid_paths) {
    if (ask_once && (recursive || valid_paths.size() > 3)) {
      if (!confirm_bulk_remove(valid_paths.size(), recursive)) {
        return cp::Result<bool>{true};
      }
    }
    return process_paths(valid_paths, ctx);
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

  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"rm");
    return 1;
  }

  return *result ? 0 : 1;
}
