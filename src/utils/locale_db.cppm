// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Windows-NLS-backed locale value database (issue #1549).
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
///
/// The layer POSIX locale keywords are answered from. Windows carries no
/// glibc locale archive, so the values are acquired from the Win32 NLS API
/// (GetLocaleInfoEx / GetCPInfo / GetStringTypeW) and projected onto the
/// shapes POSIX consumers know: lconv numeric/monetary fields, strftime
/// time patterns, the codeset name and the CTYPE classes.
///
/// Design: DOCS/2.0-locale-database-design.md.

module;

// Pre-include a C++ header so c++config.h's std declarations are first seen
// with C++ linkage (same reason as utils:i18n and utils:win32, #1412).
#include <climits>
#include <cstddef>
#include <cstdint>
// os_defines.h (via cstddef) defines NOMINMAX as 1; drop it so the bare
// NOMINMAX definition used below applies exactly.
#undef NOMINMAX

#include <windows.h>

export module utils:locale_db;

import std;
import :utf8;

namespace winux::locale_db {

// ======================================================
// Value model
// ======================================================

// The codeset of a locale: the POSIX `charmap` face. `charmap` is the name
// glibc knows (ANSI_X3.4-1968, UTF-8, CP1252, GBK, ...), `codepage` the Win32
// number it came from, and the GetCPInfo facts let consumers decide whether a
// byte is a lead byte without a second API call.
export struct Codeset {
  std::string charmap = "ANSI_X3.4-1968";
  unsigned codepage = 20127;
  unsigned max_char_size = 1;
  bool utf8 = false;
  bool dbcs = false;
};

// struct lconv's numeric half (LC_NUMERIC).
export struct Numeric {
  std::string decimal_point;
  std::string thousands_sep;
  // POSIX lconv encoding: one byte per group size, CHAR_MAX as the final
  // element meaning "repeat the previous size". Empty means no grouping.
  std::string grouping;
  // The sizes as written by Win32 ("3;0" -> "3"), which is also how glibc's
  // locale(1) renders the item in -k mode.
  std::string grouping_text;
};

// struct lconv's monetary half (LC_MONETARY), in glibc item order.
export struct Monetary {
  std::string int_curr_symbol;
  std::string currency_symbol;
  std::string mon_decimal_point;
  std::string mon_thousands_sep;
  std::string mon_grouping;
  std::string mon_grouping_text;
  std::string positive_sign;
  std::string negative_sign;
  int frac_digits = -1;
  int int_frac_digits = -1;
  int p_cs_precedes = -1;
  int p_sep_by_space = -1;
  int n_cs_precedes = -1;
  int n_sep_by_space = -1;
  int p_sign_posn = -1;
  int n_sign_posn = -1;
  // The international format (POSIX `int_p_*`/`int_n_*`) has no Win32 ABI:
  // Windows carries one monetary format per locale (LOCALE_ICURRENCY /
  // LOCALE_INEGCURR describe the local one), so these six items stay at the
  // C-locale "unset" value (-1). Documented in the design doc §8.
  int int_p_cs_precedes = -1;
  int int_p_sep_by_space = -1;
  int int_n_cs_precedes = -1;
  int int_n_sep_by_space = -1;
  int int_p_sign_posn = -1;
  int int_n_sign_posn = -1;
};

// LC_TIME values, projected to strftime syntax. day[0]/abday[0] are Sunday
// (POSIX order), mon[0]/abmon[0] January.
export struct TimeFormat {
  std::array<std::string, 7> day;
  std::array<std::string, 7> abday;
  std::array<std::string, 12> mon;
  std::array<std::string, 12> abmon;
  std::string am_pm;
  std::string d_fmt;
  std::string t_fmt;
  std::string d_t_fmt;
  std::string t_fmt_ampm;
  std::string era;
  // glibc's `date_fmt` (the default `date` output). Win32 has no equivalent
  // item; non-C records synthesize `d_t_fmt + " %Z"` (design doc §9).
  std::string date_fmt;
  // `week` line data. Win32 LOCALE_IFIRSTDAYOFWEEK/LOCALE_IFIRSTWEEKOFYEAR
  // supply first_weekday and week_1stweek; ndays/1stday/cal_direction have
  // no Win32 source and keep the C values. first_weekday uses glibc's
  // encoding (1 = Sunday .. 7 = Saturday).
  int first_weekday = 1;
  int first_workday = 2;
  int week_1stweek = 4;
};

// One immutable record per locale name. `name` is the canonical POSIX
// spelling (underscore separator, codeset and modifier stripped except for
// the codeset of C.UTF-8), `windows_name` the BCP-47 name the data came from
// (empty for the synthetic C/POSIX record). `resolved` is false when the
// Win32 database did not recognize the name; the record then carries the C
// locale values for every field so consumers never mix two locales.
export struct LocaleValues {
  std::string name;
  std::string windows_name;
  bool is_c = false;
  bool resolved = false;
  Codeset codeset;
  Numeric numeric;
  Monetary monetary;
  TimeFormat time;
  std::string yesexpr;
  std::string noexpr;
  std::string yesstr;
  std::string nostr;
  // LC_MEASUREMENT in glibc's encoding: 1 = metric, 2 = US customary.
  // Win32 LOCALE_IMEASURE is 0 (metric) / 1 (US).
  int measurement = 1;
};

// ======================================================
// Resolution
// ======================================================

// Canonical POSIX spelling of a locale name: modifier stripped, codeset
// removed, '-' normalized to '_' ("zh-CN.UTF-8@euro" -> "zh_CN"). C/POSIX
// keep their spelling upper-cased ("C", "POSIX"); an empty name stays empty.
export std::string canonical_name(std::string_view name);

// The locale name selected by the environment for one category, in POSIX
// precedence order: LC_ALL, LC_<category>, LANG. Empty when none is set; this
// is the raw environment value, so callers that need a resolved default use
// name_for_category().
export std::string environment_locale(std::string_view category);

// One environment variable, empty when unset (the plain `locale` report
// prints LANG/LANGUAGE/LC_ALL verbatim).
export std::string environment_variable(std::string_view name);

// environment_locale(), else the Windows user default in POSIX spelling,
// else "C". Always non-empty.
export std::string name_for_category(std::string_view category);

// The Windows user default locale in POSIX spelling ("zh_CN"), empty when
// GetUserDefaultLocaleName fails.
export std::string host_locale_name();

// The immutable value record for a POSIX or Windows locale spelling. The
// reference stays valid for the life of the process (records are cached and
// never erased). An unrecognized name yields a C-valued record with
// resolved=false.
export const LocaleValues &lookup(std::string_view locale_name);

// lookup(name_for_category(category)).
export const LocaleValues &for_category(std::string_view category);

// The synthetic C/POSIX record (glibc C-locale values).
export const LocaleValues &c_locale();

// ======================================================
// glibc category item model (`locale -k` / `locale -c`)
// ======================================================

// Value shapes of glibc's categories.def. The rendering rules are glibc
// print_item()'s: strings quoted in -k mode, string arrays quoted once as a
// whole (`name="a;b"`), string lists quoted per element (`name="a";"b"`),
// bytes/words bare, byte/word arrays `;`-joined.
export enum class ItemKind : unsigned char {
  kString,
  kStringArray,
  kStringList,
  kByte,
  kByteArray,
  kWord,
  kWordArray,
};

// One item of one category, in glibc categories.def order.
export struct CategoryItem {
  std::string_view name;
  ItemKind kind;
};

// One rendered item value. `text` is the plain (unquoted) form glibc's
// `locale CATEGORY` prints; `elements` keeps the elements of a string list
// so -k mode can quote each one.
export struct ItemValue {
  ItemKind kind = ItemKind::kString;
  std::string text;
  std::vector<std::string> elements;
};

// The glibc item list of a category ("LC_TIME", ...), in `locale -k` order.
// Empty when the name is not a category.
export std::span<const CategoryItem> category_items(
    std::string_view category_name);

// Whether `name` is one of glibc's locale categories. LC_ALL is not one
// (glibc's show_info() skips it: `locale LC_ALL` is an unknown name).
export bool is_category(std::string_view name);

// The category owning an item name, "" when the name is unknown. Item names
// are unique across categories (glibc categories.def).
export std::string_view item_category(std::string_view item_name);

// The value of one item for a record, shaped for glibc's dump modes.
export ItemValue item_value(std::string_view item_name,
                            const LocaleValues &values);

// Locales this layer can answer: C, C.utf8, POSIX plus every Windows NLS
// locale (EnumSystemLocalesEx(LOCALE_ALL)) projected to POSIX spelling and
// codeset suffix, de-duplicated and sorted the way `locale -a` does under
// LC_ALL=C (glibc sorts with strcoll of the active collation).
export std::vector<std::string> available_locales();

// Character maps backed by the NLS code page catalogue
// (EnumSystemCodePagesW(CP_SUPPORTED)) in glibc charmap spelling, plus UTF-8
// and ANSI_X3.4-1968, de-duplicated and sorted.
export std::vector<std::string> available_charmaps();

// ======================================================
// LC_CTYPE classification
// ======================================================

// POSIX character classes as a bit set. ASCII values are locale-invariant, so
// the table is constexpr; code points above 0x7F come from GetStringTypeW.
export enum CtypeClass : unsigned short {
  ctype_upper = 0x0001,
  ctype_lower = 0x0002,
  ctype_digit = 0x0004,
  ctype_space = 0x0008,
  ctype_punct = 0x0010,
  ctype_cntrl = 0x0020,
  ctype_blank = 0x0040,
  ctype_xdigit = 0x0080,
  ctype_alpha = 0x0100,
  ctype_graph = 0x0200,  // alpha | digit | punct
  ctype_print = 0x0400,  // graph | blank
  ctype_alnum = 0x0800,  // alpha | digit
};

// Character classes of one code point. Surrogate halves and code points
// above the BMP report 0: GetStringTypeW classifies UTF-16 units, so an
// astral character has no single unit to ask about.
export unsigned short classify(char32_t code_point);

// Convenience predicates over classify().
export bool is_alpha(char32_t code_point);
export bool is_digit(char32_t code_point);
export bool is_space(char32_t code_point);
export bool is_upper(char32_t code_point);
export bool is_lower(char32_t code_point);
export bool is_punct(char32_t code_point);
export bool is_xdigit(char32_t code_point);
export bool is_graph(char32_t code_point);
export bool is_print(char32_t code_point);

namespace {

// C1_* from GetStringTypeW's CT_CTYPE1 for the ASCII range. ASCII classes do
// not change with the locale, so this table can be constant; the
// static_asserts below pin the entries the POSIX classes are built from.
consteval unsigned short ascii_class(char c) {
  const auto uc = static_cast<unsigned char>(c);
  unsigned short bits = 0;
  if (uc >= 'A' && uc <= 'Z') bits |= ctype_upper | ctype_alpha;
  if (uc >= 'a' && uc <= 'z') bits |= ctype_lower | ctype_alpha;
  if (uc >= '0' && uc <= '9') bits |= ctype_digit;
  if ((uc >= '0' && uc <= '9') || (uc >= 'A' && uc <= 'F') ||
      (uc >= 'a' && uc <= 'f')) {
    bits |= ctype_xdigit;
  }
  if (uc == ' ' || uc == '\t') bits |= ctype_space | ctype_blank;
  if (uc == '\n' || uc == '\v' || uc == '\f' || uc == '\r') {
    bits |= ctype_space | ctype_cntrl;
  }
  if (uc < 0x20 || uc == 0x7F) bits |= ctype_cntrl;
  if (uc >= 0x21 && uc <= 0x7E && !(bits & ctype_alpha) &&
      !(bits & ctype_digit)) {
    bits |= ctype_punct;
  }
  if (bits & (ctype_alpha | ctype_digit | ctype_punct)) bits |= ctype_graph;
  if ((bits & ctype_graph) || (bits & ctype_blank)) bits |= ctype_print;
  if (bits & (ctype_alpha | ctype_digit)) bits |= ctype_alnum;
  return bits;
}

consteval std::array<unsigned short, 128> make_ascii_table() {
  std::array<unsigned short, 128> table{};
  for (int i = 0; i < 128; ++i) {
    table[static_cast<size_t>(i)] = ascii_class(static_cast<char>(i));
  }
  return table;
}

inline constexpr std::array<unsigned short, 128> kAsciiTable =
    make_ascii_table();

static_assert((kAsciiTable['A'] & ctype_alpha) != 0);
static_assert((kAsciiTable['A'] & ctype_upper) != 0);
static_assert((kAsciiTable['A'] & ctype_lower) == 0);
static_assert((kAsciiTable['z'] & ctype_lower) != 0);
static_assert((kAsciiTable['0'] & ctype_digit) != 0);
static_assert((kAsciiTable['0'] & ctype_xdigit) != 0);
static_assert((kAsciiTable['F'] & ctype_xdigit) != 0);
static_assert((kAsciiTable['g'] & ctype_xdigit) == 0);
static_assert((kAsciiTable[' '] & ctype_blank) != 0);
static_assert((kAsciiTable['\t'] & ctype_blank) != 0);
static_assert((kAsciiTable['\n'] & ctype_cntrl) != 0);
static_assert((kAsciiTable['\n'] & ctype_graph) == 0);
static_assert((kAsciiTable['!'] & ctype_punct) != 0);
static_assert((kAsciiTable['!'] & ctype_alnum) == 0);
static_assert((kAsciiTable[0x7F] & ctype_cntrl) != 0);
static_assert((kAsciiTable['$'] & ctype_print) != 0);

// ---- environment ---------------------------------------------------------

std::string environment_value(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string{};
}

// ---- Win32 data acquisition ---------------------------------------------

using NlsString = std::optional<std::string>;

// One LOCALE_* string item. nullopt means the API rejected the query (locale
// unknown or item unsupported); an empty string is a legitimate value and is
// preserved, which is what keeps de-DE's empty AM/PM from becoming C's.
NlsString query(const wchar_t *locale, LCTYPE type) {
  const int needed = ::GetLocaleInfoEx(locale, type, nullptr, 0);
  if (needed <= 0) return std::nullopt;
  std::wstring buffer(static_cast<size_t>(needed), L'\0');
  const int written = ::GetLocaleInfoEx(locale, type, buffer.data(), needed);
  if (written <= 0) return std::nullopt;
  // GetLocaleInfoEx reports the count including the terminating NUL.
  buffer.resize(static_cast<size_t>(written - 1));
  return wstring_to_utf8(buffer);
}

std::optional<int> query_int(const wchar_t *locale, LCTYPE type) {
  const auto text = query(locale, type);
  if (!text || text->empty()) return std::nullopt;
  int value = 0;
  const auto *begin = text->data();
  const auto *end = text->data() + text->size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

// ---- POSIX grouping ------------------------------------------------------

// Win32 "3;0" -> POSIX lconv bytes { 3, CHAR_MAX } and the rendering text
// glibc's locale(1) uses for the item ("3"). A trailing 0 means "repeat the
// previous size"; a 0 alone means no grouping.
void parse_grouping(const std::string &windows_grouping, std::string &bytes,
                    std::string &text) {
  std::vector<unsigned> sizes;
  bool repeat = false;
  size_t start = 0;
  while (start <= windows_grouping.size()) {
    const size_t end = windows_grouping.find(';', start);
    const std::string_view piece(
        windows_grouping.data() + start,
        (end == std::string::npos ? windows_grouping.size() : end) - start);
    start = (end == std::string::npos) ? windows_grouping.size() + 1 : end + 1;
    if (piece.empty()) continue;
    unsigned size = 0;
    const auto [ptr, error] =
        std::from_chars(piece.data(), piece.data() + piece.size(), size);
    if (error != std::errc{} || ptr != piece.data() + piece.size()) {
      // Malformed grouping: keep the record C-shaped rather than guessing.
      sizes.clear();
      repeat = false;
      break;
    }
    if (size == 0) {
      repeat = true;
      break;
    }
    sizes.push_back(size);
  }
  bytes.clear();
  text.clear();
  if (sizes.empty()) return;
  for (const unsigned size : sizes) {
    bytes.push_back(static_cast<char>(size > 127 ? 127 : size));
    if (!text.empty()) text.push_back(';');
    text += std::to_string(size);
  }
  if (repeat) bytes.push_back(static_cast<char>(CHAR_MAX));
}

// ---- Windows date/time pictures -----------------------------------------

// Translate a Windows date/time picture (LOCALE_SSHORTDATE,
// LOCALE_STIMEFORMAT) into a strftime pattern. Windows single-letter fields
// are unpadded; POSIX has no portable unpadded day/month token, so both d and
// dd map to %d (the same choice glibc's zh_CN d_fmt makes). Everything the
// POSIX projection cannot express (era names) is dropped.
std::string picture_to_strftime(std::string_view picture) {
  std::string result;
  result.reserve(picture.size() * 2);
  size_t i = 0;
  auto append_literal = [&result](std::string_view text) {
    for (const char ch : text) {
      if (ch == '%')
        result += "%%";
      else
        result.push_back(ch);
    }
  };
  bool quoted = false;
  std::string literal;
  auto flush_literal = [&]() {
    append_literal(literal);
    literal.clear();
  };
  while (i < picture.size()) {
    const char ch = picture[i];
    if (ch == '\'') {
      quoted = !quoted;
      if (!quoted) flush_literal();
      ++i;
      continue;
    }
    if (quoted) {
      if (ch == '\\' && i + 1 < picture.size()) {
        literal.push_back(picture[i + 1]);
        i += 2;
      } else {
        literal.push_back(ch);
        ++i;
      }
      continue;
    }
    size_t run = 1;
    while (i + run < picture.size() && picture[i + run] == ch) ++run;
    auto take = [&](std::string_view token) {
      flush_literal();
      result += token;
      i += run;
    };
    switch (ch) {
      case 'd':
        take(run >= 4 ? "%A" : run == 3 ? "%a" : "%d");
        break;
      case 'M':
        take(run >= 4 ? "%B" : run == 3 ? "%b" : "%m");
        break;
      case 'y':
        take(run >= 3 ? "%Y" : "%y");
        break;
      case 'H':
        take("%H");
        break;
      case 'h':
        take("%I");
        break;
      case 'm':
        take("%M");
        break;
      case 's':
        take("%S");
        break;
      case 't':
        take("%p");
        break;
      case 'g':
      case 'G':
        // Era name: no POSIX strftime token.
        flush_literal();
        i += run;
        break;
      case '/':
      case '-':
      case '.':
      case ',':
      case ' ':
      case ':':
        literal.push_back(ch);
        ++i;
        break;
      case '\\':
        if (i + 1 < picture.size()) literal.push_back(picture[i + 1]);
        i += 2;
        break;
      default:
        literal.push_back(ch);
        ++i;
        break;
    }
  }
  flush_literal();
  return result;
}

// Whether a Windows time picture uses the 12-hour clock with a meridiem
// marker (h/mm/ss with "t"/"tt"). The 24-hour patterns (H, HH) do not.
bool picture_uses_meridiem(std::string_view picture) {
  return picture.find("tt") != std::string_view::npos ||
         (picture.find('h') != std::string_view::npos &&
          picture.find('H') == std::string_view::npos);
}

// ---- codeset -------------------------------------------------------------

struct CharmapEntry {
  unsigned codepage;
  std::string_view charmap;
};

// Windows code page -> the charmap spelling glibc's locale(1) prints. Only
// names that exist in glibc's charmaps directory are used; anything else is
// reported as CP<codepage>, which is also a glibc charmap naming convention.
constexpr CharmapEntry kCharmaps[] = {
    {20127, "ANSI_X3.4-1968"}, {65001, "UTF-8"},
    {1200, "UTF-16LE"},        {1201, "UTF-16BE"},
    {437, "IBM437"},           {850, "IBM850"},
    {852, "IBM852"},           {855, "IBM855"},
    {857, "IBM857"},           {860, "IBM860"},
    {861, "IBM861"},           {862, "IBM862"},
    {863, "IBM863"},           {864, "IBM864"},
    {865, "IBM865"},           {866, "IBM866"},
    {869, "IBM869"},           {874, "CP874"},
    {932, "SHIFT_JIS"},        {936, "GBK"},
    {949, "EUC-KR"},           {950, "BIG5"},
    {1250, "CP1250"},          {1251, "CP1251"},
    {1252, "CP1252"},          {1253, "CP1253"},
    {1254, "CP1254"},          {1255, "CP1255"},
    {1256, "CP1256"},          {1257, "CP1257"},
    {1258, "CP1258"},          {10000, "MACINTOSH"},
    {10007, "MAC-CYRILLIC"},   {10029, "MAC-CENTRALEUROPE"},
    {20866, "KOI8-R"},         {21866, "KOI8-U"},
    {28591, "ISO-8859-1"},     {28592, "ISO-8859-2"},
    {28593, "ISO-8859-3"},     {28594, "ISO-8859-4"},
    {28595, "ISO-8859-5"},     {28596, "ISO-8859-6"},
    {28597, "ISO-8859-7"},     {28598, "ISO-8859-8"},
    {28599, "ISO-8859-9"},     {28605, "ISO-8859-15"},
    {50220, "ISO-2022-JP"},    {51932, "EUC-JP"},
    {51936, "EUC-CN"},         {51949, "EUC-KR"},
    {54936, "GB18030"},
};

std::string charmap_for_codepage(unsigned codepage) {
  for (const auto &entry : kCharmaps) {
    if (entry.codepage == codepage) return std::string(entry.charmap);
  }
  return "CP" + std::to_string(codepage);
}

bool same_charmap(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i) {
    const auto left =
        static_cast<char>(std::toupper(static_cast<unsigned char>(lhs[i])));
    const auto right =
        static_cast<char>(std::toupper(static_cast<unsigned char>(rhs[i])));
    if (left != right) return false;
  }
  return true;
}

Codeset codeset_from_codepage(unsigned codepage) {
  Codeset codeset;
  if (codepage == 0) {
    // No ANSI code page reported: the only defensible claim is the C locale's
    // ASCII codeset, not a guess at UTF-8.
    return codeset;
  }
  codeset.codepage = codepage;
  codeset.charmap = charmap_for_codepage(codepage);
  codeset.utf8 = codepage == 65001 || codepage == 1200 || codepage == 1201;
  CPINFO info{};
  if (::GetCPInfo(codepage, &info) != FALSE) {
    codeset.max_char_size = info.MaxCharSize;
    codeset.dbcs = info.MaxCharSize > 1;
  } else {
    // Unknown code page: a single-byte, non-UTF-8 codeset is the only claim
    // the database can back.
    codeset.max_char_size = 1;
    codeset.dbcs = false;
  }
  return codeset;
}

// An explicit POSIX codeset ("zh_CN.GBK", "C.ISO-8859-1") is authoritative:
// it names the codeset the caller asked for, whatever the ANSI code page of
// the locale is. Known glibc charmap names are mapped back to the code page
// so GetCPInfo can supply the byte-shape facts.
Codeset codeset_from_name(std::string_view codeset) {
  for (const auto &entry : kCharmaps) {
    if (same_charmap(entry.charmap, codeset)) {
      return codeset_from_codepage(entry.codepage);
    }
  }
  Codeset values;
  values.charmap = std::string(codeset);
  values.codepage = 0;
  values.max_char_size = 1;
  return values;
}

// glibc's `_nl_normalize_codeset` shape used by the locale archive names:
// lowercase and drop everything that is not alphanumeric ("UTF-8" -> "utf8",
// "ISO-8859-1" -> "iso88591").
std::string normalize_codeset(std::string_view codeset) {
  std::string normalized;
  normalized.reserve(codeset.size());
  for (const char ch : codeset) {
    const auto uc = static_cast<unsigned char>(ch);
    if (std::isalnum(uc) == 0) continue;
    normalized.push_back(static_cast<char>(std::tolower(uc)));
  }
  return normalized;
}

// ---- name resolution -----------------------------------------------------

struct ParsedName {
  std::string base;     // "zh_CN" (no codeset/modifier)
  std::string codeset;  // "UTF-8" or empty
  bool is_c = false;
  bool empty = false;
};

ParsedName parse_name(std::string_view name) {
  ParsedName parsed;
  std::string work(name);
  if (const auto at = work.find('@'); at != std::string::npos) {
    work.resize(at);
  }
  if (const auto dot = work.find('.'); dot != std::string::npos) {
    parsed.codeset = work.substr(dot + 1);
    work.resize(dot);
  }
  for (char &ch : work) {
    if (ch == '-') ch = '_';
  }
  parsed.base = work;
  if (parsed.base.empty()) {
    parsed.empty = true;
    return parsed;
  }
  std::string folded = parsed.base;
  for (char &ch : folded) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  parsed.is_c = folded == "c" || folded == "posix";
  if (parsed.is_c) parsed.base = folded == "posix" ? "POSIX" : "C";
  return parsed;
}

std::wstring windows_name(const ParsedName &parsed) {
  std::wstring name = utf8_to_wstring(parsed.base);
  for (wchar_t &ch : name) {
    if (ch == L'_') ch = L'-';
  }
  return name;
}

bool codeset_is_utf8(std::string_view codeset) {
  std::string folded(codeset);
  for (char &ch : folded) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return folded == "UTF-8" || folded == "UTF8";
}

// ---- records -------------------------------------------------------------

LocaleValues make_c_locale() {
  LocaleValues values;
  values.name = "C";
  values.is_c = true;
  values.resolved = true;
  values.codeset = Codeset{};
  values.numeric.decimal_point = ".";
  values.numeric.grouping_text = "-1";
  // glibc's C locale has no monetary data at all: every string is empty and
  // every numeric field is -1.
  values.monetary.mon_grouping_text = "-1";
  values.time.day = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                     "Thursday", "Friday", "Saturday"};
  values.time.abday = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  values.time.mon = {"January",   "February", "March",    "April",
                     "May",       "June",     "July",     "August",
                     "September", "October",  "November", "December"};
  values.time.abmon = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  values.time.am_pm = "AM;PM";
  values.time.d_fmt = "%m/%d/%y";
  values.time.t_fmt = "%H:%M:%S";
  values.time.d_t_fmt = "%a %b %e %H:%M:%S %Y";
  values.time.t_fmt_ampm = "%I:%M:%S %p";
  values.time.date_fmt = "%a %b %e %H:%M:%S %Z %Y";
  values.yesexpr = "^[yY]";
  values.noexpr = "^[nN]";
  return values;
}

LocaleValues build_record(const ParsedName &parsed) {
  if (parsed.is_c) {
    // C.UTF-8 is the C record with a UTF-8 codeset: glibc's C.UTF-8 shares
    // every LC_* value with C and differs only in the codeset.
    LocaleValues values = make_c_locale();
    values.name = parsed.codeset.empty() ? parsed.base
                                         : parsed.base + "." + parsed.codeset;
    if (codeset_is_utf8(parsed.codeset)) {
      values.codeset.charmap = "UTF-8";
      values.codeset.codepage = 65001;
      values.codeset.max_char_size = 4;
      values.codeset.utf8 = true;
    }
    return values;
  }

  LocaleValues values;
  values.name =
      parsed.base + (parsed.codeset.empty() ? "" : "." + parsed.codeset);
  values.is_c = false;

  const std::wstring locale = windows_name(parsed);
  const wchar_t *const locale_name = locale.c_str();

  // Field-by-field with the C record as the failure fallback: a locale that
  // resolves but misses one item keeps that item's C value instead of an
  // empty string, which is what "no locale archive on Windows" means.
  const LocaleValues fallback = make_c_locale();

  auto get = [&](LCTYPE type, std::string &target,
                 const std::string &fallback_value) {
    if (auto value = query(locale_name, type)) {
      target = std::move(*value);
    } else {
      target = fallback_value;
    }
  };

  const auto language = query(locale_name, LOCALE_SISO639LANGNAME);
  if (!language) {
    // The Win32 NLS database does not know this name: return a C-valued
    // record that still reports the requested spelling. Codeset detection
    // below still runs, so a name like xx_XX.UTF-8 keeps its codeset.
    LocaleValues unresolved = fallback;
    unresolved.name = values.name;
    unresolved.resolved = false;
    if (codeset_is_utf8(parsed.codeset)) {
      unresolved.codeset.charmap = "UTF-8";
      unresolved.codeset.codepage = 65001;
      unresolved.codeset.max_char_size = 4;
      unresolved.codeset.utf8 = true;
    } else if (!parsed.codeset.empty()) {
      unresolved.codeset = codeset_from_name(parsed.codeset);
    }
    return unresolved;
  }
  values.resolved = true;
  values.windows_name = wstring_to_utf8(locale);

  get(LOCALE_SDECIMAL, values.numeric.decimal_point,
      fallback.numeric.decimal_point);
  get(LOCALE_STHOUSAND, values.numeric.thousands_sep,
      fallback.numeric.thousands_sep);

  if (const auto grouping = query(locale_name, LOCALE_SGROUPING)) {
    parse_grouping(*grouping, values.numeric.grouping,
                   values.numeric.grouping_text);
    if (values.numeric.grouping.empty()) values.numeric.grouping_text = "-1";
  } else {
    values.numeric = fallback.numeric;
  }

  get(LOCALE_SINTLSYMBOL, values.monetary.int_curr_symbol,
      fallback.monetary.int_curr_symbol);
  if (!values.monetary.int_curr_symbol.empty()) {
    // ISO 4217 symbols carry a trailing space in POSIX locale data; Win32
    // stores the bare symbol.
    values.monetary.int_curr_symbol.push_back(' ');
  }
  get(LOCALE_SCURRENCY, values.monetary.currency_symbol,
      fallback.monetary.currency_symbol);
  get(LOCALE_SMONDECIMALSEP, values.monetary.mon_decimal_point,
      fallback.monetary.mon_decimal_point);
  get(LOCALE_SMONTHOUSANDSEP, values.monetary.mon_thousands_sep,
      fallback.monetary.mon_thousands_sep);
  if (const auto grouping = query(locale_name, LOCALE_SMONGROUPING)) {
    parse_grouping(*grouping, values.monetary.mon_grouping,
                   values.monetary.mon_grouping_text);
    if (values.monetary.mon_grouping.empty()) {
      values.monetary.mon_grouping_text = "-1";
    }
  } else {
    values.monetary.mon_grouping = fallback.monetary.mon_grouping;
    values.monetary.mon_grouping_text = fallback.monetary.mon_grouping_text;
  }
  get(LOCALE_SPOSITIVESIGN, values.monetary.positive_sign,
      fallback.monetary.positive_sign);
  get(LOCALE_SNEGATIVESIGN, values.monetary.negative_sign,
      fallback.monetary.negative_sign);

  const auto frac_digits = query_int(locale_name, LOCALE_ICURRDIGITS);
  values.monetary.frac_digits = frac_digits.value_or(-1);
  const auto int_frac_digits = query_int(locale_name, LOCALE_IINTLCURRDIGITS);
  values.monetary.int_frac_digits = int_frac_digits.value_or(-1);

  // Positive currency format: 0 "$1", 1 "1$", 2 "$ 1", 3 "1 $".
  const auto positive = query_int(locale_name, LOCALE_ICURRENCY);
  if (positive && *positive >= 0 && *positive <= 3) {
    values.monetary.p_cs_precedes = (*positive == 0 || *positive == 2) ? 1 : 0;
    values.monetary.p_sep_by_space = (*positive == 2 || *positive == 3) ? 1 : 0;
  }

  // Negative currency format: Windows' 16-entry table projected onto POSIX
  // n_cs_precedes / n_sep_by_space / n_sign_posn.
  const auto negative = query_int(locale_name, LOCALE_INEGCURR);
  if (negative && *negative >= 0 && *negative <= 15) {
    static constexpr int kCsPrecedes[16] = {1, 1, 1, 1, 0, 0, 0, 0,
                                            0, 1, 1, 0, 1, 0, 1, 0};
    static constexpr int kSepBySpace[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                            1, 1, 1, 1, 1, 1, 1, 1};
    static constexpr int kSignPosn[16] = {0, 1, 3, 4, 0, 1, 4, 2,
                                          1, 1, 2, 2, 3, 4, 0, 0};
    values.monetary.n_cs_precedes = kCsPrecedes[*negative];
    values.monetary.n_sep_by_space = kSepBySpace[*negative];
    values.monetary.n_sign_posn = kSignPosn[*negative];
  }

  // glibc only sets the positive sign position when the locale defines a
  // currency; the C locale reports -1 for every monetary flag.
  const bool has_currency = !values.monetary.currency_symbol.empty() ||
                            !values.monetary.int_curr_symbol.empty();
  if (has_currency) values.monetary.p_sign_posn = 1;

  // LC_TIME. Win32 day names are Monday-first; POSIX day[0] is Sunday.
  static constexpr std::array<LCTYPE, 7> kDayTypes = {
      LOCALE_SDAYNAME7, LOCALE_SDAYNAME1, LOCALE_SDAYNAME2, LOCALE_SDAYNAME3,
      LOCALE_SDAYNAME4, LOCALE_SDAYNAME5, LOCALE_SDAYNAME6};
  static constexpr std::array<LCTYPE, 7> kAbDayTypes = {
      LOCALE_SABBREVDAYNAME7, LOCALE_SABBREVDAYNAME1, LOCALE_SABBREVDAYNAME2,
      LOCALE_SABBREVDAYNAME3, LOCALE_SABBREVDAYNAME4, LOCALE_SABBREVDAYNAME5,
      LOCALE_SABBREVDAYNAME6};
  for (size_t i = 0; i < 7; ++i) {
    values.time.day[i] =
        query(locale_name, kDayTypes[i]).value_or(fallback.time.day[i]);
    values.time.abday[i] =
        query(locale_name, kAbDayTypes[i]).value_or(fallback.time.abday[i]);
  }
  for (size_t i = 0; i < 12; ++i) {
    const auto type = static_cast<LCTYPE>(LOCALE_SMONTHNAME1 + i);
    const auto abbrev = static_cast<LCTYPE>(LOCALE_SABBREVMONTHNAME1 + i);
    values.time.mon[i] =
        query(locale_name, type).value_or(fallback.time.mon[i]);
    values.time.abmon[i] =
        query(locale_name, abbrev).value_or(fallback.time.abmon[i]);
  }

  const auto am = query(locale_name, LOCALE_S1159).value_or("AM");
  const auto pm = query(locale_name, LOCALE_S2359).value_or("PM");
  values.time.am_pm = am + ";" + pm;

  const auto picture = query(locale_name, LOCALE_SSHORTDATE);
  values.time.d_fmt =
      picture ? picture_to_strftime(*picture) : fallback.time.d_fmt;
  if (values.time.d_fmt.empty()) values.time.d_fmt = fallback.time.d_fmt;

  const auto time_picture = query(locale_name, LOCALE_STIMEFORMAT);
  values.time.t_fmt =
      time_picture ? picture_to_strftime(*time_picture) : fallback.time.t_fmt;
  if (values.time.t_fmt.empty()) values.time.t_fmt = fallback.time.t_fmt;

  // Windows has no d_t_fmt item. The C record keeps glibc's exact composite;
  // other locales get the short date and time joined by one space (a
  // documented simulation choice, see the design doc §9).
  values.time.d_t_fmt = values.time.d_fmt + " " + values.time.t_fmt;

  const bool meridiem = time_picture && picture_uses_meridiem(*time_picture);
  values.time.t_fmt_ampm = (meridiem && (!am.empty() || !pm.empty()))
                               ? std::string("%I:%M:%S %p")
                               : std::string{};

  values.time.era.clear();

  // date_fmt (glibc's default `date` pattern): no Win32 item exists. The C
  // record above keeps glibc's exact value; a resolved record appends the
  // POSIX timezone token to its synthesized d_t_fmt (design doc §9).
  values.time.date_fmt = values.time.d_t_fmt + " %Z";

  // Week data. LOCALE_IFIRSTDAYOFWEEK is 0=Monday..6=Sunday; glibc numbers
  // 1=Sunday..7=Saturday. LOCALE_IFIRSTWEEKOFYEAR is 2 for the "first full
  // week" rule, which glibc spells week-1stweek=4; everything else projects
  // to 1. Both are documented projection choices (§9).
  if (const auto first_day = query_int(locale_name, LOCALE_IFIRSTDAYOFWEEK);
      first_day && *first_day >= 0 && *first_day <= 6) {
    values.time.first_weekday = (*first_day + 1) % 7 + 1;
  }
  if (const auto first_week = query_int(locale_name, LOCALE_IFIRSTWEEKOFYEAR);
      first_week) {
    values.time.week_1stweek = *first_week == 2 ? 4 : 1;
  }

  // LC_MEASUREMENT: Win32 0=metric / 1=US, glibc 1=metric / 2=US.
  if (const auto measure = query_int(locale_name, LOCALE_IMEASURE);
      measure && *measure >= 0 && *measure <= 1) {
    values.measurement = *measure == 1 ? 2 : 1;
  }

  // LC_MESSAGES. Win32 has no yes/no strings (no LOCALE_SYES/LOCALE_SNO in
  // winnls.h), so the C answers stand; yesexpr/noexpr are derived from them.
  values.yesexpr = fallback.yesexpr;
  values.noexpr = fallback.noexpr;
  values.yesstr = fallback.yesstr;
  values.nostr = fallback.nostr;

  // Codeset: an explicit POSIX codeset wins, else the NLS ANSI code page.
  if (codeset_is_utf8(parsed.codeset)) {
    values.codeset.charmap = "UTF-8";
    values.codeset.codepage = 65001;
    values.codeset.max_char_size = 4;
    values.codeset.utf8 = true;
    values.codeset.dbcs = false;
  } else if (!parsed.codeset.empty()) {
    values.codeset = codeset_from_name(parsed.codeset);
  } else {
    const auto codepage = query_int(locale_name, LOCALE_IDEFAULTANSICODEPAGE);
    values.codeset = codeset_from_codepage(codepage.value_or(0));
  }

  return values;
}

std::mutex g_cache_mutex;
std::unordered_map<std::string, LocaleValues> g_cache;

}  // namespace

