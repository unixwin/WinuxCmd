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
 * - @a --reference: Use RFILE's mode instead of MODE values [IMPLEMENTED]
 */
auto constexpr CHMOD_OPTIONS = std::array{
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    OPTION("-f", "--silent", "suppress most error messages"),
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    OPTION("-R", "--recursive", "change files and directories recursively"),
    OPTION("", "--quiet", "suppress most error messages"),
    OPTION("", "--reference", "use RFILE's mode instead of MODE values",
           STRING_TYPE),
    OPTION("-H", "",
           "traverse command-line symlinks to directories"),
    OPTION("-L", "",
           "traverse every symlink to a directory"),
    OPTION("-P", "",
           "do not traverse any symbolic links (default)"),
    OPTION("", "--dereference",
           "affect the referent of each symbolic link"),
    OPTION("", "--preserve-root",
           "fail to operate recursively on '/'"),
    OPTION("", "--no-preserve-root",
           "do not treat '/' specially")};

namespace chmod_pipeline {
namespace cp = core::pipeline;

struct SymbolicModeClause {
  std::string who;
  char op = '\0';
  std::string perms;
};

struct ParsedMode {
  bool is_numeric = false;
  int numeric_mode = 0;
  std::vector<SymbolicModeClause> symbolic_clauses;
};

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

auto expand_file_operands(const CommandContext<CHMOD_OPTIONS.size()> &ctx,
                          size_t first_index) -> std::vector<std::string> {
  std::vector<std::string> files;

  for (size_t i = first_index; i < ctx.positionals.size(); ++i) {
    std::string file_arg = std::string(ctx.positionals[i]);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded && !glob_result.files.empty()) {
        for (const auto &file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }

    files.push_back(file_arg);
  }

  return files;
}

auto format_missing_reference_error(const std::string &path, DWORD error)
    -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
      return "failed to get attributes of '" + path +
             "': No such file or directory";
    default:
      return "failed to get attributes of '" + path + "'";
  }
}

/**
 * @brief Parse symbolic mode (e.g., "u+rwx", "go-w", "a=rx")
 * @param mode_str Mode string to parse
 * @return Tuple of (who, op, perms) or error message
 */
