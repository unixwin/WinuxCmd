/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: grep_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(grep, grep_basic_match) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\nalpha beta\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"alpha", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha\nalpha beta\n");
}

TEST(grep, grep_ignore_case_and_line_number) {
  TempDir tmp;
  tmp.write("a.txt", "One\nTWO\nthree\nTwo again\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-i", L"-n", L"two", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2:TWO\n4:Two again\n");
}

TEST(grep, grep_count_and_files_with_matches) {
  TempDir tmp;
  tmp.write("a.txt", "x\ny\nx\n");
  tmp.write("b.txt", "z\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"grep.exe", {L"-c", L"x", L"a.txt", L"b.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_TRUE(r1.stdout_text.find("a.txt:2") != std::string::npos);
  EXPECT_TRUE(r1.stdout_text.find("b.txt:0") != std::string::npos);

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"grep.exe", {L"-l", L"x", L"a.txt", L"b.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "a.txt\n");
}

TEST(grep, grep_recursive_search) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "d1" / "d2");
  tmp.write("d1/a.txt", "needle\n");
  tmp.write("d1/d2/b.txt", "none\nneedle\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"needle", L"d1"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("a.txt") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b.txt") != std::string::npos);
}

TEST(grep, grep_recursive_alias_R) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "d1");
  tmp.write("d1/a.txt", "needle\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-R", L"needle", L"d1"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle") != std::string::npos);
}

TEST(grep, grep_exclude_dir_skips_nested_directory) {
  TempDir tmp;
  std::filesystem::create_directories(tmp.path / "keep" / "skipme");
  tmp.write("keep/a.txt", "needle-keep\n");
  tmp.write("keep/skipme/b.txt", "needle-skip\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-r", L"--exclude-dir", L"skipme", L"needle", L"keep"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("needle-keep") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("needle-skip") == std::string::npos);
}

TEST(grep, grep_basic_and_extended_regex_semantics) {
  TempDir tmp;
  tmp.write("a.txt", "aaa\na+\n");

  Pipeline basic;
  basic.set_cwd(tmp.wpath());
  basic.add(L"grep.exe", {L"a+", L"a.txt"});
  auto basic_result = basic.run();

  EXPECT_EQ(basic_result.exit_code, 0);
  EXPECT_EQ_TEXT(basic_result.stdout_text, "a+\n");

  Pipeline extended;
  extended.set_cwd(tmp.wpath());
  extended.add(L"grep.exe", {L"-E", L"a+", L"a.txt"});
  auto extended_result = extended.run();

  EXPECT_EQ(extended_result.exit_code, 0);
  EXPECT_EQ_TEXT(extended_result.stdout_text, "aaa\na+\n");
}

TEST(grep, grep_only_matching) {
  TempDir tmp;
  tmp.write("a.txt", "abc123def123\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-o", L"123", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "123\n123\n");
}

TEST(grep, grep_unsupported_perl_regexp) {
  TempDir tmp;
  tmp.write("a.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"-P", L"x", L"a.txt"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
}

TEST(grep, grep_wildcard_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello world\n");
  tmp.write("file2.txt", "hello again\n");
  tmp.write("other.log", "no match here\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"hello", L"*.txt"});

  TEST_LOG_CMD_LIST("grep.exe", L"hello", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("grep.exe hello *.txt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("file1.txt:hello world") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("file2.txt:hello again") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("other.log") == std::string::npos);
}

TEST(grep, grep_wildcard_pattern) {
  TempDir tmp;
  tmp.write("test.cpp", "#include <iostream>\n");
  tmp.write("test.h", "#pragma once\n");
  tmp.write("test.txt", "just text\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"grep.exe", {L"#include", L"*.cpp"});

  TEST_LOG_CMD_LIST("grep.exe", L"#include", L"*.cpp");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("grep output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("#include <iostream>") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("#pragma once") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("just text") == std::string::npos);
}
