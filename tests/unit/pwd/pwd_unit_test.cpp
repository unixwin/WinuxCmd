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
 *  - File: pwd_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(pwd, pwd_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {});

  TEST_LOG_CMD_LIST("pwd.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should output the current working directory
  EXPECT_FALSE(r.stdout_text.empty());
  // Should end with newline
  EXPECT_TRUE(r.stdout_text.back() == '\n');
  // Should contain the temp directory path
  EXPECT_TRUE(r.stdout_text.find(tmp.path.string()) != std::string::npos);
}

TEST(pwd, pwd_logical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"-L"});

  TEST_LOG_CMD_LIST("pwd.exe", L"-L");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe -L output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(tmp.path.string()) != std::string::npos);
}

TEST(pwd, pwd_physical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"-P"});

  TEST_LOG_CMD_LIST("pwd.exe", L"-P");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(tmp.path.string()) != std::string::npos);
}

TEST(pwd, pwd_long_option_logical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--logical"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--logical");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --logical output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(tmp.path.string()) != std::string::npos);
}

TEST(pwd, pwd_long_option_physical) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--physical"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--physical");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --physical output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find(tmp.path.string()) != std::string::npos);
}

TEST(pwd, pwd_help) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--help"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--help");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --help output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain help information
  EXPECT_TRUE(r.stdout_text.find("Usage:") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("OPTIONS") != std::string::npos);
}

// --version not supported

TEST(pwd, pwd_invalid_option) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"--invalid-option"});

  TEST_LOG_CMD_LIST("pwd.exe", L"--invalid-option");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe --invalid-option output", r.stderr_text);

  // Should fail with invalid option
  EXPECT_NE(r.exit_code, 0);
}

TEST(pwd, pwd_multiple_options) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"pwd.exe", {L"-L", L"-P"});

  TEST_LOG_CMD_LIST("pwd.exe", L"-L", L"-P");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe -L -P output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Physical option should take precedence
  EXPECT_TRUE(r.stdout_text.find(tmp.path.string()) != std::string::npos);
}

TEST(pwd, pwd_empty_directory) {
  TempDir tmp;

  // Create an empty subdirectory
  std::filesystem::create_directory(tmp.path / "empty_dir");

  Pipeline p;
  p.set_cwd((tmp.path / "empty_dir").wstring());
  p.add(L"pwd.exe", {});

  TEST_LOG_CMD_LIST("pwd.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pwd.exe in empty directory output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should output the empty directory path
  EXPECT_TRUE(r.stdout_text.find("empty_dir") != std::string::npos);
}
