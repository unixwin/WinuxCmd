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
 *  - File: paste_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(paste, paste_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "a\nb\nc\n");
  tmp.write("file2.txt", "1\n2\n3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("a\t1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("b\t2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("c\t3") != std::string::npos);
}

TEST(paste, paste_single_file) {
  TempDir tmp;
  tmp.write("file1.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\nc\n");
}

TEST(paste, paste_stdin) {
  Pipeline p;
  p.set_stdin("a\nb\nc\n");
  p.add(L"paste.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a\nb\nc\n");
}

TEST(paste, paste_delimiter_escapes) {
  TempDir tmp;
  tmp.write("file1.txt", "a\nb\n");
  tmp.write("file2.txt", "1\n2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"-d", L"\\0", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a1\nb2\n");
}

TEST(paste, paste_gnu_control_delimiter_escapes) {
  TempDir tmp;
  tmp.write("file1.txt", "a\n");
  tmp.write("file2.txt", "b\n");
  tmp.write("file3.txt", "c\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe",
        {L"-d", L"\\r\\v", L"file1.txt", L"file2.txt", L"file3.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a\rb\vc\n", 6));
}

TEST(paste, paste_empty_delimiter_list_uses_no_delimiters) {
  TempDir tmp;
  tmp.write("file1.txt", "a\n");
  tmp.write("file2.txt", "b\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"-d", L"", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "ab\n");
}

TEST(paste, paste_serial_replaces_newlines_with_delimiters) {
  TempDir tmp;
  tmp.write("file1.txt", "one\ntwo\nthree\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"-s", L"-d", L",\\n", L"file1.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one,two\nthree\n");
}

TEST(paste, paste_parallel_stdin_is_consumed_sequentially) {
  Pipeline p;
  p.set_stdin("1\n2\n3\n4\n");
  p.add(L"paste.exe", {L"-d", L" ", L"-", L"-"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1 2\n3 4\n");
}

TEST(paste, paste_zero_terminated_records) {
  TempDir tmp;
  tmp.write("file1.bin", std::string("a\0b\0", 4));
  tmp.write("file2.bin", std::string("1\0"
                                     "2\0",
                                     4));

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"paste.exe", {L"-z", L"-d", L",", L"file1.bin", L"file2.bin"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, std::string("a,1\0b,2\0", 8));
}
