/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: ln.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for ln.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief LN command options definition
 *
 * This array defines all the options supported by the ln command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -s, @a --symbolic: Make symbolic links instead of hard links
 * [IMPLEMENTED]
 * - @a -f, @a --force: Remove existing destination files [IMPLEMENTED]
 * - @a -v, @a --verbose: Print name of each linked file [IMPLEMENTED]
 * - @a -n, @a --no-dereference: Treat LINK_NAME as a normal file if it is a
 * symbolic link to a directory [NOT SUPPORT]
 */
auto constexpr LN_OPTIONS = std::array{
    OPTION("-s", "--symbolic", "make symbolic links instead of hard links"),
    OPTION("-f", "--force", "remove existing destination files"),
    OPTION("-v", "--verbose", "print name of each linked file"),
    OPTION("-n", "--no-dereference",
           "treat LINK_NAME as a normal file if it is a symbolic link to a "
           "directory")};

namespace ln_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Create a hard link
 * @param source Source file path
 * @param target Target link path
 * @param verbose Print verbose output
 * @return Result with success status
 */
auto create_hardlink(const std::string &source, const std::string &target,
                     bool verbose) -> cp::Result<void> {
  std::wstring wsource = utf8_to_wstring(source);
  std::wstring wtarget = utf8_to_wstring(target);

  if (CreateHardLinkW(wtarget.c_str(), wsource.c_str(), nullptr)) {
    if (verbose) {
      safePrint("'");
      safePrint(target);
      safePrint("' -> '");
      safePrint(source);
      safePrint("'\n");
    }
    return {};
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to create hard link '" + target +
                         "': " + std::to_string(error));
}

/**
 * @brief Create a symbolic link
 * @param source Source file/directory path
 * @param target Target link path
 * @param verbose Print verbose output
 * @return Result with success status
 */
auto create_symlink(const std::string &source, const std::string &target,
                    bool verbose) -> cp::Result<void> {
  std::wstring wsource = utf8_to_wstring(source);
  std::wstring wtarget = utf8_to_wstring(target);

  // Check if source is a directory
  DWORD attrs = GetFileAttributesW(wsource.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected("failed to access '" + source + "'");
  }

  bool is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  DWORD flags = is_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;

  if (CreateSymbolicLinkW(wtarget.c_str(), wsource.c_str(), flags)) {
    if (verbose) {
      safePrint("'");
      safePrint(target);
      safePrint("' -> '");
      safePrint(source);
      safePrint("'\n");
    }
    return {};
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to create symbolic link '" + target +
                         "': " + std::to_string(error));
}

/**
 * @brief Remove existing file/directory
 * @param path Path to remove
 * @return Result with success status
 */
auto remove_existing(const std::string &path) -> cp::Result<void> {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD attrs = GetFileAttributesW(wpath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return {};  // File doesn't exist, nothing to remove
  }

  bool is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  bool is_reparse_point = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  // Try to remove as reparse point (symlink)
  if (is_reparse_point) {
    if (RemoveDirectoryW(wpath.c_str()) || DeleteFileW(wpath.c_str())) {
      return {};
    }
  }

  // Try to remove as directory or file
  if (is_directory) {
    if (RemoveDirectoryW(wpath.c_str())) {
      return {};
    }
  } else {
    if (DeleteFileW(wpath.c_str())) {
      return {};
    }
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to remove '" + path +
                         "': " + std::to_string(error));
}

}  // namespace ln_pipeline

REGISTER_COMMAND(
    ln, "ln", "make links between files",
    "Create links between files. By default, make hard links.\n"
    "\n"
    "On Windows, hard links and symbolic links are supported.\n"
    "Note: Creating symbolic links may require administrator privileges.",
    "  ln source link         Create a hard link\n"
    "  ln -s source link      Create a symbolic link\n"
    "  ln -sf source link     Force create, overwrite if exists\n"
    "  ln -sv source link     Verbose symbolic link creation",
    "link(1), symlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    LN_OPTIONS) {
  using namespace ln_pipeline;

  bool symbolic =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--symbolic", false);
  bool force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);

  // Need at least source and one target
  if (ctx.positionals.size() < 2) {
    safeErrorPrint("ln: missing operand\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  }

  std::string source = std::string(ctx.positionals[0]);

  // Batch mode: first param is source, rest are targets
  size_t target_count = ctx.positionals.size() - 1;
  size_t success_count = 0;
  size_t error_count = 0;

  if (verbose && target_count > 1) {
    safePrint("ln: creating " + std::to_string(target_count) + " links from '" +
              source + "'...\n");
  }

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    std::string target = std::string(ctx.positionals[i]);

    // Remove existing target if force is set
    if (force) {
      auto result = remove_existing(target);
      if (!result) {
        safeErrorPrint("ln: ");
        safeErrorPrint(result.error());
        safeErrorPrint("\n");
        error_count++;
        continue;
      }
    }

    // Create the link
    auto result = symbolic ? create_symlink(source, target, verbose)
                           : create_hardlink(source, target, verbose);

    if (!result) {
      safeErrorPrint("ln: ");
      safeErrorPrint(result.error());
      safeErrorPrint("\n");
      error_count++;
    } else {
      success_count++;
    }
  }

  if (verbose) {
    safePrint("ln: " + std::to_string(success_count) + " links created, " +
              std::to_string(error_count) + " errors\n");
  }

  return (error_count > 0) ? 1 : 0;
}
