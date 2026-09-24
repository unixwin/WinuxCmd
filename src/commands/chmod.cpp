// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [DIFFERS]
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    // [DIFFERS]
    OPTION("-f", "--silent", "suppress most error messages"),
    // [DIFFERS]
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    // [DIFFERS]
    OPTION("-R", "--recursive", "change files and directories recursively"),
    // [DIFFERS]
    OPTION("", "--quiet", "suppress most error messages"),
    // [DIFFERS]
    OPTION("", "--reference", "use RFILE's mode instead of MODE values",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-H", "", "traverse command-line symlinks to directories"),
    // [DIFFERS]
    OPTION("-L", "", "traverse every symlink to a directory"),
    // [DIFFERS]
    OPTION("-P", "", "do not traverse any symbolic links (default)"),
    // [DIFFERS]
    OPTION("", "--dereference", "affect the referent of each symbolic link"),
    // [DIFFERS] Windows file-attribute operations follow reparse points.
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    // [DIFFERS]
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially")};

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
 * @brief GNU-style "cannot access" diagnostic with the errno-style reason
 * appended (issue 327: previously the reason was missing).
 */
auto format_access_error(const std::string &path, DWORD error) -> std::string {
  const char *reason;
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
      reason = "No such file or directory";
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_WRITE_PROTECT:
      reason = "Permission denied";
      break;
    case ERROR_FILENAME_EXCED_RANGE:
      reason = "File name too long";
      break;
    case ERROR_DIRECTORY:
      reason = "Not a directory";
      break;
    default:
      reason = "Input/output error";
      break;
  }
  return "cannot access '" + path + "': " + reason;
}

