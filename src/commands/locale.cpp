/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: locale.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for locale command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr LOCALE_OPTIONS =
    std::array{OPTION("-a", "--all", "print all available locales"),
               OPTION("-m", "--charmaps", "print available character maps"),
               OPTION("", "", "print locale information", STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Get system locale
std::string get_system_locale() {
  wchar_t locale[LOCALE_NAME_MAX_LENGTH];
  if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH)) {
    return wstring_to_utf8(locale);
  }
  return "C";
}

// Get all available locales
std::vector<std::string> get_available_locales() {
  std::vector<std::string> locales;

  // Common locales
  locales.push_back("C");
  locales.push_back("C.UTF-8");
  locales.push_back("POSIX");
  locales.push_back("en_US.UTF-8");
  locales.push_back("en_US");
  locales.push_back("en_GB.UTF-8");
  locales.push_back("en_GB");
  locales.push_back("zh_CN.UTF-8");
  locales.push_back("zh_CN");
  locales.push_back("zh_TW.UTF-8");
  locales.push_back("zh_TW");
  locales.push_back("ja_JP.UTF-8");
  locales.push_back("ja_JP");
  locales.push_back("ko_KR.UTF-8");
  locales.push_back("ko_KR");
  locales.push_back("fr_FR.UTF-8");
  locales.push_back("fr_FR");
  locales.push_back("de_DE.UTF-8");
  locales.push_back("de_DE");
  locales.push_back("es_ES.UTF-8");
  locales.push_back("es_ES");

  return locales;
}

// Print locale categories
void print_locale_categories() {
  std::string sys_locale = get_system_locale();

  safePrintLn("LANG=" + sys_locale);
  safePrintLn("LC_CTYPE=\"" + sys_locale + "\"");
  safePrintLn("LC_NUMERIC=\"" + sys_locale + "\"");
  safePrintLn("LC_TIME=\"" + sys_locale + "\"");
  safePrintLn("LC_COLLATE=\"" + sys_locale + "\"");
  safePrintLn("LC_MONETARY=\"" + sys_locale + "\"");
  safePrintLn("LC_MESSAGES=\"" + sys_locale + "\"");
  safePrintLn("LC_PAPER=\"" + sys_locale + "\"");
  safePrintLn("LC_NAME=\"" + sys_locale + "\"");
  safePrintLn("LC_ADDRESS=\"" + sys_locale + "\"");
  safePrintLn("LC_TELEPHONE=\"" + sys_locale + "\"");
  safePrintLn("LC_MEASUREMENT=\"" + sys_locale + "\"");
  safePrintLn("LC_IDENTIFICATION=\"" + sys_locale + "\"");
  safePrintLn("LC_ALL=");
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(locale,
                 /* cmd_name */ "locale",
                 /* cmd_synopsis */ "locale [OPTION]",
                 /* cmd_desc */
                 "Get locale-specific information.\n"
                 "Write locale-specific information to standard output.",
                 /* examples */
                 "  locale\n"
                 "  locale -a\n"
                 "  locale -m",
                 /* see_also */ "localedef, setlocale",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ LOCALE_OPTIONS) {
  bool show_all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);
  bool show_charmaps =
      ctx.get<bool>("-m", false) || ctx.get<bool>("--charmaps", false);

  if (show_charmaps) {
    // Print available character maps
    safePrintLn("UTF-8");
    safePrintLn("ASCII");
    safePrintLn("ISO-8859-1");
    safePrintLn("CP1252");
    return 0;
  }

  if (show_all) {
    // Print all available locales
    auto locales = get_available_locales();
    for (const auto& loc : locales) {
      safePrintLn(loc);
    }
    return 0;
  }

  // Print current locale settings
  print_locale_categories();

  return 0;
}
