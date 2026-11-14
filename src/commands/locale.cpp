// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for locale command.
/// @Version: 0.2.0
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

using winux::locale_db::LocaleValues;

// [GNU] glibc locale(1) answers NAME operands from the item names of
// categories.def, plus the special name `charmap`. Each item belongs to
// exactly one category, and glibc answers it from that category's locale -
// so `LC_TIME=de_DE locale day` reads LC_TIME while `locale currency_symbol`
// reads LC_MONETARY. `codeset` is not a name (glibc 2.39: `locale codeset`
// -> `locale: unknown name "codeset"`); `mon` is the LC_TIME full-month item.
enum class ValueKind {
  kString,   // printed name="value" in -k mode
  kNumeric,  // printed name=value (byte/word items)
  kEra,      // stringlist, printed bare even when empty
};

struct KeywordEntry {
  std::string_view name;
  std::string_view category;
  ValueKind kind;
};

auto constexpr KEYWORDS = std::array{
    KeywordEntry{"charmap", "LC_CTYPE", ValueKind::kString},
    // LC_NUMERIC
    KeywordEntry{"decimal_point", "LC_NUMERIC", ValueKind::kString},
    KeywordEntry{"thousands_sep", "LC_NUMERIC", ValueKind::kString},
    KeywordEntry{"grouping", "LC_NUMERIC", ValueKind::kNumeric},
    // LC_MONETARY
    KeywordEntry{"int_curr_symbol", "LC_MONETARY", ValueKind::kString},
    KeywordEntry{"currency_symbol", "LC_MONETARY", ValueKind::kString},
    KeywordEntry{"mon_decimal_point", "LC_MONETARY", ValueKind::kString},
    KeywordEntry{"mon_thousands_sep", "LC_MONETARY", ValueKind::kString},
    KeywordEntry{"mon_grouping", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"positive_sign", "LC_MONETARY", ValueKind::kString},
    KeywordEntry{"negative_sign", "LC_MONETARY", ValueKind::kString},
    KeywordEntry{"int_frac_digits", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"frac_digits", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"p_cs_precedes", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"p_sep_by_space", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"n_cs_precedes", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"n_sep_by_space", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"p_sign_posn", "LC_MONETARY", ValueKind::kNumeric},
    KeywordEntry{"n_sign_posn", "LC_MONETARY", ValueKind::kNumeric},
    // LC_MESSAGES
    KeywordEntry{"yesexpr", "LC_MESSAGES", ValueKind::kString},
    KeywordEntry{"noexpr", "LC_MESSAGES", ValueKind::kString},
    KeywordEntry{"yesstr", "LC_MESSAGES", ValueKind::kString},
    KeywordEntry{"nostr", "LC_MESSAGES", ValueKind::kString},
    // LC_TIME
    KeywordEntry{"day", "LC_TIME", ValueKind::kString},
    KeywordEntry{"abday", "LC_TIME", ValueKind::kString},
    KeywordEntry{"mon", "LC_TIME", ValueKind::kString},
    KeywordEntry{"abmon", "LC_TIME", ValueKind::kString},
    KeywordEntry{"am_pm", "LC_TIME", ValueKind::kString},
    KeywordEntry{"d_fmt", "LC_TIME", ValueKind::kString},
    KeywordEntry{"t_fmt", "LC_TIME", ValueKind::kString},
    KeywordEntry{"d_t_fmt", "LC_TIME", ValueKind::kString},
    KeywordEntry{"t_fmt_ampm", "LC_TIME", ValueKind::kString},
    KeywordEntry{"era", "LC_TIME", ValueKind::kEra},
};

auto find_keyword(std::string_view keyword) -> const KeywordEntry* {
  for (const auto& entry : KEYWORDS) {
    if (entry.name == keyword) return &entry;
  }
  return nullptr;
}

// glibc's locale(1) renders LC_TIME stringlists as the items joined by ';'
// (empty items included: de_DE's am_pm is ";").
template <size_t N>
auto join_list(const std::array<std::string, N>& items) -> std::string {
  std::string result;
  for (size_t i = 0; i < N; ++i) {
    if (i != 0) result.push_back(';');
    result += items[i];
  }
  return result;
}