/** @brief English Win32 error text for diagnostics. */
auto win32_error_text(DWORD error) -> std::wstring {
  LPWSTR raw = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = FormatMessageW(flags, nullptr, error,
                             MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                             reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
  if (len == 0 || raw == nullptr) {
    len = FormatMessageW(flags, nullptr, error,
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                         reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
  }
  if (len == 0 || raw == nullptr) return L"unknown error";
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
 * @brief Synthetic Unix mode for a Windows file, matching the
 * MSYS-visible mapping: FILE_ATTRIBUTE_READONLY clears all write bits and
 * executable extensions set x bits.  Used only for GNU-style -v/-c
 * diagnostics.
 */
auto synthetic_mode(const std::string &path, DWORD attrs) -> unsigned int {
  const bool is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
  // GNU sees umask-022 defaults (0644/0755) on POSIX filesystems; the
  // MSYS layer presents the same view for ordinary Windows files.
  unsigned int mode = is_dir ? 0755u : 0644u;
  const auto dot = path.find_last_of('.');
  const auto sep = path.find_last_of("\\/");
  if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
    std::string ext = path.substr(dot + 1);
    for (auto &c : ext) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == "exe" || ext == "bat" || ext == "cmd" || ext == "com" ||
        ext == "ps1") {
      mode |= 0111u;
    }
  }
  if (attrs & FILE_ATTRIBUTE_READONLY) mode &= ~0222u;
  return mode;
}

/**
 * @brief Format a mode the way GNU -v prints it: "0644 (rw-r--r--)".
 */
auto format_mode_verbose(unsigned int mode, bool is_dir) -> std::string {
  char oct[8];
  std::snprintf(oct, sizeof(oct), "%04o", mode & 07777u);
  char perms[11];
  perms[0] = is_dir ? 'd' : '-';
  constexpr std::string_view rwx = "rwxrwxrwx";
  for (int i = 0; i < 9; ++i) {
    perms[i + 1] = (mode & (0400u >> i)) ? rwx[i] : '-';
  }
  perms[10] = '\0';
  return std::string(oct) + " (" + perms + ")";
}

/**
 * @brief Apply a parsed symbolic clause to a mode for -v/-c display only.
 * (The on-disk effect is limited to the owner-write bit on Windows.)
 */
auto apply_clause_for_display(unsigned int mode,
                              const SymbolicModeClause &clause, bool is_dir)
    -> unsigned int {
  unsigned int who_mask = 0;
  for (char w : clause.who) {
    if (w == 'a' || w == 'u') who_mask |= 0700u;
    if (w == 'a' || w == 'g') who_mask |= 0070u;
    if (w == 'a' || w == 'o') who_mask |= 0007u;
  }
  unsigned int perm_bits = 0;
  for (char p : clause.perms) {
    if (p == 'r') {
      perm_bits |= 0444u;
    } else if (p == 'w') {
      perm_bits |= 0222u;
    } else if (p == 'x') {
      perm_bits |= 0111u;
    } else if (p == 'X' && (is_dir || (mode & 0111u))) {
      perm_bits |= 0111u;
    }
    // 's' and 't' have no Windows attribute counterpart.
  }
  perm_bits &= who_mask;
  switch (clause.op) {
    case '+':
      return mode | perm_bits;
    case '-':
      return mode & ~perm_bits;
    case '=':
      return (mode & ~who_mask) | perm_bits;
    default:
      return mode;
  }
}

/**
 * @brief Print GNU-style -v/-c output for one operand.
 */
auto report_mode_change(const std::string &path, unsigned int old_mode,
                        unsigned int new_mode, bool changed, bool verbose,
                        bool changes, bool is_dir) -> void {
  if (changed) {
    if (verbose || changes) {
      safePrint("mode of '");
      safePrint(path);
      safePrint("' changed from ");
      safePrint(format_mode_verbose(old_mode, is_dir));
      safePrint(" to ");
      safePrint(format_mode_verbose(new_mode, is_dir));
      safePrint("\n");
    }
  } else if (verbose) {
    safePrint("mode of '");
    safePrint(path);
    safePrint("' retained as ");
    safePrint(format_mode_verbose(old_mode, is_dir));
    safePrint("\n");
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

  // Parse permissions (r/w/x plus s/t/X; GNU accepts the full set).
  // s (setuid/setgid), t (sticky) and X (conditional execute) parse fine but
  // have no Windows attribute counterpart: they apply as no-ops (Savannah
  // #11638).
  while (i < mode_str.size() &&
         (mode_str[i] == 'r' || mode_str[i] == 'w' || mode_str[i] == 'x' ||
          mode_str[i] == 's' || mode_str[i] == 't' || mode_str[i] == 'X')) {
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
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring &wpath = operand.extended;

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    return std::unexpected(format_access_error(path, GetLastError()));
  }

  DWORD attrs = attr_data.dwFileAttributes;

  // For Windows, we simulate Unix permissions using file attributes
  // Read-only attribute is the closest approximation

  const bool affects_owner =
      who.find('a') != std::string::npos || who.find('u') != std::string::npos;
  if (!affects_owner) {
    return false;
  }

  // On Windows/MSYS, chmod's observable read-only attribute follows the owner
  // write bit.  Group/other write bits have no independent FILE_ATTRIBUTE_*.
  const bool mentions_write = perms.find('w') != std::string::npos;
  bool owner_writable = (attrs & FILE_ATTRIBUTE_READONLY) == 0;
  if (op == '+') {
    owner_writable = owner_writable || mentions_write;
  } else if (op == '-') {
    owner_writable = owner_writable && !mentions_write;
  } else if (op == '=') {
    owner_writable = mentions_write;
  }

  DWORD new_attrs = attrs;
  if (owner_writable) {
    new_attrs &= ~FILE_ATTRIBUTE_READONLY;
  } else {
    new_attrs |= FILE_ATTRIBUTE_READONLY;
  }

  bool changed = (attrs != new_attrs);

  if (changed && !SetFileAttributesW(wpath.c_str(), new_attrs)) {
    return std::unexpected("cannot change permissions of '" + path + "': " +
                           wstring_to_utf8(win32_error_text(GetLastError())));
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
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring &wpath = operand.extended;

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    return std::unexpected(format_access_error(path, GetLastError()));
  }

  DWORD attrs = attr_data.dwFileAttributes;

  // Check if file is a directory
  bool is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // Match GNU/MSYS' Windows-visible mapping: FILE_ATTRIBUTE_READONLY follows
  // the owner write bit, not group/other write bits.
  bool owner_writable = (mode & 0200) != 0;

  DWORD new_attrs = attrs;
  if (owner_writable) {
    new_attrs &= ~FILE_ATTRIBUTE_READONLY;
  } else {
    new_attrs |= FILE_ATTRIBUTE_READONLY;
  }

  bool changed = (attrs != new_attrs);

  if (changed && !SetFileAttributesW(wpath.c_str(), new_attrs)) {
    return std::unexpected("cannot change permissions of '" + path + "': " +
                           wstring_to_utf8(win32_error_text(GetLastError())));
  }

  return changed;
}

auto apply_reference_mode(const std::string &path, DWORD reference_attrs)
    -> cp::Result<bool> {
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring &wpath = operand.extended;

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    return std::unexpected(format_access_error(path, GetLastError()));
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
    return std::unexpected("cannot change permissions of '" + path + "': " +
                           wstring_to_utf8(win32_error_text(GetLastError())));
  }

  return changed;
}

/**
 * @brief Parse mode string (numeric or symbolic)
 * @param mode_str Mode string to parse
 * @return Tuple of (is_numeric, numeric_mode, who, op, perms) or error message
 */
auto parse_mode(std::string_view mode_str) -> cp::Result<ParsedMode> {
  ParsedMode parsed_mode;

  // Check if it's a numeric mode (e.g., "755", "644")
  if (mode_str.size() >= 1 && mode_str.size() <= 4) {
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

  // [GNU] -v/-c need the pre-change mode for "changed from A to B" /
  // "retained as A" diagnostics (issue 987).
  const auto operand = native_path::make_api_path_operand(path);
  WIN32_FILE_ATTRIBUTE_DATA before{};
  const bool had_before =
      GetFileAttributesExW(operand.extended.c_str(), GetFileExInfoStandard,
                           &before) != 0;
  const bool is_dir =
      had_before && (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const unsigned int old_mode =
      had_before ? synthetic_mode(path, before.dwFileAttributes) : 0;

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

  // [GNU] chmod reports the mode transition, not just whether the
  // FILE_ATTRIBUTE_READONLY bit moved: "changed" is displayed whenever
  // the resulting mode differs from the synthetic old mode (issue 987).
  if (verbose || changes) {
    unsigned int new_mode = old_mode;
    if (mode_result.value().is_numeric) {
      new_mode =
          static_cast<unsigned int>(mode_result.value().numeric_mode) & 07777u;
    } else {
      for (const auto &clause : mode_result.value().symbolic_clauses) {
        new_mode = apply_clause_for_display(new_mode, clause, is_dir);
      }
    }
    report_mode_change(path, old_mode, new_mode, old_mode != new_mode, verbose,
                       changes, is_dir);
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

  const auto operand = native_path::make_api_path_operand(path);
  WIN32_FILE_ATTRIBUTE_DATA before{};
  const bool had_before =
      GetFileAttributesExW(operand.extended.c_str(), GetFileExInfoStandard,
                           &before) != 0;
  const bool is_dir =
      had_before && (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  const unsigned int old_mode =
      had_before ? synthetic_mode(path, before.dwFileAttributes) : 0;
  const unsigned int new_mode = synthetic_mode(path, reference_attrs);

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
  if (verbose || changes) {
    report_mode_change(path, old_mode, new_mode, old_mode != new_mode, verbose,
                       changes, is_dir);
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
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring &wpath = operand.extended;

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    if (!ctx.get<bool>("-f", false) && !ctx.get<bool>("--silent", false) &&
        !ctx.get<bool>("--quiet", false)) {
      safeErrorPrint("chmod: ");
      safeErrorPrint(format_access_error(path, GetLastError()));
      safeErrorPrint("\n");
    }
    return std::unexpected("cannot access path");
  }

  bool is_directory =
      (attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // Process the current path.  process_file reports its own errors;
  // do not echo them again here (issue 327: GNU prints each diagnostic once).
  auto result = process_file(path, mode_str, ctx);
  (void)result;

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

        // Recursively process.  The child reports its own diagnostics.
        (void)process_recursive(subpath, mode_str, ctx);
      } while (FindNextFileW(hFind, &find_data) != 0);

      FindClose(hFind);
    }
  }

  return {};
}

auto process_recursive_reference(
    const std::string &path, DWORD reference_attrs,
    const CommandContext<CHMOD_OPTIONS.size()> &ctx) -> cp::Result<void> {
  const auto operand = native_path::make_api_path_operand(path);
  const std::wstring &wpath = operand.extended;

  WIN32_FILE_ATTRIBUTE_DATA attr_data;
  if (!GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &attr_data)) {
    if (!ctx.get<bool>("-f", false) && !ctx.get<bool>("--silent", false) &&
        !ctx.get<bool>("--quiet", false)) {
      safeErrorPrint("chmod: ");
      safeErrorPrint(format_access_error(path, GetLastError()));
      safeErrorPrint("\n");
    }
    return std::unexpected("cannot access path");
  }

  bool is_directory =
      (attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

  // process_file_reference reports its own errors; do not repeat them.
  (void)process_file_reference(path, reference_attrs, ctx);

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
        (void)process_recursive_reference(subpath, reference_attrs, ctx);
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

  // [DIFFERS] -H/-L/-P: symlink traversal options
  // On Windows, reparse point traversal follows OS rules.
  // -P (default, no traversal) is the effective behavior.
  (void)ctx.get<bool>("-H", false);
  (void)ctx.get<bool>("-L", false);
  (void)ctx.get<bool>("-P", false);

  // [DIFFERS] --dereference/-h/--no-dereference
  // On Windows, GetFileAttributesExW follows reparse points by default.
  // --dereference is the effective behavior.
  (void)ctx.get<bool>("--dereference", false);
  (void)ctx.get<bool>("--no-dereference", false);

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
  std::vector<std::string> files = has_reference ? expand_file_operands(ctx, 0)
                                                 : expand_file_operands(ctx, 1);
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
