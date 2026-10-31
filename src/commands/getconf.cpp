// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for getconf command.
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

auto constexpr GETCONF_OPTIONS =
    // [GNU]
    std::array{OPTION("-a", "--all", "display all configuration variables"),
               // [GNU] -v SPEC: give values for compilation environment SPEC
               OPTION("-v", "", "give values for compilation environment SPEC",
                      STRING_TYPE),
               // [GNU]
               OPTION("", "", "get system configuration values", STRING_TYPE)};

REGISTER_COMMAND(
    getconf,
    /* name */
    "getconf",

    /* synopsis */
    "getconf [OPTION]... [VARIABLE_NAME]...\n"
    "getconf [OPTION]... PATH_VAR PATH",
    "Display configuration variable values.\n"
    "\n"
    "With two operands, the first is a path variable and the second a\n"
    "path it is evaluated for. With no VARIABLE_NAME (and no -a) this\n"
    "is a usage error, as in glibc getconf.\n"
    "On Windows, this provides limited system configuration information.\n"
    "\n"
    "Options:\n"
    "  -a, --all  display all configuration variables\n"
    "  -v SPEC    give values for compilation environment SPEC",
    "  getconf\n"
    "  getconf PATH_MAX\n"
    "  getconf -a",

    /* see also */
    "sysconf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", GETCONF_OPTIONS) {
  namespace cp = core::pipeline;

  bool all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);

  // [GNU] -v SPEC: give values for compilation environment SPEC
  // On Windows, compilation environment SPEC is not applicable
  // Accept the option but ignore the SPEC value for compatibility
  (void)ctx.get<std::string>("-v", "");

  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);

  auto phys_pages = [&]() -> std::string {
    MEMORYSTATUS memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatus(&memStatus);
    return std::to_string(memStatus.dwTotalPhys / sysInfo.dwPageSize);
  };
  auto avphys_pages = [&]() -> std::string {
    MEMORYSTATUS memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatus(&memStatus);
    return std::to_string(memStatus.dwAvailPhys / sysInfo.dwPageSize);
  };

  // The supported variable table: canonical glibc-style name -> value.
  // Lookup is case-insensitive (a lenient superset of glibc, which spells
  // these names in upper case); diagnostics use the name as typed.
  auto lookup = [&](std::string_view typed) -> std::optional<std::string> {
    std::string var_name(typed);
    std::transform(var_name.begin(), var_name.end(), var_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (var_name == "arg_max" || var_name == "_arg_max") {
      // Windows has no kernel ARG_MAX; the effective per-process limit is
      // CreateProcess's 32767-character command line, so report that
      // instead of an undefined value (#1134).
      return std::string("32767");
    }
    if (var_name == "path_max" || var_name == "_posix_path_max" ||
        var_name == "name_max" || var_name == "_posix_name_max") {
      return std::string("260");  // MAX_PATH on Windows
    }
    if (var_name == "nprocessors_onln" ||
        var_name == "_nprocessors_onln" || var_name == "nprocessors_conf" ||
        var_name == "_nprocessors_conf") {
      return std::to_string(sysInfo.dwNumberOfProcessors);
    }
    if (var_name == "page_size" || var_name == "pagesize") {
      return std::to_string(sysInfo.dwPageSize);
    }
    if (var_name == "phys_pages" || var_name == "_phys_pages") {
      return phys_pages();
    }
    if (var_name == "avphys_pages" || var_name == "_avphys_pages") {
      return avphys_pages();
    }
    return std::nullopt;
  };

  // [glibc] getconf with no variable (and no -a) is a usage error, exit 1;
  // it never prints a free-form system summary.
  if (!all && ctx.positionals.empty()) {
    safeErrorPrintLn("getconf: no variable specified");
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "getconf"));
    return 1;
  }

  if (all) {
    // [glibc] -a prints one "NAME value" line per variable, no header.
    safePrintLn("ARG_MAX 32767");
    safePrintLn("NAME_MAX 260");
    safePrintLn("PATH_MAX 260");
    safePrintLn("_POSIX_NAME_MAX 260");
    safePrintLn("_POSIX_PATH_MAX 260");
    safePrintLn("PAGESIZE " + std::to_string(sysInfo.dwPageSize));
    safePrintLn("NPROCESSORS_CONF " +
                std::to_string(sysInfo.dwNumberOfProcessors));
    safePrintLn("NPROCESSORS_ONLN " +
                std::to_string(sysInfo.dwNumberOfProcessors));
    safePrintLn("PHYS_PAGES " + phys_pages());
    safePrintLn("AVPHYS_PAGES " + avphys_pages());
    return 0;
  }

  // [glibc] two operands are the pathconf form: getconf PATH_VAR PATH.
  // The variable must be a path variable, and the path must exist.
  if (ctx.positionals.size() == 2) {
    std::string typed(ctx.positionals[0]);
    std::string lower(typed);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    bool is_path_var = lower == "path_max" || lower == "_posix_path_max" ||
                       lower == "name_max" || lower == "_posix_name_max";
    if (!is_path_var) {
      safeErrorPrintLn("getconf: Unrecognized variable '" + typed + "'");
      return 1;
    }
    std::string path(ctx.positionals[1]);
    std::error_code ec;
    if (!std::filesystem::exists(
            std::filesystem::u8path(native_path::normalize_api_operand(path)),
            ec)) {
      safeErrorPrintLn("getconf: " + path + ": No such file or directory");
      return 1;
    }
    safePrintLn(*lookup(typed));
    return 0;
  }

  // Display specific configuration values
  int exit_code = 0;
  for (auto var : ctx.positionals) {
    std::string typed(var);
    if (auto value = lookup(typed)) {
      safePrintLn(*value);
    } else {
      // [glibc] an unknown variable is an error with a nonzero exit:
      // "getconf: Unrecognized variable '<name as typed>'".
      safeErrorPrintLn("getconf: Unrecognized variable '" + typed + "'");
      exit_code = 1;
    }
  }

  return exit_code;
}
