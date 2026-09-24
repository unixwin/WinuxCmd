// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unlink command.
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

auto constexpr UNLINK_OPTIONS =
    // [GNU] option
    std::array{OPTION("", "", "remove single file", STRING_TYPE)};

namespace {

auto windows_error_text(DWORD error) -> std::string {
  Win32ErrorTextOptions options;
  options.invalid_name_as_missing = true;
  return win32_posix_error_text(error, options);
}

auto describe_unlink_failure(const std::wstring& path, DWORD error)
    -> std::string {
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return "Is a directory";
  }
  if (path.find(L'*') != std::wstring::npos ||
      path.find(L'?') != std::wstring::npos ||
      path.find(L'[') != std::wstring::npos) {
    return "No such file or directory";
  }

  return windows_error_text(error);
}

auto remove_unlink_target(const std::wstring& path) -> bool {
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
      (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return RemoveDirectoryW(path.c_str()) != 0;
  }

  return DeleteFileW(path.c_str()) != 0;
}

}  // namespace

REGISTER_COMMAND(unlink,
                 /* name */
                 "unlink",

                 /* synopsis */
                 "unlink FILE",
                 "Remove a specified file.\n"
                 "\n"
                 "Unlink the file named FILE. If FILE is a symbolic link, the "
                 "symbolic link\n"
                 "is removed, not the file it points to.",
                 "  unlink file.txt",

                 /* see also */
                 "rm(1), link(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 UNLINK_OPTIONS) {
  namespace cp = core::pipeline;

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("unlink: missing operand");
    safeErrorPrintLn("Try 'unlink --help' for more information.");
    return 1;
  }
  if (ctx.positionals.size() > 1) {
    safeErrorPrintLn("unlink: extra operand '" +
                     std::string(ctx.positionals[1]) + "'");
    safeErrorPrintLn("Try 'unlink --help' for more information.");
    return 1;
  }

  std::string filename = std::string(ctx.positionals[0]);
  std::vector<std::string> expanded;
  if (contains_wildcard(filename)) {
    auto glob_result = glob_expand(filename);
    if (glob_result.expanded) {
      for (const auto& f : glob_result.files) {
        expanded.push_back(wstring_to_utf8(f));
      }
    }
    if (expanded.empty()) {
      expanded.push_back(filename);
    }
  } else {
    expanded.push_back(filename);
  }

  if (expanded.size() != 1) {
    safeErrorPrintLn("unlink: extra operand '" + expanded[1] + "'");
    safeErrorPrintLn("Try 'unlink --help' for more information.");
    return 1;
  }

  std::wstring wfilename = utf8_to_wstring(expanded[0]);
  auto operand = native_path::make_api_path_operand_w(wfilename);
  DWORD attrs = native_path::operand_target_attributes_w(operand);
  if (operand.had_trailing_separator && attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    safeErrorPrintLn("unlink: cannot unlink '" + expanded[0] +
                     "': Not a directory");
    return 1;
  }

  BOOL result = remove_unlink_target(operand.extended);
  if (!result) {
    DWORD error = GetLastError();
    safeErrorPrintLn("unlink: cannot unlink '" + expanded[0] +
                     "': " + describe_unlink_failure(operand.extended, error));
    return 1;
  }

  return 0;
}
