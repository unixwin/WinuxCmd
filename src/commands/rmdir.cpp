// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// [GNU] --ignore-fail-on-non-empty: matches GNU coreutils rmdir
// [GNU] -p, --parents: matches GNU coreutils rmdir
// [GNU] -v, --verbose: matches GNU coreutils rmdir
auto constexpr RMDIR_OPTIONS =
    std::array{OPTION("", "--ignore-fail-on-non-empty",
                      "ignore each failure to remove a non-empty directory"),
               OPTION("-p", "--parents", "remove DIRECTORY and its ancestors"),
               // [GNU] --path: deprecated alias for -p/--parents; hidden
               OPTION("", "--path", "", BOOL_TYPE),
               OPTION("-v", "--verbose",
                      "output a diagnostic for every directory processed")};

namespace rmdir_pipeline {
namespace cp = core::pipeline;

auto parent_path(std::string path) -> std::string {
  path = native_path::normalize_api_operand(path);
  if (path.empty()) return {};

  auto pos = path.find_last_of("\\/");
  if (pos == std::string::npos || pos == 0) return {};
  if (pos == 2 && path.size() >= 3 && path[1] == ':') return {};
  if (auto root_len = native_path::unc_root_length(path);
      root_len && pos <= *root_len) {
    return {};
  }

  auto parent = path.substr(0, pos);
  if (parent == "." || parent == "..") return {};
  return parent;
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

auto has_dot_final_component(std::string_view path) -> bool {
  auto last_sep = path.find_last_of("\\/");
  std::string_view leaf =
      last_sep == std::string_view::npos ? path : path.substr(last_sep + 1);
  return leaf == ".";
}

auto report_remove_failure(const std::string& utf8_path,
                           std::string_view reason) -> bool {
  safeErrorPrint("rmdir: failed to remove '");
  safeErrorPrint(utf8_path);
  safeErrorPrint("': ");
  safeErrorPrint(std::string(reason));
  safeErrorPrint("\n");
  return false;
}

auto remove_one(const std::string& utf8_path, bool ignore_non_empty,
                bool verbose) -> bool {
  auto operand = native_path::make_api_path_operand(utf8_path);
  const std::wstring& wpath = operand.extended;
  if (verbose) {
    safePrint("rmdir: removing directory, '");
    safePrint(utf8_path);
    safePrint("'\n");
  }

  // [GNU] rmdir(2) on a "dir/." operand fails EINVAL.
  if (has_dot_final_component(utf8_path)) {
    return report_remove_failure(utf8_path, "Invalid argument");
  }

  DWORD attrs = native_path::operand_target_attributes_w(operand);
  if (operand.had_trailing_separator && attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
      !native_path::attributes_are_reparse_point(attrs)) {
    return report_remove_failure(utf8_path, "Not a directory");
  }

  // [GNU] rmdir(2) never removes a final-component symlink or junction:
  // the kernel refuses with ENOTDIR (ELOOP "Symbolic link not followed"
  // for a "link/" operand with a trailing slash) and the link is
  // preserved.  GetFileAttributesW reports the reparse point's own
  // attributes (including FILE_ATTRIBUTE_DIRECTORY for dir links), and
  // RemoveDirectoryW would silently delete the reparse point itself, so
  // refuse before calling it (uutils#9980, issue 279/1062).
  if (native_path::attributes_are_reparse_point(attrs)) {
    return report_remove_failure(utf8_path, operand.had_trailing_separator
                                                ? "Symbolic link not followed"
                                                : "Not a directory");
  }

  if (RemoveDirectoryW(wpath.c_str())) {
    return true;
  }

  DWORD e = GetLastError();
  if (e == ERROR_DIR_NOT_EMPTY && ignore_non_empty) return true;

  if (e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND) {
    return report_remove_failure(utf8_path, "No such file or directory");
  }
  if (e == ERROR_DIRECTORY) {
    return report_remove_failure(utf8_path, "Not a directory");
  }
  if (e == ERROR_DIR_NOT_EMPTY) {
    return report_remove_failure(utf8_path, "Directory not empty");
  }
  if (e == ERROR_CURRENT_DIRECTORY || e == ERROR_BUSY) {
    // GNU rmdir("..") reports ENOTEMPTY.
    return report_remove_failure(utf8_path, "Directory not empty");
  }
  if (e == ERROR_ACCESS_DENIED || e == ERROR_WRITE_PROTECT) {
    return report_remove_failure(utf8_path, "Permission denied");
  }

  return report_remove_failure(utf8_path, wstring_to_utf8(win32_error_text(e)));
}

auto process_command(const CommandContext<RMDIR_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  if (ctx.positionals.empty()) return std::unexpected("missing operand");

  bool parents = ctx.get<bool>("--parents", false) ||
                 ctx.get<bool>("-p", false) || ctx.get<bool>("--path", false);
  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  bool ignore_non_empty = ctx.get<bool>("--ignore-fail-on-non-empty", false);

  bool ok_all = true;

  for (auto arg : ctx.positionals) {
    std::string cur(arg);
    if (!remove_one(cur, ignore_non_empty, verbose)) {
      ok_all = false;
      continue;
    }

    if (!parents) continue;

    while (true) {
      std::string p = parent_path(cur);
      if (p.empty()) break;

      if (!remove_one(p, ignore_non_empty, verbose)) {
        ok_all = false;
        break;
      }
      cur = p;
    }
  }

  return ok_all;
}
}  // namespace rmdir_pipeline

REGISTER_COMMAND(rmdir, "rmdir", "rmdir [OPTION]... DIRECTORY...",
                 "Remove the DIRECTORY(ies), if they are empty.",
                 "  rmdir dir\n"
                 "  rmdir -p a/b/c\n"
                 "  rmdir --ignore-fail-on-non-empty dir",
                 "mkdir(1), rm(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 RMDIR_OPTIONS) {
  using namespace rmdir_pipeline;
  auto result = process_command(ctx);
  if (!result) {
    if (result.error() == "missing operand") {
      safeErrorPrintLn("rmdir: missing operand");
      safeErrorPrintLn("Try 'rmdir --help' for more information.");
      return 1;
    }
    cp::report_error(result, L"rmdir");
    return 1;
  }
  return *result ? 0 : 1;
}