// The value the locale database answers for one keyword, read from the
// locale of the keyword's own category (POSIX setlocale semantics).
auto keyword_value(std::string_view keyword) -> std::string {
  const KeywordEntry* entry = find_keyword(keyword);
  if (entry == nullptr) return {};
  const LocaleValues& values = winux::locale_db::for_category(entry->category);
  if (keyword == "charmap") return values.codeset.charmap;
  // LC_NUMERIC
  if (keyword == "decimal_point") return values.numeric.decimal_point;
  if (keyword == "thousands_sep") return values.numeric.thousands_sep;
  if (keyword == "grouping") return values.numeric.grouping_text;
  // LC_MONETARY
  if (keyword == "int_curr_symbol") return values.monetary.int_curr_symbol;
  if (keyword == "currency_symbol") return values.monetary.currency_symbol;
  if (keyword == "mon_decimal_point") return values.monetary.mon_decimal_point;
  if (keyword == "mon_thousands_sep") return values.monetary.mon_thousands_sep;
  if (keyword == "mon_grouping") return values.monetary.mon_grouping_text;
  if (keyword == "positive_sign") return values.monetary.positive_sign;
  if (keyword == "negative_sign") return values.monetary.negative_sign;
  if (keyword == "frac_digits") {
    return std::to_string(values.monetary.frac_digits);
  }
  if (keyword == "int_frac_digits")
    return std::to_string(values.monetary.int_frac_digits);
  if (keyword == "p_cs_precedes")
    return std::to_string(values.monetary.p_cs_precedes);
  if (keyword == "p_sep_by_space")
    return std::to_string(values.monetary.p_sep_by_space);
  if (keyword == "n_cs_precedes")
    return std::to_string(values.monetary.n_cs_precedes);
  if (keyword == "n_sep_by_space")
    return std::to_string(values.monetary.n_sep_by_space);
  if (keyword == "p_sign_posn")
    return std::to_string(values.monetary.p_sign_posn);
  if (keyword == "n_sign_posn")
    return std::to_string(values.monetary.n_sign_posn);
  // LC_MESSAGES
  if (keyword == "yesexpr") return values.yesexpr;
  if (keyword == "noexpr") return values.noexpr;
  if (keyword == "yesstr") return values.yesstr;
  if (keyword == "nostr") return values.nostr;
  // LC_TIME
  if (keyword == "day") return join_list(values.time.day);
  if (keyword == "abday") return join_list(values.time.abday);
  if (keyword == "mon") return join_list(values.time.mon);
  if (keyword == "abmon") return join_list(values.time.abmon);
  if (keyword == "am_pm") return values.time.am_pm;
  if (keyword == "d_fmt") return values.time.d_fmt;
  if (keyword == "t_fmt") return values.time.t_fmt;
  if (keyword == "d_t_fmt") return values.time.d_t_fmt;
  if (keyword == "t_fmt_ampm") return values.time.t_fmt_ampm;
  if (keyword == "era") return values.time.era;
  return {};
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
void print_keyword(std::string_view keyword, ValueKind kind,
                   bool keyword_mode) {
  const std::string value = keyword_value(keyword);
  if (!keyword_mode) {
    safePrintLn(value);
    return;
  }
  if (kind == ValueKind::kNumeric || kind == ValueKind::kEra) {
    safePrintLn(std::string(keyword) + "=" + value);
  } else {
    safePrintLn(std::string(keyword) + "=\"" + value + "\"");
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

// [GNU] The no-argument form reports the locale environment: LANG, LANGUAGE
// and LC_ALL verbatim, then each category as setlocale(category, NULL) would
// report it - LC_ALL > LC_<category> > LANG, else the platform default.
// Unlike glibc, WinuxCmd's platform default is the Windows user locale (the
// "Windows-native default" documented in tests/differential/audit-locale.md),
// not POSIX; the values every consumer uses come from the same resolution, so
// the report never disagrees with the rendered values.
void print_locale_categories() {
  safePrintLn("LANG=" + winux::locale_db::environment_variable("LANG"));
  safePrintLn("LANGUAGE=" +
              winux::locale_db::environment_variable("LANGUAGE"));

  static constexpr std::array<std::string_view, 12> categories = {
      "LC_CTYPE",   "LC_NUMERIC",      "LC_TIME",       "LC_COLLATE",
      "LC_MONETARY", "LC_MESSAGES",    "LC_PAPER",      "LC_NAME",
      "LC_ADDRESS", "LC_TELEPHONE",   "LC_MEASUREMENT", "LC_IDENTIFICATION"};
  for (const auto category : categories) {
    safePrintLn(std::string(category) + "=\"" +
                winux::locale_db::name_for_category(category) + "\"");
  }
  safePrintLn("LC_ALL=" + winux::locale_db::environment_variable("LC_ALL"));
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
  // the per-NAME loop). The per-category value dump needs the codeset's
  // character tables (see DOCS/2.0-locale-database-design.md §13), so the
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
      const KeywordEntry* entry = find_keyword(positional);
      if (entry == nullptr) {
        return report_unknown_name(std::string(positional));
      }
      print_keyword(entry->name, entry->kind, /*keyword_mode=*/true);
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
      const KeywordEntry* entry = find_keyword(positional);
      if (entry == nullptr) {
        return report_unknown_name(std::string(positional));
      }
      print_keyword(entry->name, entry->kind, /*keyword_mode=*/false);
    }
    return 0;
  }

  // Print current locale settings
  print_locale_categories();

  return 0;
}