std::string canonical_name(std::string_view name) {
  const ParsedName parsed = parse_name(name);
  if (parsed.empty) return {};
  if (parsed.is_c) {
    return parsed.codeset.empty() ? parsed.base
                                  : parsed.base + "." + parsed.codeset;
  }
  return parsed.base + (parsed.codeset.empty() ? "" : "." + parsed.codeset);
}

std::string environment_locale(std::string_view category) {
  if (auto value = environment_value("LC_ALL"); !value.empty()) return value;
  // `category` is the POSIX category name, which is also its environment
  // variable name (LC_CTYPE, LC_TIME, ...).
  if (auto value = environment_value(std::string(category).c_str());
      !value.empty()) {
    return value;
  }
  return environment_value("LANG");
}

std::string environment_variable(std::string_view name) {
  return environment_value(std::string(name).c_str());
}

std::string host_locale_name() {
  wchar_t buffer[LOCALE_NAME_MAX_LENGTH] = {};
  if (::GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH) == 0) {
    return {};
  }
  std::string name = wstring_to_utf8(buffer);
  for (char &ch : name) {
    if (ch == '-') ch = '_';
  }
  return name;
}

std::string name_for_category(std::string_view category) {
  if (auto name = environment_locale(category); !name.empty()) return name;
  if (auto host = host_locale_name(); !host.empty()) return host;
  return "C";
}

