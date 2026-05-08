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
 *  - File: chmod.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for chmod.
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
 * @brief CHMOD command options definition
 *
 * This array defines all the options supported by the chmod command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -c, @a --changes: Like verbose but report only when a change is made
 * [IMPLEMENTED]
 * - @a -f, @a --silent, @a --quiet: Suppress most error messages [IMPLEMENTED]
 * - @a -v, @a --verbose: Output a diagnostic for every file processed
 * [IMPLEMENTED]
 * - @a -R, @a --recursive: Change files and directories recursively
 * [IMPLEMENTED]
 * - @a --reference: Use RFILE's mode instead of MODE values [NOT SUPPORT]
 */
auto constexpr CHMOD_OPTIONS = std::array{
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    OPTION("-f", "--silent", "suppress most error messages"),
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    OPTION("-R", "--recursive", "change files and directories recursively"),
    OPTION("", "--quiet", "suppress most error messages"),
    OPTION("", "--reference", "use RFILE's mode instead of MODE values",
           STRING_TYPE)};

namespace chmod_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Parse symbolic mode (e.g., "u+rwx", "go-w", "a=rx")
 * @param mode_str Mode string to parse
 * @return Tuple of (who, op, perms) or error message
 */
auto parse_symbolic_mode(std::string_view mode_str)
    -> cp::Result<std::tuple<std::string, char, std::string>> {
  std::string who;
  char op = '\0';
  std::string perms;

  size_t i = 0;

  // Parse who (u/g/o/a)
  while (i < mode_str.size() && (mode_str[i] == 'u' || mode_str[i] == 'g' ||
                                 mode_str[i] == 'o' || mode_str[i] == 'a')) {
    who += mode_str[i];
    i++;
  }

  if (who.empty()) {
    who = "a";  // Default to all
  }

  // Parse operator (+/-/=)
  if (i >= mode_str.size() ||
      (mode_str[i] != '+' && mode_str[i] != '-' && mode_str[i] != '=')) {
    return std::unexpected("Invalid mode operator");
  }
  op = mode_str[i];
  i++;

  // Parse permissions (r/w/x)
  while (i < mode_str.size() &&
         (mode_str[i] == 'r' || mode_str[i] == 'w' || mode_str[i] == 'x')) {
    perms += mode_str[i];
    i++;
  }

  return std::make_tuple(who, op, perms);
}

/**
 * @brief Convert numeric mode to permissions string (e.g., "755" ->
 * "rwxr-xr-x")
 * @param mode Numeric mode (e.g., 755)
 * @return Permissions string
 */
auto numeric_to_permissions(int mode) -> std::string {
  char perms[11] = "----------";

  int user = (mode / 100) % 10;
  int group = (mode / 10) % 10;
  int other = mode % 10;

  // User permissions
  if (user & 4) perms[1] = 'r';
  if (user & 2) perms[2] = 'w';
  if (user & 1) perms[3] = 'x';

  // Group permissions
  if (group & 4) perms[4] = 'r';
  if (group & 2) perms[5] = 'w';
  if (group & 1) perms[6] = 'x';

  // Other permissions
  if (other & 4) perms[7] = 'r';
  if (other & 2) perms[8] = 'w';
  if (other & 1) perms[9] = 'x';

  return std::string(perms, 10);
}

/**
 * @brief Apply symbolic mode to Windows file attributes
 * @param path File path
 * @param who Who to apply permissions to (u/g/o/a)
 * @param op Operation (+/-/=)
 * @param perms Permissions (r/w/x)
 * @return Result with success status
 */
