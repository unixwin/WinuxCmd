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
 *  - File: getconf.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
    std::array{OPTION("-a", "--all", "display all configuration variables"),
               OPTION("", "", "get system configuration values", STRING_TYPE)};

REGISTER_COMMAND(
    getconf,
    /* name */
    "getconf",

    /* synopsis */
    "getconf [OPTION]... [VARIABLE_NAME]...",
    "Display configuration variable values.\n"
    "\n"
    "If no VARIABLE_NAME is specified, display system-dependent limit values.\n"
    "On Windows, this provides limited system configuration information.\n"
    "\n"
    "Options:\n"
    "  -a, --all  display all configuration variables",
    "  getconf\n"
    "  getconf PATH_MAX\n"
    "  getconf -a",

    /* see also */
    "sysconf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", GETCONF_OPTIONS) {
  namespace cp = core::pipeline;

  bool all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);

  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);

  if (all || ctx.positionals.empty()) {
    // Display all available configuration values
    safePrintLn("System configuration:");
    safePrintLn("  NPROCESSORS_ONLN: " +
                std::to_string(sysInfo.dwNumberOfProcessors));
    safePrintLn("  PAGE_SIZE: " + std::to_string(sysInfo.dwPageSize));
    safePrintLn("  MACHINE_TYPE: " +
                std::to_string(sysInfo.wProcessorArchitecture));

    MEMORYSTATUS memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatus(&memStatus);
    safePrintLn("  PHYS_PAGES: " +
                std::to_string(memStatus.dwTotalPhys / sysInfo.dwPageSize));
    safePrintLn("  AVPHYS_PAGES: " +
                std::to_string(memStatus.dwAvailPhys / sysInfo.dwPageSize));

    return 0;
  }

  // Display specific configuration values
  for (auto var : ctx.positionals) {
    std::string var_name = std::string(var);
    std::transform(var_name.begin(), var_name.end(), var_name.begin(),
                   ::tolower);

    if (var_name == "path_max" || var_name == "name_max") {
      safePrintLn("260");  // MAX_PATH on Windows
    } else if (var_name == "nprocessors_onln" ||
               var_name == "nprocessors_conf") {
      safePrintLn(std::to_string(sysInfo.dwNumberOfProcessors));
    } else if (var_name == "page_size") {
      safePrintLn(std::to_string(sysInfo.dwPageSize));
    } else if (var_name == "phys_pages") {
      MEMORYSTATUS memStatus;
      memStatus.dwLength = sizeof(memStatus);
      GlobalMemoryStatus(&memStatus);
      safePrintLn(std::to_string(memStatus.dwTotalPhys / sysInfo.dwPageSize));
    } else if (var_name == "avphys_pages") {
      MEMORYSTATUS memStatus;
      memStatus.dwLength = sizeof(memStatus);
      GlobalMemoryStatus(&memStatus);
      safePrintLn(std::to_string(memStatus.dwAvailPhys / sysInfo.dwPageSize));
    } else {
      safeErrorPrint("getconf: '" + var_name +
                     "' is not available on Windows\n");
    }
  }

  return 0;
}
