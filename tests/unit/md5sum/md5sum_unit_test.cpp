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
 *  - File: md5sum_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(md5sum, md5sum_stdin) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {});

  TEST_LOG_CMD_LIST("md5sum.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world\n");

  TEST_LOG_FILE_CONTENT("test.txt", "hello world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum file output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_binary) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"-b"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"-b");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum binary output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(md5sum, md5sum_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "hello");
  tmp.write("file2.txt", "world");
  tmp.write("other.log", "log");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("md5sum.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("md5sum wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}