auto apply_symbolic_mode(const std::string &path, const std::string &who,
                         char op, const std::string &perms)
    -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    return std::unexpected("cannot access '" + path + "'");
  }

  DWORD attrs = attr_data.dwFileAttributes;

  // For Windows, we simulate Unix permissions using file attributes
  // Read-only attribute is the closest approximation

  bool set_readonly = false;

  for (char who_char : who) {
    for (char perm : perms) {
      // On Windows, we mainly care about the 'w' (write) permission
      // Setting 'w' means removing read-only attribute
      // Removing 'w' means setting read-only attribute

      if (perm == 'w') {
        if (op == '+' || op == '=') {
          // Grant write permission: remove read-only
          set_readonly = false;
        } else if (op == '-') {
          // Remove write permission: set read-only
          set_readonly = true;
        }
      }
    }
  }

  DWORD new_attrs = attrs;
  if (set_readonly) {
    new_attrs |= FILE_ATTRIBUTE_READONLY;
  } else {
    new_attrs &= ~FILE_ATTRIBUTE_READONLY;
  }

  bool changed = (attrs != new_attrs);

  if (changed && !SetFileAttributesW(wpath.c_str(), new_attrs)) {
    return std::unexpected("failed to set attributes for '" + path + "'");
  }

  return changed;
}

/**
 * @brief Apply numeric mode to Windows file attributes
 * @param path File path
 * @param mode Numeric mode (e.g., 755)
 * @return Result with success status
 */
auto apply_numeric_mode(const std::string &path, int mode) -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    return std::unexpected("cannot access '" + path + "'");
  }

  DWORD attrs = attr_data.dwFileAttributes;

  // Check if file is a directory
  bool is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // Extract write permission from mode
  // User write (mode 0x002), Group write (mode 0x020), Other write (mode 0x200)
  bool has_write =
      ((mode & 0x2) != 0) || ((mode & 0x20) != 0) || ((mode & 0x200) != 0);

  DWORD new_attrs = attrs;
  if (!has_write) {
    new_attrs |= FILE_ATTRIBUTE_READONLY;
  } else {
    new_attrs &= ~FILE_ATTRIBUTE_READONLY;
  }

  bool changed = (attrs != new_attrs);

  if (changed && !SetFileAttributesW(wpath.c_str(), new_attrs)) {
    return std::unexpected("failed to set attributes for '" + path + "'");
  }

  return changed;
}

/**
 * @brief Parse mode string (numeric or symbolic)
 * @param mode_str Mode string to parse
 * @return Tuple of (is_numeric, numeric_mode, who, op, perms) or error message
 */
auto parse_mode(std::string_view mode_str)
    -> cp::Result<std::tuple<bool, int, std::string, char, std::string>> {
  // Check if it's a numeric mode (e.g., "755", "644")
  if (mode_str.size() == 3 || mode_str.size() == 4) {
    bool all_digits = true;
    for (char c : mode_str) {
      if (c < '0' || c > '7') {
        all_digits = false;
        break;
      }
    }

    if (all_digits) {
      int mode = std::stoi(std::string(mode_str), nullptr, 8);
      return std::make_tuple(true, mode, "", '\0', "");
    }
  }

  // Otherwise, parse as symbolic mode
  auto result = parse_symbolic_mode(mode_str);
  if (!result) {
    return std::unexpected(result.error());
  }

  auto [who, op, perms] = result.value();
  return std::make_tuple(false, 0, who, op, perms);
}

/**
 * @brief Process a single file/directory
 * @param path File/directory path
 * @param mode_str Mode string
 * @param ctx Command context
 * @return Result with success status
 */
auto process_file(const std::string &path, std::string_view mode_str,
                  const CommandContext<CHMOD_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  bool changes =
      ctx.get<bool>("-c", false) || ctx.get<bool>("--changes", false);
  bool silent = ctx.get<bool>("-f", false) ||
                ctx.get<bool>("--silent", false) ||
                ctx.get<bool>("--quiet", false);

  auto mode_result = parse_mode(mode_str);
  if (!mode_result) {
    if (!silent) {
      safeErrorPrint("chmod: ");
      safeErrorPrint(mode_result.error());
      safeErrorPrint("\n");
    }
    return std::unexpected(mode_result.error());
  }

  auto [is_numeric, numeric_mode, who, op, perms] = mode_result.value();

  bool changed = false;

  if (is_numeric) {
    auto result = apply_numeric_mode(path, numeric_mode);
    if (!result) {
      if (!silent) {
        safeErrorPrint("chmod: ");
        safeErrorPrint(result.error());
        safeErrorPrint("\n");
      }
      return result;
    }
    changed = result.value();
  } else {
    auto result = apply_symbolic_mode(path, who, op, perms);
    if (!result) {
      if (!silent) {
        safeErrorPrint("chmod: ");
        safeErrorPrint(result.error());
        safeErrorPrint("\n");
      }
      return result;
    }
    changed = result.value();
  }

  // Report change
  if (changed && (verbose || changes)) {
    safePrint("mode of '");
    safePrint(path);
    safePrint("' changed to ");
    safePrint(mode_str);
    safePrint("\n");
  }

  return changed;
}

