// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

auto constexpr PRINTENV_OPTIONS = std::array{
    // [GNU]
    OPTION("-0", "--null", "end each output line with NUL, not newline")};

namespace {
void print_separator(bool null_terminated) {
  if (null_terminated) {
    safePrint(std::string_view("\0", 1));
  } else {
    safePrint("\n");
  }
}

bool is_hidden_windows_env_entry(const std::wstring& entry) {
  return !entry.empty() && entry.front() == L'=';
}

bool is_invalid_printenv_variable_name(std::string_view name) {
  return name.find('=') != std::string_view::npos;
}
}  // namespace

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
    wchar_t* env_block = GetEnvironmentStringsW();
    if (!env_block) {
      safeErrorPrintLn("printenv: cannot read environment");
      return 1;
    }

    for (wchar_t* env = env_block; *env; env += wcslen(env) + 1) {
      std::wstring wenv(env);
      if (is_hidden_windows_env_entry(wenv)) {
        continue;
      }
      std::string utf8_env = wstring_to_utf8(wenv);
      safePrint(utf8_env);
      print_separator(null_terminated);
    }
    FreeEnvironmentStringsW(env_block);
  } else {
    // Print specified environment variables
    bool all_found = true;
    for (auto var : ctx.positionals) {
      std::string var_name(var);
      if (is_invalid_printenv_variable_name(var_name)) {
        all_found = false;
        continue;
      }

      std::wstring wvar = utf8_to_wstring(var_name);
      wchar_t* value = _wgetenv(wvar.c_str());

      if (value) {
        safePrint(wstring_to_utf8(std::wstring(value)));
        print_separator(null_terminated);
      } else {
        all_found = false;
      }
    }

    return all_found ? 0 : 1;
  }

  return 0;
}