const LocaleValues &lookup(std::string_view locale_name) {
  const std::string key = canonical_name(locale_name);
  if (key.empty()) return c_locale();
  {
    const std::lock_guard lock(g_cache_mutex);
    if (const auto it = g_cache.find(key); it != g_cache.end()) {
      return it->second;
    }
  }
  const ParsedName parsed = parse_name(key);
  if (parsed.is_c && !codeset_is_utf8(parsed.codeset)) return c_locale();
  const LocaleValues built = build_record(parsed);
  const std::lock_guard lock(g_cache_mutex);
  // References into an unordered_map are stable across rehash, so returning
  // the stored value is safe for the life of the process.
  return g_cache.emplace(key, built).first->second;
}

const LocaleValues &for_category(std::string_view category) {
  return lookup(name_for_category(category));
}

const LocaleValues &c_locale() {
  static const LocaleValues c = [] {
    LocaleValues values = make_c_locale();
    return values;
  }();
  return c;
}

unsigned short classify(char32_t code_point) {
  if (code_point < 0x80) {
    return kAsciiTable[static_cast<size_t>(code_point)];
  }
  if (code_point > 0xFFFF) return 0;  // UTF-16 units only (documented)
  if (code_point >= 0xD800 && code_point <= 0xDFFF) return 0;
  const wchar_t unit = static_cast<wchar_t>(code_point);
  WORD types = 0;
  if (::GetStringTypeW(CT_CTYPE1, &unit, 1, &types) == FALSE) return 0;
  unsigned short bits = 0;
  if (types & C1_UPPER) bits |= ctype_upper;
  if (types & C1_LOWER) bits |= ctype_lower;
  if (types & C1_DIGIT) bits |= ctype_digit;
  if (types & C1_SPACE) bits |= ctype_space;
  if (types & C1_PUNCT) bits |= ctype_punct;
  if (types & C1_CNTRL) bits |= ctype_cntrl;
  if (types & C1_BLANK) bits |= ctype_blank;
  if (types & C1_XDIGIT) bits |= ctype_xdigit;
  if (types & C1_ALPHA) bits |= ctype_alpha;
  if (bits & (ctype_alpha | ctype_digit | ctype_punct)) bits |= ctype_graph;
  if ((bits & ctype_graph) || (bits & ctype_blank)) bits |= ctype_print;
  if (bits & (ctype_alpha | ctype_digit)) bits |= ctype_alnum;
  return bits;
}