auto parse_symbolic_mode(std::string_view mode_str)
    -> cp::Result<SymbolicModeClause> {
  const std::string original_mode(mode_str);
  SymbolicModeClause clause;

  size_t i = 0;

  // Parse who (u/g/o/a)
  while (i < mode_str.size() && (mode_str[i] == 'u' || mode_str[i] == 'g' ||
                                 mode_str[i] == 'o' || mode_str[i] == 'a')) {
    clause.who += mode_str[i];
    i++;
  }

  if (clause.who.empty()) {
    clause.who = "a";  // Default to all
  }

  // Parse operator (+/-/=)
  if (i >= mode_str.size() ||
      (mode_str[i] != '+' && mode_str[i] != '-' && mode_str[i] != '=')) {
    return std::unexpected("invalid mode: '" + original_mode + "'");
  }
  clause.op = mode_str[i];
  i++;

  // Parse permissions (r/w/x)
  while (i < mode_str.size() &&
         (mode_str[i] == 'r' || mode_str[i] == 'w' || mode_str[i] == 'x')) {
    clause.perms += mode_str[i];
    i++;
  }

  if (clause.perms.empty() || i != mode_str.size()) {
    return std::unexpected("invalid mode: '" + original_mode + "'");
  }

  return clause;
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

auto apply_reference_mode(const std::string &path, DWORD reference_attrs)
    -> cp::Result<bool> {
  std::wstring wpath = utf8_to_wstring(path);

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    return std::unexpected("cannot access '" + path + "'");
  }

  DWORD attrs = attr_data.dwFileAttributes;
  DWORD new_attrs = attrs;
  if ((reference_attrs & FILE_ATTRIBUTE_READONLY) != 0) {
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
    -> cp::Result<ParsedMode> {
  ParsedMode parsed_mode;

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
      parsed_mode.is_numeric = true;
      parsed_mode.numeric_mode = std::stoi(std::string(mode_str), nullptr, 8);
      return parsed_mode;
    }
  }

  std::string mode_text(mode_str);
  size_t start = 0;
  while (start <= mode_text.size()) {
    size_t comma = mode_text.find(',', start);
    std::string_view clause_text =
        comma == std::string::npos
            ? std::string_view(mode_text).substr(start)
            : std::string_view(mode_text).substr(start, comma - start);

    if (clause_text.empty()) {
      return std::unexpected("invalid mode: '" + mode_text + "'");
    }

    auto result = parse_symbolic_mode(clause_text);
    if (!result) {
      return std::unexpected("invalid mode: '" + mode_text + "'");
    }
    parsed_mode.symbolic_clauses.push_back(result.value());

    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }

  return parsed_mode;
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

  bool changed = false;

  if (mode_result.value().is_numeric) {
    auto result = apply_numeric_mode(path, mode_result.value().numeric_mode);
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
    for (const auto &clause : mode_result.value().symbolic_clauses) {
      auto result =
          apply_symbolic_mode(path, clause.who, clause.op, clause.perms);
      if (!result) {
        if (!silent) {
          safeErrorPrint("chmod: ");
          safeErrorPrint(result.error());
          safeErrorPrint("\n");
        }
        return result;
      }
      changed = changed || result.value();
    }
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

auto process_file_reference(const std::string &path, DWORD reference_attrs,
                            const CommandContext<CHMOD_OPTIONS.size()> &ctx)
    -> cp::Result<bool> {
  bool verbose =
      ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  bool changes =
      ctx.get<bool>("-c", false) || ctx.get<bool>("--changes", false);
  bool silent = ctx.get<bool>("-f", false) ||
                ctx.get<bool>("--silent", false) ||
                ctx.get<bool>("--quiet", false);

  auto result = apply_reference_mode(path, reference_attrs);
  if (!result) {
    if (!silent) {
      safeErrorPrint("chmod: ");
      safeErrorPrint(result.error());
      safeErrorPrint("\n");
    }
    return result;
  }

  bool changed = result.value();
  if (changed && (verbose || changes)) {
    safePrint("mode of '");
    safePrint(path);
    safePrint("' changed\n");
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

auto process_recursive_reference(
    const std::string &path, DWORD reference_attrs,
    const CommandContext<CHMOD_OPTIONS.size()> &ctx) -> cp::Result<void> {
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

  auto result = process_file_reference(path, reference_attrs, ctx);
  if (!result && !ctx.get<bool>("-f", false) &&
      !ctx.get<bool>("--silent", false) && !ctx.get<bool>("--quiet", false)) {
    safeErrorPrint("chmod: ");
    safeErrorPrint(result.error());
    safeErrorPrint("\n");
  }

  if (is_directory) {
    std::wstring search_path = wpath + L"\\*";
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        std::wstring filename = find_data.cFileName;
        if (filename == L"." || filename == L"..") {
          continue;
        }

        std::string subpath = path + "\\" + wstring_to_utf8(filename);
        auto sub_result =
            process_recursive_reference(subpath, reference_attrs, ctx);
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
    "  -H  traverse command-line symlinks to directories\n"
    "  -L  traverse every symlink to a directory\n"
    "  -P  do not traverse any symbolic links (default)\n"
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
  std::string reference_file = ctx.get<std::string>("--reference", "");
  bool has_reference = !reference_file.empty();

  if ((!has_reference && ctx.positionals.size() < 2) ||
      (has_reference && ctx.positionals.empty())) {
    safeErrorPrint("chmod: missing operand\n");
    safeErrorPrint("Try 'chmod --help' for more information.\n");
    return 1;
  }

  std::string_view mode_str = {};
  std::vector<std::string> files =
      has_reference ? expand_file_operands(ctx, 0) : expand_file_operands(ctx, 1);
  if (!has_reference) {
    mode_str = ctx.positionals[0];
    auto mode_result = parse_mode(mode_str);
    if (!mode_result) {
      safeErrorPrint("chmod: ");
      safeErrorPrint(mode_result.error());
      safeErrorPrint("\n");
      safeErrorPrint("Try 'chmod --help' for more information.\n");
      return 1;
    }
  }

  bool preserve_root = ctx.get<bool>("--preserve-root", false) &&
                       !ctx.get<bool>("--no-preserve-root", false);

  if (recursive && preserve_root) {
    for (const auto &path : files) {
      if (!is_root_path(path)) {
        continue;
      }

      safeErrorPrint("chmod: it is dangerous to operate recursively on '/'\n");
      safeErrorPrint(
          "chmod: use --no-preserve-root to override this failsafe\n");
      return 1;
    }
  }

  DWORD reference_attrs = 0;
  if (has_reference) {
    WIN32_FILE_ATTRIBUTE_DATA reference_attr_data;
    std::wstring wreference = utf8_to_wstring(reference_file);
    if (!GetFileAttributesExW(wreference.c_str(), GetFileExInfoStandard,
                              &reference_attr_data)) {
      DWORD error = GetLastError();
      safeErrorPrint("chmod: ");
      safeErrorPrint(format_missing_reference_error(reference_file, error));
      safeErrorPrint("\n");
      return 1;
    }
    reference_attrs = reference_attr_data.dwFileAttributes;
  }

  int exit_code = 0;

  for (const auto &path : files) {
    if (has_reference) {
      if (recursive) {
        auto result = process_recursive_reference(path, reference_attrs, ctx);
        if (!result) {
          exit_code = 1;
        }
      } else {
        auto result = process_file_reference(path, reference_attrs, ctx);
        if (!result) {
          exit_code = 1;
        }
      }
    } else {
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
  }

  return exit_code;
}
