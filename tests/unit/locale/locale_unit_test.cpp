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
  p.add(L"locale.exe", {L"charmap"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "UTF-8\n");

  Pipeline days;
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