bool is_alpha(char32_t code_point) {
  return (classify(code_point) & ctype_alpha) != 0;
}
bool is_digit(char32_t code_point) {
  return (classify(code_point) & ctype_digit) != 0;
}
bool is_space(char32_t code_point) {
  return (classify(code_point) & ctype_space) != 0;
}
bool is_upper(char32_t code_point) {
  return (classify(code_point) & ctype_upper) != 0;
}
bool is_lower(char32_t code_point) {
  return (classify(code_point) & ctype_lower) != 0;
}
bool is_punct(char32_t code_point) {
  return (classify(code_point) & ctype_punct) != 0;
}
bool is_xdigit(char32_t code_point) {
  return (classify(code_point) & ctype_xdigit) != 0;
}
bool is_graph(char32_t code_point) {
  return (classify(code_point) & ctype_graph) != 0;
}
bool is_print(char32_t code_point) {
  return (classify(code_point) & ctype_print) != 0;
}

// ======================================================
// Category item model
// ======================================================

namespace {

// Item tables in glibc categories.def order, limited to the item shapes
// print_item() actually renders (the wide-character duplicates are skipped by
// glibc too, which is why `locale -k LC_TIME` has 27 lines and not 40).
constexpr CategoryItem kCollateItems[] = {
    {"collate-nrules", ItemKind::kWord},
    {"collate-rulesets", ItemKind::kString},
    {"collate-symb-hash-sizemb", ItemKind::kWord},
    {"collate-codeset", ItemKind::kString},
};

constexpr CategoryItem kCtypeItems[] = {
    {"ctype-class-names", ItemKind::kStringList},
    {"ctype-map-names", ItemKind::kStringList},
    {"ctype-width", ItemKind::kByteArray},
    {"ctype-mb-cur-max", ItemKind::kWord},
    {"charmap", ItemKind::kString},
    {"ctype-class-offset", ItemKind::kWord},
    {"ctype-map-offset", ItemKind::kWord},
    {"ctype-indigits_mb-len", ItemKind::kWord},
    {"ctype-indigits0_mb", ItemKind::kString},
    {"ctype-indigits1_mb", ItemKind::kString},
    {"ctype-indigits2_mb", ItemKind::kString},
    {"ctype-indigits3_mb", ItemKind::kString},
    {"ctype-indigits4_mb", ItemKind::kString},
    {"ctype-indigits5_mb", ItemKind::kString},
    {"ctype-indigits6_mb", ItemKind::kString},
    {"ctype-indigits7_mb", ItemKind::kString},
    {"ctype-indigits8_mb", ItemKind::kString},
    {"ctype-indigits9_mb", ItemKind::kString},
    {"ctype-indigits_wc-len", ItemKind::kWord},
    {"ctype-outdigit0_mb", ItemKind::kString},
    {"ctype-outdigit1_mb", ItemKind::kString},
    {"ctype-outdigit2_mb", ItemKind::kString},
    {"ctype-outdigit3_mb", ItemKind::kString},
    {"ctype-outdigit4_mb", ItemKind::kString},
    {"ctype-outdigit5_mb", ItemKind::kString},
    {"ctype-outdigit6_mb", ItemKind::kString},
    {"ctype-outdigit7_mb", ItemKind::kString},
    {"ctype-outdigit8_mb", ItemKind::kString},
    {"ctype-outdigit9_mb", ItemKind::kString},
    {"ctype-outdigit0_wc", ItemKind::kWord},
    {"ctype-outdigit1_wc", ItemKind::kWord},
    {"ctype-outdigit2_wc", ItemKind::kWord},
    {"ctype-outdigit3_wc", ItemKind::kWord},
    {"ctype-outdigit4_wc", ItemKind::kWord},
    {"ctype-outdigit5_wc", ItemKind::kWord},
    {"ctype-outdigit6_wc", ItemKind::kWord},
    {"ctype-outdigit7_wc", ItemKind::kWord},
    {"ctype-outdigit8_wc", ItemKind::kWord},
    {"ctype-outdigit9_wc", ItemKind::kWord},
    {"ctype-translit-tab-size", ItemKind::kWord},
    {"ctype-translit-default-missing-len", ItemKind::kWord},
    {"ctype-translit-ignore-len", ItemKind::kWord},
    {"ctype-translit-ignore", ItemKind::kString},
    {"map-to-nonascii", ItemKind::kWord},
    {"nonascii-case", ItemKind::kWord},
};

constexpr CategoryItem kNumericItems[] = {
    {"decimal_point", ItemKind::kString},
    {"thousands_sep", ItemKind::kString},
    {"grouping", ItemKind::kByteArray},
    {"numeric-decimal-point-wc", ItemKind::kWord},
    {"numeric-thousands-sep-wc", ItemKind::kWord},
    {"numeric-codeset", ItemKind::kString},
};

constexpr CategoryItem kTimeItems[] = {
    {"abday", ItemKind::kStringArray},
    {"day", ItemKind::kStringArray},
    {"abmon", ItemKind::kStringArray},
    {"mon", ItemKind::kStringArray},
    {"am_pm", ItemKind::kStringArray},
    {"d_t_fmt", ItemKind::kString},
    {"d_fmt", ItemKind::kString},
    {"t_fmt", ItemKind::kString},
    {"t_fmt_ampm", ItemKind::kString},
    {"era", ItemKind::kStringList},
    {"era_year", ItemKind::kString},
    {"era_d_fmt", ItemKind::kString},
    {"alt_digits", ItemKind::kStringList},
    {"era_d_t_fmt", ItemKind::kString},
    {"era_t_fmt", ItemKind::kString},
    {"time-era-num-entries", ItemKind::kWord},
    {"time-era-entries", ItemKind::kString},
    {"week-ndays", ItemKind::kWord},
    {"week-1stday", ItemKind::kWord},
    {"week-1stweek", ItemKind::kWord},
    {"first_weekday", ItemKind::kWord},
    {"first_workday", ItemKind::kWord},
    {"cal_direction", ItemKind::kWord},
    {"timezone", ItemKind::kString},
    {"date_fmt", ItemKind::kString},
    {"time-codeset", ItemKind::kString},
    {"alt_mon", ItemKind::kStringArray},
    {"ab_alt_mon", ItemKind::kStringArray},
};

constexpr CategoryItem kMonetaryItems[] = {
    {"int_curr_symbol", ItemKind::kString},
    {"currency_symbol", ItemKind::kString},
    {"mon_decimal_point", ItemKind::kString},
    {"mon_thousands_sep", ItemKind::kString},
    {"mon_grouping", ItemKind::kByteArray},
    {"positive_sign", ItemKind::kString},
    {"negative_sign", ItemKind::kString},
    {"int_frac_digits", ItemKind::kByte},
    {"frac_digits", ItemKind::kByte},
    {"p_cs_precedes", ItemKind::kByte},
    {"p_sep_by_space", ItemKind::kByte},
    {"n_cs_precedes", ItemKind::kByte},
    {"n_sep_by_space", ItemKind::kByte},
    {"p_sign_posn", ItemKind::kByte},
    {"n_sign_posn", ItemKind::kByte},
    {"crncystr", ItemKind::kString},
    {"int_p_cs_precedes", ItemKind::kByte},
    {"int_p_sep_by_space", ItemKind::kByte},
    {"int_n_cs_precedes", ItemKind::kByte},
    {"int_n_sep_by_space", ItemKind::kByte},
    {"int_p_sign_posn", ItemKind::kByte},
    {"int_n_sign_posn", ItemKind::kByte},
    {"duo_int_curr_symbol", ItemKind::kString},
    {"duo_currency_symbol", ItemKind::kString},
    {"duo_int_frac_digits", ItemKind::kByte},
    {"duo_frac_digits", ItemKind::kByte},
    {"duo_p_cs_precedes", ItemKind::kByte},
    {"duo_p_sep_by_space", ItemKind::kByte},
    {"duo_n_cs_precedes", ItemKind::kByte},
    {"duo_n_sep_by_space", ItemKind::kByte},
    {"duo_int_p_cs_precedes", ItemKind::kByte},
    {"duo_int_p_sep_by_space", ItemKind::kByte},
    {"duo_int_n_cs_precedes", ItemKind::kByte},
    {"duo_int_n_sep_by_space", ItemKind::kByte},
    {"duo_p_sign_posn", ItemKind::kByte},
    {"duo_n_sign_posn", ItemKind::kByte},
    {"duo_int_p_sign_posn", ItemKind::kByte},
    {"duo_int_n_sign_posn", ItemKind::kByte},
    {"uno_valid_from", ItemKind::kWord},
    {"uno_valid_to", ItemKind::kWord},
    {"duo_valid_from", ItemKind::kWord},
    {"duo_valid_to", ItemKind::kWord},
    {"conversion_rate", ItemKind::kWordArray},
    {"monetary-decimal-point-wc", ItemKind::kWord},
    {"monetary-thousands-sep-wc", ItemKind::kWord},
    {"monetary-codeset", ItemKind::kString},
};

constexpr CategoryItem kMessagesItems[] = {
    {"yesexpr", ItemKind::kString},          {"noexpr", ItemKind::kString},
    {"yesstr", ItemKind::kString},           {"nostr", ItemKind::kString},
    {"messages-codeset", ItemKind::kString},
};

constexpr CategoryItem kPaperItems[] = {
    {"height", ItemKind::kWord},
    {"width", ItemKind::kWord},
    {"paper-codeset", ItemKind::kString},
};

constexpr CategoryItem kNameItems[] = {
    {"name_fmt", ItemKind::kString},     {"name_gen", ItemKind::kString},
    {"name_mr", ItemKind::kString},      {"name_mrs", ItemKind::kString},
    {"name_miss", ItemKind::kString},    {"name_ms", ItemKind::kString},
    {"name-codeset", ItemKind::kString},
};

constexpr CategoryItem kAddressItems[] = {
    {"postal_fmt", ItemKind::kString},      {"country_name", ItemKind::kString},
    {"country_post", ItemKind::kString},    {"country_ab2", ItemKind::kString},
    {"country_ab3", ItemKind::kString},     {"country_car", ItemKind::kString},
    {"country_num", ItemKind::kWord},       {"country_isbn", ItemKind::kString},
    {"lang_name", ItemKind::kString},       {"lang_ab", ItemKind::kString},
    {"lang_term", ItemKind::kString},       {"lang_lib", ItemKind::kString},
    {"address-codeset", ItemKind::kString},
};

constexpr CategoryItem kTelephoneItems[] = {
    {"tel_int_fmt", ItemKind::kString},
    {"tel_dom_fmt", ItemKind::kString},
    {"int_select", ItemKind::kString},
    {"int_prefix", ItemKind::kString},
    {"telephone-codeset", ItemKind::kString},
};

constexpr CategoryItem kMeasurementItems[] = {
    {"measurement", ItemKind::kByte},
    {"measurement-codeset", ItemKind::kString},
};

constexpr CategoryItem kIdentificationItems[] = {
    {"title", ItemKind::kString},
    {"source", ItemKind::kString},
    {"address", ItemKind::kString},
    {"contact", ItemKind::kString},
    {"email", ItemKind::kString},
    {"tel", ItemKind::kString},
    {"fax", ItemKind::kString},
    {"language", ItemKind::kString},
    {"territory", ItemKind::kString},
    {"audience", ItemKind::kString},
    {"application", ItemKind::kString},
    {"abbreviation", ItemKind::kString},
    {"revision", ItemKind::kString},
    {"date", ItemKind::kString},
    {"category", ItemKind::kStringArray},
    {"identification-codeset", ItemKind::kString},
};

struct CategoryDesc {
  std::string_view name;
  std::span<const CategoryItem> items;
};

constexpr CategoryDesc kCategories[] = {
    {"LC_CTYPE", kCtypeItems},
    {"LC_NUMERIC", kNumericItems},
    {"LC_TIME", kTimeItems},
    {"LC_COLLATE", kCollateItems},
    {"LC_MONETARY", kMonetaryItems},
    {"LC_MESSAGES", kMessagesItems},
    {"LC_PAPER", kPaperItems},
    {"LC_NAME", kNameItems},
    {"LC_ADDRESS", kAddressItems},
    {"LC_TELEPHONE", kTelephoneItems},
    {"LC_MEASUREMENT", kMeasurementItems},
    {"LC_IDENTIFICATION", kIdentificationItems},
};

// ---- ItemValue construction ---------------------------------------------

ItemValue make_string(std::string text) {
  ItemValue value;
  value.kind = ItemKind::kString;
  value.text = std::move(text);
  return value;
}

ItemValue make_array(std::string text) {
  ItemValue value;
  value.kind = ItemKind::kStringArray;
  value.text = std::move(text);
  return value;
}

ItemValue make_list(std::vector<std::string> elements) {
  ItemValue value;
  value.kind = ItemKind::kStringList;
  for (const auto &element : elements) {
    if (!value.text.empty()) value.text.push_back(';');
    value.text += element;
  }
  value.elements = std::move(elements);
  return value;
}

ItemValue make_number(long long number, ItemKind kind) {
  ItemValue value;
  value.kind = kind;
  value.text = std::to_string(number);
  return value;
}

ItemValue make_numbers(std::string text, ItemKind kind) {
  ItemValue value;
  value.kind = kind;
  value.text = std::move(text);
  return value;
}

std::string join_values(const std::vector<std::string> &values) {
  std::string result;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i != 0) result.push_back(';');
    result += values[i];
  }
  return result;
}

