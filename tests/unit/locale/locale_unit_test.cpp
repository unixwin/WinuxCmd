// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

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

// [#1127 P2c] glibc's long name for -a is --all-locales.
TEST(locale, locale_all_locales_long_option) {
  Pipeline p;
  p.add(L"locale.exe", {L"--all-locales"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("C.UTF-8") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("POSIX") != std::string::npos);
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
  EXPECT_EQ_TEXT(bogus_result.stderr_text, "locale: unknown name \"codeset\"\n");

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
  p.add(L"locale.exe", {L"-k", L"currency_symbol", L"int_curr_symbol",
                        L"mon_decimal_point", L"mon_thousands_sep",
                        L"int_frac_digits", L"p_cs_precedes", L"p_sign_posn",
                        L"grouping", L"mon_grouping", L"yesstr"});
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
  p.add(L"locale.exe", {L"-k", L"decimal_point", L"thousands_sep",
                        L"grouping", L"int_curr_symbol", L"currency_symbol",
                        L"frac_digits", L"p_cs_precedes", L"p_sep_by_space",
                        L"d_fmt", L"t_fmt", L"am_pm", L"t_fmt_ampm"});
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
