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
 *  - File: nohup_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(nohup, nohup_missing_operand) {
  Pipeline p;
  p.add(L"nohup.exe", {});

  TEST_LOG_CMD_LIST("nohup.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: missing operand\nTry 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_missing_operand_posixly_correct_returns_127) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"nohup.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup posix missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: missing operand\nTry 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("nohup.exe", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test") != std::string::npos);
}

TEST(nohup, nohup_preserves_argument_with_spaces) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"printf.exe", L"<%s>", L"a b"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup preserved space argument output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "<a b>");
}

TEST(nohup, nohup_invalid_option_default_returns_125) {
  Pipeline p;
  p.add(L"nohup.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: unrecognized option '--invalid'\n"
      "Try 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_invalid_option_posixly_correct_returns_127) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"nohup.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup posix invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: unrecognized option '--invalid'\n"
      "Try 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_missing_command_reports_gnu_style_error_and_127) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"definitely-not-a-command"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup missing command stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: failed to run command 'definitely-not-a-command': No such file "
      "or directory\n");
}
