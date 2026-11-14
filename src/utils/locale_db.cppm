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
#include <cstddef>
#include <cstdint>
#include <climits>
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
    const auto [ptr, error] = std::from_chars(
        piece.data(), piece.data() + piece.size(), size);
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
      if (ch == '%') result += "%%";
      else result.push_back(ch);
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
    {20127, "ANSI_X3.4-1968"}, {65001, "UTF-8"},   {1200, "UTF-16LE"},
    {1201, "UTF-16BE"},        {437, "IBM437"},     {850, "IBM850"},
    {852, "IBM852"},           {855, "IBM855"},     {857, "IBM857"},
    {860, "IBM860"},           {861, "IBM861"},     {862, "IBM862"},
    {863, "IBM863"},           {864, "IBM864"},     {865, "IBM865"},
    {866, "IBM866"},           {869, "IBM869"},     {874, "CP874"},
    {932, "SHIFT_JIS"},        {936, "GBK"},        {949, "EUC-KR"},
    {950, "BIG5"},             {1250, "CP1250"},    {1251, "CP1251"},
    {1252, "CP1252"},          {1253, "CP1253"},    {1254, "CP1254"},
    {1255, "CP1255"},          {1256, "CP1256"},    {1257, "CP1257"},
    {1258, "CP1258"},          {10000, "MACINTOSH"}, {10007, "MAC-CYRILLIC"},
    {10029, "MAC-CENTRALEUROPE"}, {20866, "KOI8-R"}, {21866, "KOI8-U"},
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
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
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
  values.time.day = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                     "Friday", "Saturday"};
  values.time.abday = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  values.time.mon = {"January", "February", "March", "April", "May", "June",
                     "July", "August", "September", "October", "November",
                     "December"};
  values.time.abmon = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  values.time.am_pm = "AM;PM";
  values.time.d_fmt = "%m/%d/%y";
  values.time.t_fmt = "%H:%M:%S";
  values.time.d_t_fmt = "%a %b %e %H:%M:%S %Y";
  values.time.t_fmt_ampm = "%I:%M:%S %p";
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
    values.monetary.p_sep_by_space =
        (*positive == 2 || *positive == 3) ? 1 : 0;
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
      LOCALE_SDAYNAME7,   LOCALE_SDAYNAME1, LOCALE_SDAYNAME2,
      LOCALE_SDAYNAME3,   LOCALE_SDAYNAME4, LOCALE_SDAYNAME5,
      LOCALE_SDAYNAME6};
  static constexpr std::array<LCTYPE, 7> kAbDayTypes = {
      LOCALE_SABBREVDAYNAME7, LOCALE_SABBREVDAYNAME1,
      LOCALE_SABBREVDAYNAME2, LOCALE_SABBREVDAYNAME3,
      LOCALE_SABBREVDAYNAME4, LOCALE_SABBREVDAYNAME5,
      LOCALE_SABBREVDAYNAME6};
  for (size_t i = 0; i < 7; ++i) {
    values.time.day[i] = query(locale_name, kDayTypes[i])
                             .value_or(fallback.time.day[i]);
    values.time.abday[i] = query(locale_name, kAbDayTypes[i])
                               .value_or(fallback.time.abday[i]);
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
  values.time.d_fmt = picture ? picture_to_strftime(*picture)
                              : fallback.time.d_fmt;
  if (values.time.d_fmt.empty()) values.time.d_fmt = fallback.time.d_fmt;

  const auto time_picture = query(locale_name, LOCALE_STIMEFORMAT);
  values.time.t_fmt = time_picture ? picture_to_strftime(*time_picture)
                                   : fallback.time.t_fmt;
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
    const auto codepage =
        query_int(locale_name, LOCALE_IDEFAULTANSICODEPAGE);
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

}  // namespace winux::locale_db
