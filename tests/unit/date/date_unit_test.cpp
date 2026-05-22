/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: date_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(date, date_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {});

  TEST_LOG_CMD_LIST("date.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_format) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"+%Y-%m-%d"});

  TEST_LOG_CMD_LIST("date.exe", L"+%Y-%m-%d");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date format output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Check format YYYY-MM-DD
  EXPECT_TRUE(r.stdout_text.find('-') != std::string::npos);
}

TEST(date, date_utc) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u"});

  TEST_LOG_CMD_LIST("date.exe", L"-u");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date UTC output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_rfc2822) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-R"});

  TEST_LOG_CMD_LIST("date.exe", L"-R");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("date RFC2822 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(date, date_date_option_formats_fixed_utc_time) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"2024-01-02 03:04:05 UTC",
                      L"+%Y-%m-%dT%H:%M:%S%z"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02T03:04:05+0000\n");
}

TEST(date, date_common_format_specifiers) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"@0", L"+%F %T %s %j %u"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1970-01-01 00:00:00 0 001 4\n");
}

TEST(date, date_iso8601_seconds) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"--date", L"@0", L"--iso-8601=seconds"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1970-01-01T00:00:00+00:00\n");
}

TEST(date, date_rfc3339_seconds) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe",
        {L"-u", L"--date", L"2024-01-02 03:04:05 UTC", L"--rfc-3339=seconds"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "2024-01-02 03:04:05+00:00\n");
}

TEST(date, date_rfc_email_fixed_utc_time) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"date.exe", {L"-u", L"-R", L"--date", L"2024-01-02 03:04:05 UTC"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "Tue, 02 Jan 2024 03:04:05 +0000\n");
}
