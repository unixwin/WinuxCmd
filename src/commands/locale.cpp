// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for locale command.
/// @Version: 0.3.0
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

auto constexpr LOCALE_OPTIONS = std::array{
    // [GNU] -a, --all-locales (glibc name; --all kept as an
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
    OPTION("", "", "print locale information", STRING_TYPE),
    // [GNU] -V: version shorthand (probed: `locale -V`)
    OPTION("-V", "--version", "output version information and exit")};

// ======================================================
// Helper functions
// ======================================================

namespace {

using winux::locale_db::CategoryItem;
using winux::locale_db::ItemKind;
using winux::locale_db::LocaleValues;

// [GNU] A NAME operand is either a category (dump every item of it) or one
// glibc item name (answer just that item). Both are answered from the item
// model in utils:locale_db, which owns the glibc categories.def order and the
// value projection, so `locale(1)` only renders.
const LocaleValues& values_for(std::string_view category) {
  return winux::locale_db::for_category(category);
}

// [GNU] glibc print_item(): strings and string arrays are quoted as a whole
// (`charmap="UTF-8"`, `day="Sun;Mon"`), string lists quote every element
// (`ctype-class-names="upper";"lower"`), bytes/words/arrays stay bare.
void print_item(const CategoryItem& item, const LocaleValues& values,
                bool keyword_mode) {
  const auto value = winux::locale_db::item_value(item.name, values);
  if (!keyword_mode) {
    safePrintLn(value.text);
    return;
  }
  switch (item.kind) {
    case ItemKind::kString:
    case ItemKind::kStringArray:
      safePrintLn(std::string(item.name) + "=\"" + value.text + "\"");
      return;
    case ItemKind::kStringList: {
      std::string line(item.name);
      line += '=';
      for (size_t i = 0; i < value.elements.size(); ++i) {
        if (i != 0) line += ';';
        line += '"';
        line += value.elements[i];
        line += '"';
      }
      safePrintLn(line);
      return;
    }
    case ItemKind::kByte:
    case ItemKind::kByteArray:
    case ItemKind::kWord:
    case ItemKind::kWordArray:
      safePrintLn(std::string(item.name) + "=" + value.text);
      return;
  }
}

// [GNU] One category operand: -c writes the category name first, then every
// item in categories.def order. `locale -k LC_TIME` is the same stream with
// names and quoting.
void print_category(std::string_view category, const LocaleValues& values,
                    bool show_category_name, bool keyword_mode) {
  if (show_category_name) safePrintLn(std::string(category));
  for (const auto& item : winux::locale_db::category_items(category)) {
    print_item(item, values, keyword_mode);
  }
}

// [GNU] The NAME operand answer. glibc show_info() looks categories up first,
// then item names; anything else is `locale: unknown name "X"` and exit 1
// (glibc locale_special.c).
auto print_name(std::string_view name, bool show_category_name,
                bool keyword_mode) -> int {
  if (winux::locale_db::is_category(name)) {
    print_category(name, values_for(name), show_category_name, keyword_mode);
    return 0;
  }
  const std::string_view category = winux::locale_db::item_category(name);
  if (!category.empty()) {
    for (const auto& item : winux::locale_db::category_items(category)) {
      if (item.name != name) continue;
      if (show_category_name) safePrintLn(std::string(category));
      print_item(item, values_for(category), keyword_mode);
      return 0;
    }
  }
  safeErrorPrintLn("locale: unknown name \"" + std::string(name) + "\"");
  return 1;
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
  safePrintLn("LANGUAGE=" + winux::locale_db::environment_variable("LANGUAGE"));

  static constexpr std::array<std::string_view, 12> categories = {
      "LC_CTYPE",    "LC_NUMERIC",   "LC_TIME",        "LC_COLLATE",
      "LC_MONETARY", "LC_MESSAGES",  "LC_PAPER",       "LC_NAME",
      "LC_ADDRESS",  "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION"};
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

  // [GNU] -a wins over -m when both are given (glibc main(): the do_all
  // branch exits before the do_charmaps branch).
  if (show_all) {
    for (const auto& locale : winux::locale_db::available_locales()) {
      safePrintLn(locale);
    }
    return 0;
  }

  if (show_charmaps) {
    for (const auto& charmap : winux::locale_db::available_charmaps()) {
      safePrintLn(charmap);
    }
    return 0;
  }

  // [GNU] NAME operands are answered in argv order. -c and -k only modify
  // rendering; neither is a mode switch (glibc can print `-c -k` together).
  if (!ctx.positionals.empty()) {
    for (const auto& positional : ctx.positionals) {
      if (print_name(positional, show_category_names, show_keyword_names) !=
          0) {
        return 1;
      }
    }
    return 0;
  }

  // [GNU] bare -c / bare -k write nothing and exit 0 (glibc main(): only the
  // no-argument-at-all branch dumps the environment).
  if (show_category_names || show_keyword_names) return 0;

  print_locale_categories();
  return 0;
}
