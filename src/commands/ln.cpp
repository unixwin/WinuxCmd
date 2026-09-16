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
    // [GNU]
    OPTION("-s", "--symbolic", "make symbolic links instead of hard links"),
    // [GNU]
    OPTION("-f", "--force", "remove existing destination files"),
    // [GNU]
    OPTION("-v", "--verbose", "print name of each linked file"),
    // [DIFFERS] -n: on Windows, directory-symlink target detection uses
    // GetFileAttributesW; behaviour differs from GNU.
    OPTION("-n", "--no-dereference",
           "treat LINK_NAME as a normal file if it is a symbolic link to a "
           "directory"),
    // [GNU]
    OPTION("-i", "--interactive", "prompt whether to remove destinations"),
    // [DIFFERS] -L/--logical: on Windows, CreateHardLinkW and the default
    // file-open behaviour already dereference symlinks, making this flag
    // effectively a no-op for hard links.
    OPTION("-L", "--logical", "dereference TARGETs that are symbolic links"),
    // [DIFFERS] -P/--physical: on Windows, hard links are always created
    // directly to the target (symlinks are not followed); this is the
    // default behaviour.
    OPTION("-P", "--physical", "make hard links directly to symbolic links"),
    // [DIFFERS] --dereference: alias for -L; same Windows caveats apply.
    OPTION("", "--dereference", "dereference TARGETs that are symbolic links"),
    // [GNU]
    OPTION("-b", "--backup", "make a backup of each existing destination file"),
    // [GNU]
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    // [DIFFERS] -r/--relative: on Windows, relative symlink targets are
    // computed using std::filesystem::relative which may differ from POSIX
    // path resolution.
    OPTION("-r", "--relative",
           "with -s, create links relative to link location"),
    // [GNU]
    OPTION("-t", "--target-directory",
           "specify the DIRECTORY in which to create the links", STRING_TYPE),
    // [GNU]
    OPTION("-T", "--no-target-directory",
           "treat LINK_NAME as a normal file always"),
    // [DIFFERS] -d/--directory: hard links to directories are not supported
    // on Windows; an error is reported at runtime.
    OPTION("-d", "--directory",
           "allow the hard link to be a directory (privileged)"),
    // [DIFFERS] -F: alias for -d; same Windows limitation applies.
    OPTION("-F", "", "allow hard link to directory (alias for -d)")};

