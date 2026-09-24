// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    std::array{// [GNU]
               OPTION("-a", "--all", "print all available locales"),
               // [GNU]
               OPTION("-c", "--category-name",
                      "print the names of defined locale categories"),
               // [GNU]
               OPTION("-k", "--keyword-name",
                      "print the names and values of keyword definitions"),
               // [GNU]
               OPTION("-m", "--charmaps", "print available character maps"),
               // [GNU] -v, --verbose: print more information
               OPTION("-v", "--verbose", "print more information"),
               // [GNU]
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

// The locale name the environment selects, in the POSIX precedence order
// (LC_ALL overrides LC_CTYPE overrides LANG). Empty when none is set.
std::string effective_locale_name() {
  for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
    const char* value = std::getenv(name);
    if (value != nullptr && *value != '\0') {
      return value;
    }
  }
  return {};
}

// [GNU] Codeset of the locale selected by the environment.
//
// GNU `locale charmap` answers from the POSIX locale database, so LC_ALL=C
// yields ANSI_X3.4-1968 (pure ASCII) while LC_ALL=C.UTF-8 yields UTF-8. Windows
// ships no such database, but the C/POSIX case is fully determined: it is
// ASCII, and a WinuxCmd that ignored the environment answered "UTF-8" there -
// a semantic difference from GNU, not a formatting one. Unknown and unset
// locales keep the process codeset.
std::string effective_codeset() {
  const std::string locale = effective_locale_name();
  if (locale == "C" || locale == "POSIX") {
    return "ANSI_X3.4-1968";
  }
  return "UTF-8";
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
                 "  locale -m\n"
                 "  locale -c\n"
                 "  locale -k charmap",
                 /* see_also */ "localedef, setlocale",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ LOCALE_OPTIONS) {
  bool show_all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);
  bool show_charmaps =
      ctx.get<bool>("-m", false) || ctx.get<bool>("--charmaps", false);
  bool show_category_names =
      ctx.get<bool>("-c", false) || ctx.get<bool>("--category-name", false);
  bool show_keyword_names =
      ctx.get<bool>("-k", false) || ctx.get<bool>("--keyword-name", false);

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

  // -c/--category-name: print names of defined locale categories
  if (show_category_names) {
    safePrintLn("LC_CTYPE");
    safePrintLn("LC_NUMERIC");
    safePrintLn("LC_TIME");
    safePrintLn("LC_COLLATE");
    safePrintLn("LC_MONETARY");
    safePrintLn("LC_MESSAGES");
    safePrintLn("LC_PAPER");
    safePrintLn("LC_NAME");
    safePrintLn("LC_ADDRESS");
    safePrintLn("LC_TELEPHONE");
    safePrintLn("LC_MEASUREMENT");
    safePrintLn("LC_IDENTIFICATION");
    return 0;
  }

  // -k/--keyword-name: print keyword names and values
  if (show_keyword_names) {
    if (!ctx.positionals.empty()) {
      const auto keyword = std::string(ctx.positionals.front());
      const auto value = get_system_locale();
      if (keyword == "charmap") {
        safePrintLn("charmap=" + effective_codeset());
      } else if (keyword == "codeset") {
        safePrintLn("codeset=" + effective_codeset());
      } else if (keyword == "collate") {
        safePrintLn("collate=" + value);
      } else if (keyword == "ctype") {
        safePrintLn("ctype=" + value);
      } else if (keyword == "monetary") {
        safePrintLn("monetary=" + value);
      } else if (keyword == "numeric") {
        safePrintLn("numeric=" + value);
      } else if (keyword == "time") {
        safePrintLn("time=" + value);
      } else {
        safePrintLn(keyword + "=" + value);
      }
    } else {
      // No keyword specified: print all locale keywords and values
      safePrintLn("LANG=" + get_system_locale());
      safePrintLn(
          "LC_CTYPE="
          " + get_system_locale() + "
          "");
      safePrintLn(
          "LC_NUMERIC="
          " + get_system_locale() + "
          "");
      safePrintLn(
          "LC_TIME="
          " + get_system_locale() + "
          "");
      safePrintLn(
          "LC_COLLATE="
          " + get_system_locale() + "
          "");
      safePrintLn(
          "LC_MONETARY="
          " + get_system_locale() + "
          "");
      safePrintLn(
          "LC_MESSAGES="
          " + get_system_locale() + "
          "");
      safePrintLn("charmap=" + effective_codeset());
      safePrintLn("codeset=" + effective_codeset());
    }
    return 0;
  }

  if (!ctx.positionals.empty()) {
    const auto keyword = std::string(ctx.positionals.front());
    const auto value = get_system_locale();
    if (keyword == "charmap") {
      safePrintLn(effective_codeset());
      return 0;
    }
    if (keyword == "default_language") {
      safePrintLn(value.substr(0, value.find_first_of("_-")));
      return 0;
    }
    if (keyword == "decimal_point") {
      safePrintLn(".");
      return 0;
    }
    if (keyword == "thousands_sep") {
      safePrintLn("");
      return 0;
    }
    if (keyword == "codeset") {
      safePrintLn(effective_codeset());
      return 0;
    }
    if (keyword == "yesexpr") {
      safePrintLn("^[yY]");
      return 0;
    }
    if (keyword == "noexpr") {
      safePrintLn("^[nN]");
      return 0;
    }
    if (keyword == "yesstr") {
      safePrintLn("yes");
      return 0;
    }
    if (keyword == "nostr") {
      safePrintLn("no");
      return 0;
    }
    if (keyword == "era") {
      safePrintLn("");
      return 0;
    }
    if (keyword == "day") {
      safePrintLn("Sunday;Monday;Tuesday;Wednesday;Thursday;Friday;Saturday");
      return 0;
    }
    if (keyword == "abday") {
      safePrintLn("Sun;Mon;Tue;Wed;Thu;Fri;Sat");
      return 0;
    }
    if (keyword == "month") {
      safePrintLn(
          "January;February;March;April;May;June;July;August;September;October;"
          "November;December");
      return 0;
    }
    if (keyword == "abmon") {
      safePrintLn("Jan;Feb;Mar;Apr;May;Jun;Jul;Aug;Sep;Oct;Nov;Dec");
      return 0;
    }
    if (keyword == "int_curr_symbol") {
      safePrintLn("USD ");
      return 0;
    }
    if (keyword == "currency_symbol") {
      safePrintLn("$");
      return 0;
    }
    if (keyword == "mon_decimal_point") {
      safePrintLn(".");
      return 0;
    }
    if (keyword == "mon_thousands_sep") {
      safePrintLn(",");
      return 0;
    }
    if (keyword == "positive_sign" || keyword == "negative_sign") {
      safePrintLn("");
      return 0;
    }
    if (keyword == "frac_digits" || keyword == "int_frac_digits" ||
        keyword == "p_cs_precedes" || keyword == "p_sep_by_space" ||
        keyword == "n_cs_precedes" || keyword == "n_sep_by_space") {
      safePrintLn("1");
      return 0;
    }
    safeErrorPrintLn("locale: unknown keyword '" + keyword + "'");
    return 1;
  }

  // Print current locale settings
  print_locale_categories();

  return 0;
}