template <size_t N>
std::string join_array(const std::array<std::string, N> &values) {
  return join_values(std::vector<std::string>(values.begin(), values.end()));
}

// First Unicode code point of a UTF-8 string, 0 when empty. Used for glibc's
// `*-wc` width items (the wide-character value of a separator).
unsigned first_code_point(std::string_view text) {
  if (text.empty()) return 0;
  const auto lead = static_cast<unsigned char>(text[0]);
  if (lead < 0x80) return lead;
  int extra = 0;
  unsigned code_point = 0;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    code_point = lead & 0x1Fu;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    code_point = lead & 0x0Fu;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    code_point = lead & 0x07u;
  } else {
    return 0;
  }
  if (static_cast<size_t>(extra) + 1 > text.size()) return 0;
  for (int i = 1; i <= extra; ++i) {
    const auto unit = static_cast<unsigned char>(text[static_cast<size_t>(i)]);
    if ((unit & 0xC0) != 0x80) return 0;
    code_point = (code_point << 6) | (unit & 0x3Fu);
  }
  return code_point;
}

// Whether the record is the compiled C locale's ASCII ctype table (as opposed
// to a UTF-8/multibyte table): glibc's layout constants and class-name lists
// differ on exactly that split.
bool ascii_ctype_table(const LocaleValues &values) {
  return values.is_c && !values.codeset.utf8;
}

