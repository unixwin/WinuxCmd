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
 *  - File: tput_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(tput, tput_clear) {
  Pipeline p;
  p.add(L"tput.exe", {L"clear"});

  TEST_LOG_CMD_LIST("tput.exe", L"clear");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_reset) {
  Pipeline p;
  p.add(L"tput.exe", {L"reset"});

  TEST_LOG_CMD_LIST("tput.exe", L"reset");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_bold) {
  Pipeline p;
  p.add(L"tput.exe", {L"bold"});

  TEST_LOG_CMD_LIST("tput.exe", L"bold");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_sgr0) {
  Pipeline p;
  p.add(L"tput.exe", {L"sgr0"});

  TEST_LOG_CMD_LIST("tput.exe", L"sgr0");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_cup) {
  Pipeline p;
  p.add(L"tput.exe", {L"cup"});

  TEST_LOG_CMD_LIST("tput.exe", L"cup");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("tput output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(tput, tput_missing_argument) {
  Pipeline p;
  p.add(L"tput.exe", {});

  TEST_LOG_CMD_LIST("tput.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(tput, tput_unknown_capability) {
  Pipeline p;
  p.add(L"tput.exe", {L"unknown"});

  TEST_LOG_CMD_LIST("tput.exe", L"unknown");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tput stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}
