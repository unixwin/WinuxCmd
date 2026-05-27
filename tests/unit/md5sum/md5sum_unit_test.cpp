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

TEST(md5sum, md5sum_text_mode) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"-t"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Text mode should produce valid hash
  EXPECT_TRUE(r.stdout_text.find("md5sum") == std::string::npos);
}

TEST(md5sum, md5sum_tag) {
  Pipeline p;
  p.set_stdin("hello world\n");
  p.add(L"md5sum.exe", {L"--tag"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --tag should output in BSD-style format
  EXPECT_TRUE(r.stdout_text.find("MD5") != std::string::npos);
}

TEST(md5sum, md5sum_quiet) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-q", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Quiet mode: only hash, no filename
  EXPECT_TRUE(r.stdout_text.find("test.txt") == std::string::npos);
}

TEST(md5sum, md5sum_check_valid) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");
  // Get the hash first
  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"md5sum.exe", {L"check.txt"});
  auto r1 = p1.run();
  EXPECT_EQ(r1.exit_code, 0);

  // Write hash to check file
  std::string hash_line = r1.stdout_text.substr(0, 32) + "  check.txt";
  tmp.write("check.md5", hash_line);

  // Verify
  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
}

TEST(md5sum, md5sum_check_invalid) {
  TempDir tmp;
  tmp.write("check.txt", "hello world\n");
  tmp.write("check.md5", "00000000000000000000000000000000  check.txt");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"-c", L"check.md5"});
  auto r = p.run();

  // Should fail with mismatched hash
  EXPECT_NE(r.exit_code, 0);
}

TEST(md5sum, md5sum_strict) {
  TempDir tmp;
  tmp.write("strict.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"md5sum.exe", {L"--strict", L"strict.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}
