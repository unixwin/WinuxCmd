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
 *  - File: sort_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(sort, sort_basic_lexicographic) {
  TempDir tmp;
  tmp.write("a.txt", "pear\napple\nbanana\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "apple\nbanana\npear\n");
}

TEST(sort, sort_numeric_reverse_unique) {
  TempDir tmp;
  tmp.write("n.txt", "2\n10\n2\n1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-n", L"-r", L"-u", L"n.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "10\n2\n1\n");
}

TEST(sort, sort_ignore_case_and_key) {
  TempDir tmp;
  tmp.write("k.txt", "b 2\nA 3\na 1\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-f", L"-k", L"1", L"k.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "A 3\na 1\nb 2\n");
}

TEST(sort, sort_output_file_option) {
  TempDir tmp;
  tmp.write("in.txt", "z\nx\ny\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-o", L"out.txt", L"in.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(tmp.read("out.txt"), "x\ny\nz\n");
}

TEST(sort, sort_version_sort) {
  TempDir tmp;
  tmp.write("v.txt", "1.2.10\n1.2.2\n1.10.0\n1.2.0\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-V", L"v.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.2.0\n1.2.2\n1.2.10\n1.10.0\n");
}

TEST(sort, sort_unsupported_merge) {
  TempDir tmp;
  tmp.write("a.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"-m", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
}

TEST(sort, sort_uniq_pipeline_accepts_utf16le_stdin_with_bom) {
  // Simulate PowerShell native pipeline output with UTF-16LE and per-line BOM.
  std::u16string u16 = u"\ufeffdog\r\n\ufeffdog\r\n\ufeffcat\r\n";
  std::string bytes;
  bytes.reserve(u16.size() * 2);
  for (char16_t ch : u16) {
    bytes.push_back(static_cast<char>(ch & 0xFF));
    bytes.push_back(static_cast<char>((ch >> 8) & 0xFF));
  }

  Pipeline p;
  p.set_stdin(bytes);
  p.add(L"sort.exe", {});
  p.add(L"uniq.exe", {L"-c"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1 cat") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2 dog") != std::string::npos);
}

TEST(sort, sort_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "cherry\napple\n");
  tmp.write("file2.txt", "banana\ndate\n");
  tmp.write("other.log", "zzz\naaa\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sort.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("sort.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sort output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // Should contain content from .txt files but not .log
  EXPECT_TRUE(r.stdout_text.find("apple") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("banana") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cherry") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("date") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("aaa") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("zzz") == std::string::npos);
}