namespace ln_pipeline {
namespace cp = core::pipeline;

auto join_target_path(const std::string &directory, const std::string &source)
    -> std::string {
  std::string filename = source;
  size_t sep = filename.find_last_of("/\\");
  if (sep != std::string::npos) {
    filename = filename.substr(sep + 1);
  }
  return directory + "\\" + filename;
}

// [GNU] -r/--relative: symbolic-link targets are relative to the link.
auto relative_symlink_target(const std::string &source,
                             const std::string &target) -> std::string {
  std::error_code ec;
  // Build paths from the wide form: the narrow path ctor decodes via the
  // system ACP, which mangles non-ASCII UTF-8 sources/targets (#88).
  auto source_path = std::filesystem::absolute(utf8_to_wstring(source), ec);
  if (ec) return source;
  auto target_path = std::filesystem::absolute(utf8_to_wstring(target), ec);
  if (ec) return source;
  auto relative =
      std::filesystem::relative(source_path, target_path.parent_path(), ec);
  return ec ? source : wstring_to_utf8(relative.wstring());
}

auto ln_windows_error_text(DWORD error) -> std::string {
  Win32ErrorTextOptions options;
  options.file_exists = true;
  options.privilege_not_held_as_not_permitted = true;
  return win32_posix_error_text(error, options);
}

auto ln_creation_failure_prefix(bool symbolic, const std::string &target)
    -> std::string {
  return symbolic ? "failed to create symbolic link '" + target + "'"
                  : "failed to create hard link '" + target + "'";
}

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
      safePrint("' => '");
      safePrint(source);
      safePrint("'\n");
    }
    return {};
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to create hard link '" + target +
                         "': " + ln_windows_error_text(error));
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
  // [DIFFERS] #1101: reparse-point targets must use backslash separators;
  // "./bds" or "dir/file" resolve as ERROR_INVALID_NAME on native Windows.
  // Store the user-visible link text unchanged, but normalize what goes
  // into the reparse point.
  std::wstring wsource =
      win32_normalize_symlink_target(utf8_to_wstring(source));
  std::wstring wtarget = utf8_to_wstring(target);

  // Check if source is a directory
  DWORD attrs = GetFileAttributesW(wsource.c_str());
  bool is_directory = attrs != INVALID_FILE_ATTRIBUTES &&
                      (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  DWORD flags = is_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  // [GNU] a symlink source may be dangling; creation must not fail when the
  // source does not exist (uutils #13667)

  if (CreateSymbolicLinkW(wtarget.c_str(), wsource.c_str(), flags)) {
    if (verbose) {
      safePrint("'");
      safePrint(target);
      safePrint("' => '");
      safePrint(source);
      safePrint("'\n");
    }
    return {};
  }

  DWORD error = GetLastError();
  return std::unexpected("failed to create symbolic link '" + target +
                         "': " + ln_windows_error_text(error));
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
                         "': " + ln_windows_error_text(error));
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
  bool no_target_dir = ctx.get<bool>("-T", false) ||
                       ctx.get<bool>("--no-target-directory", false);
  // [DIFFERS] -n/--no-dereference: on Windows, directory-symlink target
  // detection uses GetFileAttributesW, which may differ from POSIX
  // lstat() behaviour.
  bool no_dereference =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--no-dereference", false);
  // [DIFFERS] -L/--logical/--dereference: on Windows, CreateHardLinkW
  // already dereferences symlinks transparently, so this flag is
  // effectively a no-op for hard links.
  bool logical = ctx.get<bool>("-L", false) ||
                 ctx.get<bool>("--logical", false) ||
                 ctx.get<bool>("--dereference", false);
  // [DIFFERS] -P/--physical: on Windows, hard links are always created
  // directly to the target without following symlinks (the default).
  bool physical =
      ctx.get<bool>("-P", false) || ctx.get<bool>("--physical", false);
  // [DIFFERS] -d/--directory/-F: hard links to directories are not
  // supported on Windows.
  bool directory_link = ctx.get<bool>("-d", false) ||
                        ctx.get<bool>("--directory", false) ||
                        ctx.get<bool>("-F", false);
  // [DIFFERS] -r/--relative: on Windows, relative symlink targets are
  // computed using std::filesystem::relative, which may differ from POSIX
  // path resolution.
  bool relative =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--relative", false);

  // [DIFFERS] -d/--directory/-F: not supported on Windows.
  if (directory_link) {
    safeErrorPrintLn(winux::i18n::translate(
        "command.ln.error.directory-hardlink-unsupported",
        "ln: hard links to directories are not supported on Windows"));
    return 1;
  }
  if (logical && physical) {
    safeErrorPrintLn(
        "ln: options --logical and --physical are mutually exclusive");
    return 1;
  }

  if (!target_dir.empty() && no_target_dir) {
    // GNU 9.11 ln.c: "cannot combine --target-directory and
    // --no-target-directory"
    safeErrorPrint(
        "ln: cannot combine --target-directory "
        "and --no-target-directory\n");
    return 1;
  }

  std::vector<std::pair<std::string, std::string>> link_jobs;

  if (!target_dir.empty()) {
    if (ctx.positionals.empty()) {
      safeErrorPrint("ln: missing file operand\n");
      safeErrorPrint("Try 'ln --help' for more information.\n");
      return 1;
    }
    // GNU 9.11 ln.c: missing -t dir reports "failed to access"; an
    // existing non-directory reports "target 'x' is not a directory".
    std::wstring wtd = utf8_to_wstring(target_dir);
    DWORD td_attrs = GetFileAttributesW(wtd.c_str());
    if (td_attrs == INVALID_FILE_ATTRIBUTES) {
      safeErrorPrint("ln: failed to access '" + target_dir +
                     "': No such file or directory\n");
      return 1;
    }
    if (!(td_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      safeErrorPrint("ln: target '" + target_dir + "' is not a directory\n");
      return 1;
    }
    for (auto arg : ctx.positionals) {
      std::string source(arg);
      link_jobs.emplace_back(source, join_target_path(target_dir, source));
    }
  } else if (ctx.positionals.empty()) {
    safeErrorPrint("ln: missing file operand\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  } else if (no_target_dir && ctx.positionals.size() == 1) {
    // GNU: -T requires exactly two operands.
    safeErrorPrint("ln: missing destination file operand after '" +
                   std::string(ctx.positionals[0]) + "'\n");
    safeErrorPrint("Try 'ln --help' for more information.\n");
    return 1;
  } else if (ctx.positionals.size() == 1) {
    std::string source(std::string(ctx.positionals[0]));
    link_jobs.emplace_back(source, join_target_path(".", source));
  } else if (no_target_dir) {
    if (ctx.positionals.size() > 2) {
      safeErrorPrint("ln: extra operand '" + std::string(ctx.positionals[2]) +
                     "'\n");
      safeErrorPrint("Try 'ln --help' for more information.\n");
      return 1;
    }
    link_jobs.emplace_back(std::string(ctx.positionals[0]),
                           std::string(ctx.positionals[1]));
  } else if (ctx.positionals.size() == 2) {
    std::string source(std::string(ctx.positionals[0]));
    std::string target(std::string(ctx.positionals[1]));
    std::wstring wtarget = utf8_to_wstring(target);
    DWORD target_attrs = GetFileAttributesW(wtarget.c_str());
    std::error_code target_ec;
    const bool target_is_symlink = std::filesystem::is_symlink(
        std::filesystem::symlink_status(target, target_ec));
    if (target_attrs != INVALID_FILE_ATTRIBUTES &&
        (target_attrs & FILE_ATTRIBUTE_DIRECTORY) &&
        !(no_dereference && target_is_symlink)) {
      link_jobs.emplace_back(source, join_target_path(target, source));
    } else {
      link_jobs.emplace_back(source, target);
    }
  } else {
    target_dir = std::string(ctx.positionals.back());
    std::wstring wtd = utf8_to_wstring(target_dir);
    DWORD td_attrs = GetFileAttributesW(wtd.c_str());
    if (td_attrs == INVALID_FILE_ATTRIBUTES ||
        !(td_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
      // GNU 9.11 ln.c: errno-style "target 'x': <reason>".
      safeErrorPrint("ln: target '" + target_dir +
                     (td_attrs == INVALID_FILE_ATTRIBUTES
                          ? "': No such file or directory\n"
                          : "': Not a directory\n"));
      return 1;
    }

    for (size_t i = 0; i + 1 < ctx.positionals.size(); ++i) {
      std::string source(ctx.positionals[i]);
      link_jobs.emplace_back(source, join_target_path(target_dir, source));
    }
  }

  size_t target_count = link_jobs.size();
  size_t success_count = 0;
  size_t error_count = 0;

  for (const auto &[job_source, target] : link_jobs) {
    std::string source = job_source;
    if (!symbolic && logical) {
      std::error_code source_ec;
      auto resolved =
          std::filesystem::canonical(utf8_to_wstring(source), source_ec);
      if (!source_ec) source = wstring_to_utf8(resolved.wstring());
    }
    if (symbolic && relative) source = relative_symlink_target(source, target);

    // [GNU] For hard links, ln stats the source first so a missing TARGET
    // reports "failed to access 'src'".  FindFirstFileW is the lstat
    // equivalent here: a dangling symlink still counts as existing.
    if (!symbolic) {
      WIN32_FIND_DATAW sfd{};
      HANDLE sh = FindFirstFileW(utf8_to_wstring(source).c_str(), &sfd);
      if (sh == INVALID_HANDLE_VALUE) {
        safeErrorPrint("ln: failed to access '" + source +
                       "': No such file or directory\n");
        error_count++;
        continue;
      }
      FindClose(sh);
    }

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
        if (!no_target_dir || (target_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
          safeErrorPrint("ln: " + ln_creation_failure_prefix(symbolic, target) +
                         ": File exists\n");
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

  return (error_count > 0) ? 1 : 0;
}
