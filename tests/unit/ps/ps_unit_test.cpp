/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: ps_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(ps, ps_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {});

  TEST_LOG_CMD_LIST("ps.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should show some process information
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain header line
  EXPECT_TRUE(r.stdout_text.find("PID") != std::string::npos);
}

TEST(ps, ps_all_processes) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"-e"});

  TEST_LOG_CMD_LIST("ps.exe", L"-e");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe -e output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain more processes than default
  EXPECT_TRUE(r.stdout_text.find("PID") != std::string::npos);
}

TEST(ps, ps_full_format) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"-f"});

  TEST_LOG_CMD_LIST("ps.exe", L"-f");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe -f output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Full format should show more columns
  EXPECT_TRUE(r.stdout_text.find("UID") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("PPID") != std::string::npos);
}

TEST(ps, ps_no_headers) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"--no-headers"});

  TEST_LOG_CMD_LIST("ps.exe", L"--no-headers");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe --no-headers output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should not contain header line
  EXPECT_TRUE(r.stdout_text.find("PID TTY") == std::string::npos);
}

TEST(ps, ps_sort_by_pid) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"--sort", L"+pid"});

  TEST_LOG_CMD_LIST("ps.exe", L"--sort", L"+pid");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe --sort +pid output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(ps, ps_sort_by_memory) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"--sort", L"-mem"});

  TEST_LOG_CMD_LIST("ps.exe", L"--sort", L"-mem");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe --sort -mem output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(ps, ps_invalid_option) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"--invalid-option"});

  TEST_LOG_CMD_LIST("ps.exe", L"--invalid-option");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe --invalid-option output", r.stderr_text);

  // Should fail with invalid option
  EXPECT_NE(r.exit_code, 0);
}

TEST(ps, ps_help) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"ps.exe", {L"--help"});

  TEST_LOG_CMD_LIST("ps.exe", L"--help");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("ps.exe --help output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain help information
  EXPECT_TRUE(r.stdout_text.find("Usage:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OPTIONS") != std::string::npos);
}

// --version not supported
