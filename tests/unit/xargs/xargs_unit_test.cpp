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
 *  - File: xargs_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(xargs, xargs_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {});
  p.set_stdin("file1\nfile2\nfile3\n");

  TEST_LOG_CMD_LIST("xargs.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file3") != std::string::npos);
}

TEST(xargs, xargs_max_args) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-n", L"2"});
  p.set_stdin("a\nb\nc\nd\ne\n");

  TEST_LOG_CMD_LIST("xargs.exe", L"-n", L"2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs -n output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should execute echo multiple times, each with 2 args
  EXPECT_TRUE(r.stdout_text.find("a b") != std::string::npos ||
              r.stdout_text.find("a") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c d") != std::string::npos ||
              r.stdout_text.find("c") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("e") != std::string::npos);
}

TEST(xargs, xargs_no_run_if_empty) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-r"});
  p.set_stdin("");

  TEST_LOG_CMD_LIST("xargs.exe", L"-r");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(xargs, xargs_default_echo) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {});
  p.set_stdin("hello\nworld\n");

  TEST_LOG_CMD_LIST("xargs.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs default echo output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(xargs, xargs_verbose) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"xargs.exe", {L"-t"});
  p.set_stdin("test\n");

  TEST_LOG_CMD_LIST("xargs.exe", L"-t");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xargs -t output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test") != std::string::npos);
  // With -t, should also see the command in stderr
  EXPECT_TRUE(r.stderr_text.find("echo") != std::string::npos);
}
