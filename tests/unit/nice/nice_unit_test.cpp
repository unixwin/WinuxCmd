/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software", to
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
 *  - File: nice_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(nice, nice_no_command) {
  Pipeline p;
  p.add(L"nice.exe", {});

  TEST_LOG_CMD_LIST("nice.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ(r.stdout_text.find("Current priority class"), std::string::npos);
}

TEST(nice, nice_with_adjustment) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"5", L"echo.exe", L"hello"});

  TEST_LOG_CMD_LIST("nice.exe", L"-n", L"5", L"echo.exe", L"hello");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(nice, nice_with_attached_adjustment) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n5", L"echo.exe", L"hello"});

  TEST_LOG_CMD_LIST("nice.exe", L"-n5", L"echo.exe", L"hello");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(nice, nice_preserves_argument_with_spaces) {
  Pipeline p;
  p.add(L"nice.exe", {L"printf.exe", L"<%s>", L"a b"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice preserved space argument output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "<a b>");
}

TEST(nice, nice_legacy_bare_adjustment_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"-1", L"echo.exe", L"-n", L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice bare adjustment output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a");
}

TEST(nice, nice_legacy_double_dash_adjustment_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"--1", L"echo.exe", L"-n", L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice double dash adjustment output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a");
}

TEST(nice, nice_legacy_plus_prefixed_adjustment_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"-+1", L"echo.exe", L"-n", L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice plus prefixed adjustment output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a");
}

TEST(nice, nice_huge_adjustment_is_clamped_and_still_runs_command) {
  Pipeline p;
  p.add(L"nice.exe",
        {L"-n",
         L"9999999999999999999999999999999999999999999999999999999999999999",
         L"true.exe"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice huge adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(nice, nice_huge_negative_adjustment_is_clamped_and_still_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"-9999999999999999999999", L"true.exe"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice huge negative adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(nice, nice_invalid_adjustment_reports_usage_error) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"-2+4", L"true.exe"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice invalid adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nice: invalid adjustment '-2+4'\n"
      "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_invalid_option_returns_125) {
  Pipeline p;
  p.add(L"nice.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nice: unrecognized option '--invalid'\n"
      "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_explicit_adjustment_without_command_reports_usage_error) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"19"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice explicit adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nice: A command must be given with an adjustment.\n"
      "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_long_adjustment_without_command_reports_usage_error) {
  Pipeline p;
  p.add(L"nice.exe", {L"--adjustment=19"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice long adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nice: A command must be given with an adjustment.\n"
      "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_missing_command_reports_gnu_style_error_and_127) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nice.exe", {L"definitely-not-a-command"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice missing command stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nice: failed to run command 'definitely-not-a-command': No such file "
      "or directory\n");
}