// The glibc-internal LC_CTYPE table layout numbers. They describe how
// localedef compiled the locale's ctype data (class/map offsets, translit
// table size, bytes per class word), not locale data Windows exposes. The
// layer reports the constants of glibc's C/cs stock tables so the C/POSIX
// dump is byte-identical; the design doc §10 records the limitation.
int ctype_layout_width(const LocaleValues &values) {
  return ascii_ctype_table(values) ? 7 : 16;
}

int ctype_layout_map_offset(const LocaleValues &values) {
  return ascii_ctype_table(values) ? 84 : 86;
}

int ctype_layout_translit_size(const LocaleValues &values) {
  return ascii_ctype_table(values) ? 1659 : 6398;
}

bool is_digit_item(std::string_view item, std::string_view prefix,
                   std::string_view suffix, int &digit) {
  if (!item.starts_with(prefix) || !item.ends_with(suffix)) return false;
  const size_t position = prefix.size();
  if (position >= item.size() - suffix.size()) return false;
  const char ch = item[position];
  if (ch < '0' || ch > '9') return false;
  digit = ch - '0';
  return true;
}

// ---- NLS enumeration -----------------------------------------------------

BOOL CALLBACK collect_locale(LPWSTR name, DWORD /*flags*/, LPARAM param) {
  auto *names = reinterpret_cast<std::vector<std::wstring> *>(param);
  names->emplace_back(name != nullptr ? name : L"");
  return TRUE;
}

