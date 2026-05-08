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
 *  - File: whoami.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for whoami.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <lmcons.h>
#include <windows.h>

#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr WHOAMI_OPTIONS = std::array{
    OPTION("", "", "print effective userid", STRING_TYPE)
    // whoami has no standard options
    // -a, --all (not implemented - print all information)
    // -b, --boot (not implemented - print system boot time)
    // -d, --dead (not implemented - print dead processes)
    // -H, --heading (not implemented - print line of column headings)
    // -l, --login (not implemented - print login name)
    // -p, --process (not implemented - print process information)
    // -r, --runlevel (not implemented - print runlevel)
    // -t, --time (not implemented - print last system clock change)
    // -u, --users (not implemented - print logged in users)
};

namespace whoami_pipeline {
namespace cp = core::pipeline;

struct Config {
  // No configuration needed for basic whoami
};

auto build_config(const CommandContext<WHOAMI_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  return cfg;
}

auto run(const Config& cfg) -> int {
  // Get current username
  char username[UNLEN + 1];
  DWORD username_size = UNLEN + 1;

  if (!GetUserNameA(username, &username_size)) {
    cp::Result<int> result = std::unexpected("failed to get username");
    cp::report_error(result, L"whoami");
    return 1;
  }

  safePrintLn(username);
  return 0;
}

}  // namespace whoami_pipeline

REGISTER_COMMAND(
    whoami, "whoami", "whoami [OPTION]",
    "Print effective userid.\n"
    "\n"
    "Print the user name associated with the current effective user ID.\n"
    "Same as id -un.\n"
    "\n"
    "Note: This implementation only supports basic username printing.\n"
    "Advanced features are not implemented.",
    "  whoami\n"
    "  id -un",
    "id(1), logname(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    WHOAMI_OPTIONS) {
  using namespace whoami_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"whoami");
    return 1;
  }

  return run(*cfg_result);
}
