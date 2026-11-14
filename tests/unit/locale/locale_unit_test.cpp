// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <algorithm>

#include "framework/winuxtest.h"

namespace {

// Output lines of a Pipeline run, without the trailing empty element a final
// newline would produce.
auto split_output_lines(const std::string& text) -> std::vector<std::string> {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

}  // namespace

TEST(locale, locale_basic) {
  Pipeline p;
  p.add(L"locale.exe", {});

  TEST_LOG_CMD_LIST("locale.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("locale output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("LANG=") != std::string::npos);
}

TEST(locale, locale_all) {
  Pipeline p;
  p.add(L"locale.exe", {L"-a"});

  TEST_LOG_CMD_LIST("locale.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("locale output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("C") != std::string::npos);
}

TEST(locale, locale_charmaps) {
  Pipeline p;
  p.add(L"locale.exe", {L"-m"});

  TEST_LOG_CMD_LIST("locale.exe", L"-m");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("locale output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("UTF-8") != std::string::npos);
}

TEST(locale, locale_keyword_values_are_not_full_environment_dump) {
  Pipeline p;
  // The values come from the locale environment (POSIX precedence); pin it so
  // the expectation does not depend on the machine's Windows locale.
  p.set_env(L"LC_ALL", L"C.UTF-8");
  p.add(L"locale.exe", {L"charmap"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "UTF-8\n");

  Pipeline days;
  days.set_env(L"LC_ALL", L"C");
  days.add(L"locale.exe", {L"abday"});
  auto days_result = days.run();
  EXPECT_EQ(days_result.exit_code, 0);
  EXPECT_TRUE(days_result.stdout_text.find("Sun") != std::string::npos);
  EXPECT_TRUE(days_result.stdout_text.find("LANG=") == std::string::npos);
}

TEST(locale, locale_charmap_follows_the_selected_locale) {
  // [GNU] `locale charmap` answers from the locale the environment selects, in
  // the POSIX precedence order: LC_ALL=C is pure ASCII (ANSI_X3.4-1968), a
  // UTF-8 locale stays UTF-8, and an unset locale keeps the process codeset.
  Pipeline c_locale;
  c_locale.set_env(L"LC_ALL", L"C");
  c_locale.add(L"locale.exe", {L"charmap"});
  auto c_result = c_locale.run();
  EXPECT_EQ(c_result.exit_code, 0);
  EXPECT_EQ_TEXT(c_result.stdout_text, "ANSI_X3.4-1968\n");

  Pipeline posix_locale;
  posix_locale.set_env(L"LC_ALL", L"POSIX");
  posix_locale.add(L"locale.exe", {L"charmap"});
  auto posix_result = posix_locale.run();
  EXPECT_EQ(posix_result.exit_code, 0);
  EXPECT_EQ_TEXT(posix_result.stdout_text, "ANSI_X3.4-1968\n");

  Pipeline utf8_locale;
  utf8_locale.set_env(L"LC_ALL", L"C.UTF-8");
  utf8_locale.add(L"locale.exe", {L"charmap"});
  auto utf8_result = utf8_locale.run();
  EXPECT_EQ(utf8_result.exit_code, 0);
  EXPECT_EQ_TEXT(utf8_result.stdout_text, "UTF-8\n");

  // LC_CTYPE applies only when LC_ALL is unset or empty.
  Pipeline ctype_only;
  ctype_only.set_env(L"LC_ALL", L"");
  ctype_only.set_env(L"LC_CTYPE", L"C");
  ctype_only.add(L"locale.exe", {L"charmap"});
  auto ctype_result = ctype_only.run();
  EXPECT_EQ(ctype_result.exit_code, 0);
  EXPECT_EQ_TEXT(ctype_result.stdout_text, "ANSI_X3.4-1968\n");
}

// [#1127 P2c] glibc's long name for -a is --all-locales. The inventory is the
// Windows NLS catalogue (#1549 phase 2): C/C.utf8/POSIX always, then every
// EnumSystemLocalesEx name in POSIX spelling with its codeset suffix,
// de-duplicated and sorted the way glibc sorts under LC_ALL=C.
TEST(locale, locale_all_locales_long_option) {
  Pipeline p;
  p.set_env(L"LC_ALL", L"C");
  p.add(L"locale.exe", {L"--all-locales"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C.utf8") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("POSIX") != std::string::npos);
  // Byte-sorted and duplicate-free (glibc tsearch() + strcoll under C).
  auto lines = split_output_lines(r.stdout_text);
  auto sorted = lines;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_TRUE(sorted == lines);
  EXPECT_TRUE(std::adjacent_find(lines.begin(), lines.end()) == lines.end());
  EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const auto& line) {
    return line.rfind("en_US.", 0) == 0;
  }));
}

// [#1127 P2c] --usage prints a short usage message and exits 0.
TEST(locale, locale_usage_option) {
  Pipeline p;
  p.add(L"locale.exe", {L"--usage"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Usage: locale") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("--all-locales") != std::string::npos);
}

// [GNU] glibc locale(1) writes nothing unless NAME operands are given:
// a bare -c (locale.c main(): only the no-argument branch dumps the
// environment) is silent and exits 0.
TEST(locale, locale_bare_c_is_silent) {
  Pipeline p;
  p.add(L"locale.exe", {L"-c"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

// [GNU] bare -k is likewise silent, exit 0.
TEST(locale, locale_bare_k_is_silent) {
  Pipeline p;
  p.add(L"locale.exe", {L"-k"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_TRUE(r.stderr_text.empty());
}

// [GNU] an unknown NAME operand is `locale: unknown name "X"`, exit 1
// (glibc locale_special.c wording; glibc 2.39 rejects `codeset`,
// `month` and `default_language` — `mon` is the real LC_TIME item).
TEST(locale, locale_unknown_name_wording) {
  Pipeline bogus;
  bogus.add(L"locale.exe", {L"codeset"});
  auto bogus_result = bogus.run();
  EXPECT_EQ(bogus_result.exit_code, 1);
  EXPECT_EQ_TEXT(bogus_result.stdout_text, "");
  EXPECT_EQ_TEXT(bogus_result.stderr_text,
                 "locale: unknown name \"codeset\"\n");

  Pipeline empty;
  empty.add(L"locale.exe", {L""});
  auto empty_result = empty.run();
  EXPECT_EQ(empty_result.exit_code, 1);
  EXPECT_EQ_TEXT(empty_result.stderr_text, "locale: unknown name \"\"\n");

  Pipeline kmode;
  kmode.add(L"locale.exe", {L"-k", L"nosuchkw123"});
  auto kmode_result = kmode.run();
  EXPECT_EQ(kmode_result.exit_code, 1);
  EXPECT_EQ_TEXT(kmode_result.stderr_text,
                 "locale: unknown name \"nosuchkw123\"\n");
}

// [GNU] `mon` is the LC_TIME full-month item (glibc categories.def);
// the C-locale value is the ;-joined English month list.
TEST(locale, locale_mon_keyword) {
  Pipeline p;
  p.set_env(L"LC_ALL", L"C");
  p.add(L"locale.exe", {L"mon"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "January;February;March;April;May;June;July;August;September;"
                 "October;November;December\n");
}

// [GNU] glibc print_item(): -k renders string items as name="value"
// (quoted), numeric monetary flags unquoted, in argv order.
TEST(locale, locale_k_mode_quoting) {
  Pipeline charmap;
  charmap.set_env(L"LC_ALL", L"C");
  charmap.add(L"locale.exe", {L"-k", L"charmap"});
  auto charmap_result = charmap.run();
  EXPECT_EQ(charmap_result.exit_code, 0);
  EXPECT_EQ_TEXT(charmap_result.stdout_text, "charmap=\"ANSI_X3.4-1968\"\n");

  Pipeline both;
  both.set_env(L"LC_ALL", L"C");
  both.add(L"locale.exe", {L"-k", L"decimal_point", L"frac_digits"});
  auto both_result = both.run();
  EXPECT_EQ(both_result.exit_code, 0);
  EXPECT_EQ_TEXT(both_result.stdout_text,
                 "decimal_point=\".\"\nfrac_digits=-1\n");
}

// [GNU] The C locale has no monetary data: glibc answers empty strings and
// -1 flags. The value model must not invent an en-US-shaped table (#1549).
TEST(locale, locale_c_locale_monetary_is_empty) {
  Pipeline p;
  p.set_env(L"LC_ALL", L"C");
  p.add(L"locale.exe",
        {L"-k", L"currency_symbol", L"int_curr_symbol", L"mon_decimal_point",
         L"mon_thousands_sep", L"int_frac_digits", L"p_cs_precedes",
         L"p_sign_posn", L"grouping", L"mon_grouping", L"yesstr"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "currency_symbol=\"\"\n"
                 "int_curr_symbol=\"\"\n"
                 "mon_decimal_point=\"\"\n"
                 "mon_thousands_sep=\"\"\n"
                 "int_frac_digits=-1\n"
                 "p_cs_precedes=-1\n"
                 "p_sign_posn=-1\n"
                 "grouping=-1\n"
                 "mon_grouping=-1\n"
                 "yesstr=\"\"\n");
}

// The value model is Windows-NLS-backed: an environment-selected locale other
// than C answers that locale's data, not a hard-coded table. en_US is used
// because every Windows installation resolves it, independent of the
// machine's display language.
TEST(locale, locale_en_us_values_come_from_the_locale_database) {
  Pipeline p;
  p.set_env(L"LC_ALL", L"en_US.UTF-8");
  p.add(L"locale.exe", {L"-k", L"decimal_point", L"thousands_sep", L"grouping",
                        L"int_curr_symbol", L"currency_symbol", L"frac_digits",
                        L"p_cs_precedes", L"p_sep_by_space", L"d_fmt", L"t_fmt",
                        L"am_pm", L"t_fmt_ampm"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "decimal_point=\".\"\n"
                 "thousands_sep=\",\"\n"
                 "grouping=3\n"
                 "int_curr_symbol=\"USD \"\n"
                 "currency_symbol=\"$\"\n"
                 "frac_digits=2\n"
                 "p_cs_precedes=1\n"
                 "p_sep_by_space=0\n"
                 "d_fmt=\"%m/%d/%Y\"\n"
                 "t_fmt=\"%I:%M:%S %p\"\n"
                 "am_pm=\"AM;PM\"\n"
                 "t_fmt_ampm=\"%I:%M:%S %p\"\n");
}

// A locale code set in the POSIX spelling is the codeset source, even when the
// name itself is unknown to the Windows NLS database.
TEST(locale, locale_unknown_name_with_codeset_keeps_its_charmap) {
  Pipeline p;
  p.set_env(L"LC_ALL", L"zz_ZZ.UTF-8");
  p.add(L"locale.exe", {L"charmap"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "UTF-8\n");
}

// ---------------------------------------------------------------------------
// [#1549 phase 2] glibc category dumps (categories.def item streams).
// C/POSIX is the byte-for-byte oracle (glibc 2.39, `LC_ALL=C locale ...`).
// ---------------------------------------------------------------------------

namespace {

// `LC_ALL=C <args>` through the process boundary.
auto run_locale_c(std::initializer_list<std::wstring> args) -> CommandResult {
  Pipeline p;
  p.set_env(L"LC_ALL", L"C");
  p.add(L"locale.exe", args);
  return p.run();
}

}  // namespace

// [GNU] `locale LC_TIME` (no -c/-k) prints the raw item values only.
TEST(locale, locale_category_operand_prints_values) {
  const auto r = run_locale_c({L"LC_TIME"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, R"(Sun;Mon;Tue;Wed;Thu;Fri;Sat
Sunday;Monday;Tuesday;Wednesday;Thursday;Friday;Saturday
Jan;Feb;Mar;Apr;May;Jun;Jul;Aug;Sep;Oct;Nov;Dec
January;February;March;April;May;June;July;August;September;October;November;December
AM;PM
%a %b %e %H:%M:%S %Y
%m/%d/%y
%H:%M:%S
%I:%M:%S %p






0

7
19971130
4
1
2
1

%a %b %e %H:%M:%S %Z %Y
ANSI_X3.4-1968
January;February;March;April;May;June;July;August;September;October;November;December
Jan;Feb;Mar;Apr;May;Jun;Jul;Aug;Sep;Oct;Nov;Dec
)");
}

// [GNU] -c writes the category name first, then the same value stream.
TEST(locale, locale_c_category_prefixes_the_values) {
  const auto r = run_locale_c({L"-c", L"LC_NUMERIC"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "LC_NUMERIC\n.\n\n-1\n46\n0\nANSI_X3.4-1968\n");
}

// [GNU] An item operand answered with -c gets its category name first
// (`locale -c charmap decimal_point`).
TEST(locale, locale_c_item_operand_prints_its_category) {
  const auto r = run_locale_c({L"-c", L"charmap", L"decimal_point"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "LC_CTYPE\nANSI_X3.4-1968\nLC_NUMERIC\n.\n");
}

// [GNU] -k renders names and values; -c -k keeps the category header.
TEST(locale, locale_k_category_quoting) {
  const auto r = run_locale_c({L"-k", L"-c", L"LC_MESSAGES"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "LC_MESSAGES\n"
                 "yesexpr=\"^[yY]\"\n"
                 "noexpr=\"^[nN]\"\n"
                 "yesstr=\"\"\n"
                 "nostr=\"\"\n"
                 "messages-codeset=\"ANSI_X3.4-1968\"\n");
}

// [GNU] LC_TIME -k, byte-identical to glibc 2.39's C locale (28 items,
// including date_fmt and the week/calendar items #1549 phase 2 added).
TEST(locale, locale_k_time_matches_glibc_c) {
  const auto r = run_locale_c({L"-k", L"LC_TIME"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, R"(abday="Sun;Mon;Tue;Wed;Thu;Fri;Sat"
day="Sunday;Monday;Tuesday;Wednesday;Thursday;Friday;Saturday"
abmon="Jan;Feb;Mar;Apr;May;Jun;Jul;Aug;Sep;Oct;Nov;Dec"
mon="January;February;March;April;May;June;July;August;September;October;November;December"
am_pm="AM;PM"
d_t_fmt="%a %b %e %H:%M:%S %Y"
d_fmt="%m/%d/%y"
t_fmt="%H:%M:%S"
t_fmt_ampm="%I:%M:%S %p"
era=
era_year=""
era_d_fmt=""
alt_digits=
era_d_t_fmt=""
era_t_fmt=""
time-era-num-entries=0
time-era-entries=""
week-ndays=7
week-1stday=19971130
week-1stweek=4
first_weekday=1
first_workday=2
cal_direction=1
timezone=""
date_fmt="%a %b %e %H:%M:%S %Z %Y"
time-codeset="ANSI_X3.4-1968"
alt_mon="January;February;March;April;May;June;July;August;September;October;November;December"
ab_alt_mon="Jan;Feb;Mar;Apr;May;Jun;Jul;Aug;Sep;Oct;Nov;Dec"
)");
}

// [GNU] LC_CTYPE -k: class/map names, the codeset and the indigit/outdigit
// tables. The C values are glibc's (72/84/1659 layout constants included).
TEST(locale, locale_k_ctype_matches_glibc_c) {
  const auto r = run_locale_c({L"-k", L"LC_CTYPE"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(
      r.stdout_text,
      R"(ctype-class-names="upper";"lower";"alpha";"digit";"xdigit";"space";"print";"graph";"blank";"cntrl";"punct";"alnum"
ctype-map-names="toupper";"tolower"
ctype-width=7
ctype-mb-cur-max=1
charmap="ANSI_X3.4-1968"
ctype-class-offset=72
ctype-map-offset=84
ctype-indigits_mb-len=1
ctype-indigits0_mb="0"
ctype-indigits1_mb="1"
ctype-indigits2_mb="2"
ctype-indigits3_mb="3"
ctype-indigits4_mb="4"
ctype-indigits5_mb="5"
ctype-indigits6_mb="6"
ctype-indigits7_mb="7"
ctype-indigits8_mb="8"
ctype-indigits9_mb="9"
ctype-indigits_wc-len=1
ctype-outdigit0_mb="0"
ctype-outdigit1_mb="1"
ctype-outdigit2_mb="2"
ctype-outdigit3_mb="3"
ctype-outdigit4_mb="4"
ctype-outdigit5_mb="5"
ctype-outdigit6_mb="6"
ctype-outdigit7_mb="7"
ctype-outdigit8_mb="8"
ctype-outdigit9_mb="9"
ctype-outdigit0_wc=48
ctype-outdigit1_wc=49
ctype-outdigit2_wc=50
ctype-outdigit3_wc=51
ctype-outdigit4_wc=52
ctype-outdigit5_wc=53
ctype-outdigit6_wc=54
ctype-outdigit7_wc=55
ctype-outdigit8_wc=56
ctype-outdigit9_wc=57
ctype-translit-tab-size=1659
ctype-translit-default-missing-len=1
ctype-translit-ignore-len=0
ctype-translit-ignore=""
map-to-nonascii=0
nonascii-case=0
)");
}

// [GNU] LC_MONETARY -k: the fifteen lconv items, crncystr, the six
// international-format fields (#1549 phase 2; unset because Win32 has one
// monetary format) and the financial duo_*/uno_* defaults.
TEST(locale, locale_k_monetary_matches_glibc_c) {
  const auto r = run_locale_c({L"-k", L"LC_MONETARY"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, R"(int_curr_symbol=""
currency_symbol=""
mon_decimal_point=""
mon_thousands_sep=""
mon_grouping=-1
positive_sign=""
negative_sign=""
int_frac_digits=-1
frac_digits=-1
p_cs_precedes=-1
p_sep_by_space=-1
n_cs_precedes=-1
n_sep_by_space=-1
p_sign_posn=-1
n_sign_posn=-1
crncystr="-"
int_p_cs_precedes=-1
int_p_sep_by_space=-1
int_n_cs_precedes=-1
int_n_sep_by_space=-1
int_p_sign_posn=-1
int_n_sign_posn=-1
duo_int_curr_symbol=""
duo_currency_symbol=""
duo_int_frac_digits=-1
duo_frac_digits=-1
duo_p_cs_precedes=-1
duo_p_sep_by_space=-1
duo_n_cs_precedes=-1
duo_n_sep_by_space=-1
duo_int_p_cs_precedes=-1
duo_int_p_sep_by_space=-1
duo_int_n_cs_precedes=-1
duo_int_n_sep_by_space=-1
duo_p_sign_posn=-1
duo_n_sign_posn=-1
duo_int_p_sign_posn=-1
duo_int_n_sign_posn=-1
uno_valid_from=10101
uno_valid_to=99991231
duo_valid_from=10101
duo_valid_to=99991231
conversion_rate=1;1
monetary-decimal-point-wc=0
monetary-thousands-sep-wc=0
monetary-codeset="ANSI_X3.4-1968"
)");
}

// [GNU] LC_NUMERIC -k, and the remaining categories whose values are C
// constants plus the codeset (collate/paper/name/address/telephone/
// measurement/identification).
TEST(locale, locale_k_other_categories_match_glibc_c) {
  const auto numeric = run_locale_c({L"-k", L"LC_NUMERIC"});
  EXPECT_EQ_TEXT(numeric.stdout_text,
                 "decimal_point=\".\"\n"
                 "thousands_sep=\"\"\n"
                 "grouping=-1\n"
                 "numeric-decimal-point-wc=46\n"
                 "numeric-thousands-sep-wc=0\n"
                 "numeric-codeset=\"ANSI_X3.4-1968\"\n");

  const auto collate = run_locale_c({L"-k", L"LC_COLLATE"});
  EXPECT_EQ_TEXT(collate.stdout_text,
                 "collate-nrules=0\n"
                 "collate-rulesets=\"\"\n"
                 "collate-symb-hash-sizemb=0\n"
                 "collate-codeset=\"ANSI_X3.4-1968\"\n");

  const auto paper = run_locale_c({L"-k", L"LC_PAPER"});
  EXPECT_EQ_TEXT(paper.stdout_text,
                 "height=297\nwidth=210\n"
                 "paper-codeset=\"ANSI_X3.4-1968\"\n");

  const auto name = run_locale_c({L"-k", L"LC_NAME"});
  EXPECT_EQ_TEXT(name.stdout_text,
                 "name_fmt=\"%p%t%g%t%m%t%f\"\n"
                 "name_gen=\"\"\n"
                 "name_mr=\"\"\n"
                 "name_mrs=\"\"\n"
                 "name_miss=\"\"\n"
                 "name_ms=\"\"\n"
                 "name-codeset=\"ANSI_X3.4-1968\"\n");

  const auto telephone = run_locale_c({L"-k", L"LC_TELEPHONE"});
  EXPECT_EQ_TEXT(telephone.stdout_text,
                 "tel_int_fmt=\"+%c %a %l\"\n"
                 "tel_dom_fmt=\"\"\n"
                 "int_select=\"\"\n"
                 "int_prefix=\"\"\n"
                 "telephone-codeset=\"ANSI_X3.4-1968\"\n");

  const auto measurement = run_locale_c({L"-k", L"LC_MEASUREMENT"});
  EXPECT_EQ_TEXT(measurement.stdout_text,
                 "measurement=1\n"
                 "measurement-codeset=\"ANSI_X3.4-1968\"\n");

  const auto identification = run_locale_c({L"-k", L"LC_IDENTIFICATION"});
  EXPECT_EQ_TEXT(identification.stdout_text,
                 "title=\"ISO/IEC 14652 i18n FDCC-set\"\n"
                 "source=\"ISO/IEC JTC1/SC22/WG20 - internationalization\"\n"
                 "address=\"C/o Keld Simonsen, Skt. Jorgens Alle 8, DK-1615 "
                 "Kobenhavn V\"\n"
                 "contact=\"Keld Simonsen\"\n"
                 "email=\"keld@dkuug.dk\"\n"
                 "tel=\"+45 3122-6543\"\n"
                 "fax=\"+45 3325-6543\"\n"
                 "language=\"\"\n"
                 "territory=\"ISO\"\n"
                 "audience=\"\"\n"
                 "application=\"\"\n"
                 "abbreviation=\"\"\n"
                 "revision=\"1.0\"\n"
                 "date=\"1997-12-20\"\n"
                 "category=\"i18n:1999;ANSI_X3.4-1968;;;;;;;;;;;\"\n"
                 "identification-codeset=\"ANSI_X3.4-1968\"\n");
}

// [#1549 phase 2] The six international monetary fields have no Win32
// source: they keep the C "unset" value for every locale (design doc §8).
TEST(locale, locale_international_monetary_fields_are_unset) {
  Pipeline p;
  p.set_env(L"LC_ALL", L"en_US.UTF-8");
  p.add(L"locale.exe", {L"-k", L"int_p_cs_precedes", L"int_p_sep_by_space",
                        L"int_n_cs_precedes", L"int_n_sep_by_space",
                        L"int_p_sign_posn", L"int_n_sign_posn"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "int_p_cs_precedes=-1\n"
                 "int_p_sep_by_space=-1\n"
                 "int_n_cs_precedes=-1\n"
                 "int_n_sep_by_space=-1\n"
                 "int_p_sign_posn=-1\n"
                 "int_n_sign_posn=-1\n");
}

// [#1549 phase 2] date_fmt: the C record keeps glibc's pattern; other
// records synthesize d_t_fmt + " %Z" (no Win32 date_fmt item).
TEST(locale, locale_date_fmt_follows_the_model) {
  const auto c = run_locale_c({L"date_fmt"});
  EXPECT_EQ_TEXT(c.stdout_text, "%a %b %e %H:%M:%S %Z %Y\n");

  Pipeline p;
  p.set_env(L"LC_ALL", L"en_US.UTF-8");
  p.add(L"locale.exe", {L"date_fmt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "%m/%d/%Y %I:%M:%S %p %Z\n");
}

// [GNU] LC_MEASUREMENT follows LOCALE_IMEASURE (0 metric, 1 US) projected
// onto glibc's 1/2 encoding.
TEST(locale, locale_measurement_follows_the_locale) {
  Pipeline us;
  us.set_env(L"LC_ALL", L"en_US.UTF-8");
  us.add(L"locale.exe", {L"-k", L"measurement"});
  auto us_result = us.run();
  EXPECT_EQ_TEXT(us_result.stdout_text, "measurement=2\n");

  Pipeline metric;
  metric.set_env(L"LC_ALL", L"de_DE.UTF-8");
  metric.add(L"locale.exe", {L"-k", L"measurement"});
  auto metric_result = metric.run();
  EXPECT_EQ_TEXT(metric_result.stdout_text, "measurement=1\n");
}

// [GNU] `locale -m` lists the NLS code page catalogue in sorted, duplicate-
// free glibc charmap spelling.
TEST(locale, locale_charmaps_are_sorted_and_deduplicated) {
  const auto r = run_locale_c({L"-m"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("UTF-8\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("ANSI_X3.4-1968\n") != std::string::npos);
  auto lines = split_output_lines(r.stdout_text);
  auto sorted = lines;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_TRUE(sorted == lines);
  EXPECT_TRUE(std::adjacent_find(lines.begin(), lines.end()) == lines.end());
}

// [GNU] -a wins when -a and -m are both present (glibc main() branches on
// do_all before do_charmaps).
TEST(locale, locale_a_wins_over_m) {
  const auto r = run_locale_c({L"-m", L"-a"});
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C.utf8\n") != std::string::npos);
  EXPECT_EQ(r.stdout_text.find("ANSI_X3.4-1968\n"), std::string::npos);
}

// [GNU] Unknown category names take the unknown-name path, like any other
// unrecognized NAME operand.
TEST(locale, locale_unknown_category_is_an_unknown_name) {
  const auto r = run_locale_c({L"LC_FOO"});
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text, "locale: unknown name \"LC_FOO\"\n");
}