BOOL CALLBACK collect_codepage(LPWSTR name);

// EnumSystemCodePagesW has no user parameter, unlike EnumSystemLocalesEx.
thread_local std::vector<unsigned> *g_codepage_sink = nullptr;

BOOL CALLBACK collect_codepage(LPWSTR name) {
  if (name == nullptr || g_codepage_sink == nullptr) return TRUE;
  const std::string text = wstring_to_utf8(name);
  unsigned codepage = 0;
  const auto [ptr, error] =
      std::from_chars(text.data(), text.data() + text.size(), codepage);
  if (error == std::errc{} && ptr == text.data() + text.size()) {
    g_codepage_sink->push_back(codepage);
  }
  return TRUE;
}

}  // namespace

std::span<const CategoryItem> category_items(std::string_view category_name) {
  for (const auto &category : kCategories) {
    if (category.name == category_name) return category.items;
  }
  return {};
}

bool is_category(std::string_view name) {
  for (const auto &category : kCategories) {
    if (category.name == name) return true;
  }
  return false;
}

std::string_view item_category(std::string_view item_name) {
  for (const auto &category : kCategories) {
    for (const auto &item : category.items) {
      if (item.name == item_name) return category.name;
    }
  }
  return {};
}

ItemValue item_value(std::string_view item, const LocaleValues &values) {
  const std::string &charmap = values.codeset.charmap;

  // ---- LC_COLLATE --------------------------------------------------------
  if (item == "collate-nrules") return make_number(0, ItemKind::kWord);
  if (item == "collate-rulesets") return make_string("");
  if (item == "collate-symb-hash-sizemb") {
    return make_number(0, ItemKind::kWord);
  }
  if (item == "collate-codeset") return make_string(charmap);

  // ---- LC_CTYPE ----------------------------------------------------------
  if (item == "ctype-class-names") {
    std::vector<std::string> classes = {"upper",  "lower", "alpha", "digit",
                                        "xdigit", "space", "print", "graph",
                                        "blank",  "cntrl", "punct", "alnum"};
    if (!ascii_ctype_table(values)) {
      classes.push_back("combining");
      classes.push_back("combining_level3");
    }
    return make_list(std::move(classes));
  }
  if (item == "ctype-map-names") {
    std::vector<std::string> maps = {"toupper", "tolower"};
    if (!ascii_ctype_table(values)) maps.push_back("totitle");
    return make_list(std::move(maps));
  }
  if (item == "ctype-width") {
    return make_numbers(std::to_string(ctype_layout_width(values)),
                        ItemKind::kByteArray);
  }
  if (item == "ctype-mb-cur-max") {
    return make_number(values.codeset.max_char_size, ItemKind::kWord);
  }
  if (item == "charmap") return make_string(charmap);
  if (item == "ctype-class-offset") return make_number(72, ItemKind::kWord);
  if (item == "ctype-map-offset") {
    return make_number(ctype_layout_map_offset(values), ItemKind::kWord);
  }
  if (item == "ctype-indigits_mb-len") return make_number(1, ItemKind::kWord);
  if (item == "ctype-indigits_wc-len") return make_number(1, ItemKind::kWord);
  if (item == "ctype-translit-tab-size") {
    return make_number(ctype_layout_translit_size(values), ItemKind::kWord);
  }
  if (item == "ctype-translit-default-missing-len") {
    return make_number(1, ItemKind::kWord);
  }
  if (item == "ctype-translit-ignore-len")
    return make_number(0, ItemKind::kWord);
  if (item == "ctype-translit-ignore") return make_string("");
  if (item == "map-to-nonascii") return make_number(0, ItemKind::kWord);
  if (item == "nonascii-case") return make_number(0, ItemKind::kWord);
  {
    int digit = 0;
    if (is_digit_item(item, "ctype-indigits", "_mb", digit)) {
      return make_string(std::to_string(digit));
    }
    if (is_digit_item(item, "ctype-outdigit", "_mb", digit)) {
      return make_string(std::to_string(digit));
    }
    if (is_digit_item(item, "ctype-outdigit", "_wc", digit)) {
      return make_number(48 + digit, ItemKind::kWord);
    }
  }

  // ---- LC_NUMERIC --------------------------------------------------------
  if (item == "decimal_point") return make_string(values.numeric.decimal_point);
  if (item == "thousands_sep") {
    return make_string(values.numeric.thousands_sep);
  }
  if (item == "grouping") {
    return make_numbers(values.numeric.grouping_text, ItemKind::kByteArray);
  }
  if (item == "numeric-decimal-point-wc") {
    return make_number(first_code_point(values.numeric.decimal_point),
                       ItemKind::kWord);
  }
  if (item == "numeric-thousands-sep-wc") {
    return make_number(first_code_point(values.numeric.thousands_sep),
                       ItemKind::kWord);
  }
  if (item == "numeric-codeset") return make_string(charmap);

  // ---- LC_MONETARY -------------------------------------------------------
  const Monetary &money = values.monetary;
  if (item == "int_curr_symbol") return make_string(money.int_curr_symbol);
  if (item == "currency_symbol") return make_string(money.currency_symbol);
  if (item == "mon_decimal_point") {
    return make_string(money.mon_decimal_point);
  }
  if (item == "mon_thousands_sep") {
    return make_string(money.mon_thousands_sep);
  }
  if (item == "mon_grouping") {
    return make_numbers(money.mon_grouping_text, ItemKind::kByteArray);
  }
  if (item == "positive_sign") return make_string(money.positive_sign);
  if (item == "negative_sign") return make_string(money.negative_sign);
  if (item == "int_frac_digits") {
    return make_number(money.int_frac_digits, ItemKind::kByte);
  }
  if (item == "frac_digits") {
    return make_number(money.frac_digits, ItemKind::kByte);
  }
  if (item == "p_cs_precedes") {
    return make_number(money.p_cs_precedes, ItemKind::kByte);
  }
  if (item == "p_sep_by_space") {
    return make_number(money.p_sep_by_space, ItemKind::kByte);
  }
  if (item == "n_cs_precedes") {
    return make_number(money.n_cs_precedes, ItemKind::kByte);
  }
  if (item == "n_sep_by_space") {
    return make_number(money.n_sep_by_space, ItemKind::kByte);
  }
  if (item == "p_sign_posn") {
    return make_number(money.p_sign_posn, ItemKind::kByte);
  }
  if (item == "n_sign_posn") {
    return make_number(money.n_sign_posn, ItemKind::kByte);
  }
  if (item == "crncystr") {
    // nl_langinfo(CRNCYSTR): sign-position marker ('-' symbol precedes,
    // '+' symbol follows) followed by the local currency symbol.
    return make_string(std::string(money.p_cs_precedes == 0 ? "+" : "-") +
                       money.currency_symbol);
  }
  if (item == "int_p_cs_precedes") {
    return make_number(money.int_p_cs_precedes, ItemKind::kByte);
  }
  if (item == "int_p_sep_by_space") {
    return make_number(money.int_p_sep_by_space, ItemKind::kByte);
  }
  if (item == "int_n_cs_precedes") {
    return make_number(money.int_n_cs_precedes, ItemKind::kByte);
  }
  if (item == "int_n_sep_by_space") {
    return make_number(money.int_n_sep_by_space, ItemKind::kByte);
  }
  if (item == "int_p_sign_posn") {
    return make_number(money.int_p_sign_posn, ItemKind::kByte);
  }
  if (item == "int_n_sign_posn") {
    return make_number(money.int_n_sign_posn, ItemKind::kByte);
  }
  // The financial (duo_*) set mirrors the primary values: glibc's locale
  // files define one dual-currency record per locale and every stock locale
  // copies the primary into it.
  if (item == "duo_int_curr_symbol") return make_string(money.int_curr_symbol);
  if (item == "duo_currency_symbol") return make_string(money.currency_symbol);
  if (item == "duo_int_frac_digits") {
    return make_number(money.int_frac_digits, ItemKind::kByte);
  }
  if (item == "duo_frac_digits") {
    return make_number(money.frac_digits, ItemKind::kByte);
  }
  if (item == "duo_p_cs_precedes") {
    return make_number(money.p_cs_precedes, ItemKind::kByte);
  }
  if (item == "duo_p_sep_by_space") {
    return make_number(money.p_sep_by_space, ItemKind::kByte);
  }
  if (item == "duo_n_cs_precedes") {
    return make_number(money.n_cs_precedes, ItemKind::kByte);
  }
  if (item == "duo_n_sep_by_space") {
    return make_number(money.n_sep_by_space, ItemKind::kByte);
  }
  if (item == "duo_int_p_cs_precedes") {
    return make_number(money.int_p_cs_precedes, ItemKind::kByte);
  }
  if (item == "duo_int_p_sep_by_space") {
    return make_number(money.int_p_sep_by_space, ItemKind::kByte);
  }
  if (item == "duo_int_n_cs_precedes") {
    return make_number(money.int_n_cs_precedes, ItemKind::kByte);
  }
  if (item == "duo_int_n_sep_by_space") {
    return make_number(money.int_n_sep_by_space, ItemKind::kByte);
  }
  if (item == "duo_p_sign_posn") {
    return make_number(money.p_sign_posn, ItemKind::kByte);
  }
  if (item == "duo_n_sign_posn") {
    return make_number(money.n_sign_posn, ItemKind::kByte);
  }
  if (item == "duo_int_p_sign_posn") {
    return make_number(money.int_p_sign_posn, ItemKind::kByte);
  }
  if (item == "duo_int_n_sign_posn") {
    return make_number(money.int_n_sign_posn, ItemKind::kByte);
  }
  // glibc's "no dual-currency validity window" defaults; no Win32 source.
  if (item == "uno_valid_from" || item == "duo_valid_from") {
    return make_number(10101, ItemKind::kWord);
  }
  if (item == "uno_valid_to" || item == "duo_valid_to") {
    return make_number(99991231, ItemKind::kWord);
  }
  if (item == "conversion_rate") {
    return make_numbers("1;1", ItemKind::kWordArray);
  }
  if (item == "monetary-decimal-point-wc") {
    return make_number(first_code_point(money.mon_decimal_point),
                       ItemKind::kWord);
  }
  if (item == "monetary-thousands-sep-wc") {
    return make_number(first_code_point(money.mon_thousands_sep),
                       ItemKind::kWord);
  }
  if (item == "monetary-codeset") return make_string(charmap);

  // ---- LC_TIME -----------------------------------------------------------
  const TimeFormat &time = values.time;
  if (item == "abday") return make_array(join_array(time.abday));
  if (item == "day") return make_array(join_array(time.day));
  if (item == "abmon") return make_array(join_array(time.abmon));
  if (item == "mon") return make_array(join_array(time.mon));
  if (item == "am_pm") return make_array(time.am_pm);
  if (item == "d_t_fmt") return make_string(time.d_t_fmt);
  if (item == "d_fmt") return make_string(time.d_fmt);
  if (item == "t_fmt") return make_string(time.t_fmt);
  if (item == "t_fmt_ampm") return make_string(time.t_fmt_ampm);
  if (item == "era") return make_list({});
  if (item == "era_year" || item == "era_d_fmt" || item == "era_d_t_fmt" ||
      item == "era_t_fmt") {
    return make_string("");
  }
  if (item == "alt_digits") return make_list({});
  if (item == "time-era-num-entries") return make_number(0, ItemKind::kWord);
  if (item == "time-era-entries") return make_string("");
  if (item == "week-ndays") return make_number(7, ItemKind::kWord);
  if (item == "week-1stday") return make_number(19971130, ItemKind::kWord);
  if (item == "week-1stweek") {
    return make_number(time.week_1stweek, ItemKind::kWord);
  }
  if (item == "first_weekday") {
    return make_number(time.first_weekday, ItemKind::kWord);
  }
  if (item == "first_workday") {
    return make_number(time.first_workday, ItemKind::kWord);
  }
  if (item == "cal_direction") return make_number(1, ItemKind::kWord);
  if (item == "timezone") return make_string("");
  if (item == "date_fmt") return make_string(time.date_fmt);
  if (item == "time-codeset") return make_string(charmap);
  if (item == "alt_mon") return make_array(join_array(time.mon));
  if (item == "ab_alt_mon") return make_array(join_array(time.abmon));

  // ---- LC_MESSAGES -------------------------------------------------------
  if (item == "yesexpr") return make_string(values.yesexpr);
  if (item == "noexpr") return make_string(values.noexpr);
  if (item == "yesstr") return make_string(values.yesstr);
  if (item == "nostr") return make_string(values.nostr);
  if (item == "messages-codeset") return make_string(charmap);

  // ---- LC_PAPER ----------------------------------------------------------
  if (item == "height") return make_number(297, ItemKind::kWord);
  if (item == "width") return make_number(210, ItemKind::kWord);
  if (item == "paper-codeset") return make_string(charmap);

  // ---- LC_NAME -----------------------------------------------------------
  if (item == "name_fmt") return make_string("%p%t%g%t%m%t%f");
  if (item == "name-codeset") return make_string(charmap);
  if (item == "name_gen" || item == "name_mr" || item == "name_mrs" ||
      item == "name_miss" || item == "name_ms") {
    return make_string("");
  }

  // ---- LC_ADDRESS --------------------------------------------------------
  if (item == "postal_fmt") {
    return make_string("%a%N%f%N%d%N%b%N%s %h %e %r%N%C-%z %T%N%c%N");
  }
  if (item == "country_num") return make_number(0, ItemKind::kWord);
  if (item == "address-codeset") return make_string(charmap);
  if (item == "country_name" || item == "country_post" ||
      item == "country_ab2" || item == "country_ab3" || item == "country_car" ||
      item == "country_isbn" || item == "lang_name" || item == "lang_ab" ||
      item == "lang_term" || item == "lang_lib") {
    return make_string("");
  }

  // ---- LC_TELEPHONE ------------------------------------------------------
  if (item == "tel_int_fmt") return make_string("+%c %a %l");
  if (item == "telephone-codeset") return make_string(charmap);
  if (item == "tel_dom_fmt" || item == "int_select" || item == "int_prefix") {
    return make_string("");
  }

  // ---- LC_MEASUREMENT ----------------------------------------------------
  if (item == "measurement") {
    return make_number(values.measurement, ItemKind::kByte);
  }
  if (item == "measurement-codeset") return make_string(charmap);

  // ---- LC_IDENTIFICATION -------------------------------------------------
  if (item == "title") return make_string("ISO/IEC 14652 i18n FDCC-set");
  if (item == "source") {
    return make_string("ISO/IEC JTC1/SC22/WG20 - internationalization");
  }
  if (item == "address") {
    return make_string(
        "C/o Keld Simonsen, Skt. Jorgens Alle 8, DK-1615 Kobenhavn V");
  }
  if (item == "contact") return make_string("Keld Simonsen");
  if (item == "email") return make_string("keld@dkuug.dk");
  if (item == "tel") return make_string("+45 3122-6543");
  if (item == "fax") return make_string("+45 3325-6543");
  if (item == "territory") return make_string("ISO");
  if (item == "revision") return make_string("1.0");
  if (item == "date") return make_string("1997-12-20");
  if (item == "category") {
    std::vector<std::string> categories{"i18n:1999", charmap};
    categories.resize(13);
    return make_array(join_values(categories));
  }
  if (item == "identification-codeset") return make_string(charmap);
  if (item == "language" || item == "audience" || item == "application" ||
      item == "abbreviation") {
    return make_string("");
  }

  return make_string("");
}

