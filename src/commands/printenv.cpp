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
 *  - File: printenv.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for printenv command.
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

auto constexpr PRINTENV_OPTIONS =
    std::array{OPTION("", "", "print environment variables", STRING_TYPE)};

REGISTER_COMMAND(
    printenv,
    /* name */
    "printenv",

    /* synopsis */
    "printenv [OPTION]... [VARIABLE]...",
    "Print all or part of environment.\n"
    "\n"
    "If no VARIABLE is specified, print all environment variables.\n"
    "If one or more VARIABLE names are specified, print the values of those\n"
    "variables only.\n"
    "\n"
    "Options:\n"
    "  -0, --null    end each output line with NUL, not newline",
    "  printenv\n"
    "  printenv PATH\n"
    "  printenv HOME USER\n"
    "  printenv -0 | xargs -0 ...",

    /* see also */
    "env(1), set(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    PRINTENV_OPTIONS) {
  namespace cp = core::pipeline;

  bool null_terminated =
      ctx.get<bool>("--null", false) || ctx.get<bool>("-0", false);

  if (ctx.positionals.empty()) {
    // Print all environment variables
    std::vector<std::string> env_vars;
    for (wchar_t* env = GetEnvironmentStringsW(); *env;
         env += wcslen(env) + 1) {
      std::wstring wenv(env);
      std::string utf8_env = wstring_to_utf8(wenv);
      env_vars.push_back(utf8_env);
    }

    // Sort alphabetically
    std::sort(env_vars.begin(), env_vars.end());

    for (const auto& var : env_vars) {
      safePrint(var);
      if (null_terminated) {
        safePrint("\0");
      } else {
        safePrint("\n");
      }
    }
  } else {
    // Print specified environment variables
    bool all_found = true;
    for (auto var : ctx.positionals) {
      std::wstring wvar = utf8_to_wstring(std::string(var));
      wchar_t* value = _wgetenv(wvar.c_str());

      if (value) {
        safePrint(std::string(var));
        safePrint("=");
        safePrint(wstring_to_utf8(std::wstring(value)));
        if (null_terminated) {
          safePrint("\0");
        } else {
          safePrint("\n");
        }
      } else {
        all_found = false;
      }
    }

    return all_found ? 0 : 1;
  }

  return 0;
}
