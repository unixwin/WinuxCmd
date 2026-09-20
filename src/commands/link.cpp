// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for link command.
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

// [GNU] link accepts no options besides --help/--version; FILE1 FILE2 are
// positional operands only.
auto constexpr LINK_OPTIONS =
    std::array{OPTION("", "", "FILE1 FILE2", STRING_TYPE)};

auto link_windows_error_text(DWORD error) -> std::string {
  Win32ErrorTextOptions options;
  options.file_exists = true;
  options.privilege_not_held_as_not_permitted = true;
  return win32_posix_error_text(error, options);
}

REGISTER_COMMAND(
    link_cmd, "link", "link FILE1 FILE2",
    "Call the link function to create a link named FILE2 to an existing "
    "FILE1.\n",
    "  link existing.txt newlink.txt", "ln(1), symlink(2)", "WinuxCmd",
    "Copyright © 2026 WinuxCmd", LINK_OPTIONS) {
  const auto& positionals = ctx.positionals;
  if (positionals.size() < 2) {
    if (positionals.empty()) {
      safeErrorPrintLn("link: missing operand");
    } else {
      safeErrorPrint("link: missing operand after '");
      safeErrorPrint(std::string(positionals[0]));
      safeErrorPrintLn("'");
    }
    safeErrorPrintLn("Try 'link --help' for more information.");
    return 1;
  }
  if (positionals.size() > 2) {
    safeErrorPrint("link: extra operand '");
    safeErrorPrint(std::string(positionals[2]));
    safeErrorPrintLn("'");
    safeErrorPrintLn("Try 'link --help' for more information.");
    return 1;
  }

  const std::string file1(positionals[0]);
  const std::string file2(positionals[1]);

  // Resolve through the shared operand boundary so MSYS-style paths and
  // extended-length names behave like other tools.
  const auto target = native_path::make_api_path_operand(file1);
  const auto link_name = native_path::make_api_path_operand(file2);

  if (CreateHardLinkW(link_name.extended.c_str(), target.extended.c_str(),
                      nullptr)) {
    return 0;
  }

  const DWORD error = GetLastError();
  safeErrorPrint("link: cannot create link '");
  safeErrorPrint(file2);
  safeErrorPrint("' to '");
  safeErrorPrint(file1);
  safeErrorPrint("': ");
  safeErrorPrintLn(link_windows_error_text(error));
  return 1;
}
