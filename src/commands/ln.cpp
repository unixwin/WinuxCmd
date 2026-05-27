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
           "directory"),
    OPTION("-i", "--interactive",
           "prompt whether to remove destinations"),
    OPTION("-L", "--logical",
           "dereference TARGETs that are symbolic links"),
    OPTION("-P", "--physical",
           "make hard links directly to symbolic links"),
    OPTION("", "--dereference",
           "dereference TARGETs that are symbolic links"),
    OPTION("-b", "--backup",
           "make a backup of each existing destination file"),
    OPTION("-S", "--suffix",
           "override the usual backup suffix", STRING_TYPE),
    OPTION("-r", "--relative",
           "with -s, create links relative to link location"),
    OPTION("-t", "--target-directory",
           "specify the DIRECTORY in which to create the links", STRING_TYPE),
    OPTION("-T", "--no-target-directory",
           "treat LINK_NAME as a normal file always"),
    OPTION("-d", "--directory",
           "allow the hard link to be a directory (privileged)"),
    OPTION("-F", "", "allow hard link to directory (alias for -d)")};

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
    "  -s, --symbolic         make symbolic links instead of hard links\n"
    "  -f, --force            remove existing destination files\n"
    "  -i, --interactive      prompt whether to remove destinations\n"
    "  -b, --backup           make a backup of each existing destination\n"
    "  -S, --suffix=SUFFIX    override the usual backup suffix\n"
    "  -t, --target-directory=DIRECTORY  specify the target directory\n"
    "  -T, --no-target-directory  treat LINK_NAME as a normal file\n"
    "  -r, --relative         create links relative to link location\n"
    "\n"
    "On Windows, hard links and symbolic links are supported.\n"
    "Note: Creating symbolic links may require administrator privileges.",
    "  ln source link         Create a hard link\n"
    "  ln -s source link      Create a symbolic link\n"
    "  ln -sf source link     Force create, overwrite if exists\n"
    "  ln -sv source link     Verbose symbolic link creation\n"
    "  ln -t dir/ src1 src2   Create links in dir/\n"
    "  ln -b source link      Backup existing link before creating",
    "link(1), symlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    LN_OPTIONS) {
  using namespace ln_pipeline;

  bool symbolic =
      ctx.get<bool>("-s", false) || ctx.get<bool>("--symbolic", false);
  bool force = ctx.get<bool>("-f", false) || ctx.get<bool>("--force", false);
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  bool interactive =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--interactive", false);
  bool backup = ctx.get<bool>("-b", false) || ctx.get<bool>("--backup", false);
  std::string backup_suffix = ctx.get<std::string>("--suffix", "");
  if (backup_suffix.empty()) backup_suffix = ctx.get<std::string>("-S", "");
  if (backup && backup_suffix.empty()) backup_suffix = "~";
  std::string target_dir = ctx.get<std::string>("--target-directory", "");
  if (target_dir.empty()) target_dir = ctx.get<std::string>("-t", "");
  bool no_target_dir =
      ctx.get<bool>("-T", false) || ctx.get<bool>("--no-target-directory", false);

  // Determine source and targets
  std::string source;
  std::vector<std::string> targets;

  if (!target_dir.empty()) {
    // -t mode: all positionals are sources, target_dir is the destination
    if (ctx.positionals.empty()) {
      safeErrorPrint("ln: missing operand\n");
      safeErrorPrint("Try 'ln --help' for more information.\n");
      return 1;
    }
    // Check if target_dir exists and is a directory
    std::wstring wtd = utf8_to_wstring(target_dir);
    DWORD td_attrs = GetFileAttributesW(wtd.c_str());
    if (td_attrs == INVALID_FILE_ATTRIBUTES ||
        !(td_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      safeErrorPrint("ln: target '" + target_dir + "' is not a directory\n");
      return 1;
    }
    for (auto arg : ctx.positionals) {
      std::string s(arg);
      // Extract filename from source path
      std::string filename = s;
      size_t sep = filename.find_last_of("/\\");
      if (sep != std::string::npos) filename = filename.substr(sep + 1);
      targets.push_back(target_dir + "\\" + filename);
    }
    source = std::string(ctx.positionals[0]);
  } else if (ctx.positionals.size() < 2) {
    safeErrorPrint("ln: missing operand\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  } else {
    source = std::string(ctx.positionals[0]);
    for (size_t i = 1; i < ctx.positionals.size(); ++i) {
      targets.push_back(std::string(ctx.positionals[i]));
    }
  }

  size_t target_count = targets.size();
  size_t success_count = 0;
  size_t error_count = 0;

  if (verbose && target_count > 1) {
    safePrint("ln: creating " + std::to_string(target_count) + " links from '" +
              source + "'...\n");
  }

  for (const auto& target : targets) {
    // Check if target exists
    std::wstring wtarget = utf8_to_wstring(target);
    DWORD target_attrs = GetFileAttributesW(wtarget.c_str());
    bool target_exists = (target_attrs != INVALID_FILE_ATTRIBUTES);

    if (target_exists) {
      if (interactive && !force) {
        // Prompt user
        safePrint("ln: replace '" + target + "'? ");
        std::string response;
        std::getline(std::cin, response);
        if (response.empty() || (response[0] != 'y' && response[0] != 'Y')) {
          continue;
        }
      }

      // Backup if requested
      if (backup && target_exists) {
        std::string backup_name = target + backup_suffix;
        std::wstring wbackup = utf8_to_wstring(backup_name);
        // Remove existing backup if any
        DeleteFileW(wbackup.c_str());
        RemoveDirectoryW(wbackup.c_str());
        if (!MoveFileW(wtarget.c_str(), wbackup.c_str())) {
          safeErrorPrint("ln: cannot backup '" + target + "'\n");
          error_count++;
          continue;
        }
        if (verbose) {
          safePrint("'" + target + "' -> '" + backup_name + "' (backup)\n");
        }
      } else if (force) {
        // Remove existing target if force is set
        auto result = remove_existing(target);
        if (!result) {
          safeErrorPrint("ln: ");
          safeErrorPrint(result.error());
          safeErrorPrint("\n");
          error_count++;
          continue;
        }
      } else if (!backup) {
        // No force, no backup, no interactive - error
        if (!no_target_dir ||
            (target_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
          safeErrorPrint("ln: '" + target + "' exists\n");
          error_count++;
          continue;
        }
        // With -T, try to overwrite
        auto result = remove_existing(target);
        if (!result) {
          safeErrorPrint("ln: ");
          safeErrorPrint(result.error());
          safeErrorPrint("\n");
          error_count++;
          continue;
        }
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
