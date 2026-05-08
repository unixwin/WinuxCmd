/*
 *  Copyright  2026 WinuxCmd
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
 *  - File: kill_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(kill, list_signals) {
  Pipeline p;
  p.add(L"kill.exe", {L"-l"});

  TEST_LOG_CMD_LIST("kill.exe", L"-l");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -l output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("INT") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("KILL") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
}

TEST(kill, list_signals_long) {
  Pipeline p;
  p.add(L"kill.exe", {L"--list"});

  TEST_LOG_CMD_LIST("kill.exe", L"--list");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe --list output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("HUP") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
}

TEST(kill, list_signals_table) {
  Pipeline p;
  p.add(L"kill.exe", {L"-L"});

  TEST_LOG_CMD_LIST("kill.exe", L"-L");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -L output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Signal") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Name") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Description") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("KILL") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("TERM") != std::string::npos);
}

TEST(kill, list_signals_table_long) {
  Pipeline p;
  p.add(L"kill.exe", {L"--table"});

  TEST_LOG_CMD_LIST("kill.exe", L"--table");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe --table output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Signal") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("Name") != std::string::npos);
}

TEST(kill, invalid_pid) {
  Pipeline p;
  p.add(L"kill.exe", {L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe 99999999 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, invalid_signal_name) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"INVALID", L"1234"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"INVALID", L"1234");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s INVALID stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("unknown signal") != std::string::npos ||
              r.stderr_text.find("invalid signal") != std::string::npos);
}

TEST(kill, invalid_signal_number) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"999", L"1234"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"999", L"1234");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s 999 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid signal") != std::string::npos);
}

TEST(kill, no_pid_specified) {
  Pipeline p;
  p.add(L"kill.exe", {});

  TEST_LOG_CMD_LIST("kill.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe (no args) stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("no process ID") != std::string::npos ||
              r.stderr_text.find("PID") != std::string::npos);
}

TEST(kill, invalid_pid_format) {
  Pipeline p;
  p.add(L"kill.exe", {L"abc"});

  TEST_LOG_CMD_LIST("kill.exe", L"abc");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe abc stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid PID") != std::string::npos);
}

TEST(kill, kill_with_sigterm) {
  Pipeline p;
  p.add(L"kill.exe", {L"-15", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-15", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -15 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, kill_with_sigkill) {
  Pipeline p;
  p.add(L"kill.exe", {L"-9", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-9", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -9 stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, multiple_pids) {
  Pipeline p;
  p.add(L"kill.exe", {L"99999998", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"99999998", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe multiple PIDs stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("99999998") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("99999999") != std::string::npos);
}

TEST(kill, signal_by_name) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"TERM", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"TERM", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s TERM stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}

TEST(kill, signal_with_sig_prefix) {
  Pipeline p;
  p.add(L"kill.exe", {L"-s", L"SIGKILL", L"99999999"});

  TEST_LOG_CMD_LIST("kill.exe", L"-s", L"SIGKILL", L"99999999");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("kill.exe -s SIGKILL stderr", r.stderr_text);

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("kill:") != std::string::npos);
}