std::vector<std::string> available_locales() {
  std::vector<std::wstring> windows_names;
  ::EnumSystemLocalesEx(collect_locale, LOCALE_ALL,
                        reinterpret_cast<LPARAM>(&windows_names), nullptr);

  std::set<std::string> names{"C", "C.utf8", "POSIX"};
  for (const auto &windows_name : windows_names) {
    if (windows_name.empty()) continue;
    // `_` introduces a Windows sort-order suffix (de-DE_phoneb) that has no
    // POSIX spelling; listing it would produce a name lookup() cannot answer.
    if (windows_name.find(L'_') != std::wstring::npos) continue;
    std::string name = wstring_to_utf8(windows_name);
    for (char &ch : name) {
      if (ch == '-') ch = '_';
    }
    const auto codepage =
        query_int(windows_name.c_str(), LOCALE_IDEFAULTANSICODEPAGE);
    const Codeset codeset =
        codeset_from_codepage(codepage ? static_cast<unsigned>(*codepage) : 0);
    names.insert(name + "." + normalize_codeset(codeset.charmap));
  }
  return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> available_charmaps() {
  std::vector<unsigned> codepages;
  g_codepage_sink = &codepages;
  ::EnumSystemCodePagesW(collect_codepage, CP_SUPPORTED);
  g_codepage_sink = nullptr;

  std::set<std::string> names;
  for (const unsigned codepage : codepages) {
    names.insert(charmap_for_codepage(codepage));
  }
  // The two codesets the value model always answers, whether or not the host
  // installs a code page for them.
  names.insert("UTF-8");
  names.insert("ANSI_X3.4-1968");
  return std::vector<std::string>(names.begin(), names.end());
}

}  // namespace winux::locale_db
