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
    std::array{// [GNU] -a, --all-locales (glibc name; --all kept as an
               // alias)
               OPTION("-a", "--all-locales", "print all available locales"),
               OPTION("", "--all", "same as --all-locales"),
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
               // [GNU] --usage: print a short usage message and exit
               OPTION("", "--usage", "print a short usage message and exit"),
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

// [GNU] glibc locale(1) answers NAME operands from a fixed set of locale
// item names (glibc categories.def) plus the special names locale_special
// handles; `charmap` is one of those specials while `codeset` is not
// (glibc 2.39: `locale codeset` -> `locale: unknown name "codeset"`).
// This is the whitelist of names this applet can answer on Windows; any
// other operand is the GNU "unknown name" error.
auto constexpr KNOWN_KEYWORDS = std::array{
    "charmap",
    "decimal_point",
    "thousands_sep",
    "yesexpr",
    "noexpr",
    "yesstr",
    "nostr",
    "era",
    "day",
    "abday",
    "mon",
    "abmon",
    "int_curr_symbol",
    "currency_symbol",
    "mon_decimal_point",
    "mon_thousands_sep",
    "positive_sign",
    "negative_sign",
    "frac_digits",
    "int_frac_digits",
    "p_cs_precedes",
    "p_sep_by_space",
    "n_cs_precedes",
    "n_sep_by_space",
};

// [GNU] glibc print_item() renders -k values by item type: string items
// as name="value" (quoted even when empty), byte/word items (the numeric
// monetary flags) unquoted, and the era stringlist as a bare name= when
// empty (glibc locale/programs/locale.c print_item).
auto is_numeric_keyword(const std::string& keyword) -> bool {
  return keyword == "frac_digits" || keyword == "int_frac_digits" ||
         keyword == "p_cs_precedes" || keyword == "p_sep_by_space" ||
         keyword == "n_cs_precedes" || keyword == "n_sep_by_space";
}

auto is_known_keyword(const std::string& keyword) -> bool {
  return std::find(KNOWN_KEYWORDS.begin(), KNOWN_KEYWORDS.end(), keyword) !=
         KNOWN_KEYWORDS.end();
}

// The value the applet's simulation model answers for a known keyword.
auto keyword_value(const std::string& keyword) -> std::string {
  const auto value = get_system_locale();
  if (keyword == "charmap") return effective_codeset();
  if (keyword == "decimal_point") return ".";
  if (keyword == "thousands_sep") return "";
  if (keyword == "yesexpr") return "^[yY]";
  if (keyword == "noexpr") return "^[nN]";
  if (keyword == "yesstr") return "yes";
  if (keyword == "nostr") return "no";
  if (keyword == "era") return "";
  if (keyword == "day")
    return "Sunday;Monday;Tuesday;Wednesday;Thursday;Friday;Saturday";
  if (keyword == "abday") return "Sun;Mon;Tue;Wed;Thu;Fri;Sat";
  if (keyword == "mon")
    return "January;February;March;April;May;June;July;August;September;"
           "October;November;December";
  if (keyword == "abmon") return "Jan;Feb;Mar;Apr;May;Jun;Jul;Aug;Sep;Oct;"
                                "Nov;Dec";
  if (keyword == "int_curr_symbol") return "USD ";
  if (keyword == "currency_symbol") return "$";
  if (keyword == "mon_decimal_point") return ".";
  if (keyword == "mon_thousands_sep") return ",";
  if (keyword == "positive_sign" || keyword == "negative_sign") return "";
  return "1";  // numeric monetary flags (frac_digits, *_cs_precedes, ...)
}

// [GNU] A NAME operand in the plain or -k mode is either the special
// charmap or one of the glibc item names; anything else is diagnosed as
// `locale: unknown name "X"` and exits 1 (glibc locale_special.c).
auto report_unknown_name(const std::string& name) -> int {
  safeErrorPrintLn("locale: unknown name \"" + name + "\"");
  return 1;
}

// Print one keyword value: bare value in answer mode, `name="value"` in
// -k mode (strings quoted, numeric flags unquoted, empty era bare).
void print_keyword(const std::string& keyword, bool keyword_mode) {
  const std::string value = keyword_value(keyword);
  if (!keyword_mode) {
    safePrintLn(value);
    return;
  }
  if (keyword == "era") {
    safePrintLn(keyword + "=" + value);
  } else if (is_numeric_keyword(keyword)) {
    safePrintLn(keyword + "=" + value);
  } else {
    safePrintLn(keyword + "=\"" + value + "\"");
  }
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
  // [GNU] --usage prints a short usage message and exits, before any
  // other mode is considered (glibc locale behavior).
  if (ctx.get<bool>("--usage", false)) {
    safePrintLn("Usage: locale [OPTION...] name");
    safePrintLn("  or:  locale [OPTION...] [-a|-m] [OPTION...]");
    safePrintLn("");
    safePrintLn("Get locale-specific information.");
    safePrintLn("");
    safePrintLn(
        "Mandatory arguments to long options are also mandatory "
        "for any corresponding");
    safePrintLn("short options.");
    safePrintLn("");
    safePrintLn("  -a, --all-locales    print all available locales");
    safePrintLn(
        "  -c, --category-name  print the names of defined locale "
        "categories");
    safePrintLn(
        "  -k, --keyword-name   print the names and values of "
        "keyword definitions");
    safePrintLn("  -m, --charmaps       print available character maps");
    safePrintLn("  -v, --verbose        print more information");
    safePrintLn(
        "      --usage          print this short usage message "
        "and exit");
    return 0;
  }

  bool show_all = ctx.get<bool>("-a", false) ||
                  ctx.get<bool>("--all-locales", false) ||
                  ctx.get<bool>("--all", false);
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

  // -c/--category-name: print names of defined locale categories.
  // [GNU] glibc locale(1) writes nothing unless NAME operands are given:
  // a bare `-c` is silent and exits 0 (glibc 2.39 locale.c main(): only
  // the "no argument at all" branch dumps the environment, -c/-k feed
  // the per-NAME loop). The per-category value dump needs a locale
  // archive Windows does not carry (simulation layer, niu #758), so the
  // named form keeps answering the category-name list.
  if (show_category_names) {
    if (ctx.positionals.empty()) {
      return 0;
    }
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

  // -k/--keyword-name: print keyword names and values.
  // [GNU] bare -k is silent (see -c above); each NAME operand must be a
  // known item name, else the "unknown name" error, exit 1.
  if (show_keyword_names) {
    if (ctx.positionals.empty()) {
      return 0;
    }
    for (const auto& positional : ctx.positionals) {
      const auto keyword = std::string(positional);
      if (!is_known_keyword(keyword)) {
        return report_unknown_name(keyword);
      }
      print_keyword(keyword, /*keyword_mode=*/true);
    }
    return 0;
  }

  if (!ctx.positionals.empty()) {
    // [GNU] a NAME operand is answered from the item-name whitelist; an
    // unknown name is `locale: unknown name "X"`, exit 1 (glibc
    // locale_special.c, observed on glibc 2.39: `locale codeset` is an
    // unknown name, `mon` is the LC_TIME full-month item — `month` is
    // not a name at all). Several NAMEs are answered in argv order.
    for (const auto& positional : ctx.positionals) {
      const auto keyword = std::string(positional);
      if (!is_known_keyword(keyword)) {
        return report_unknown_name(keyword);
      }
      print_keyword(keyword, /*keyword_mode=*/false);
    }
    return 0;
  }

  // Print current locale settings
  print_locale_categories();

  return 0;
}