/**
 * @brief Process file/directory recursively
 * @param path File/directory path
 * @param mode_str Mode string
 * @param ctx Command context
 * @return Result with success status
 */
auto process_recursive(const std::string &path, std::string_view mode_str,
                       const CommandContext<CHMOD_OPTIONS.size()> &ctx)
    -> cp::Result<void> {
  std::wstring wpath = utf8_to_wstring(path);

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    if (!ctx.get<bool>("-f", false) && !ctx.get<bool>("--silent", false) &&
        !ctx.get<bool>("--quiet", false)) {
      safeErrorPrint("chmod: cannot access '");
      safeErrorPrint(path);
      safeErrorPrint("'\n");
    }
    return std::unexpected("cannot access path");
  }

  bool is_directory =
      (attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // Process the current path
  auto result = process_file(path, mode_str, ctx);
  if (!result && !ctx.get<bool>("-f", false) &&
      !ctx.get<bool>("--silent", false) && !ctx.get<bool>("--quiet", false)) {
    safeErrorPrint("chmod: ");
    safeErrorPrint(result.error());
    safeErrorPrint("\n");
  }

  // If it's a directory, process its contents
  if (is_directory) {
    std::wstring search_path = wpath + L"\\*";
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        std::wstring filename = find_data.cFileName;

        // Skip . and ..
        if (filename == L"." || filename == L"..") {
          continue;
        }

        std::string subpath = path + "\\" + wstring_to_utf8(filename);

        // Recursively process
        auto sub_result = process_recursive(subpath, mode_str, ctx);
        if (!sub_result && !ctx.get<bool>("-f", false) &&
            !ctx.get<bool>("--silent", false) &&
            !ctx.get<bool>("--quiet", false)) {
          safeErrorPrint("chmod: ");
          safeErrorPrint(sub_result.error());
          safeErrorPrint("\n");
        }
      } while (FindNextFileW(hFind, &find_data) != 0);

      FindClose(hFind);
    }
  }

  return {};
}

}  // namespace chmod_pipeline

REGISTER_COMMAND(
    chmod, "chmod", "change file mode bits",
    "Change the mode of each FILE to MODE.\n"
    "\n"
    "Each MODE is of the form '[ugoa]*([-+=]([rwxXst]*|[ugo]))+'.\n"
    "\n"
    "Note: On Windows, this command simulates Unix permissions using\n"
    "file attributes. Write permission is mapped to the read-only attribute.",
    "  chmod 755 script.sh        Set permissions to rwxr-xr-x\n"
    "  chmod 644 file.txt        Set permissions to rw-r--r--\n"
    "  chmod u+x script.sh       Add execute for user\n"
    "  chmod go-w file.txt       Remove write for group and other\n"
    "  chmod -R 755 dir/         Recursively set permissions",
    "chown(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", CHMOD_OPTIONS) {
  using namespace chmod_pipeline;

  bool recursive =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--recursive", false);

  // Get mode and files from positional arguments
  if (ctx.positionals.size() < 2) {
    safeErrorPrint("chmod: missing operand\n");
    safeErrorPrint("Try 'chmod --help' for more information.\n");
    return 1;
  }

  std::string_view mode_str = ctx.positionals[0];

  int exit_code = 0;

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    std::string path = std::string(ctx.positionals[i]);

    if (recursive) {
      auto result = process_recursive(path, mode_str, ctx);
      if (!result) {
        exit_code = 1;
      }
    } else {
      auto result = process_file(path, mode_str, ctx);
      if (!result) {
        exit_code = 1;
      }
    }
  }

  return exit_code;
}
