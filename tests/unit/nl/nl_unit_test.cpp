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
 *  - File: nl_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(nl, nl_basic_file) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1\tline1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2\tline2") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("3\tline3") != std::string::npos);
}

TEST(nl, nl_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"nl.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("1\tline1") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2\tline2") != std::string::npos);
}

TEST(nl, nl_empty_file) {
  TempDir tmp;
  tmp.write("empty.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"empty.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty() || r.stdout_text == "\n");
}

TEST(nl, nl_number_format_and_negative_increment) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-n", L"rz", L"-w", L"3", L"-v", L"-1", L"-i", L"-2",
                    L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "-01:line1\n-03:line2\n");
}

TEST(nl, nl_numbers_logical_page_sections) {
  TempDir tmp;
  tmp.write("test.txt", "\\:\\:\\:\nheader\n\\:\\:\nbody\n\\:\nfooter\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe",
        {L"-h", L"a", L"-f", L"a", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "\n1:header\n\n1:body\n\n1:footer\n");
}

TEST(nl, nl_no_renumber_keeps_count_across_sections) {
  TempDir tmp;
  tmp.write("test.txt", "\\:\\:\\:\nheader\n\\:\\:\nbody\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-p", L"-h", L"a", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "\n1:header\n\n2:body\n");
}

TEST(nl, nl_join_blank_lines_numbers_only_group_boundary) {
  TempDir tmp;
  tmp.write("test.txt", "line1\n\n\n\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe",
        {L"-b", L"a", L"-l", L"2", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1:line1\n:\n2:\n:\n3:line2\n");
}

TEST(nl, nl_pattern_body_numbering) {
  TempDir tmp;
  tmp.write("test.txt", "ERR first\nok\nERR second\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-b", L"p^ERR", L"-w", L"1", L"-s", L":", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1:ERR first\n:ok\n2:ERR second\n");
}

TEST(nl, nl_empty_number_separator) {
  TempDir tmp;
  tmp.write("test.txt", "line\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nl.exe", {L"-w", L"1", L"-s", L"", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "1line\n");
}
